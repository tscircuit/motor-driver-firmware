"""Portable repeat policy for an optional hardware speech adapter."""
class HotAlert:
    def __init__(self, buzzer, clock, enabled=False):
        self.buzzer = buzzer
        self.clock = clock
        self.enabled = enabled
        self.supported = bool(getattr(buzzer, 'speech_supported', False))
        self.error = getattr(buzzer, 'speech_error', None)
        self.mode = None
        self.playing = False
        self.next_at = None

    def stop(self):
        if self.supported:
            self.buzzer.speech_stop()
        self.mode = None
        self.playing = False
        self.next_at = None

    def configure(self, enabled):
        self.stop()
        self.enabled = enabled
        if self.supported:
            self.error = None

    def preview(self):
        if not self.supported:
            raise ValueError(self.error or 'Speech unavailable on this board')
        self.stop()
        self.error = None
        self.buzzer.speech_start()
        self.mode = 'preview'
        self.playing = True

    def tick(self, alarm, now):
        if alarm:
            if not self.enabled or not self.supported or self.error:
                if self.mode:
                    self.stop()
                return False
            if self.mode != 'alarm':
                self.stop()
                self.mode = 'alarm'
        elif self.mode == 'alarm':
            self.stop()
        if self.mode is None:
            return False
        try:
            if self.playing:
                self.playing = self.buzzer.speech_poll()
                if not self.playing:
                    if self.mode == 'preview':
                        self.stop()
                        return False
                    self.next_at = self.clock.ticks_add(now, 2000)
            elif self.next_at is None or self.clock.ticks_diff(now, self.next_at) >= 0:
                self.buzzer.speech_start()
                self.playing = True
                self.next_at = None
            return True  # Owns the buzzer during both speech and the two-second pause.
        except Exception as error:
            self.error = str(error)
            self.stop()
            return False  # Fall back to the ordinary chirp alarm.
