"""DRV8847 direct-input stepping. Pins and clock are injected, not imported."""

class DRV8847:
    name = 'DRV8847'
    resolutions = {'full': 1, 'half': 2}
    # Half stepping alternates one/two windings without current normalization.
    step_note = ('Quarter stepping is unavailable on this board. Half stepping '
                 'alternates one and two energized coils and can produce more heat.')
    states = ((1,0,0,0), (1,0,1,0), (0,0,1,0), (0,1,1,0),
              (0,1,0,0), (0,1,0,1), (0,0,0,1), (1,0,0,1))

    def __init__(self, enable, inputs, fault, clock):
        if len(inputs) != 4:
            raise ValueError('DRV8847 needs four bridge inputs')
        self.enable_pin = enable
        self.inputs = inputs
        self.fault_pin = fault
        self.clock = clock
        self.phase = 0
        self.resolution = 'full'
        self.enabled = False
        self.disable()

    def enable(self, resolution):
        if resolution not in self.resolutions:
            raise ValueError('Unsupported step resolution')
        self.resolution = resolution
        for pin in self.inputs:
            pin.value(0)
        self.enable_pin.value(1)
        self.enabled = True
        self.clock.sleep_ms(3)

    def disable(self):
        self.enable_pin.value(0)
        self.enabled = False
        for pin in self.inputs:
            pin.value(0)

    def fault_asserted(self):
        return not bool(self.fault_pin.value())

    def step(self, direction):
        if not self.enabled:
            raise RuntimeError('Motor is disabled')
        self.phase = (self.phase + direction * (1 if self.resolution == 'half' else 2)) % 8
        # Clear bridge inputs before changing polarity.
        for pin in self.inputs:
            pin.value(0)
        for pin, value in zip(self.inputs, self.states[self.phase]):
            pin.value(value)
