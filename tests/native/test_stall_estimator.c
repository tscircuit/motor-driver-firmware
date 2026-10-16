#include "stall_estimator.h"
#include "stall_fixture.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
static uint64_t now=1000000;
static void feed(const float s[500][4],unsigned count){for(unsigned i=0;i<count;i++){stall_estimator_bin(s[i%500][0],s[i%500][1],s[i%500][2],s[i%500][3],now);now+=1000;}for(unsigned j=0;j<2048;j++)stall_estimator_evaluate(now);}
int main(void){
 float f[7];bool outlier;
 assert(stall_estimator_features(free_samples,500,60,f));
 assert(stall_estimator_score(f,false,&outlier)<10&&!outlier);
 assert(stall_estimator_features(stalled_samples,500,60,f));
 assert(stall_estimator_score(f,false,&outlier)>65);
 stall_estimator_init(true);
 stall_context_t c={.moving=true,.healthy=true,.voltage=9,.direction=1,.factor=2,.speed=120,.profile_speed=120,.temperature=30};
 stall_estimator_context(c,now);feed(free_samples,1500);
 stall_result_t r=stall_estimator_result();assert(r.valid&&!r.extrapolated&&r.score<10&&r.coverage==1);
 c.factor=16;c.speed=c.profile_speed=960;stall_estimator_context(c,now);feed(free_samples,1500);r=stall_estimator_result();assert(r.valid&&!r.extrapolated&&r.score<10);
 c.voltage=15;c.direction=-1;stall_estimator_context(c,now);feed(free_samples,1500);r=stall_estimator_result();assert(r.valid&&r.extrapolated);
 stall_estimator_gap(1000);r=stall_estimator_result();assert(!r.valid&&r.dropped==1000);
 feed(stalled_samples,1500);assert(stall_estimator_result().valid);
 c.healthy=false;stall_estimator_context(c,now);assert(!stall_estimator_result().valid);
 c.healthy=true;c.moving=false;stall_estimator_context(c,now);assert(!stall_estimator_result().valid);
 c.moving=true;c.factor=1;c.speed=c.profile_speed=400;stall_estimator_context(c,now);feed(free_samples,1500);assert(!stall_estimator_result().valid);
 stall_estimator_init(false);c.speed=c.profile_speed=60;stall_estimator_context(c,now);feed(free_samples,1500);assert(!stall_estimator_result().valid);
 /* Full-rate sample accounting, valid ADC bounds, gaps, and RMS consume raw
    samples independently of any USB export. */
 stall_estimator_init(true);stall_estimator_context(c,now);
 for(unsigned i=0;i<150000;i++){float phase=6.2831853f*15*i/100000;uint16_t a=(uint16_t)(2048+1000*sinf(phase)),b=(uint16_t)(2048+1000*cosf(phase));stall_estimator_sample(a,b,now);now+=10;}
 for(unsigned j=0;j<2048;j++){stall_estimator_evaluate(now);}
 r=stall_estimator_result();assert(r.analyzed==150000&&r.dropped==0&&r.valid);
 stall_estimator_sample(0,2048,now);assert(!stall_estimator_result().valid&&stall_estimator_result().dropped==1);
 puts("PASS native current estimator: recorded classes, resolution normalization, extrapolation, raw 100 kHz accounting, stopped/fault/data-gap and bandwidth gates");
}
