"""Host-side checks; no board or motor movement required."""
import importlib.util
import os
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('device_config', ROOT / 'firmware/device_config.py')
import fakes  # Adds firmware to the host import path
from platforms.usb_identity import usb_strings
config = importlib.util.module_from_spec(spec)
spec.loader.exec_module(config)


class DeviceNameTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.previous = os.getcwd()
        os.chdir(self.temp.name)

    def tearDown(self):
        os.chdir(self.previous)
        self.temp.cleanup()

    def test_persistence_and_corrupt_config_fallback(self):
        self.assertEqual(config.load_name(), 'Motor bench')
        self.assertEqual(config.save_name('  Left motor  '), 'Left motor')
        self.assertEqual(config.load_name(), 'Left motor')
        Path('device.json').write_text('{broken')
        self.assertEqual(config.load_name(), 'Motor bench')

    def test_invalid_names_leave_saved_name_unchanged(self):
        config.save_name('Axis X')
        for value in ('', '   ', 'x' * 33, 'a\nb', 'a\x00b', 'Mötör', None, 5):
            with self.assertRaises(ValueError):
                config.save_name(value)
            self.assertEqual(config.load_name(), 'Axis X')
        self.assertEqual(config.save_name('x' * 32), 'x' * 32)

    def test_usb_strings_preserve_serial_and_descriptor_bytes(self):
        class Driver:
            desc_dev = bytes([0] * 15 + [2, 3, 1])
            desc_cfg = bytes([9,2,26,0,2,1,0,128,50, 8,11,0,2,2,2,1,4, 9,4,0,0,1,2,2,1,5])
        before = Driver.desc_dev, Driver.desc_cfg
        self.assertEqual(usb_strings(Driver, 'Axis X'), {2:'Axis X',4:'Axis X',5:'Axis X'})
        self.assertEqual(before, (Driver.desc_dev, Driver.desc_cfg))

if __name__ == '__main__':
    unittest.main()
