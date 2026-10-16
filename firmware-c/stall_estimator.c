#include "stall_estimator.h"
#include "stall_model.h"
#include <math.h>
#include <string.h>

static float bins[STALL_HISTORY][4], window[STALL_MAX_WINDOW][4];
static unsigned head, available;
static uint64_t last_bin, stable_since, last_evaluation;
static bool calibrated, job_active, trace_capture, trace_frozen, steady;
static stall_context_t context;
static stall_result_t result;
static uint32_t raw_count, sum[2], low[2], high[2];
static uint64_t square[2], bin_start;

typedef struct {
    const float (*samples)[4];
    unsigned n, pos;
    float previous_radial, speed;
    double radial, radial2, ripple, ripple2, variance, difference;
    uint64_t end_us;
} feature_job_t;
static feature_job_t job;

static void clear_window(void) {
    job_active=false; head=available=raw_count=0; last_bin=bin_start=0;
    memset(sum,0,sizeof(sum)); memset(square,0,sizeof(square));
}
void stall_estimator_init(bool device) {
    calibrated=device; trace_capture=trace_frozen=steady=false;
    context=(stall_context_t){0};
    result=(stall_result_t){.reason="Motor stopped",.state="unknown"};
    stable_since=last_evaluation=0; clear_window();
}
void stall_estimator_context(stall_context_t c,uint64_t now) {
    bool changed=c.moving!=context.moving || c.voltage!=context.voltage ||
        c.direction!=context.direction || c.factor!=context.factor || fabsf(c.speed-context.speed)>.001f;
    context=c;
    steady=c.moving && c.healthy && c.factor>0 &&
        fabsf(c.profile_speed-c.speed)<=fmaxf(.01f,c.speed*.05f);
    if(changed && c.moving) { trace_frozen=false; clear_window(); }
    if(trace_capture && !steady && available>=100) trace_frozen=true;
    if(changed || !steady) {
        if(trace_capture && (trace_frozen || !c.moving)) job_active=false;
        else clear_window();
        stable_since=now; result.valid=false; result.state="unknown";
    }
    if(!c.moving) result.reason="Motor stopped";
    else if(!c.healthy) result.reason="Electrical or sensor fault";
    else if(!steady) result.reason="Waiting for steady speed";
}
void stall_estimator_gap(uint64_t count) {
    result.dropped+=count; clear_window(); result.valid=false;
    result.state="unknown"; result.reason="Current sample gap";
}
void stall_estimator_bin(float a,float b,float ripple,float variance,uint64_t at) {
    if(last_bin && at-last_bin!=1000) stall_estimator_gap(0);
    last_bin=at; bins[head][0]=a; bins[head][1]=b; bins[head][2]=ripple; bins[head][3]=variance;
    head=(head+1)%STALL_HISTORY; if(available<STALL_HISTORY) available++;
}
void stall_estimator_sample(uint16_t a,uint16_t b,uint64_t at) {
    if(!context.moving || !context.healthy || trace_frozen) return;
    if(a<16 || a>4079 || b<16 || b>4079 || (a&0x8000) || (b&0x8000)) { stall_estimator_gap(1); return; }
    if(!raw_count) { bin_start=at; low[0]=high[0]=a; low[1]=high[1]=b; }
    if(at!=bin_start+raw_count*10) { stall_estimator_gap(1); return; }
    uint16_t v[2]={a,b};
    for(unsigned w=0;w<2;w++) {
        sum[w]+=v[w]; square[w]+=(uint64_t)v[w]*v[w];
        if(v[w]<low[w]) low[w]=v[w];
        if(v[w]>high[w]) high[w]=v[w];
    }
    raw_count++; result.analyzed++;
    if(raw_count==100) {
        const float gain=3.3f/4095;
        float mean[2]={sum[0]/100.0f,sum[1]/100.0f};
        float variance=fmaxf(0,(float)(square[0]/100.0)-mean[0]*mean[0])+
            fmaxf(0,(float)(square[1]/100.0)-mean[1]*mean[1]);
        stall_estimator_bin(mean[0]*gain-1.65f,mean[1]*gain-1.65f,
            ((high[0]-low[0])+(high[1]-low[1]))*.5f*gain,variance*.5f*gain*gain,bin_start);
        raw_count=0; memset(sum,0,sizeof(sum)); memset(square,0,sizeof(square));
    }
}
static void feature_step(feature_job_t *j,unsigned limit) {
    unsigned end=j->pos+limit; if(end>j->n) end=j->n;
    for(;j->pos<end;j->pos++) {
        const float *s=j->samples[j->pos]; float r=sqrtf(s[0]*s[0]+s[1]*s[1]),p=s[2];
        j->radial+=r; j->radial2+=(double)r*r;
        j->ripple+=p; j->ripple2+=(double)p*p; j->variance+=fmaxf(0,s[3]);
        if(j->pos) j->difference+=fabsf(r-j->previous_radial);
        j->previous_radial=r;
    }
}
static bool feature_finish(const feature_job_t *j,float f[STALL_FEATURE_COUNT]) {
    double r=j->radial/j->n,p=j->ripple/j->n;
    if(r<.05 || !isfinite(r)) return false;
    f[0]=(float)r;
    f[1]=(float)(sqrt(fmax(0,j->radial2/j->n-r*r))/(r+1e-8));
    f[2]=(float)(p/(r+1e-8));
    f[3]=(float)(sqrt(fmax(0,j->ripple2/j->n-p*p))/(p+1e-8));
    f[4]=(float)(sqrt(j->variance/j->n)/(r+1e-8));
    f[5]=(float)(j->difference/(j->n-1)/(r*j->speed/1000+1e-8));
    for(unsigned i=0;i<STALL_FEATURE_COUNT;i++) if(!isfinite(f[i])) return false;
    return true;
}
bool stall_estimator_features(const float (*samples)[4],unsigned n,float speed,float f[STALL_FEATURE_COUNT]) {
    if(n<2 || n>STALL_MAX_WINDOW || !isfinite(speed) || speed<=0) return false;
    feature_job_t local={.samples=samples,.n=n,.speed=speed};
    feature_step(&local,n); return feature_finish(&local,f);
}
static bool profile_at(float speed,stall_model_profile_t *p,bool *exact) {
    if(!isfinite(speed) || speed<stall_profiles[0].speed || speed>stall_profiles[STALL_MODEL_PROFILE_COUNT-1].speed) return false;
    for(unsigned i=0;i<STALL_MODEL_PROFILE_COUNT;i++) {
        if(fabsf(speed-stall_profiles[i].speed)<.01f) { *p=stall_profiles[i]; *exact=true; return true; }
        if(i && speed<stall_profiles[i].speed) {
            const stall_model_profile_t *a=&stall_profiles[i-1],*b=&stall_profiles[i];
            float t=(speed-a->speed)/(b->speed-a->speed); *p=*a; p->speed=speed;
            p->window_ms=(unsigned)ceilf(a->window_ms+t*((float)b->window_ms-a->window_ms));
            for(unsigned k=0;k<STALL_FEATURE_COUNT;k++) {
                p->coefficient[k]=a->coefficient[k]+t*(b->coefficient[k]-a->coefficient[k]);
                p->low[k]=a->low[k]+t*(b->low[k]-a->low[k]);
                p->high[k]=a->high[k]+t*(b->high[k]-a->high[k]);
            }
            p->bias=a->bias+t*(b->bias-a->bias); p->span=a->span+t*(b->span-a->span);
            p->supported=false; *exact=false; return true;
        }
    }
    return false;
}
stall_result_t stall_estimator_classify(const float f[STALL_FEATURE_COUNT],float speed,int voltage,float temperature) {
    stall_result_t r={.state="unknown",.reason="Outside trained speed range (12.5-100 full steps/sec)"};
    stall_model_profile_t p; bool exact;
    if(!profile_at(speed,&p,&exact)) return r;
    r.window_ms=p.window_ms;
    if(voltage!=9) { r.reason="No stall model for this voltage request"; return r; }
    if(!isfinite(temperature)) { r.reason="Temperature unavailable"; return r; }
    if(f[0]<.05f) { r.reason="Insufficient winding current"; return r; }
    float margin=p.bias;
    for(unsigned i=0;i<STALL_FEATURE_COUNT;i++) {
        if(!isfinite(f[i])) { r.reason="Invalid current features"; return r; }
        margin+=p.coefficient[i]*f[i];
        if((i==0 || p.coefficient[i]!=0) && (f[i]<p.low[i] || f[i]>p.high[i])) r.outlier=true;
    }
    r.score=fminf(100,fmaxf(0,65+35*margin/p.span)); r.valid=true; r.coverage=1;
    r.extrapolated=!exact || !p.supported || temperature<STALL_MODEL_MIN_TEMP || temperature>STALL_MODEL_MAX_TEMP;
    if(r.outlier) { r.state="uncertain"; r.reason="Current pattern outside recorded range"; }
    else if(!exact) { r.state="uncertain"; r.reason="Interpolated speed; not validated"; }
    else if(!p.supported) { r.state="uncertain"; r.reason="Low-speed validation was inconsistent"; }
    else if(r.extrapolated) { r.state="uncertain"; r.reason="Temperature outside recorded range"; }
    else if(fabsf(margin)<p.span*.1f) { r.state="uncertain"; r.reason="Current patterns overlap"; }
    else { r.state=margin>=0?"suspected":"free"; r.reason="Speed-conditioned current model"; }
    return r;
}
void stall_estimator_evaluate(uint64_t now) {
    if(job_active) {
        feature_step(&job,128);
        if(job.pos<job.n) return;
        job_active=false; float f[STALL_FEATURE_COUNT];
        if(!feature_finish(&job,f)) { result.valid=false; result.state="unknown"; result.reason="Insufficient winding current"; return; }
        stall_result_t estimate=stall_estimator_classify(f,job.speed,context.voltage,context.temperature);
        estimate.analyzed=result.analyzed; estimate.dropped=result.dropped; estimate.compute_max_us=result.compute_max_us;
        estimate.window_end_us=job.end_us; estimate.computed_at_us=now; estimate.ripple_rms=sqrtf((float)(job.variance/job.n));
        result=estimate; return;
    }
    if(!calibrated || !context.moving || !context.healthy || !steady) {
        result.valid=false; result.state="unknown";
        if(!calibrated) result.reason="No calibration for this motor";
        return;
    }
    if(now-last_evaluation<STALL_EVALUATION_US) return;
    last_evaluation=now;
    float speed=context.factor>0?context.speed/context.factor:0;
    stall_model_profile_t p; bool exact;
    if(!profile_at(speed,&p,&exact)) { result.valid=false; result.state="unknown"; result.reason="Outside trained speed range (12.5-100 full steps/sec)"; return; }
    result.window_ms=p.window_ms;
    if(context.voltage!=9) { result.valid=false; result.state="unknown"; result.reason="No stall model for this voltage request"; return; }
    if(now-stable_since<p.window_ms*1000u || available<p.window_ms || !last_bin || now-last_bin>100000) {
        result.valid=false; result.state="unknown"; result.reason="Collecting continuous current samples"; return;
    }
    for(unsigned i=0;i<p.window_ms;i++) memcpy(window[i],bins[(head+STALL_HISTORY-p.window_ms+i)%STALL_HISTORY],sizeof(window[i]));
    job=(feature_job_t){.samples=window,.n=p.window_ms,.speed=speed,.end_us=last_bin+1000};
    job_active=true;
}
void stall_estimator_compute_time(uint32_t us) { if(us>result.compute_max_us) result.compute_max_us=us; }
stall_result_t stall_estimator_result(void) { return result; }
const char *stall_estimator_model_id(void) { return STALL_MODEL_ID; }
bool stall_estimator_active(void) { return context.moving && context.healthy && !trace_frozen; }
bool stall_estimator_trace_begin(void) {
    if(context.moving) return false;
    trace_capture=true; trace_frozen=false; clear_window(); return true;
}
unsigned stall_estimator_trace_count(void) { return trace_capture && !context.moving?available:0; }
bool stall_estimator_trace_bin(unsigned index,float values[4],uint64_t *at) {
    unsigned count=stall_estimator_trace_count(); if(index>=count) return false;
    memcpy(values,bins[(head+STALL_HISTORY-count+index)%STALL_HISTORY],sizeof(bins[0]));
    *at=last_bin-(uint64_t)(count-1-index)*1000; return true;
}
