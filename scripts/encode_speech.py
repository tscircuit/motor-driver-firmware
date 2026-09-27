#!/usr/bin/env python3
"""Encode mono 16 kHz signed LE PCM as 128 kbit/s, LSB-first PDM for GPIO audio."""
import argparse
import struct
from pathlib import Path


def encode(raw):
    if not raw or len(raw) % 2 or len(raw) > 128000:
        raise ValueError('Expected up to four seconds of mono s16le at 16000 Hz')
    samples = struct.unpack('<%dh' % (len(raw) // 2), raw)
    result = bytearray()
    error = 0
    for sample in samples:
        # Leave headroom; first-order pulse-density modulation, eight bits/sample.
        target = 32768 + sample * 4 // 5
        byte = 0
        for bit in range(8):
            error += target
            if error >= 65536:
                error -= 65536
                byte |= 1 << bit
        result.append(byte)
    # Finish low even if the foreground loop stalls after DMA completion.
    result.extend(b'\x00' * (4 + (-len(result) % 4)))
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('pcm')
    parser.add_argument('output')
    args = parser.parse_args()
    data = encode(Path(args.pcm).read_bytes())
    Path(args.output).write_bytes(data)
    print('%d bytes, %.3f seconds' % (len(data), len(data) / 16000))
