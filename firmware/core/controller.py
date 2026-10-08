"""Portable motion, protection and JSON command handling; no machine imports."""
import json
import math
from core.motion import Motion
from core.hot_alert import HotAlert
from core.song import Song, MAX_BYTES


def number(value, low, high):
    if (isinstance(value, bool) or not isinstance(value, (int, float))
            or not math.isfinite(value) or value < low or (high is not None and value > high)):
        raise ValueError('Value outside allowed range')
    return value


def motion_acceleration(command, board, speed):
    if 'ramp_ms' in command:
        if isinstance(command['ramp_ms'], bool) or command['ramp_ms'] != 400:
            raise ValueError('Ramp duration is fixed at 400 ms')
        return number(speed / 0.4, 0, None)
    return number(command.get('acceleration_sps2', getattr(board, 'default_acceleration_sps2', 100)),
                  getattr(board, 'min_acceleration_sps2', 10), getattr(board, 'max_acceleration_sps2', 1000))


def led_value(now, alarm_active, moving):
    if alarm_active:
        return (now // 125) % 2
    if moving:
        return (now // 500) % 2
    return 0


class Controller:
    def __init__(self, board, platform, settings, emit):
        self.board = board
        self.platform = platform
        self.clock = platform.clock
        self.motor = board.motor
        self.settings = settings
        self.emit = emit
        self.device_name = settings.load_name()
        self.threshold = settings.load_threshold(board.threshold_default_c,
                                                 board.threshold_min_c,
                                                 board.threshold_max_c)
        self.song = Song(self.clock)
        self.hot = HotAlert(board.buzzer, self.clock, getattr(settings, "load_hot_alert", lambda: False)())
        self.buzzer_frequency = 0
        self.reset_at = None
        self.temperature = None
        self.sensor_error = None
        self.alarm = False
        self.sounding = False
        self.led_on = False
        self.resolution = next(iter(self.motor.resolutions))
        self.mode = 'stopped'
        self.direction = 1
        self.speed = max(board.min_speed_sps, min(40, board.max_speed_sps or 40))
        self.motion = None
        self.acceleration = getattr(board, 'default_acceleration_sps2', 100)
        self.remaining = 0
        self.executed = 0
        self.position_full_steps = 0
        self.position_session = getattr(platform, 'motion_session', lambda: None)()
        now = self.clock.ticks_ms()
        self.test_until = now
        self.last_sample = self.clock.ticks_add(now, -1000)
        self.last_emit = self.clock.ticks_add(now, -1000)
        self.last_heartbeat = self.clock.ticks_add(now, -board.heartbeat_timeout_ms - 1)
        self.stop('Boot: motor disabled')

    def stop(self, reason):
        self.motor.disable()
        self.hot.stop()
        self.song.stop()
        self.mode = 'stopped'
        self.stop_reason = reason

    def sample(self):
        try:
            value = self.board.temperature()
            if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
                raise ValueError('Invalid temperature')
            self.temperature = value
            self.sensor_error = None
        except Exception as error:
            self.temperature = None
            self.sensor_error = str(error)
        if self.temperature is None or self.board.thermal_alert():
            self.alarm = True
        elif self.temperature >= self.threshold:
            self.alarm = True
        elif self.temperature < self.threshold - 2:
            self.alarm = False

    def capabilities(self):
        b = self.board
        return {'hot_speech_supported': self.hot.supported, 'hot_speech_pause_ms': 2000,
                'song_supported': callable(getattr(b.buzzer, 'play', None)),
                'song_format': 'u16le-hz-u16le-ms', 'song_max_bytes': MAX_BYTES,
                'resolutions': self.motor.resolutions,
                'full_steps_per_revolution': b.full_steps_per_revolution,
                'min_speed_sps': b.min_speed_sps, 'max_speed_sps': b.max_speed_sps,
                'acceleration_supported': True, 'deceleration_supported': True, 'jog_update_supported': True, 'fixed_ramp_ms': 400,
                'min_acceleration_sps2': getattr(b, 'min_acceleration_sps2', 10),
                'max_acceleration_sps2': getattr(b, 'max_acceleration_sps2', 1000),
                'default_acceleration_sps2': getattr(b, 'default_acceleration_sps2', 100),
                'max_steps': b.max_steps, 'start_below_c': b.start_below_c,
                'shutdown_c': b.shutdown_c, 'threshold_min_c': b.threshold_min_c,
                'threshold_max_c': b.threshold_max_c, 'buzzer_hz': b.buzzer_hz,
                'heartbeat_timeout_ms': b.heartbeat_timeout_ms,
                'step_note': self.motor.step_note, 'current_note': b.current_note}

    def status(self):
        current = self.board.current_a()
        if current is not None and (isinstance(current, bool) or not isinstance(current, (int, float))
                                    or not math.isfinite(current) or current < 0):
            current = None
        usb = self.platform.usb
        return {'type': 'telemetry', 'protocol': 3, 'uptime_ms': self.clock.ticks_ms(),
                'temperature_c': self.temperature, 'device_name': self.device_name,
                'usb_name': usb.usb_name, 'usb_name_supported': usb.usb_name is not None,
                'usb_name_error': usb.usb_name_error, 'device_id': self.platform.device_id(),
                'restarting': self.reset_at is not None, 'led_on': self.led_on,
                'led_mode': 'alarm' if self.alarm else 'running' if self.mode != 'stopped' else 'off',
                'threshold_c': self.threshold, 'alarm_active': self.alarm,
                'hot_alert_enabled': self.hot.enabled, 'hot_speech_playing': self.hot.playing,
                'hot_speech_error': self.hot.error,
                'buzzer_on': self.sounding, 'buzzer_frequency_hz': self.buzzer_frequency,
                'song_playing': self.song.playing, 'song_note': self.song.index + 1 if self.song.playing else 0, 'sensor_error': self.sensor_error,
                'thermal_alert': self.board.thermal_alert(),
                'driver_fault_asserted': self.motor.fault_asserted(),
                'step_resolution': self.resolution,
                'steps_per_revolution': self.board.full_steps_per_revolution * self.motor.resolutions[self.resolution],
                'supported_resolutions': list(self.motor.resolutions),
                'motor_enabled': self.motor.enabled, 'motion_mode': self.mode,
                'direction': self.direction, 'speed_sps': self.speed,
                'acceleration_sps2': self.acceleration,
                'profile_speed_sps': self.motion.rate if self.motion and self.mode != 'stopped' else 0,
                'late_steps': self.motion.late_steps if self.motion else 0,
                'max_step_lateness_us': self.motion.max_lateness_us if self.motion else 0,
                'steps_remaining': self.remaining if self.mode in ('steps', 'braking') else None,
                'steps_executed': self.executed, 'stop_reason': self.stop_reason,
                'position_full_steps': self.position_full_steps,
                'position_session': self.position_session,
                'current_a': current, 'current_available': current is not None,
                'board': self.board.name, 'board_id': self.board.id,
                'mcu': self.board.mcu, 'driver': self.motor.name,
                'capabilities': self.capabilities()}

    def start(self, command):
        b = self.board
        if self.reset_at is not None:
            raise ValueError('Restart pending')
        if self.mode != 'stopped':
            raise ValueError('Stop current movement first')
        resolution = command.get('resolution', 'full')
        if resolution not in self.motor.resolutions:
            raise ValueError('Unsupported resolution; supported: ' + ', '.join(self.motor.resolutions))
        mode = command.get('mode')
        if mode not in ('steps', 'continuous'):
            raise ValueError('Unknown motion mode')
        direction = command.get('direction')
        if isinstance(direction, bool) or direction not in (-1, 1):
            raise ValueError('Direction must be -1 or 1')
        speed = number(command.get('speed_sps'), b.min_speed_sps, b.max_speed_sps)
        if speed <= 0:
            raise ValueError('Speed must be positive')
        acceleration = motion_acceleration(command, b, speed)
        count = command.get('steps') if mode == 'steps' else 0
        if mode == 'steps':
            number(count, 1, b.max_steps)
            if int(count) != count:
                raise ValueError('Steps must be an integer')
        if self.clock.ticks_diff(self.clock.ticks_ms(), self.last_heartbeat) > b.heartbeat_start_ms:
            raise ValueError('Waiting for browser heartbeat')
        self.sample()
        if self.temperature is None or self.temperature >= b.start_below_c or b.thermal_alert():
            raise ValueError('Temperature must be valid and below %s C' % b.start_below_c)
        # An adapter may raise after partially enabling hardware: always release it.
        try:
            self.motor.enable(resolution)
            if self.motor.fault_asserted() or b.thermal_alert():
                raise ValueError('Driver or thermal fault at wake')
        except Exception:
            self.stop('Driver or thermal fault at wake')
            raise
        self.song.stop()
        self.hot.stop()
        self.resolution = resolution
        self.direction = direction
        self.speed = speed
        self.remaining = int(count)
        self.executed = 0
        self.mode = mode
        self.stop_reason = ''
        self.acceleration = acceleration
        self.motion = Motion(self.clock, speed, acceleration, getattr(b, 'start_speed_sps', 10),
                             int(count) if mode == 'steps' else None,
                             0 if 'ramp_ms' in command else getattr(b, 'settle_ms', 100),
                             fixed_ramp='ramp_ms' in command)

    def jog(self, command):
        """Update held-key intent without releasing coils or waiting for telemetry."""
        if self.mode == 'steps':
            raise ValueError('Stop finite movement before jogging')
        if self.mode == 'stopped':
            self.start(dict(command, mode='continuous'))
            # Jogging needs only the driver wake delay, not the alignment dwell.
            if not self.motion.fixed_ramp:
                self.motion.deadline = self.clock.ticks_add(self.clock.ticks_us(), 1000)
            return
        b = self.board
        speed = number(command.get('speed_sps'), b.min_speed_sps, b.max_speed_sps)
        if speed <= 0:
            raise ValueError('Speed must be positive')
        acceleration = motion_acceleration(command, b, speed)
        direction = command.get('direction')
        resolution = command.get('resolution', self.resolution)
        if isinstance(direction, bool) or direction not in (-1, 1):
            raise ValueError('Direction must be -1 or 1')
        if resolution not in self.motor.resolutions:
            raise ValueError('Unsupported resolution')
        if self.reset_at is not None:
            raise ValueError('Restart pending')
        previous_rate = self.motion.rate
        same_direction = direction == self.direction and resolution == self.resolution
        if resolution != self.resolution:
            self.motor.enable(resolution)
        self.direction, self.resolution = direction, resolution
        self.speed, self.acceleration = speed, acceleration
        self.mode, self.remaining, self.stop_reason = 'continuous', 0, ''
        self.motion.fixed_ramp = 'ramp_ms' in command
        initial = 0 if self.motion.fixed_ramp else min(getattr(b, 'start_speed_sps', 10), speed)
        self.motion.retarget(speed, acceleration, previous_rate if same_direction else initial)
        if not same_direction:
            self.motion.deadline = self.clock.ticks_add(self.clock.ticks_us(), self.motion.interval_us if self.motion.fixed_ramp else 1000)

    def handle(self, line):
        ident = None
        try:
            command = json.loads(line)
            if not isinstance(command, dict):
                raise ValueError('Expected JSON object')
            ident = command.get('id')
            operation = command.get('cmd')
            if operation == 'heartbeat':
                self.last_heartbeat = self.clock.ticks_ms()
                return
            if operation == 'stop':
                self.stop('Stopped by user')
            elif operation == 'decelerate':
                if self.mode == 'continuous':
                    self.remaining = self.motion.brake()
                    self.mode = 'braking'
                elif self.mode not in ('stopped', 'braking'):
                    raise ValueError('Deceleration requires continuous motion')
            elif operation == 'status':
                self.emit(self.status())
            elif operation == 'set_threshold':
                if self.mode != 'stopped':
                    raise ValueError('Stop motor before saving settings')
                value = number(command.get('threshold_c'), self.board.threshold_min_c, self.board.threshold_max_c)
                self.settings.save_threshold(value)
                self.threshold = float(value)
            elif operation == 'set_device_name':
                if self.mode != 'stopped':
                    raise ValueError('Stop motor before renaming')
                if self.platform.usb.usb_name is None:
                    raise ValueError('USB naming unavailable on this platform or boot configuration')
                if self.reset_at is not None:
                    raise ValueError('Restart already pending')
                self.device_name = self.settings.save_name(command.get('name'))
                self.stop('Restarting to apply USB name')
                self.reset_at = self.clock.ticks_add(self.clock.ticks_ms(), 750)
            elif operation == 'set_hot_alert':
                if self.mode != 'stopped' or self.reset_at is not None:
                    raise ValueError('Stop motor and wait for restart before saving settings')
                enabled = command.get('enabled')
                if not isinstance(enabled, bool):
                    raise ValueError('enabled must be boolean')
                if enabled and not self.hot.supported:
                    raise ValueError('Speech unavailable on this board')
                self.settings.save_hot_alert(enabled)
                self.hot.configure(enabled)
            elif operation == 'test_hot_alert':
                if self.mode != 'stopped' or self.reset_at is not None:
                    raise ValueError('Stop motor and wait for restart before testing speech')
                self.sample()
                if self.alarm:
                    raise ValueError('Temperature alarm takes priority over preview')
                self.song.stop()
                self.test_until = self.clock.ticks_ms()
                self.hot.preview()
            elif operation == 'play_song':
                if self.mode != 'stopped' or self.reset_at is not None:
                    raise ValueError('Stop motor and wait for restart before playing a song')
                if not callable(getattr(self.board.buzzer, 'play', None)):
                    raise ValueError('Variable-pitch buzzer unavailable')
                self.sample()
                if self.alarm:
                    raise ValueError('Temperature alarm takes priority over songs')
                self.song.start(command.get('hex'))
                self.hot.stop()
                self.test_until = self.clock.ticks_ms()
            elif operation == 'stop_song':
                self.song.stop()
                self.hot.stop()
                self.test_until = self.clock.ticks_ms()
            elif operation == 'beep':
                self.song.stop()
                self.hot.stop()
                self.test_until = self.clock.ticks_add(self.clock.ticks_ms(), 600)
            elif operation == 'jog':
                self.jog(command)
            elif operation == 'start':
                self.start(command)
            else:
                raise ValueError('Unknown command')
            self.emit({'type': 'ack', 'id': ident, 'ok': True, 'threshold_c': self.threshold,
                       'device_name': self.device_name, 'hot_alert_enabled': self.hot.enabled,
                       'restarting': self.reset_at is not None})
        except Exception as error:
            self.emit({'type': 'ack', 'id': ident, 'ok': False, 'error': str(error)})

    def protect(self):
        now = self.clock.ticks_ms()
        if self.clock.ticks_diff(now, self.last_sample) >= 100:
            self.sample()
            self.last_sample = now
        if self.mode != 'stopped':
            if self.temperature is None:
                self.stop('Temperature sensor error')
            elif self.temperature >= self.board.shutdown_c or self.board.thermal_alert():
                self.stop('Thermal shutdown')
            elif self.motor.fault_asserted():
                self.stop('Driver fault')
            elif self.clock.ticks_diff(now, self.last_heartbeat) > self.board.heartbeat_timeout_ms:
                self.stop('Browser heartbeat lost')

    def tick(self):
        # Recheck protection after handling commands, before any step.
        self.protect()
        now = self.clock.ticks_ms()
        if self.mode in ('steps', 'braking') and self.remaining == 0 and self.motion.due():
            self.stop('Jog release complete' if self.mode == 'braking' else 'Step move complete')
        if self.mode != 'stopped' and self.motion.due():
            # Emit one overdue step and rebase the deadline on actual output
            # time. Lateness is diagnostic; it does not stop the move or burst.
            self.motor.step(self.direction)
            self.motion.advanced()
            self.executed += 1
            self.position_full_steps += self.direction / self.motor.resolutions[self.resolution]
            if self.mode in ('steps', 'braking'):
                self.remaining -= 1
                # Retain the final phase for one interval before release.
        if self.alarm:
            self.song.stop()
        frequency = self.song.tick(now)
        if self.alarm:
            frequency = self.board.buzzer_hz if now % 1000 < 200 else 0
        elif self.clock.ticks_diff(self.test_until, now) > 0:
            frequency = self.board.buzzer_hz
        speech_owns_buzzer = self.hot.tick(self.alarm, now)
        self.buzzer_frequency = 0 if speech_owns_buzzer else frequency
        self.sounding = self.hot.playing if speech_owns_buzzer else bool(frequency)
        if speech_owns_buzzer:
            pass
        elif callable(getattr(self.board.buzzer, 'play', None)):
            self.board.buzzer.play(frequency)
        else:
            self.board.buzzer.set(self.sounding)
        self.led_on = bool(led_value(now, self.alarm, self.mode != 'stopped'))
        self.board.led.value(int(self.led_on))
        if self.reset_at is not None and self.clock.ticks_diff(now, self.reset_at) >= 0:
            self.stop('Restarting to apply USB name')
            self.board.buzzer.set(False)
            self.board.led.value(0)
            self.platform.reset()
        if self.clock.ticks_diff(now, self.last_emit) >= 250:
            self.emit(self.status())
            self.last_emit = now

    def close(self):
        self.stop('Firmware stopped')
        self.board.led.value(0)
        self.board.buzzer.close()
