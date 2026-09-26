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
