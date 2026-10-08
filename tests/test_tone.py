"""The board buzzer uses PWM alone for tones, rests, and shutdown."""
import importlib.util
import types
import unittest
from pathlib import Path
from unittest.mock import patch
from fakes import Clock


class PWM:
    def __init__(self, pin):
        self.closed = False
        self.duty = None
        self.frequencies = []

    def freq(self, value):
        self.frequency = value
        self.frequencies.append(value)

    def duty_u16(self, value):
        if self.closed:
            raise OSError('PWM closed')
        self.duty = value

    def deinit(self):
        self.closed = True


class ToneTests(unittest.TestCase):
    def test_pwm_test_alarm_song_rest_and_close(self):
        pin = type('Pin', (), {'OUT': 1, '__init__': lambda self, *a, **kw: None})
        path = Path(__file__).resolve().parents[1] / 'firmware/platforms/micropython.py'
        spec = importlib.util.spec_from_file_location('tone_test_platform', path)
        module = importlib.util.module_from_spec(spec)
        with patch.dict('sys.modules', {'machine': types.SimpleNamespace(Pin=pin, PWM=PWM)}):
            spec.loader.exec_module(module)
        tone = module.Tone(16, 2731)
        self.assertEqual(tone.pwm.duty, 0)
        tone.play(2731)
        self.assertEqual(tone.pwm.frequency, 2731)
        self.assertEqual(tone.pwm.duty, 32768)
        tone.play(440)
        self.assertEqual(tone.pwm.frequency, 440)
        tone.play(0)
        self.assertEqual(tone.pwm.duty, 0)
        tone.play(2731)
        self.assertEqual(tone.pwm.frequency, 2731)
        self.assertEqual(tone.pwm.duty, 32768)
        tone.close()
        self.assertEqual(tone.pwm.duty, 0)
        self.assertTrue(tone.pwm.closed)
