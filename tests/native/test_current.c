#include "current_capture.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
 current_stats_t s={0};
 current_stats_add(&s,0);current_stats_add(&s,4095);current_stats_add(&s,0x8000|2048);
 assert(!s.count);
 for(unsigned i=0;i<100;i++)current_stats_add(&s,i==37?3000:2000);
 assert(s.count==100&&s.min==2000&&s.max==3000&&s.sum==201000);
 current_stats_t negative={0};current_stats_add(&negative,100);current_stats_add(&negative,16);
 assert(negative.min==16&&negative.max==100&&negative.sum==116&&negative.count==2);
 puts("Native current capture: short peaks, signed range and invalid ADC exclusion passed");
}
