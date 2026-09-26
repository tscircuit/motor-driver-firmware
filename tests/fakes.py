"""Deterministic host adapters: no machine, real sleeps, USB, or motor."""
import sys
from pathlib import Path
from types import SimpleNamespace
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'firmware'))


class Clock:
    modulus = 1 << 30
    now = 0

    def ticks_us(self):
        return int(self.now * 1000) % self.modulus

    def sleep_us(self, value):
        self.now = (self.now + value / 1000) % self.modulus

    def ticks_ms(self):
        return self.now

    def ticks_add(self, value, delta):
        return (value + delta) % self.modulus

    def ticks_diff(self, a, b):
        half = self.modulus // 2
        return (a - b + half) % self.modulus - half

    def sleep_ms(self, value):
        self.now = self.ticks_add(self.now, value)


class Pin:
    def __init__(self, value=0):
        self.state = value
        self.writes = []

    def value(self, value=None):
        if value is not None:
            self.state = value
            self.writes.append(value)
        return self.state


class Buzzer:
    sounding = False
    closed = False

    def set(self, value):
        self.sounding = value

    def close(self):
        self.sounding = False
        self.closed = True


class Motor:
    """An unrelated motor adapter that deliberately supports quarter steps."""
    name = 'Simulated STEP/DIR'
    resolutions = {'full': 1, 'quarter': 4}
    step_note = 'Simulated motor; no physical hardware support claimed.'
    enabled = False
    fault = False
    fail_enable = False
    fail_step = False

    def __init__(self):
        self.steps = []

    def enable(self, resolution):
        self.enabled = True
        self.resolution = resolution
        if self.fail_enable:
            raise OSError('Enable failed')

    def disable(self):
        self.enabled = False

    def fault_asserted(self):
        return self.fault

    def step(self, direction):
        if self.fail_step:
            raise OSError('Step failed')
        self.steps.append((direction, self.resolution))


class Board:
    id = 'simulator'
    name = 'Host simulator'
    mcu = 'CPython'
    full_steps_per_revolution = 400
    min_speed_sps = 5
    max_speed_sps = 100
    max_steps = 100000
    start_below_c = 60
    shutdown_c = 75
    threshold_min_c = 10
    threshold_max_c = 74
    threshold_default_c = 65
    watchdog_ms = 2000
    heartbeat_timeout_ms = 1500
    heartbeat_start_ms = 1000
    buzzer_hz = 2731
    current_note = 'Simulated current.'
    temp = 25
    alert = False

    def __init__(self, motor=None):
        self.motor = motor or Motor()
        self.led = Pin()
        self.buzzer = Buzzer()

    def temperature(self):
        if isinstance(self.temp, Exception):
            raise self.temp
        return self.temp

    def thermal_alert(self):
        return self.alert

    def current_a(self):
        return 0.25


class Platform:
    def __init__(self):
        self.clock = Clock()
        self.usb = SimpleNamespace(usb_name='Motor bench', usb_name_error=None)
        self.resets = 0
        self.feeds = 0

    def device_id(self):
        return 'simulator-001'

    def reset(self):
        self.resets += 1

    def watchdog(self, timeout_ms):
        return SimpleNamespace(feed=self.feed)

    def feed(self):
        self.feeds += 1
