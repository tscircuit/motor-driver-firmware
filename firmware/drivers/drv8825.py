"""STEP/DIR adapter for the PCB's hardwired 1/16 microstep mode."""

class DRV8825:
    name = 'DRV8825'
    resolutions = {'full': 1, 'half': 2, 'quarter': 4, 'eighth': 8, 'sixteenth': 16}
    step_note = ('Hardware uses fixed 1/16 microsteps (3200 pulses/revolution). '
                 'Larger selected increments group 16, 8, 4 or 2 pulses; they do not change the hardware mode.')

    def __init__(self, sleep, step, direction, reset, enable_n, fault, clock, ready):
        self.sleep_pin, self.step_pin, self.direction_pin = sleep, step, direction
        self.reset_pin, self.enable_n, self.fault_pin = reset, enable_n, fault
        self.clock, self.ready = clock, ready
        self.enabled = False
        self.resolution = 'full'
        self.disable()
        self.reset_pin.value(1)

    def enable(self, resolution):
        if resolution not in self.resolutions:
            raise ValueError('Unsupported step resolution')
        if not self.ready():
            raise ValueError('Select motor power and wait for USB-PD settling')
        self.resolution = resolution
        self.step_pin.value(0)
        self.reset_pin.value(1)
        self.sleep_pin.value(1)
        self.clock.sleep_ms(3)
        self.enable_n.value(0)
        self.enabled = True

    def disable(self):
        self.enable_n.value(1)
        self.sleep_pin.value(0)
        self.step_pin.value(0)
        self.enabled = False

    def fault_asserted(self):
        return not bool(self.fault_pin.value())

    def step(self, direction):
        if not self.enabled:
            raise RuntimeError('Motor is disabled')
        self.direction_pin.value(1 if direction > 0 else 0)
        self.clock.sleep_us(3)
        for _ in range(16 // self.resolutions[self.resolution]):
            self.step_pin.value(1)
            self.clock.sleep_us(3)
            self.step_pin.value(0)
            self.clock.sleep_us(3)
