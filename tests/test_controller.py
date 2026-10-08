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

    def test_live_jog_updates_and_unbounded_speed(self):
        c = self.controller
        self.board.min_speed_sps = 0
        self.board.max_speed_sps = None
        self.send('heartbeat')
        self.assertTrue(self.send('jog', resolution='full', direction=1,
                                  speed_sps=10000, acceleration_sps2=600)['ok'])
        self.assertLessEqual(c.motion.lateness(), 0)
        self.platform.clock.sleep_ms(1)
        c.tick()
        self.assertEqual(c.executed, 1)
        rate = c.motion.rate
        self.assertTrue(self.send('decelerate')['ok'])
        self.assertEqual(c.mode, 'braking')
        self.assertTrue(self.send('jog', resolution='full', direction=1,
                                  speed_sps=50000, acceleration_sps2=600)['ok'])
        self.assertEqual(c.mode, 'continuous')
        self.assertGreaterEqual(c.motion.rate, rate)
        self.assertTrue(self.send('jog', resolution='quarter', direction=-1,
                                  speed_sps=0.5, acceleration_sps2=600)['ok'])
        self.assertEqual(c.direction, -1)
        self.assertEqual(c.resolution, 'quarter')
        self.assertEqual(c.speed, 0.5)
        self.assertFalse(self.send('jog', direction=True, speed_sps=50)['ok'])
        self.assertFalse(self.send('jog', direction=1, speed_sps=0)['ok'])
        self.assertFalse(self.send('jog', direction=1, speed_sps=float('inf'))['ok'])

    def test_live_speed_decrease_ramps_without_dwell(self):
        c = self.controller
        self.start(mode='continuous', speed_sps=100)
        c.motion.rate = 80
        deadline = c.motion.deadline
        self.assertTrue(self.send('jog', direction=1, resolution='quarter',
                                  speed_sps=20, acceleration_sps2=100)['ok'])
        self.assertEqual(c.motion.deadline, deadline)
        self.assertLess(c.motion.rate, 80)
        self.assertGreater(c.motion.rate, 20)
        self.send('stop')
        self.start()
        self.assertFalse(self.send('jog', direction=1, speed_sps=40)['ok'])

    def test_fixed_400ms_ramp_bypasses_acceleration_caps_and_starts_at_zero(self):
        self.board.max_speed_sps = None
        self.send('heartbeat')
        self.assertTrue(self.send('jog', direction=1, resolution='full', speed_sps=10000, ramp_ms=400)['ok'])
        self.assertEqual(self.controller.acceleration, 25000)
        self.assertEqual(self.controller.motion.initial, 0)
        self.assertTrue(self.controller.motion.fixed_ramp)
        self.assertFalse(self.send('jog', direction=1, speed_sps=40, ramp_ms=250)['ok'])
        self.send('stop')
        self.assertTrue(self.start(steps=1, ramp_ms=400)['ok'])
        c = self.controller
        self.platform.clock.sleep_us(c.motion.interval_us)
        c.tick()
        c.tick()
        self.assertEqual(c.mode, 'stopped')
        self.assertEqual(c.motion.rate, 0)

    def test_position_accumulates_across_moves_and_resolutions(self):
        for resolution, direction, count, expected in (
                ('quarter', 1, 4, 1), ('full', -1, 2, -1), ('quarter', 1, 2, -0.5)):
            self.assertTrue(self.start(resolution=resolution, direction=direction, steps=count)['ok'])
            for _ in range(2000):
                self.send('heartbeat')
                self.platform.clock.sleep_us(1000)
                self.controller.tick()
                if self.controller.mode == 'stopped':
                    break
            self.assertEqual(self.controller.status()['position_full_steps'], expected)
            self.assertEqual(self.controller.status()['position_session'], 'simulator-session-001')
        self.send('stop')
        self.assertEqual(self.controller.status()['position_full_steps'], -0.5)
        restarted = Controller(self.board, self.platform, Settings(), self.messages.append)
        self.assertEqual(restarted.status()['position_full_steps'], 0)
        restarted.close()

    def test_release_decelerates_then_stops_and_never_extends_tail(self):
        self.assertTrue(self.start(mode='continuous')['ok'])
        for _ in range(1000):
            self.send('heartbeat')
            self.platform.clock.sleep_us(1000)
            self.controller.tick()
        before = self.controller.executed
        self.assertTrue(self.send('decelerate')['ok'])
        self.assertEqual(self.controller.mode, 'braking')
        tail = self.controller.remaining
        self.assertGreater(tail, 0)
        self.assertLessEqual(tail, 8)
        self.send('decelerate')
        self.assertEqual(self.controller.remaining, tail)
        rates = []
        for _ in range(2000):
            self.send('heartbeat')
            self.platform.clock.sleep_us(1000)
            self.controller.tick()
            rates.append(self.controller.status()['profile_speed_sps'])
            if self.controller.mode == 'stopped':
                break
        self.assertEqual(rates, sorted(rates, reverse=True))
        self.assertFalse(self.board.motor.enabled)
        self.assertEqual(self.controller.executed, before + tail)
        self.assertEqual(self.controller.stop_reason, 'Jog release complete')
        self.assertTrue(self.send('decelerate')['ok'])
        self.assertFalse(self.board.motor.enabled)

    def test_braking_keeps_emergency_and_fault_stops_immediate(self):
        for fault in ('stop', 'driver', 'thermal', 'sensor', 'heartbeat'):
            self.send('stop')
            self.board.motor.fault = False
            self.board.temp = 25
            self.assertTrue(self.start(mode='continuous')['ok'])
            self.send('decelerate')
            before = self.controller.executed
            if fault == 'stop':
                self.send('stop')
            elif fault == 'driver':
                self.board.motor.fault = True
            elif fault == 'thermal':
                self.board.temp = 75
                self.controller.sample()
            elif fault == 'sensor':
                self.board.temp = OSError('Sensor unavailable')
                self.controller.sample()
            else:
                self.platform.clock.sleep_ms(1501)
            self.controller.tick()
            self.assertFalse(self.board.motor.enabled, fault)
            self.assertEqual(self.controller.executed, before, fault)

    def test_tone_only_buzzer_beep_expiry_and_alarm_priority(self):
        # Legacy saved voice preferences cannot suppress PWM alerts.
        with open('hot_alert.json', 'w') as file:
            file.write('{"enabled":true}')
        self.assertFalse(self.send('set_hot_alert', enabled=True)['ok'])
        self.assertFalse(self.send('test_hot_alert')['ok'])
        self.assertNotIn('hot_speech_supported', self.controller.capabilities())
        self.assertTrue(self.send('beep')['ok'])
        self.controller.tick()
        self.assertEqual(self.board.buzzer.frequency, self.board.buzzer_hz)
        self.assertTrue(self.controller.status()['buzzer_on'])
        self.platform.clock.sleep_ms(600)
        self.controller.tick()
        self.assertEqual(self.board.buzzer.frequency, 0)
        self.board.temp = 70
        self.platform.clock.now = 1000
        self.controller.tick()
        self.assertTrue(self.controller.alarm)
        self.assertEqual(self.board.buzzer.frequency, self.board.buzzer_hz)
        self.platform.clock.now = 1250
        self.controller.tick()
        self.assertEqual(self.board.buzzer.frequency, 0)

    def test_alternate_driver_and_mcu_finite_move(self):
        self.assertFalse(self.board.motor.enabled)
        self.assertTrue(self.start()['ok'])
        for _ in range(1000):
            self.send('heartbeat')
            self.controller.tick()
            self.platform.clock.sleep_ms(1)
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
        self.assertEqual(len(self.board.motor.steps), 0)
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

    def test_late_motion_continues_without_catchup_burst(self):
        for mode in ('continuous', 'steps'):
            self.send('stop')
            self.assertTrue(self.start(mode=mode, steps=3)['ok'])
            before = len(self.board.motor.steps)
            self.platform.clock.sleep_us(400000)
            self.send('heartbeat')
            self.controller.tick()
            self.assertTrue(self.board.motor.enabled)
            self.assertEqual(len(self.board.motor.steps), before + 1)
            self.assertGreater(self.controller.motion.max_lateness_us, 20000)
            for _ in range(5):
                self.controller.tick()
            self.assertEqual(len(self.board.motor.steps), before + 1)
            self.platform.clock.sleep_us(self.controller.motion.interval_us)
            self.controller.tick()
            self.assertEqual(len(self.board.motor.steps), before + 2)

    def test_acceleration_validation_and_immediate_stop(self):
        for value in (0, -1, True, float('nan'), 1001):
            self.assertFalse(self.start(acceleration_sps2=value)['ok'])
            self.assertFalse(self.board.motor.enabled)
        self.assertTrue(self.start(mode='continuous', acceleration_sps2=100)['ok'])
        self.send('stop')
        self.assertFalse(self.board.motor.enabled)
        self.assertEqual(self.board.motor.steps, [])

    def test_song_command_alarm_priority_and_stop(self):
        from test_song import tune
        ack=self.send('play_song', hex=tune((440,500),(880,500)))
        self.assertTrue(ack['ok']);self.controller.tick()
        self.assertEqual(self.board.buzzer.frequency,440)
        self.assertTrue(self.controller.status()['song_playing'])
        self.board.temp=70;self.platform.clock.sleep_ms(100);self.controller.tick()
        self.assertFalse(self.controller.song.playing)
        self.assertEqual(self.board.buzzer.frequency,self.board.buzzer_hz)
        self.assertFalse(self.send('play_song',hex=tune((440,100)))['ok'])
        self.send('stop_song');self.controller.tick()
        self.assertTrue(self.controller.alarm)
        self.board.temp=25;self.controller.sample()
        self.assertTrue(self.send('play_song',hex=tune((880,100)))['ok'])
        self.send('stop_song');self.controller.tick()
        self.assertEqual(self.board.buzzer.frequency,0)

    def test_motion_cancels_song_and_rejects_new_song(self):
        from test_song import tune
        self.send('play_song',hex=tune((440,500)))
        self.assertTrue(self.start(mode='continuous')['ok'])
        self.assertFalse(self.controller.song.playing)
        self.assertFalse(self.send('play_song',hex=tune((440,100)))['ok'])

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
