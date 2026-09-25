"""Persistent USB product name; shared by boot.py and the serial protocol."""
import json
import os

DEFAULT_NAME = 'Motor bench'


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
