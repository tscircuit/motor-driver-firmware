#include "stall_response.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    stall_response_t s = {0};
    stall_response_update(&s, true, 1000000, 0);
    stall_response_update(&s, true, 90000000, 0);
    assert(s.active && !s.timed_out); /* default off even for a long stall */
    assert(stall_response_tone(&s, true, 1000000) == 1800);
    assert(stall_response_tone(&s, true, 1070000) == 0);
    assert(stall_response_tone(&s, true, 1140000) == 1800);
    assert(stall_response_tone(&s, true, 1280000) == 1800);
    assert(stall_response_tone(&s, true, 1350000) == 0);
    assert(stall_response_tone(&s, false, 1000000) == 0);
    stall_response_update(&s, false, 90000001, 1000);
    assert(!s.active && !s.timed_out);
    stall_response_update(&s, true, 91000000, 1000);
    stall_response_update(&s, true, 91999999, 1000);
    assert(!s.timed_out);
    stall_response_update(&s, true, 92000000, 1000);
    assert(s.timed_out);
    stall_response_update(&s, false, 92000001, 1000);
    stall_response_update(&s, true, 93000000, 1000);
    assert(!s.timed_out && s.since_us == 93000000);
    puts("Stall response: timing, reset, disabled auto-stop and triple chirp passed");
}
