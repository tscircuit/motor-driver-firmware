"""TMP102 at 0x48: TI SBOS397I, normal 12-bit mode, active-low comparator."""

CONFIG = 0x60C0  # continuous, 8 Hz, one fault, active-low comparator, EM=0
CONFIG_MASK = 0x7FD0  # exclude OS and read-only ALERT status


def encode_temperature(celsius):
    raw = (int(round(celsius * 16)) & 0xFFF) << 4
    return bytes((raw >> 8, raw & 0xFF))


def decode_temperature(data):
    if len(data) != 2 or data[1] & 0x0F:
        raise ValueError("Invalid TMP102 normal-mode temperature")
    raw = ((data[0] << 8) | data[1]) >> 4
    if raw & 0x800:
        raw -= 0x1000
    result = raw / 16.0
    if not -40 <= result <= 125:
        raise ValueError("Temperature outside sensor operating range")
    return result


class TMP102:
    def __init__(self, i2c, address=0x48, trip_c=75.0, restart_c=60.0):
        self.i2c = i2c
        self.address = address
        self.trip_c = trip_c
        self.restart_c = restart_c

    def read_register(self, register):
        data = self.i2c.readfrom_mem(self.address, register, 2)
        if len(data) != 2:
            raise OSError("Short TMP102 register read")
        return (data[0] << 8) | data[1]

    def configure(self):
        # Caller must keep the motor disabled throughout configuration and settling.
        self.i2c.writeto_mem(self.address, 1, bytes((CONFIG >> 8, CONFIG & 255)))
        self.i2c.writeto_mem(self.address, 2, encode_temperature(self.restart_c))
        self.i2c.writeto_mem(self.address, 3, encode_temperature(self.trip_c))
        self.verify()

    def verify(self):
        # Detect sensor reset or changed polarity/mode/limits during operation.
        if self.read_register(1) & CONFIG_MASK != CONFIG & CONFIG_MASK:
            raise OSError("TMP102 protection configuration changed")
        for register, value in ((2, self.restart_c), (3, self.trip_c)):
            encoded = encode_temperature(value)
            if self.read_register(register) != (encoded[0] << 8) | encoded[1]:
                raise OSError("TMP102 protection threshold changed")

    def temperature(self):
        self.verify()
        return decode_temperature(self.i2c.readfrom_mem(self.address, 0, 2))
