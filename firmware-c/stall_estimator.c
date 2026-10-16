#include "stall_estimator.h"
#include <math.h>
#include <string.h>
static float bins[STALL_HISTORY][4];
static unsigned head, available;
static uint64_t last_bin, stable_since, last_evaluation;
static bool calibrated, job_active, trace_capture, trace_frozen;
static stall_context_t context;
static stall_result_t result;
static uint32_t raw_count, sum[2], low[2], high[2];
static uint64_t square[2], bin_start;
static const float free_model[7]={0.8541027778f,0.03647127341f,0.09991558061f,0.2047651325f,0.01310903761f,0.005485620016f,0.007647866823f};
static const float stall_model[7]={0.8248254064f,0.1464264593f,0.116381948f,0.7381990353f,0.3902486648f,1.191072041f,0.1620298415f};
static const float scale[7]={0.01f,0.01869123035f,0.005f,0.0382675868f,0.2023342691f,0.5694764015f,0.08654615453f};
static void clear_window(void){job_active=false;head=available=raw_count=0;last_bin=0;bin_start=0;memset(sum,0,sizeof(sum));memset(square,0,sizeof(square));}
void stall_estimator_init(bool device){calibrated=device;trace_capture=trace_frozen=false;context=(stall_context_t){0};result=(stall_result_t){.reason="Motor stopped"};stable_since=last_evaluation=0;clear_window();}
void stall_estimator_context(stall_context_t c,uint64_t now){
 bool changed=c.moving!=context.moving||c.voltage!=context.voltage||c.direction!=context.direction||c.factor!=context.factor||fabsf(c.speed-context.speed)>.001f;
 context=c;
 bool steady=c.moving&&c.healthy&&c.factor>0&&fabsf(c.profile_speed-c.speed)<=fmaxf(.01f,c.speed*.05f);
 if(changed&&c.moving)trace_frozen=false;
 if(trace_capture&&!steady&&available>=100)trace_frozen=true;
 if(changed||!steady){
  /* Preserve samples for post-run download, without running the estimator on
     a stopped motor. Starting again still clears the old window normally. */
  if(trace_capture&&(trace_frozen||!c.moving)){job_active=false;}else{clear_window();}
  stable_since=now;result.valid=false;
 }
 if(!c.moving){result.valid=false;result.reason="Motor stopped";}
 else if(!c.healthy){result.valid=false;result.reason="Electrical or sensor fault";}
 else if(!steady){result.valid=false;result.reason="Waiting for steady speed";}
}
void stall_estimator_gap(uint64_t count){result.dropped+=count;clear_window();result.valid=false;result.reason="Current sample gap";}
void stall_estimator_bin(float a,float b,float ripple,float variance,uint64_t at){
 if(last_bin&&at-last_bin!=1000){clear_window();result.valid=false;result.reason="Current sample gap";}
 last_bin=at;bins[head][0]=a;bins[head][1]=b;bins[head][2]=ripple;bins[head][3]=variance;head=(head+1)%STALL_HISTORY;if(available<STALL_HISTORY)available++;
}
void stall_estimator_sample(uint16_t a,uint16_t b,uint64_t at){
 if(!context.moving||!context.healthy||trace_frozen)return;
 if(a<16||a>4079||b<16||b>4079||(a&0x8000)||(b&0x8000)){stall_estimator_gap(1);return;}
 if(!raw_count){bin_start=at;low[0]=high[0]=a;low[1]=high[1]=b;}
 if(at!=bin_start+raw_count*10){stall_estimator_gap(1);return;}
 uint16_t v[2]={a,b};
 for(unsigned w=0;w<2;w++){sum[w]+=v[w];square[w]+=(uint64_t)v[w]*v[w];if(v[w]<low[w])low[w]=v[w];if(v[w]>high[w])high[w]=v[w];}
 raw_count++;result.analyzed++;
 if(raw_count==100){
  const float gain=3.3f/4095;float mean[2]={sum[0]/100.0f,sum[1]/100.0f};
  float variance=fmaxf(0,(float)(square[0]/100.0)-mean[0]*mean[0])+fmaxf(0,(float)(square[1]/100.0)-mean[1]*mean[1]);
  stall_estimator_bin(mean[0]*gain-1.65f,mean[1]*gain-1.65f,((high[0]-low[0])+(high[1]-low[1]))*.5f*gain,variance*.5f*gain*gain,bin_start);
  raw_count=0;memset(sum,0,sizeof(sum));memset(square,0,sizeof(square));
 }
}
typedef struct {
 const float (*samples)[4];unsigned n,pos;bool centered;float ma,mb,sr,sr2,sp,sp2;
 float re[3][2],im[3][2],cs[3],sn[3],dc[3],ds[3];
} feature_job_t;
static feature_job_t job;
static void feature_start(feature_job_t *j,const float (*samples)[4],unsigned n,float speed){
 memset(j,0,sizeof(*j));j->samples=samples;j->n=n;
 for(unsigned h=0;h<3;h++){float angle=6.28318530718f*speed/4*(2*h+1)/1000;j->cs[h]=1;j->dc[h]=cosf(angle);j->ds[h]=sinf(angle);}
}
static void feature_step(feature_job_t *j,unsigned limit){
 if(!j->centered){
  unsigned end=j->pos+limit;if(end>j->n)end=j->n;
  for(;j->pos<end;j->pos++){j->ma+=j->samples[j->pos][0];j->mb+=j->samples[j->pos][1];}
  if(j->pos==j->n){j->ma/=j->n;j->mb/=j->n;j->centered=true;j->pos=0;}return;
 }
 unsigned end=j->pos+limit;if(end>j->n)end=j->n;
 for(;j->pos<end;j->pos++){
  const float *s=j->samples[j->pos];float a=s[0]-j->ma,b=s[1]-j->mb,r=sqrtf(a*a+b*b),p=s[2];j->sr+=r;j->sr2+=r*r;j->sp+=p;j->sp2+=p*p;
  for(unsigned h=0;h<3;h++){j->re[h][0]+=a*j->cs[h];j->im[h][0]-=a*j->sn[h];j->re[h][1]+=b*j->cs[h];j->im[h][1]-=b*j->sn[h];float c=j->cs[h]*j->dc[h]-j->sn[h]*j->ds[h];j->sn[h]=j->sn[h]*j->dc[h]+j->cs[h]*j->ds[h];j->cs[h]=c;}
 }
}
static bool feature_finish(feature_job_t *j,float f[7]){
 float mean=j->sr/j->n,rip=j->sp/j->n,amp[3];if(mean<.05f||!isfinite(mean))return false;
 for(unsigned h=0;h<3;h++)amp[h]=hypotf(j->re[h][0],j->im[h][0])+hypotf(j->re[h][1],j->im[h][1]);
 f[0]=mean;f[1]=sqrtf(fmaxf(0,j->sr2/j->n-mean*mean))/mean;f[2]=rip/mean;f[3]=sqrtf(fmaxf(0,j->sp2/j->n-rip*rip))/(rip+1e-9f);f[4]=amp[1]/(amp[0]+1e-9f);f[5]=amp[2]/(amp[0]+1e-9f);f[6]=fabsf(j->re[0][0]*j->re[0][1]+j->im[0][0]*j->im[0][1])/(hypotf(j->re[0][0],j->im[0][0])*hypotf(j->re[0][1],j->im[0][1])+1e-12f);
 for(unsigned i=0;i<7;i++){if(!isfinite(f[i]))return false;}return true;
}
bool stall_estimator_features(const float (*samples)[4],unsigned n,float speed,float f[7]){
 if(n<100||!isfinite(speed)||speed<=0)return false;
 feature_job_t local;feature_start(&local,samples,n,speed);feature_step(&local,n);feature_step(&local,n);return feature_finish(&local,f);
}
float stall_estimator_score(const float f[7],bool extrapolate,bool *outlier){
 float a=0,b=0;for(unsigned i=0;i<7;i++){/* Unknown current amplitude is removed when extrapolating. */float v=extrapolate&&i==0?free_model[0]:f[i];a+=(v-free_model[i])*(v-free_model[i])/(scale[i]*scale[i]);b+=(v-stall_model[i])*(v-stall_model[i])/(scale[i]*scale[i]);}
 a=sqrtf(a/7);b=sqrtf(b/7);*outlier=fminf(a,b)>3.5f;return 100*a/(a+b+1e-12f);
}
void stall_estimator_evaluate(uint64_t now){
 if(job_active){
  feature_step(&job,job.centered?32:512);
  if(!job.centered||job.pos<job.n)return;
  job_active=false;float f[7];
  if(!feature_finish(&job,f)){result.valid=false;result.reason="Insufficient winding current";return;}
  result.score=stall_estimator_score(f,result.extrapolated,&result.outlier);result.extrapolated|=result.outlier;result.coverage=1;result.valid=true;result.reason=result.extrapolated?"Unvalidated extrapolation":"Calibrated operating point";return;
 }
 if(now-last_evaluation<250000)return;
 last_evaluation=now;
 if(!calibrated){result.valid=false;result.reason="No calibration for this motor";return;}
 if(!context.moving){result.valid=false;result.reason="Motor stopped";return;}
 if(!context.healthy){result.valid=false;result.reason="Electrical or sensor fault";return;}
 float speed=context.factor>0?context.speed/context.factor:0;
 if(speed<4||speed>320){result.valid=false;result.reason="Insufficient electrical cycles or feature bandwidth";return;}
 unsigned n=(unsigned)ceilf(fmaxf(500,16000/speed));result.window_ms=n;
 if(now-stable_since<1000000||available<n||!last_bin||now-last_bin>100000){result.valid=false;result.reason="Collecting continuous current samples";return;}
 static float window[4000][4];float variance=0;
 for(unsigned i=0;i<n;i++){unsigned index=(head+STALL_HISTORY-n+i)%STALL_HISTORY;memcpy(window[i],bins[index],sizeof(window[i]));variance+=window[i][3];}
 result.extrapolated=context.voltage!=9||context.direction!=1||fabsf(speed-60)>.01f||context.temperature<25||context.temperature>40;
 result.ripple_rms=sqrtf(variance/n);feature_start(&job,window,n,speed);job_active=true;
}
void stall_estimator_compute_time(uint32_t us){if(us>result.compute_max_us)result.compute_max_us=us;}
stall_result_t stall_estimator_result(void){return result;}

bool stall_estimator_active(void){return context.moving&&context.healthy&&!trace_frozen;}
bool stall_estimator_trace_begin(void){
 if(context.moving)return false;
 trace_capture=true;trace_frozen=false;clear_window();return true;
}
unsigned stall_estimator_trace_count(void){return trace_capture&&!context.moving?available:0;}
bool stall_estimator_trace_bin(unsigned index,float values[4],uint64_t *at){
 unsigned count=stall_estimator_trace_count();
 if(index>=count)return false;
 memcpy(values,bins[(head+STALL_HISTORY-count+index)%STALL_HISTORY],sizeof(bins[0]));
 *at=last_bin-(uint64_t)(count-1-index)*1000;return true;
}
