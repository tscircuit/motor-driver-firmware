"""GPIO16 speech: one PIO SM and one DMA channel, no audio-rate Python loop."""
import machine
import rp2
import time
from platforms.micropython import Tone


@rp2.asm_pio(out_init=rp2.PIO.OUT_LOW, out_shiftdir=rp2.PIO.SHIFT_RIGHT,
             autopull=True, pull_thresh=32, fifo_join=rp2.PIO.JOIN_TX)
def _pdm():
    out(pins, 1)


class SpeechTone(Tone):
    def __init__(self, pin, frequency, sm_id=0):
        super().__init__(pin, frequency)
        self.speech_mode = False
        self.sm_id = sm_id  # Reserved by the board profile; other adapters must not use it.
        self.sm = None
        self.dma = None
        self.speech_supported = False
        self.speech_error = None
        self.data = None
        try:
            with open('assets/hot.pdm', 'rb') as file:
                self.data = file.read(64009)
            if (not self.data or len(self.data) > 64008 or len(self.data) % 4
                    or self.data[-4:] != b'\x00\x00\x00\x00'):
                raise ValueError('Invalid speech asset')
            if not hasattr(rp2, 'DMA'):
                raise ValueError('MicroPython rp2.DMA required')
            self.speech_supported = True
        except Exception as error:
            self.speech_error = str(error)
            self.data = None

    def speech_start(self):
        if not self.speech_supported:
            raise ValueError(self.speech_error or 'Speech unavailable')
        self.speech_stop()
        try:
            self.speech_mode = True
            self.pwm.deinit()
            self.sm = rp2.StateMachine(self.sm_id, _pdm, freq=128000,
                                       out_base=machine.Pin(self.pin))
            self.dma = rp2.DMA()
            control = self.dma.pack_ctrl(size=2, inc_read=True, inc_write=False,
                                        treq_sel=(self.sm_id // 4) * 8 + self.sm_id % 4)
            self.dma.config(read=self.data, write=self.sm, count=len(self.data) // 4,
                            ctrl=control, trigger=True)
            self.sm.active(1)
            self.end_at = time.ticks_add(time.ticks_ms(), (len(self.data) + 15) // 16 + 2)
        except Exception:
            self.speech_stop()
            raise

    def speech_poll(self):
        if self.dma is None:
            return False
        if self.dma.ctrl & ((1 << 29) | (1 << 30)):
            self.speech_stop()
            raise OSError('Speech DMA bus error')
        if time.ticks_diff(time.ticks_ms(), self.end_at) >= 0:
            # Bound even a stuck transfer; never leave a repeating hardware stream.
            incomplete = self.dma.active()
            self.speech_stop()
            if incomplete:
                raise OSError('Speech DMA did not finish')
            return False
        return True

    def speech_stop(self):
        if not self.speech_mode:
            return
        if self.dma is not None:
            self.dma.close()
            self.dma = None
        if self.sm is not None:
            self.sm.active(0)
            self.sm = None
        machine.Pin(self.pin, machine.Pin.OUT, value=0)
        self.pwm = machine.PWM(machine.Pin(self.pin))
        self.frequency = 2731
        self.pwm.freq(self.frequency)
        self.pwm.duty_u16(0)
        self.speech_mode = False

    def close(self):
        self.speech_stop()
        super().close()
