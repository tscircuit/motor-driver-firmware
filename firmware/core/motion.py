"""Distance-based trapezoidal profile and non-bursting microsecond deadlines."""
import math


class Motion:
    def __init__(self, clock, target, acceleration, start_speed, count, settle_ms=100):
        self.clock = clock
        self.target = target
        self.acceleration = acceleration
        self.initial = min(start_speed, target)
        self.count = count  # None means continuous
        self.executed = 0
        self.late_steps = 0
        self.max_lateness_us = 0
        self.rate = self.initial
        self.interval_us = self.interval(0)
        self.deadline = clock.ticks_add(clock.ticks_us(), settle_ms * 1000 + self.interval_us)

    def interval(self, index):
        # Speed at the boundaries of each unit-distance segment. Finite profiles
        # are symmetric, including short triangular moves. Never round speed up.
        def speed(position):
            distance = position if self.count is None else min(position, self.count - position)
            return min(self.target, math.sqrt(self.initial ** 2 + 2 * self.acceleration * max(0, distance)))
        before, after = speed(index), speed(index + 1)
        self.rate = after
        return max(1, math.ceil(2000000 / (before + after)))

    def due(self):
        return self.clock.ticks_diff(self.clock.ticks_us(), self.deadline) >= 0

    def lateness(self):
        return max(0, self.clock.ticks_diff(self.clock.ticks_us(), self.deadline))

    def advanced(self):
        late = self.lateness()
        self.max_lateness_us = max(self.max_lateness_us, late)
        if late > 250:
            self.late_steps += 1
        self.executed += 1
        if self.count is not None and self.executed >= self.count:
            self.deadline = self.clock.ticks_add(self.clock.ticks_us(), self.interval_us)
            return
        self.interval_us = self.interval(self.executed)
        # Rebase on the actual output time. No catch-up bursts after USB/I2C/GC.
        self.deadline = self.clock.ticks_add(self.clock.ticks_us(), self.interval_us)
