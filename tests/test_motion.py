import unittest
from fakes import Clock
from core.motion import Motion


class MotionTests(unittest.TestCase):
    def test_short_and_long_moves_are_symmetric_and_speed_limited(self):
        for count in (1, 2, 3, 10, 2000):
            motion = Motion(Clock(), 400, 100, 10, count)
            periods = [motion.interval(i) for i in range(count)]
            self.assertEqual(periods, periods[::-1])
            self.assertGreaterEqual(min(periods), 2500)
            self.assertEqual(periods[:(count+1)//2], sorted(periods[:(count+1)//2], reverse=True))
            if count > 1000: self.assertEqual(min(periods), 2500)

    def test_continuous_accelerates_and_caps_at_target(self):
        motion = Motion(Clock(), 400, 100, 10, None)
        periods = [motion.interval(i) for i in range(2000)]
        self.assertEqual(periods, sorted(periods, reverse=True))
        self.assertEqual(periods[-1], 2500)
        self.assertGreater(periods[0], 50000)

    def test_wrap_and_lateness_never_create_catchup_burst(self):
        clock = Clock()
        clock.now = (clock.modulus - 1000) / 1000
        motion = Motion(clock, 400, 100, 10, None)
        self.assertFalse(motion.due())
        clock.sleep_us(100000 + motion.interval_us)
        self.assertTrue(motion.due())
        clock.sleep_us(1000)
        motion.advanced()
        self.assertEqual(motion.late_steps, 1)
        self.assertEqual(motion.max_lateness_us, 1000)
        self.assertFalse(motion.due())
        self.assertEqual(clock.ticks_diff(motion.deadline, clock.ticks_us()), motion.interval_us)

    def test_target_below_start_rate_is_respected(self):
        motion = Motion(Clock(), 5, 100, 10, 2)
        self.assertEqual(motion.interval(0), 200000)
        self.assertEqual(motion.interval(1), 200000)

    def test_braking_is_bounded_monotonic_and_idempotent(self):
        for speed in (5, 10, 40, 100, 400):
            clock = Clock()
            motion = Motion(clock, speed, 100, 10, None)
            for _ in range(1000):
                clock.sleep_us(max(0, clock.ticks_diff(motion.deadline, clock.ticks_us())))
                motion.advanced()
            deadline = motion.deadline
            tail = motion.brake()
            self.assertLessEqual(tail, max(1, __import__('math').ceil(speed ** 2 / 200)))
            self.assertEqual(motion.deadline, deadline)
            self.assertEqual(motion.brake(), tail)
            periods = [motion.interval(i) for i in range(motion.executed, motion.count)]
            self.assertEqual(periods, sorted(periods))
            self.assertLessEqual(motion.rate, min(speed, 10))

    def test_fixed_ramp_zero_to_full_and_back_is_400ms(self):
        for speed in (50, 150, 1000, 10000):
            motion = Motion(Clock(), speed, speed / 0.4, 10, None, 0, fixed_ramp=True)
            count = int(speed * 0.2)
            ramp_us = sum(motion.interval(i) for i in range(count))
            self.assertAlmostEqual(ramp_us, 400000, delta=count + 1)
            motion.executed = count
            motion.rate = speed
            self.assertEqual(motion.brake(), count)
            brake_us = sum(motion.interval(i) for i in range(count, count * 2))
            self.assertAlmostEqual(brake_us, 400000, delta=count + 1)
            self.assertEqual(motion.rate, 0)

    def test_fixed_finite_single_step_and_short_profiles(self):
        for count in (1, 2, 3, 10, 60, 120):
            motion = Motion(Clock(), 150, 375, 10, count, 0, fixed_ramp=True)
            periods = [motion.interval(i) for i in range(count)]
            self.assertEqual(periods, periods[::-1])
            self.assertEqual(motion.rate, 0)
            if count == 60:
                self.assertAlmostEqual(sum(periods), 800000, delta=count)

    def test_fixed_ramp_crossings_and_fractional_braking_distance(self):
        slow = Motion(Clock(), 0.5, 1.25, 10, None, 0, fixed_ramp=True)
        self.assertAlmostEqual(slow.interval_us, 2200000, delta=1)
        for speed in (0.5, 51, 151):
            motion = Motion(Clock(), speed, speed / 0.4, 10, None, 0, fixed_ramp=True)
            motion.rate = speed
            tail = motion.brake()
            self.assertAlmostEqual(sum(motion.interval(i) for i in range(tail)), 400000, delta=tail + 1)
