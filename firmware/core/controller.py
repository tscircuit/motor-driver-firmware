"""Portable motion, protection and JSON command handling; no machine imports."""
import json
import math


def number(value, low, high):
    if (isinstance(value, bool) or not isinstance(value, (int, float))
            or not math.isfinite(value) or not low <= value <= high):
        raise ValueError('Value outside allowed range')
    return value


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
        self.reset_at = None
        self.temperature = None
        self.sensor_error = None
        self.alarm = False
        self.sounding = False
        self.led_on = False
        self.resolution = next(iter(self.motor.resolutions))
        self.mode = 'stopped'
        self.direction = 1
        self.speed = max(board.min_speed_sps, min(40, board.max_speed_sps))
        self.remaining = 0
        self.executed = 0
        now = self.clock.ticks_ms()
        self.next_step = now
        self.test_until = now
        self.last_sample = self.clock.ticks_add(now, -1000)
        self.last_emit = self.clock.ticks_add(now, -1000)
        self.last_heartbeat = self.clock.ticks_add(now, -board.heartbeat_timeout_ms - 1)
        self.stop('Boot: motor disabled')

    def stop(self, reason):
        self.motor.disable()
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
        return {'resolutions': self.motor.resolutions,
                'full_steps_per_revolution': b.full_steps_per_revolution,
                'min_speed_sps': b.min_speed_sps, 'max_speed_sps': b.max_speed_sps,
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
                'buzzer_on': self.sounding, 'sensor_error': self.sensor_error,
                'thermal_alert': self.board.thermal_alert(),
                'driver_fault_asserted': self.motor.fault_asserted(),
                'step_resolution': self.resolution,
                'steps_per_revolution': self.board.full_steps_per_revolution * self.motor.resolutions[self.resolution],
                'supported_resolutions': list(self.motor.resolutions),
                'motor_enabled': self.motor.enabled, 'motion_mode': self.mode,
                'direction': self.direction, 'speed_sps': self.speed,
                'steps_remaining': self.remaining if self.mode == 'steps' else None,
                'steps_executed': self.executed, 'stop_reason': self.stop_reason,
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
        self.resolution = resolution
        self.direction = direction
        self.speed = speed
        self.remaining = int(count)
        self.executed = 0
        self.mode = mode
        self.stop_reason = ''
        self.next_step = self.clock.ticks_ms()

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
            elif operation == 'beep':
                self.test_until = self.clock.ticks_add(self.clock.ticks_ms(), 600)
            elif operation == 'start':
                self.start(command)
            else:
                raise ValueError('Unknown command')
            self.emit({'type': 'ack', 'id': ident, 'ok': True, 'threshold_c': self.threshold,
                       'device_name': self.device_name, 'restarting': self.reset_at is not None})
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
        if self.mode != 'stopped' and self.clock.ticks_diff(now, self.next_step) >= 0:
            if self.mode == 'steps' and self.remaining == 0:
                self.stop('Step move complete')
            else:
                self.motor.step(self.direction)
                self.executed += 1
                if self.mode == 'steps':
                    self.remaining -= 1
                self.next_step = self.clock.ticks_add(now, round(1000 / self.speed))
        self.sounding = ((self.alarm and now % 1000 < 200)
                         or self.clock.ticks_diff(self.test_until, now) > 0)
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
