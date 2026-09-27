import unittest
from fakes import Clock
from core.hot_alert import HotAlert


class Speech:
    speech_supported = True
    speech_error = None
    active = False
    starts = 0
    fail = False

    def speech_start(self):
        if self.fail:
            raise OSError('DMA failed')
        self.active = True
        self.starts += 1

    def speech_poll(self):
        return self.active

    def speech_stop(self):
        self.active = False


class HotAlertTests(unittest.TestCase):
    def setUp(self):
        self.clock = Clock()
        self.buzzer = Speech()
        self.hot = HotAlert(self.buzzer, self.clock, True)

    def tick(self, alarm=True):
        return self.hot.tick(alarm, self.clock.ticks_ms())

    def test_repeat_waits_two_seconds_after_completion_and_wraps(self):
        self.clock.now = self.clock.modulus - 1000
        self.assertTrue(self.tick())
        self.assertEqual(self.buzzer.starts, 1)
        self.buzzer.active = False
        self.assertTrue(self.tick())
        self.clock.sleep_ms(1999)
        self.assertTrue(self.tick())
        self.assertEqual(self.buzzer.starts, 1)
        self.clock.sleep_ms(1)
        self.assertTrue(self.tick())
        self.assertEqual(self.buzzer.starts, 2)
        self.assertFalse(self.tick(False))
        self.assertFalse(self.buzzer.active)

    def test_off_and_failures_fall_back_to_chirps(self):
        self.hot.configure(False)
        self.assertFalse(self.tick())
        self.hot.configure(True)
        self.buzzer.fail = True
        self.assertFalse(self.tick())
        self.assertEqual(self.hot.error, 'DMA failed')
        self.buzzer.fail = False
        self.assertFalse(self.tick())  # No repeated hardware retries during motion.
        self.hot.configure(True)
        self.assertTrue(self.tick())
        self.hot.configure(False)
        self.assertFalse(self.buzzer.active)

    def test_preview_once_even_with_option_off_and_alarm_preempts(self):
        self.hot.configure(False)
        self.hot.preview()
        self.assertTrue(self.tick(False))
        self.buzzer.active = False
        self.assertFalse(self.tick(False))
        self.clock.sleep_ms(3000)
        self.assertFalse(self.tick(False))
        self.assertEqual(self.buzzer.starts, 1)
        self.hot.preview()
        self.assertFalse(self.tick(True))
        self.assertFalse(self.buzzer.active)
