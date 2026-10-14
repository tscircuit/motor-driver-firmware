import json
import unittest
from unittest.mock import patch
from types import SimpleNamespace
from fakes import Pin, Clock, Board as FakeBoard, Platform as FakePlatform, Buzzer
from drivers.drv8825 import DRV8825
from core.controller import Controller


class DriverTests(unittest.TestCase):
    def setUp(self):
        self.pins = [Pin() for _ in range(6)]
        self.pins[-1].value(1)
        self.clock = Clock()
        self.ready = True
        self.motor = DRV8825(*self.pins, self.clock, lambda: self.ready)

    def test_pulse_count_polarity_and_cleanup(self):
        self.assertEqual(self.pins[4].value(), 1)
        for name, factor in self.motor.resolutions.items():
            self.motor.enable(name)
            for direction in (-1, 1):
                self.pins[1].writes.clear()
                self.motor.step(direction)
                self.assertEqual(self.pins[1].writes.count(1), 16 // factor)
                self.assertEqual(self.pins[2].value(), int(direction > 0))
                self.assertEqual(self.pins[1].value(), 0)
            self.motor.disable()
            self.assertEqual(self.pins[0].value(), 0)
            self.assertEqual(self.pins[4].value(), 1)
        with self.assertRaises(RuntimeError): self.motor.step(1)

    def test_power_interlock(self):
        self.ready = False
        with self.assertRaises(ValueError): self.motor.enable('half')
        self.assertFalse(self.motor.enabled)
        self.assertEqual(self.pins[0].value(), 0)
        with self.assertRaises(ValueError): self.motor.enable('bogus')
        self.pins[-1].value(0)
        self.assertTrue(self.motor.fault_asserted())


class BoardTests(unittest.TestCase):
    def setUp(self):
        with patch.dict('sys.modules', {'machine': SimpleNamespace()}):
            from boards.rp2040_drv8825 import Board, RGB
        self.Board, self.RGB = Board, RGB
        self.board = Board.__new__(Board)
        self.board.platform = FakePlatform()
        self.board.pd_pins = [Pin() for _ in range(3)]
        self.board.motor = SimpleNamespace(disable=lambda: self.events.append('disabled'))
        self.events = []
        self.board.pd_voltage = 5
        self.board.pd_ready_at = 0

    def test_pd_codes_disabled_and_settling(self):
        for volts, code in [(5,(0,0,0)),(9,(1,1,1)),(12,(1,1,0)),(15,(1,0,0)),(20,(1,0,1))]:
            self.board.set_pd_voltage(volts)
            self.assertEqual(tuple(p.value() for p in self.board.pd_pins), code)
            self.assertEqual(self.board.pd_pins[0].writes[-2:], [0, code[0]])
            self.assertFalse(self.board.power_ready())
            self.board.platform.clock.sleep_ms(2000)
            self.assertEqual(self.board.power_ready(), volts != 5)
        self.assertEqual(len(self.events), 5)
        for value in (True, 6, None, '12'):
            with self.assertRaises(ValueError): self.board.set_pd_voltage(value)
        self.assertFalse(self.board.thermal_alert())

    def test_current_conversion_and_clipping(self):
        self.board.adcs = [SimpleNamespace(read_u16=lambda: 32768), SimpleNamespace(read_u16=lambda: 52627)]
        self.assertAlmostEqual(self.board.current_a(), 1, places=3)
        self.assertAlmostEqual(self.board.currents[1], 1, places=3)
        self.board.adcs[0] = SimpleNamespace(read_u16=lambda: 65535)
        self.assertIsNone(self.board.current_a())
        self.assertIsNone(self.board.currents)

    def test_led_alarm_priority_and_voltage_command_interlock(self):
        b = FakeBoard()
        b.led = self.RGB([Pin() for _ in range(3)])
        b.led_colors = tuple(b.led.colors)
        b.pd_voltages = (5, 12)
        b.set_pd_voltage = self.board.set_pd_voltage
        settings = SimpleNamespace(load_name=lambda:'Test', load_threshold=lambda *a:65)
        p = FakePlatform()
        packets = []
        c = Controller(b, p, settings, packets.append)
        c.handle(json.dumps({'cmd':'set_led','color':'magenta'})); c.tick()
        self.assertEqual(b.led.color, 'magenta')
        c.handle(json.dumps({'cmd':'set_led','color':'invalid'}))
        self.assertFalse(packets[-1]['ok'])
        b.temp = 70; p.clock.sleep_ms(125); c.tick()
        self.assertEqual(b.led.color, 'red')
        b.temp = 25; p.clock.sleep_ms(125); c.tick()
        self.assertEqual(b.led.color, 'magenta')
        c.mode = 'prepared'
        c.handle(json.dumps({'cmd':'set_pd_voltage','voltage':12}))
        self.assertFalse(packets[-1]['ok']); self.assertEqual(self.events, [])
        c.stop('test')
        c.handle(json.dumps({'cmd':'set_pd_voltage','voltage':12}))
        self.assertTrue(packets[-1]['ok']); self.assertEqual(self.events, ['disabled'])
        c.close(); self.assertEqual(b.led.color, 'off')
