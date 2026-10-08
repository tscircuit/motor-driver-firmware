"""Distance-based trapezoidal profile and non-bursting microsecond deadlines."""
import math


class Motion:
    def __init__(self, clock, target, acceleration, start_speed, count, settle_ms=100, fixed_ramp=False):
        self.clock = clock
        self.target = target
        self.acceleration = acceleration
        self.fixed_ramp = fixed_ramp
        self.initial = 0 if fixed_ramp else min(start_speed, target)
        self.count = count  # None means continuous
        self.brake_origin = None
        self.brake_speed = None
        self.cruise_origin = 0
        self.cruise_speed = self.initial
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
            if self.brake_origin is not None:
                return max(self.initial, math.sqrt(max(self.initial ** 2, self.brake_speed ** 2 -
                           2 * self.acceleration * max(0, position - self.brake_origin))))
            if self.count is None:
                distance = max(0, position - self.cruise_origin)
                if self.cruise_speed > self.target:
                    return max(self.target, math.sqrt(max(self.target ** 2, self.cruise_speed ** 2 - 2 * self.acceleration * distance)))
                return min(self.target, math.sqrt(self.cruise_speed ** 2 + 2 * self.acceleration * distance))
            distance = min(position, self.count - position)
            return min(self.target, math.sqrt(self.initial ** 2 + 2 * self.acceleration * max(0, distance)))
        if self.fixed_ramp and self.count is not None and self.brake_origin is None:
            peak = min(self.target, math.sqrt(self.acceleration * self.count))
            ramp_distance = peak ** 2 / (2 * self.acceleration)
            duration = 2 * peak / self.acceleration + (self.count - 2 * ramp_distance) / peak
            def time_at(position):
                if position <= ramp_distance:
                    return math.sqrt(2 * position / self.acceleration)
                if position >= self.count - ramp_distance:
                    return duration - math.sqrt(max(0, 2 * (self.count - position) / self.acceleration))
                return peak / self.acceleration + (position - ramp_distance) / peak
            self.rate = speed(index + 1)
            return max(1, math.ceil(1000000 * (time_at(index + 1) - time_at(index))))
        if self.fixed_ramp:
            if self.brake_origin is not None:
                origin, initial, target = self.brake_origin, self.brake_speed, 0
            else:
                origin, initial, target = self.cruise_origin, self.cruise_speed, self.target
            distance_to_target = abs(target ** 2 - initial ** 2) / (2 * self.acceleration)
            def time_at(position):
                distance = max(0, position - origin)
                if not distance:
                    return 0
                if distance >= distance_to_target:
                    ramp_time = abs(target - initial) / self.acceleration
                    return ramp_time + ((distance - distance_to_target) / target if target else 0)
                sign = 1 if target > initial else -1
                end_speed = math.sqrt(max(0, initial ** 2 + sign * 2 * self.acceleration * distance))
                return 2 * distance / (initial + end_speed)
            self.rate = speed(index + 1)
            return max(1, math.ceil(1000000 * (time_at(index + 1) - time_at(index))))
        before, after = speed(index), speed(index + 1)
        self.rate = after
        return max(1, math.ceil(2000000 / (before + after)))

    def retarget(self, target, acceleration, rate):
        self.target, self.acceleration = target, acceleration
        self.initial = 0 if self.fixed_ramp else min(10, target)
        self.count = None
        self.brake_origin = self.brake_speed = None
        self.cruise_origin, self.cruise_speed = self.executed, rate
        self.interval_us = self.interval(self.executed)

    def brake(self):
        """Convert continuous motion to a bounded deceleration tail, once."""
        if self.brake_origin is not None:
            return self.count - self.executed
        self.brake_origin = self.executed
        self.brake_speed = max(self.initial, self.rate)
        tail = max(1, math.ceil((self.brake_speed ** 2 - self.initial ** 2) /
                               (2 * self.acceleration)))
        self.count = self.executed + tail
        # Legacy profiles retain their deadline; fixed ramps begin at release.
        self.interval_us = self.interval(self.executed)
        if self.fixed_ramp:
            self.deadline = self.clock.ticks_add(self.clock.ticks_us(), self.interval_us)
        return tail

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
            self.deadline = self.clock.ticks_add(self.clock.ticks_us(), 0 if self.fixed_ramp else self.interval_us)
            return
        self.interval_us = self.interval(self.executed)
        # Rebase on the actual output time. No catch-up bursts after USB/I2C/GC.
        self.deadline = self.clock.ticks_add(self.clock.ticks_us(), self.interval_us)
