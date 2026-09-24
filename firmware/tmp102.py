"""TMP102 at 0x48: TI SBOS397I, normal 12-bit mode, active-low comparator."""

WARN_C = 65.0
TRIP_C = 75.0
RESTART_C = 60.0
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
    def __init__(self, i2c, address=0x48):
        self.i2c = i2c
        self.address = address

    def read_register(self, register):
        data = self.i2c.readfrom_mem(self.address, register, 2)
        if len(data) != 2:
            raise OSError("Short TMP102 register read")
        return (data[0] << 8) | data[1]

    def configure(self):
        # Caller must hold GPIO22 LOW throughout configuration and settling.
        self.i2c.writeto_mem(self.address, 1, bytes((CONFIG >> 8, CONFIG & 255)))
        self.i2c.writeto_mem(self.address, 2, encode_temperature(RESTART_C))
        self.i2c.writeto_mem(self.address, 3, encode_temperature(TRIP_C))
        self.verify()

    def verify(self):
        # Detect sensor reset or changed polarity/mode/limits during operation.
        if self.read_register(1) & CONFIG_MASK != CONFIG & CONFIG_MASK:
            raise OSError("TMP102 protection configuration changed")
        for register, value in ((2, RESTART_C), (3, TRIP_C)):
            encoded = encode_temperature(value)
            if self.read_register(register) != (encoded[0] << 8) | encoded[1]:
                raise OSError("TMP102 protection threshold changed")

    def temperature(self):
        self.verify()
        return decode_temperature(self.i2c.readfrom_mem(self.address, 0, 2))
