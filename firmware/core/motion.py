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
        if fixed_ramp and count is not None:
            self.peak = min(target, math.sqrt(acceleration * count))
            self.ramp_distance = self.peak ** 2 / (2 * acceleration)
            self.duration = 2 * self.peak / acceleration + (count - 2 * self.ramp_distance) / self.peak
        self.interval_us = self.interval(0)
        self.deadline = clock.ticks_add(clock.ticks_us(), settle_ms * 1000 + self.interval_us)

    def speed_at(self, position):
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

    def time_at(self, position):
        if self.count is not None and self.brake_origin is None:
            if position <= self.ramp_distance:
                return math.sqrt(2 * position / self.acceleration)
            if position >= self.count - self.ramp_distance:
                return self.duration - math.sqrt(max(0, 2 * (self.count - position) / self.acceleration))
            return self.peak / self.acceleration + (position - self.ramp_distance) / self.peak
        if self.brake_origin is not None:
            origin, initial, target = self.brake_origin, self.brake_speed, 0
        else:
            origin, initial, target = self.cruise_origin, self.cruise_speed, self.target
        distance = max(0, position - origin)
        if not distance:
            return 0
        distance_to_target = abs(target ** 2 - initial ** 2) / (2 * self.acceleration)
        if distance >= distance_to_target:
            return abs(target - initial) / self.acceleration + ((distance - distance_to_target) / target if target else 0)
        sign = 1 if target > initial else -1
        end_speed = math.sqrt(max(0, initial ** 2 + sign * 2 * self.acceleration * distance))
        return 2 * distance / (initial + end_speed)

    def interval(self, index):
        # Methods and cached finite-profile constants avoid per-pulse closures/GC.
        if self.fixed_ramp:
            if self.count is not None and self.brake_origin is None and index >= self.ramp_distance and index + 1 <= self.count - self.ramp_distance:
                self.rate = self.peak
                return max(1, math.ceil(1000000 / self.peak))
            self.rate = self.speed_at(index + 1)
            return max(1, math.ceil(1000000 * (self.time_at(index + 1) - self.time_at(index))))
        before, after = self.speed_at(index), self.speed_at(index + 1)
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
        # Preserve phase after small loop delays instead of accumulating them.
        # A missed whole period still rebases, avoiding a catch-up pulse burst.
        planned = self.clock.ticks_add(self.deadline, self.interval_us)
        self.deadline = planned if self.clock.ticks_diff(planned, self.clock.ticks_us()) > 0 else self.clock.ticks_add(self.clock.ticks_us(), self.interval_us)
