"""tscircuit RP2040 motor controller v1.0.42, DRV8825 and RGB."""
import math
from platforms.micropython import Platform, Tone
from boards.rp2040_drv8847 import Board as LegacyBoard
from drivers.drv8825 import DRV8825
from sensors.tmp102 import TMP102

PD_CODES = {5: (0, 0, 0), 9: (1, 1, 1), 12: (1, 1, 0),
            15: (1, 0, 0), 20: (1, 0, 1)}


def safe_outputs(platform):
    platform.output(22)  # nSLEEP permission off before any selector changes
    platform.output(21).value(1)  # active-low nENBL
    for pin in (18, 19, 20, 4, 5, 6, 25, 2, 3, 16):
        platform.output(pin)


class RGB:
    colors = {'off': (0, 0, 0), 'red': (1, 0, 0), 'green': (0, 1, 0),
              'blue': (0, 0, 1), 'yellow': (1, 1, 0), 'cyan': (0, 1, 1),
              'magenta': (1, 0, 1), 'white': (1, 1, 1)}

    def __init__(self, pins):
        self.pins = pins
        self.color = 'off'

    def set_color(self, color):
        if color not in self.colors:
            raise ValueError('Unknown LED color')
        for pin, value in zip(self.pins, self.colors[color]):
            pin.value(value)
        self.color = color

    def value(self, value):
        self.set_color('green' if value else 'off')


class Board(LegacyBoard):
    id = 'rp2040_drv8825'
    name = 'RP2040 / DRV8825 / RGB'
    current_note = 'INA240 winding readings: nominal 1.65 V midpoint, 1 V/A; uncalibrated instantaneous samples.'
    pd_voltages = (5, 9, 12, 15, 20)
    led_colors = tuple(RGB.colors)

    def __init__(self, platform):
        safe_outputs(platform)
        self.platform = platform
        self.pd_pins = [platform.output(pin) for pin in (4, 5, 6)]
        self.pd_voltage = 5
        self.pd_ready_at = platform.clock.ticks_ms()
        self.motor = DRV8825(platform.output(22), platform.output(18),
                            platform.output(19), platform.output(20),
                            platform.output(21), platform.input(23),
                            platform.clock, self.power_ready)
        self.led = RGB([platform.output(pin) for pin in (25, 2, 3)])
        self.buzzer = Tone(16, self.buzzer_hz)
        self.adcs = [platform.adc(pin) for pin in (28, 29)]
        self.currents = None
        self.sensor = None
        try:
            sensor = TMP102(platform.i2c(1, 26, 27, 100000),
                            trip_c=self.shutdown_c, restart_c=self.start_below_c)
            sensor.configure()
            platform.clock.sleep_ms(300)
            self.sensor = sensor
        except Exception:
            pass

    def thermal_alert(self):
        # TMP102 ALERT/GPIO24 are unconnected on this revision.
        return False

    def set_pd_voltage(self, voltage):
        if isinstance(voltage, bool) or voltage not in PD_CODES:
            raise ValueError('Choose 5, 9, 12, 15 or 20 V')
        self.motor.disable()
        code = PD_CODES[voltage]
        self.pd_pins[0].value(0)
        self.pd_pins[1].value(code[1])
        self.pd_pins[2].value(code[2])
        self.pd_pins[0].value(code[0])
        self.pd_voltage = voltage
        self.pd_ready_at = self.platform.clock.ticks_add(self.platform.clock.ticks_ms(), 2000)

    def power_ready(self):
        return self.pd_voltage >= 9 and self.platform.clock.ticks_diff(
            self.platform.clock.ticks_ms(), self.pd_ready_at) >= 0

    def current_a(self):
        try:
            raw = [adc.read_u16() for adc in self.adcs]
            if any(value < 256 or value > 65279 for value in raw):
                self.currents = None
                return None
            self.currents = [(value * 3.3 / 65535 - 1.65) for value in raw]
            return math.sqrt(sum(value * value for value in self.currents))
        except Exception:
            self.currents = None
            return None

    def extra_status(self):
        return {'winding_current_a': self.currents,
                'pd_voltage_requested': self.pd_voltage, 'motor_power_ready': self.power_ready(),
                'pd_voltage_measured': None, 'led_color': self.led.color}
