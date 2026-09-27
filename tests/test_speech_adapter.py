"""Resource recovery tests: failures must leave the normal PWM buzzer usable."""
import importlib.util
import os
import tempfile
import types
import unittest
from pathlib import Path
from unittest.mock import patch
from fakes import Clock


class PWM:
    def __init__(self, pin):
        self.closed = False
        self.duty = None

    def freq(self, value):
        self.frequency = value

    def duty_u16(self, value):
        if self.closed:
            raise OSError('PWM closed')
        self.duty = value

    def deinit(self):
        self.closed = True


class Tone:
    def __init__(self, pin, frequency):
        self.pin = pin
        self.pwm = PWM(pin)

    def close(self):
        self.pwm.deinit()


class DMA:
    ctrl = 0
    running = True
    closed = False

    def pack_ctrl(self, **fields):
        return fields

    def config(self, **fields):
        self.configured = fields

    def active(self):
        return self.running

    def close(self):
        self.closed = True


class AdapterTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.cwd = os.getcwd()
        os.chdir(self.temp.name)
        Path('assets').mkdir()
        Path('assets/hot.pdm').write_bytes(b'\xaa'*16000+b'\x00'*4)
        self.clock = Clock()
        pin = type('Pin', (), {'OUT':1, '__init__':lambda self,*a,**kw: None})
        self.rp2 = types.SimpleNamespace(PIO=types.SimpleNamespace(OUT_LOW=0,SHIFT_RIGHT=0,JOIN_TX=0),
                    asm_pio=lambda **kw:lambda fn:fn, DMA=DMA,
                    StateMachine=lambda *a,**kw:types.SimpleNamespace(active=lambda x:None))
        path=Path(__file__).resolve().parents[1]/'firmware/platforms/rp2040_speech.py'
        spec=importlib.util.spec_from_file_location('speech_test_adapter',path)
        self.module=importlib.util.module_from_spec(spec)
        with patch.dict('sys.modules',{'machine':types.SimpleNamespace(Pin=pin,PWM=PWM),
                    'rp2':self.rp2,'platforms.micropython':types.SimpleNamespace(Tone=Tone)}):
            spec.loader.exec_module(self.module)
        self.module.time=self.clock

    def tearDown(self):
        os.chdir(self.cwd)
        self.temp.cleanup()

    def test_completed_finite_dma_restores_pwm(self):
        tone=self.module.SpeechTone(16,2731)
        tone.speech_start()
        dma=tone.dma
        self.assertEqual(dma.configured['count'],4001)
        self.assertEqual(dma.configured['ctrl']['treq_sel'],0)
        self.assertTrue(tone.speech_poll())
        dma.running=False
        self.clock.sleep_ms(1003)
        self.assertFalse(tone.speech_poll())
        self.assertTrue(dma.closed)
        self.assertEqual(tone.pwm.duty,0)
        tone.pwm.duty_u16(32768)
        tone.speech_stop()

    def test_pio_initialization_failure_restores_pwm(self):
        def fail(*args,**kw):
            raise OSError('PIO unavailable')
        self.rp2.StateMachine=fail
        tone=self.module.SpeechTone(16,2731)
        with self.assertRaises(OSError):tone.speech_start()
        tone.pwm.duty_u16(32768)
        self.assertFalse(tone.speech_mode)

    def test_stuck_dma_is_stopped(self):
        tone=self.module.SpeechTone(16,2731)
        tone.speech_start()
        dma=tone.dma
        self.clock.sleep_ms(1003)
        with self.assertRaises(OSError):tone.speech_poll()
        self.assertTrue(dma.closed)
        self.assertEqual(tone.pwm.duty,0)

    def test_missing_asset_keeps_tone_available(self):
        Path('assets/hot.pdm').unlink()
        tone=self.module.SpeechTone(16,2731)
        self.assertFalse(tone.speech_supported)
        tone.pwm.duty_u16(32768)
