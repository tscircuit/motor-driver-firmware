#include "profile.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static void finite_case(double speed, double acceleration, uint32_t count, double initial) {
    profile_t p;
    profile_init(&p, speed, acceleration, initial, count, true);
    uint32_t generated = 0, n, delay;
    double seconds = 0;
    while (profile_segment(&p, generated, &n, &delay)) {
        assert(n >= 1 && n <= 64 && generated + n <= count);
        assert(delay >= 16 && delay <= UINT32_MAX - 34);
        seconds += (double)(delay + 34) * n / PULSE_CLOCK_HZ;
        generated += n;
    }
    assert(generated == count);
    assert(isfinite(seconds) && seconds > 0);
    /* Quantization is never faster than the requested profile. */
    assert(seconds >= p.duration - 1e-9);
    assert(seconds - p.duration < count / (double)PULSE_CLOCK_HZ + 1e-6);
}
int main(void) {
    assert(resolution_factor("quarter") == 4);
    assert(resolution_factor("bad") == 0);
    assert(profile_validate(16000, 40000, 4, 100000));
    assert(!profile_validate(50001, 100, 4, 0));
    assert(!profile_validate(NAN, 100, 4, 0));
    assert(!profile_validate(INFINITY, 100, 4, 0));
    assert(!profile_validate(0, 100, 4, 0));
    assert(!profile_validate(1, 100, 3, 0));
    assert(!profile_validate(1, 100, 1, 100001));
    const uint32_t counts[] = {1, 2, 17, 64, 65, 800, 64000, 100000};
    const double rates[] = {0.01, 40, 64000, 200000};
    for (size_t i = 0; i < sizeof(counts) / sizeof(counts[0]); ++i)
        for (size_t j = 0; j < sizeof(rates) / sizeof(rates[0]); ++j)
            finite_case(rates[j], rates[j] / 0.4, counts[i], 0);
    finite_case(640, 1600, 1600, 160);
    profile_t p;
    profile_init(&p, 64000, 160000, 0, 0, true);
    assert(fabs(profile_time(&p, 12800) - 0.4) < 1e-8);
    assert(fabs(profile_speed(&p, 12800) - 64000) < 1e-8);
    profile_init(&p, 32000, 80000, 64000, 0, true);
    assert(fabs(profile_time(&p, 19200) - 0.4) < 1e-8);
    assert(fabs(profile_speed(&p, 19200) - 32000) < 1e-8);
    p = (profile_t){.initial = 64000, .count = 12800, .acceleration = 160000, .braking = true};
    assert(fabs(profile_time(&p, 12800) - 0.4) < 1e-8);
    assert(profile_speed(&p, 12800) == 0);
    uint32_t n, delay;
    assert(profile_segment(&p, 12799, &n, &delay) && n == 1 && delay >= 16);
    profile_init(&p, 200000, 1, 200000, 100000, false);
    for (uint32_t i = 0; i < 100000; i += 64) {
        assert(profile_segment(&p, i, &n, &delay));
        assert(delay == 16); /* Exactly 50 ticks; no floating-point cancellation jitter. */
    }
    profile_init(&p, 200000, 500000, 200000, 0, true);
    assert(profile_segment(&p, 1000000000u, &n, &delay) && delay == 16);
    puts("Native motion profiles: counts, 400 ms ramps, braking, speed/resolution bounds and "
         "timing quantization passed");
}
