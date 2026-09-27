"""Filesystem-backed settings. Existing board files remain compatible."""
import json
import os
import math
import device_config


class Settings:
    def load_name(self):
        return device_config.load_name()

    def save_name(self, value):
        return device_config.save_name(value)

    def load_threshold(self, default, minimum, maximum):
        try:
            with open('alarm.json') as file:
                value = json.load(file)['threshold_c']
            if (not isinstance(value, bool) and isinstance(value, (int, float))
                    and math.isfinite(value) and minimum <= value <= maximum):
                return float(value)
        except Exception:
            pass
        return float(default)

    def save_threshold(self, value):
        with open('alarm.tmp', 'w') as file:
            json.dump({'threshold_c': float(value)}, file)
        os.rename('alarm.tmp', 'alarm.json')

    def load_hot_alert(self):
        try:
            with open('hot_alert.json') as file:
                return json.load(file).get('enabled') is True
        except Exception:
            return False

    def save_hot_alert(self, enabled):
        with open('hot_alert.tmp', 'w') as file:
            json.dump({'enabled': enabled}, file)
        os.rename('hot_alert.tmp', 'hot_alert.json')
