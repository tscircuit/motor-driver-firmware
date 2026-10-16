#include "stall_estimator.h"
#include "stall_trained_fixture.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static uint64_t now=1000000;
static uint64_t feed(const trained_case_t *t,unsigned count) {
    uint64_t first=0;
    for(unsigned i=0;i<count;i++) {
        const float *s=t->samples[i%t->n];
        stall_estimator_bin(s[0],s[1],s[2],s[3],now); now+=1000;
        stall_estimator_evaluate(now);
        if(!first && stall_estimator_result().valid) first=now;
    }
    for(unsigned i=0;i<32;i++) stall_estimator_evaluate(now);
    return first;
}
int main(void) {
    float f[STALL_FEATURE_COUNT];
    for(unsigned i=0;i<sizeof(trained_cases)/sizeof(trained_cases[0]);i++) {
        const trained_case_t *t=&trained_cases[i];
        assert(stall_estimator_features(t->samples,t->n,t->speed,f));
        for(unsigned k=0;k<STALL_FEATURE_COUNT;k++) assert(fabsf(f[k]-t->expected[k])<1e-5f);
        stall_result_t r=stall_estimator_classify(f,t->speed,9,40);
        assert(r.valid && fabsf(r.score-t->score)<.01f);
        assert((r.score>=65)==t->stalled);
        if(t->speed==12.5f) assert(strcmp(r.state,"uncertain")==0 && r.extrapolated);
    }
    const trained_case_t *free60=&trained_cases[4];
    assert(stall_estimator_features(free60->samples,free60->n,60,f));
    assert(!stall_estimator_classify(f,60,12,40).valid);
    assert(!stall_estimator_classify(f,4,9,40).valid);
    assert(!stall_estimator_classify(f,101,9,40).valid);
    assert(strcmp(stall_estimator_classify(f,40,9,40).state,"uncertain")==0);
    assert(strcmp(stall_estimator_classify(f,60,9,25).state,"uncertain")==0);
    float outlier[STALL_FEATURE_COUNT]={20,20,20,20,20,20};
    assert(stall_estimator_classify(outlier,60,9,40).outlier);
    outlier[1]=NAN; assert(!stall_estimator_classify(outlier,60,9,40).valid);

    stall_estimator_init();
    stall_context_t c={.moving=true,.healthy=true,.voltage=9,.direction=1,.factor=2,.speed=120,.profile_speed=120,.temperature=40};
    stall_estimator_context(c,now);uint64_t start=now,first=feed(free60,500);
    assert(first && first-start<200000); /* No fixed one-second startup delay. */
    stall_result_t r=stall_estimator_result();assert(r.valid && r.score<65 && r.window_ms==100);
    c.factor=16;c.speed=c.profile_speed=960;stall_estimator_context(c,now);feed(free60,500);
    assert(stall_estimator_result().valid && stall_estimator_result().score<65);
    c.direction=-1;stall_estimator_context(c,now);feed(free60,500);assert(stall_estimator_result().valid);
    stall_estimator_gap(1000);assert(!stall_estimator_result().valid && stall_estimator_result().dropped==1000);
    feed(free60,500);assert(stall_estimator_result().valid);
    c.healthy=false;stall_estimator_context(c,now);assert(!stall_estimator_result().valid);
    c.healthy=true;c.moving=false;stall_estimator_context(c,now);assert(!stall_estimator_result().valid);
    stall_estimator_init();c.moving=true;stall_estimator_context(c,now);feed(free60,500);assert(stall_estimator_result().valid);

    stall_estimator_init();stall_estimator_context(c,now);
    for(unsigned i=0;i<150000;i++) {
        float phase=6.2831853f*15*i/100000;
        stall_estimator_sample((uint16_t)(2048+1000*sinf(phase)),(uint16_t)(2048+1000*cosf(phase)),now);now+=10;
    }
    for(unsigned i=0;i<32;i++) stall_estimator_evaluate(now);
    r=stall_estimator_result();assert(r.analyzed==150000 && r.dropped==0 && r.valid);
    stall_estimator_sample(0,2048,now);assert(!stall_estimator_result().valid && stall_estimator_result().dropped==1);

    c.moving=false;stall_estimator_context(c,now);assert(stall_estimator_trace_begin());
    c.moving=true;stall_estimator_context(c,now);assert(!stall_estimator_trace_begin());feed(free60,1500);
    float values[4];uint64_t at;
    assert(stall_estimator_trace_count()==0 && !stall_estimator_trace_bin(0,values,&at));
    c.profile_speed=10;stall_estimator_context(c,now);assert(!stall_estimator_active());
    stall_estimator_context(c,now+1000);c.moving=false;stall_estimator_context(c,now);
    assert(stall_estimator_trace_count()==1500 && !stall_estimator_result().valid);
    assert(stall_estimator_trace_bin(1499,values,&at) && at==now-1000);
    assert(fabsf(values[0]-free60->samples[1499%free60->n][0])<1e-6f);
    assert(!stall_estimator_trace_bin(1500,values,&at));
    c.moving=true;c.profile_speed=c.speed;stall_estimator_context(c,now);feed(free60,5000);
    c.moving=false;stall_estimator_context(c,now);
    assert(stall_estimator_trace_count()==STALL_HISTORY);
    assert(stall_estimator_trace_bin(0,values,&at) && at==now-STALL_HISTORY*1000);
    assert(stall_estimator_trace_begin() && stall_estimator_trace_count()==0);
    puts("PASS trained current model: recorded feature parity, fast windows, uncertainty/domain gates, raw accounting, gaps and trace retention");
}
