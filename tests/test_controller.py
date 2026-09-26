import json
import os
import tempfile
import unittest
from fakes import Board, Platform, Pin
from core.controller import Controller, led_value
from core.settings import Settings
from core.runtime import run
from drivers.drv8847 import DRV8847


class ControllerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.cwd = os.getcwd()
        os.chdir(self.temp.name)
        self.board = Board()
        self.platform = Platform()
        self.messages = []
        self.controller = Controller(self.board, self.platform, Settings(), self.messages.append)

    def tearDown(self):
        self.controller.close()
        os.chdir(self.cwd)
        self.temp.cleanup()

    def send(self, cmd, **values):
        self.controller.handle(json.dumps(dict(cmd=cmd, id=7, **values)))
        return self.messages[-1] if self.messages else None

    def start(self, **values):
        self.send('heartbeat')
        args = dict(mode='steps', resolution='quarter', direction=1, speed_sps=40, steps=3)
        args.update(values)
        return self.send('start', **args)

    def test_temperature_telemetry_at_eight_hz_across_clock_wrap(self):
        self.platform.clock.now = self.platform.clock.modulus - 50
        self.controller = Controller(self.board, self.platform, Settings(), self.messages.append)
        for elapsed in range(1000):
            self.board.temp = 25 + elapsed / 1000
            self.controller.tick()
            self.platform.clock.sleep_ms(1)
        self.assertEqual(len(self.messages), 8)
        for index, message in enumerate(self.messages):
            self.assertEqual(message['type'], 'telemetry')
            self.assertEqual(message['capabilities']['telemetry_interval_ms'], 125)
            self.assertEqual(message['uptime_ms'],
                             (self.platform.clock.modulus - 50 + index * 125) % self.platform.clock.modulus)
            self.assertAlmostEqual(message['temperature_c'], 25 + (index * 125 // 100) / 10)

    def test_alternate_driver_and_mcu_finite_move(self):
        self.assertFalse(self.board.motor.enabled)
        self.assertTrue(self.start()['ok'])
        for _ in range(4):
            self.controller.tick()
            self.platform.clock.sleep_ms(25)
        self.assertEqual(self.board.motor.steps, [(1, 'quarter')] * 3)
        self.assertFalse(self.board.motor.enabled)
        status = self.controller.status()
        self.assertEqual(status['steps_per_revolution'], 1600)
        self.assertEqual(status['supported_resolutions'], ['full', 'quarter'])
        self.assertEqual(status['mcu'], 'CPython')
        self.assertEqual(status['current_a'], 0.25)

    def test_heartbeat_expiry_across_tick_wrap(self):
        self.platform.clock.now = self.platform.clock.modulus - 100
        self.assertTrue(self.start(mode='continuous', direction=-1)['ok'])
        self.controller.tick()
        self.platform.clock.sleep_ms(1501)
        self.controller.tick()
        self.assertEqual(self.controller.stop_reason, 'Browser heartbeat lost')
        self.assertEqual(len(self.board.motor.steps), 1)
        self.assertFalse(self.board.motor.enabled)

    def test_start_requires_recent_heartbeat_and_safe_temperature(self):
        self.send('start', mode='continuous', resolution='quarter', direction=1, speed_sps=40)
        self.assertFalse(self.messages[-1]['ok'])
        for value in (60, 75, None, float('nan'), OSError('Sensor absent')):
            self.board.temp = value
            self.assertFalse(self.start()['ok'])
            self.assertFalse(self.board.motor.enabled)

    def test_each_protection_releases_coils_before_another_step(self):
        for cause in ('sensor', 'temperature', 'alert', 'fault'):
            with self.subTest(cause=cause):
                self.board.temp, self.board.alert, self.board.motor.fault = 25, False, False
                self.assertTrue(self.start(mode='continuous')['ok'])
                count = len(self.board.motor.steps)
                if cause == 'sensor': self.board.temp = OSError('Failed')
                if cause == 'temperature': self.board.temp = 75
                if cause == 'alert': self.board.alert = True
                if cause == 'fault': self.board.motor.fault = True
                self.platform.clock.sleep_ms(100)
                self.controller.tick()
                self.assertFalse(self.board.motor.enabled)
                self.assertEqual(len(self.board.motor.steps), count)

    def test_driver_enable_exception_releases_motor(self):
        self.board.motor.fail_enable = True
        self.assertFalse(self.start()['ok'])
        self.assertFalse(self.board.motor.enabled)

    def test_input_validation_does_not_enable_motor(self):
        for values in ({'resolution':'half'}, {'direction':True}, {'speed_sps':0},
                       {'steps':1.5}, {'steps':True}, {'mode':'bad'}):
            self.assertFalse(self.start(**values)['ok'])
            self.assertFalse(self.board.motor.enabled)

    def test_alarm_hysteresis_and_led_priority(self):
        self.send('set_threshold', threshold_c=45)
        self.board.temp = 46
        self.controller.sample()
        self.assertTrue(self.controller.alarm)
        self.board.temp = 44
        self.controller.sample()
        self.assertTrue(self.controller.alarm)
        self.board.temp = 42
        self.controller.sample()
        self.assertFalse(self.controller.alarm)
        self.assertEqual([led_value(t, False, True) for t in (0, 499, 500, 999)], [0,0,1,1])
        self.assertEqual([led_value(t, True, True) for t in (0, 124, 125, 249, 250)], [0,0,1,1,0])
        self.assertEqual(led_value(750, False, False), 0)

    def test_rename_persists_ack_then_reset_without_motion(self):
        self.send('set_threshold', threshold_c=45)
        ack = self.send('set_device_name', name='Axis X')
        self.assertTrue(ack['ok'])
        self.assertTrue(ack['restarting'])
        self.assertEqual(self.platform.resets, 0)
        self.assertFalse(self.start()['ok'])
        self.platform.clock.sleep_ms(750)
        self.controller.tick()
        self.assertEqual(self.platform.resets, 1)
        again = Controller(self.board, self.platform, Settings(), self.messages.append)
        self.assertEqual(again.device_name, 'Axis X')
        self.assertEqual(again.threshold, 45)
        self.assertFalse(self.board.motor.enabled)

    def test_no_usb_support_and_settings_while_moving(self):
        self.platform.usb.usb_name = None
        self.assertFalse(self.send('set_device_name', name='Axis X')['ok'])
        self.assertTrue(self.start()['ok'])
        self.assertFalse(self.send('set_device_name', name='Axis X')['ok'])
        self.assertFalse(self.send('set_threshold', threshold_c=45)['ok'])
        self.send('stop')
        self.assertFalse(self.board.motor.enabled)

    def test_runtime_releases_outputs_if_transport_fails(self):
        class Transport:
            def read_lines(self): raise OSError('Serial failure')
        self.platform.transport = lambda: Transport()
        with self.assertRaises(OSError):
            run(self.board, self.platform, Settings())
        self.assertFalse(self.board.motor.enabled)
        self.assertEqual(self.board.led.value(), 0)
        self.assertTrue(self.board.buzzer.closed)


class DRV8847Tests(unittest.TestCase):
    def test_full_half_forward_reverse_sequences_and_disable(self):
        for resolution, delta in (('full',2),('half',1)):
            for direction in (-1,1):
                with self.subTest(resolution=resolution, direction=direction):
                    platform = Platform()
                    enable, pins, fault = Pin(), [Pin() for _ in range(4)], Pin(1)
                    driver = DRV8847(enable, pins, fault, platform.clock)
                    driver.enable(resolution)
                    for step in range(1,9):
                        driver.step(direction)
                        self.assertEqual(tuple(p.value() for p in pins), driver.states[(step*direction*delta)%8])
                        self.assertTrue(all(p.writes[-2] == 0 for p in pins))
                    driver.disable()
                    self.assertEqual([enable.value()] + [p.value() for p in pins], [0]*5)
                    fault.value(0)
                    self.assertTrue(driver.fault_asserted())
                    with self.assertRaises(RuntimeError):driver.step(1)
                    with self.assertRaises(ValueError):driver.enable('quarter')

if __name__ == '__main__': unittest.main()
