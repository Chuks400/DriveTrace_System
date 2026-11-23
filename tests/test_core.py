import json
import tempfile
import unittest
from pathlib import Path

from drivetrace_core import DriveTraceRecorder


class DriveTraceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.recorder = DriveTraceRecorder(self.temp.name, speed_limit_kmh=50,
                                           secret_key=b"test-only-key-material-32-bytes!!")

    def tearDown(self):
        self.temp.cleanup()

    def test_stop_saves_recent_speed_and_overspeed(self):
        self.recorder.add_sample(0, 1000)
        self.recorder.add_sample(40, 1001)
        self.recorder.add_sample(62, 1002)
        self.recorder.add_sample(12, 1003)
        event = self.recorder.add_sample(0, 1004)
        self.assertEqual(event["payload"]["speed_before_stop_kmh"], 12)
        self.assertEqual(event["payload"]["maximum_recent_speed_kmh"], 62)
        self.assertTrue(event["payload"]["overspeed_detected"])
        self.assertTrue(self.recorder.verify_file(self.recorder.last_event_path))

    def test_stopping_only_creates_one_event(self):
        self.recorder.add_sample(20, 1000)
        self.assertIsNotNone(self.recorder.add_sample(0, 1001))
        self.assertIsNone(self.recorder.add_sample(0, 1002))
        self.assertEqual(len(list(Path(self.temp.name).glob("events/*.json"))), 1)

    def test_history_window(self):
        self.recorder.add_sample(99, 1000)
        self.recorder.add_sample(10, 1040)
        event = self.recorder.add_sample(0, 1041)
        self.assertEqual(event["payload"]["maximum_recent_speed_kmh"], 10)
        self.assertEqual(len(event["payload"]["samples"]), 2)

    def test_modified_event_fails_integrity_check(self):
        self.recorder.add_sample(60, 1000)
        self.recorder.add_sample(0, 1001)
        path = self.recorder.last_event_path
        record = json.loads(path.read_text(encoding="utf-8"))
        record["payload"]["maximum_recent_speed_kmh"] = 20
        path.write_text(json.dumps(record), encoding="utf-8")
        self.assertFalse(self.recorder.verify_file(path))

    def test_tamper_event(self):
        event = self.recorder.log_tamper(1000)
        self.assertEqual(event["payload"]["event_type"], "TAMPER")
        self.assertTrue(self.recorder.verify_file(self.recorder.last_event_path))


if __name__ == "__main__":
    unittest.main()
