"""The tested tscircuit RP2040 / DRV8847 PCB and its safety limits."""
from platforms.micropython import Platform
from drivers.drv8847 import DRV8847
from sensors.tmp102 import TMP102

ENABLE = 22
BRIDGE_INPUTS = (18, 19, 20, 21)
FAULT = 23
ALERT = 24
LED = 25
BUZZER = 16
SDA = 26
SCL = 27


def safe_outputs(platform):
    platform.output(ENABLE)
    for pin in BRIDGE_INPUTS:
        platform.output(pin)


class Board:
    id = 'rp2040_drv8847'
    name = 'RP2040 / DRV8847'
    mcu = 'RP2040'
    full_steps_per_revolution = 200
    min_speed_sps = 5
    max_speed_sps = 400
    start_speed_sps = 10
    default_acceleration_sps2 = 100
    min_acceleration_sps2 = 10
    max_acceleration_sps2 = 1000
    settle_ms = 100
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
    current_note = 'No current measurement connection to the MCU.'

    def __init__(self, platform):
        safe_outputs(platform)
        self.motor = DRV8847(platform.output(ENABLE),
                            [platform.output(pin) for pin in BRIDGE_INPUTS],
                            platform.input(FAULT), platform.clock)
        self.alert = platform.input(ALERT)
        self.led = platform.output(LED)
        self.buzzer = platform.tone(BUZZER, self.buzzer_hz)
        self.sensor = None
        try:
            sensor = TMP102(platform.i2c(1, SDA, SCL, 100000),
                            trip_c=self.shutdown_c, restart_c=self.start_below_c)
            sensor.configure()
            platform.clock.sleep_ms(300)
            self.sensor = sensor
        except Exception:
            # Missing or misconfigured temperature sensing prevents motion.
            pass

    def temperature(self):
        if self.sensor is None:
            raise OSError('Temperature sensor unavailable; reset to retry')
        return self.sensor.temperature()

    def thermal_alert(self):
        return not bool(self.alert.value())

    def current_a(self):
        return None
