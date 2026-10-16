#include "travel_limits.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    travel_limits_t l={true,0,1600};
    assert(travel_budget(l,0,-1,0,10)==0);
    assert(travel_budget(l,1600,1,0,10)==0);
    assert(travel_budget(l,1590,1,0,100)==10);
    assert(travel_budget(l,10,-1,0,100)==10);
    assert(travel_budget(l,1580,1,15,100)==5);
    assert(travel_budget(l,20,-1,15,100)==5);
    assert(travel_budget(l,1580,1,20,100)==0);
    assert(travel_budget(l,-1,1,0,100)==0);
    assert(travel_budget(l,1601,-1,0,100)==0);
    /* Simulate a pipeline: reserve blocks before any pulse reaches the counter. */
    unsigned generated=0,executed=0;
    for(int i=0;i<10;i++) generated+=travel_budget(l,1500+executed,1,generated-executed,32);
    assert(generated==100);
    executed=70;
    assert(travel_budget(l,1500+executed,1,generated-executed,32)==0);
    l.enabled=false;assert(travel_budget(l,-1,-1,1000,32)==32);
    puts("Travel limits: both ends, queued pulse reservations, out-of-range and unbounded mode passed");
}
