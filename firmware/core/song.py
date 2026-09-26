"""Bounded, non-blocking monophonic byte tunes; no file or audio decoder."""
import binascii

MAX_BYTES = 192
MAX_DURATION_MS = 60000


def decode_song(value):
    if not isinstance(value, str) or not value or len(value) > MAX_BYTES * 2 or len(value) % 8:
        raise ValueError('Song must contain 1-48 four-byte notes as hex')
    try:
        raw = binascii.unhexlify(value)
    except (ValueError, TypeError):
        raise ValueError('Invalid song hex')
    notes = []
    total = 0
    for i in range(0, len(raw), 4):
        frequency = raw[i] | raw[i + 1] << 8
        duration = raw[i + 2] | raw[i + 3] << 8
        if frequency != 0 and not 100 <= frequency <= 10000:
            raise ValueError('Frequency must be 0 (rest) or 100-10000 Hz')
        if not 20 <= duration <= 5000:
            raise ValueError('Note duration must be 20-5000 ms')
        total += duration
        if total > MAX_DURATION_MS:
            raise ValueError('Song must be at most 60 seconds')
        notes.append((frequency, duration))
    return notes


class Song:
    def __init__(self, clock):
        self.clock = clock
        self.notes = []
        self.index = 0
        self.deadline = 0
        self.frequency = 0

    @property
    def playing(self):
        return bool(self.notes)

    def start(self, value):
        notes = decode_song(value)  # Validate fully before replacing playback.
        self.notes = notes
        self.index = 0
        self.frequency = notes[0][0]
        self.deadline = self.clock.ticks_add(self.clock.ticks_ms(), notes[0][1])

    def stop(self):
        self.notes = []
        self.index = 0
        self.frequency = 0

    def tick(self, now):
        # At most 48 entries; skip expired notes instead of extending a tune
        # indefinitely if the foreground loop was delayed.
        while self.playing and self.clock.ticks_diff(now, self.deadline) >= 0:
            self.index += 1
            if self.index == len(self.notes):
                self.stop()
            else:
                self.frequency, duration = self.notes[self.index]
                self.deadline = self.clock.ticks_add(self.deadline, duration)
        return self.frequency
