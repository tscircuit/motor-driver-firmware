"""Persistent USB product name; shared by boot.py and the serial protocol."""
import json
import os

DEFAULT_NAME = 'Motor bench'
usb_name = None
usb_name_error = None


def validate_name(value):
    if not isinstance(value, str):
        raise ValueError('Device name must be text')
    value = value.strip()
    if not 1 <= len(value) <= 32 or any(ord(c) < 32 or ord(c) > 126 for c in value):
        raise ValueError('Use 1-32 printable ASCII characters')
    return value


def load_name():
    try:
        with open('device.json') as f:
            return validate_name(json.load(f)['name'])
    except Exception:
        return DEFAULT_NAME


def save_name(value):
    value = validate_name(value)
    with open('device.tmp', 'w') as f:
        json.dump({'name': value}, f)
    os.rename('device.tmp', 'device.json')
    return value


def usb_strings(driver, name):
    # Preserve VID, PID, serial number, endpoints, and CDC interfaces.
    strings = {driver.desc_dev[15]: name}
    cfg = driver.desc_cfg
    offset = 0
    while offset < len(cfg):
        size = cfg[offset]
        if size < 2 or offset + size > len(cfg):
            raise ValueError('Invalid built-in USB descriptor')
        kind = cfg[offset + 1]
        if kind == 4 and size >= 9 and cfg[offset + 5] in (2, 10):
            index = cfg[offset + 8]  # CDC interface label
            if index:
                strings[index] = name
        elif kind == 11 and size >= 8 and cfg[offset + 4] == 2:
            index = cfg[offset + 7]  # CDC interface association label
            if index:
                strings[index] = name
        offset += size
    return strings
