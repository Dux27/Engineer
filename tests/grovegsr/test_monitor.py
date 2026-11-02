import importlib.util
from pathlib import Path
import sys
import unittest

path = Path(__file__).resolve().parents[2] / 'tools' / 'sensor_monitor.py'
spec = importlib.util.spec_from_file_location('sensor_monitor', path)
monitor = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = monitor
spec.loader.exec_module(monitor)


class EDAMonitorTests(unittest.TestCase):
    def test_all_four_sensors(self):
        model = monitor.Dashboard()
        for char in monitor.demo_lines(8):
            model.feed(char, 10)
        self.assertEqual(model.sensors['GroveGSR'].values, (1800, 1360))
        self.assertEqual(model.sensors['GroveGSR'].updated, 10)
        self.assertEqual(model.sensors['MAX30102'].values, (75, 800))
        self.assertTrue(all(s.updated == 10 for s in model.sensors.values()))

    def test_raw_only_and_clipped(self):
        model = monitor.Dashboard()
        model.feed('I (100) APP: [EDA] Raw=1800 | Voltage=unavailable | Clipped=no\n', 1)
        self.assertEqual(model.sensors['GroveGSR'].values, (1800, None))
        self.assertIn('calibration unavailable', model.sensors['GroveGSR'].status)
        model.feed('I (200) APP: [EDA] Raw=4095 | Voltage=3100 mV | Clipped=yes\n', 2)
        self.assertIn('clipped', model.sensors['GroveGSR'].status)

    def test_read_error_preserves_last_reading_and_age(self):
        model = monitor.Dashboard()
        model.feed(monitor.demo_lines(8), 1)
        model.feed('E (900) APP: Failed to read GroveGSR: ESP_ERR_TIMEOUT\n', 2)
        sensor = model.sensors['GroveGSR']
        self.assertEqual(sensor.updated, 1)
        self.assertIn('ESP_ERR_TIMEOUT', sensor.status)
        self.assertIn('STALE', monitor.age(sensor, 10))

    def test_initialization_is_not_sensor_contact(self):
        model = monitor.Dashboard()
        model.feed('I (500) GroveGSR: Initialized successfully: GPIO=2 ADC1 channel=1\n', 1)
        self.assertIsNone(model.sensors['GroveGSR'].updated)
        self.assertIn('Initialized', model.sensors['GroveGSR'].status)


if __name__ == '__main__':
    unittest.main()
