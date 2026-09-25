"""MicroPython services. New ports adapt this layer, not the controller."""
import machine
import time
import sys
import select
from platforms import usb_identity


class Tone:
    def __init__(self, pin, frequency):
        self.pin = pin
        self.pwm = machine.PWM(machine.Pin(pin))
        self.pwm.freq(frequency)
        self.set(False)

    def set(self, sounding):
        self.pwm.duty_u16(32768 if sounding else 0)

    def close(self):
        self.set(False)
        self.pwm.deinit()
        machine.Pin(self.pin, machine.Pin.OUT, value=0)


class SerialTransport:
    """Bounded newline framing; oversized commands are discarded in full."""
    def __init__(self):
        self.poll = select.poll()
        self.poll.register(sys.stdin, select.POLLIN)
        self.buffer = ''
        self.overflow = False

    def read_lines(self):
        lines = []
        for _ in range(128):
            if not self.poll.poll(0):
                break
            char = sys.stdin.read(1)
            if char in ('\n', '\r'):
                if self.buffer and not self.overflow:
                    lines.append(self.buffer)
                self.buffer = ''
                self.overflow = False
            elif not self.overflow:
                self.buffer += char
                if len(self.buffer) > 512:
                    self.buffer = ''
                    self.overflow = True
        return lines

    def write_line(self, line):
        print(line)


class Platform:
    clock = time
    usb = usb_identity

    def output(self, pin):
        return machine.Pin(pin, machine.Pin.OUT, value=0)

    def input(self, pin):
        return machine.Pin(pin, machine.Pin.IN)

    def i2c(self, bus, sda, scl, frequency):
        return machine.I2C(bus, sda=machine.Pin(sda), scl=machine.Pin(scl), freq=frequency)

    def tone(self, pin, frequency):
        return Tone(pin, frequency)

    def watchdog(self, timeout_ms):
        return machine.WDT(timeout=timeout_ms)

    def device_id(self):
        return machine.unique_id().hex()

    def reset(self):
        machine.reset()

    def configure_usb(self, name):
        usb_identity.configure(machine, name)

    def transport(self):
        return SerialTransport()
