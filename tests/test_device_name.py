"""Host-side checks; no board or motor movement required."""
import ast
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('device_config', ROOT / 'firmware/device_config.py')
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
        self.assertEqual(config.usb_strings(Driver, 'Axis X'), {2:'Axis X',4:'Axis X',5:'Axis X'})
        self.assertEqual(before, (Driver.desc_dev, Driver.desc_cfg))

    def command_context(self):
        tree = ast.parse((ROOT / 'firmware/main.py').read_text())
        handler = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == 'handle')
        events = []
        namespace = dict(json=json, device_config=config, mode='stopped', reset_at=None,
                         device_name='Motor bench', threshold=45, ticks_ms=lambda:1000,
                         ticks_add=lambda a,b:a+b, emit=events.append, stop=lambda reason:None)
        exec(compile(ast.Module(body=[handler], type_ignores=[]), '<handle>', 'exec'), namespace)
        return namespace, events

    def test_rename_is_blocked_during_motion_or_without_boot_support(self):
        namespace, events = self.command_context()
        config.usb_name = 'Motor bench'
        namespace['mode'] = 'continuous'
        namespace['handle']('{"cmd":"set_device_name","id":1,"name":"Axis X"}')
        self.assertFalse(events[-1]['ok'])
        self.assertFalse(Path('device.json').exists())
        namespace['mode'] = 'stopped'
        config.usb_name = None
        namespace['handle']('{"cmd":"set_device_name","id":2,"name":"Axis X"}')
        self.assertFalse(events[-1]['ok'])
        self.assertIsNone(namespace['reset_at'])

    def test_ack_before_deferred_restart_and_no_motion_during_restart(self):
        namespace, events = self.command_context()
        config.usb_name = 'Motor bench'
        namespace['handle']('{"cmd":"set_device_name","id":3,"name":"Axis X"}')
        self.assertTrue(events[-1]['ok'])
        self.assertTrue(events[-1]['restarting'])
        self.assertEqual(events[-1]['device_name'], 'Axis X')
        self.assertEqual(namespace['reset_at'], 1750)
        self.assertEqual(config.load_name(), 'Axis X')
        namespace['handle']('{"cmd":"start","id":4}')
        self.assertFalse(events[-1]['ok'])
        self.assertEqual(events[-1]['error'], 'Restart pending')

if __name__ == '__main__':
    unittest.main()
