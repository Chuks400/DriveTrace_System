import json
import tempfile
import unittest
from pathlib import Path

from main import HardwareReplay, SimulationEngine


class Phase3DataTests(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.engine = SimulationEngine(self.dir.name)
        self.engine.external_input = True

    def tearDown(self):
        self.engine.shutdown()
        self.dir.cleanup()

    def test_replay_reports_replay_not_hardware(self):
        self.engine.ingest_hardware({"type":"telemetry", "speed_kmh":42.0,
                                    "imu_valid":True, "imu_ax_g":0.13,
                                    "gps_valid":True, "gps_speed_kmh":43.0,
                                    "wheel_pulses":2}, source="REPLAY")
        state = self.engine.state()
        self.assertEqual(state["mode"], "REPLAY")
        self.assertFalse(state["hardware_connected"])
        self.assertEqual(state["gps_consistency"], "GPS_AGREES")
        self.assertEqual(state["sensor_snapshot"]["wheel_pulses"], 2)
        self.assertAlmostEqual(state["sensor_snapshot"]["imu_ax_g"], 0.13)
        with self.assertRaisesRegex(ValueError, "disabled"):
            self.engine.action("accelerate")

    def test_gps_disagreement_flag_not_evidence_of_malice(self):
        self.engine.ingest_hardware({"type":"telemetry", "speed_kmh":65.0,
                                    "gps_valid":True, "gps_speed_kmh":24.0}, source="REPLAY")
        self.assertEqual(self.engine.state()["gps_consistency"], "GPS_DISAGREES")

    def test_rejects_corrupt_fields_and_nonfinite(self):
        bad = [
            {"type":"telemetry", "speed_kmh":True},
            {"type":"telemetry", "speed_kmh":float("nan")},
            {"type":"telemetry", "speed_kmh":7.2, "gps_valid":True},
            {"type":"telemetry", "speed_kmh":7.2, "gps_valid":42},
            {"type":"telemetry", "speed_kmh":7.2, "wheel_pulses":-5},
            {"type":"telemetry", "speed_kmh":7.2, "imu_ax_g":float("inf")},
        ]
        for packet in bad:
            with self.subTest(packet=packet), self.assertRaises(ValueError):
                self.engine.ingest_hardware(packet)

    def test_software_replay_creates_verified_stop_without_hardware(self):
        source = Path(__file__).resolve().parents[1] / "samples" / "hardware_replay.jsonl"
        replay = HardwareReplay(self.engine, source, interval=0.001)
        replay.start()
        replay.thread.join(timeout=3.0)
        self.assertFalse(replay.thread.is_alive(), "Replay must finish quickly in tests")
        self.assertIsNone(replay.error)
        record = self.engine.recorder.last_event
        self.assertIsNotNone(record)
        self.assertEqual(record["payload"]["event_type"], "STOP")
        self.assertEqual(record["payload"]["source"], "REPLAY")
        self.assertEqual(record["payload"]["maximum_recent_speed_kmh"], 70.0)
        self.assertFalse(self.engine.hardware_connected)
        self.assertTrue(self.engine.recorder.verify_file(self.engine.recorder.last_event_path))

    def test_real_usb_source_is_distinct(self):
        self.engine.ingest_hardware({"type":"telemetry", "speed_kmh":9.5}, source="HARDWARE")
        state = self.engine.state()
        self.assertEqual(state["mode"], "HARDWARE")
        self.assertTrue(state["hardware_connected"])
        self.assertEqual(state["gps_consistency"], "GPS_UNAVAILABLE")


if __name__ == "__main__":
    unittest.main()
