import struct
import unittest
from fakes import Clock
from core.song import Song, decode_song


def tune(*notes):
    return b''.join(struct.pack('<HH', *note) for note in notes).hex()


class SongTests(unittest.TestCase):
    def test_play_rest_complete_and_wrap(self):
        clock=Clock();clock.now=clock.modulus-50
        song=Song(clock);song.start(tune((440,100),(0,20),(880,100)))
        self.assertEqual(song.tick(clock.now),440)
        clock.sleep_ms(100);self.assertEqual(song.tick(clock.now),0)
        self.assertTrue(song.playing)
        clock.sleep_ms(20);self.assertEqual(song.tick(clock.now),880)
        clock.sleep_ms(100);self.assertEqual(song.tick(clock.now),0)
        self.assertFalse(song.playing)

    def test_validation_and_atomic_replace(self):
        song=Song(Clock());song.start(tune((440,100)))
        for value in ('',None,'zzzzzzzz','00',tune((99,100)),tune((440,19)),tune((440,5001)),tune(*([(440,20)]*49)),tune(*([(440,5000)]*13))):
            with self.assertRaises(ValueError):song.start(value)
            self.assertEqual(song.frequency,440)
        self.assertEqual(len(decode_song(tune(*([(440,20)]*48)))),48)

    def test_stall_skips_expired_notes_and_stop_clears(self):
        clock=Clock();song=Song(clock);song.start(tune((440,100),(880,100)))
        clock.sleep_ms(300);self.assertEqual(song.tick(clock.now),0)
        self.assertFalse(song.playing)
        song.start(tune((440,100)));song.stop();self.assertEqual(song.tick(clock.now),0)
