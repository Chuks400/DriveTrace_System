"""DriveTrace Phase 3 — local dashboard, simulator, replay and optional USB sensors.

Standard-library Python backend. Launch via: py main.py
No hardware, framework, database or network connection is required.
"""
from __future__ import annotations

import argparse
import json
import math
import mimetypes
from pathlib import Path
import re
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import unquote, urlsplit
import webbrowser

from drivetrace_core import DriveTraceRecorder

ROOT = Path(__file__).resolve().parent
STATIC = ROOT / "static"
ALLOWED_ASSETS = {
    "/": "index.html",
    "/index.html": "index.html",
    "/static/app.js": "app.js",
    "/static/style.css": "style.css",
    "/static/favicon.svg": "favicon.svg",
}
EVENT_NAME = re.compile(r"^\d{8}T\d{6}_\d{6}Z_[0-9a-f]{8}\.json$")


class SimulationEngine:
    """One state lock for the simulator and its event recorder."""

    def __init__(self, data_dir: Path | str | None = None):
        self.lock = threading.RLock()
        self.recorder = DriveTraceRecorder(data_dir or ROOT / "data")
        self.speed = 0.0
        self.running = False
        self.demo_started: float | None = None
        self.message = "Ready for a simulated drive. No hardware required."
        self.started_at = time.time()
        self._stop = threading.Event()
        self._worker: threading.Thread | None = None
        self.latest_stop_event: dict | None = None
        self.latest_stop_path: Path | None = None
        self.hardware_connected = False
        self.external_input = False
        self.hardware_last_seen: float | None = None
        self.hardware_status = "Not connected — simulation mode"
        self.feed_source: str | None = None
        self.sensor_snapshot: dict = {}
        self.sensor_consistency = "NOT_CHECKED"
        self._restore_recent_records()

    def _restore_recent_records(self):
        """Restore the last saved event and last STOP after a dashboard restart."""
        for path in sorted(self.recorder.events_dir.glob("*.json"), reverse=True):
            if not EVENT_NAME.fullmatch(path.name):
                continue
            try:
                record = json.loads(path.read_text(encoding="utf-8"))
                kind = record["payload"]["event_type"]
                if self.recorder.last_event_path is None:
                    self.recorder.last_event = record
                    self.recorder.last_event_path = path
                if kind == "STOP":
                    self.latest_stop_event = record
                    self.latest_stop_path = path
                    break
            except (ValueError, KeyError, TypeError, OSError):
                continue

    def _remember(self, event: dict | None):
        if event and event["payload"]["event_type"] == "STOP":
            self.latest_stop_event = event
            self.latest_stop_path = self.recorder.last_event_path

    def start_worker(self):
        if self._worker is None:
            self._worker = threading.Thread(target=self._run, name="drivetrace-simulator", daemon=True)
            self._worker.start()

    def shutdown(self):
        self._stop.set()
        if self._worker is not None:
            self._worker.join(timeout=1.0)

    def _run(self):
        while not self._stop.wait(0.25):
            with self.lock:
                if not self.external_input:
                    self._tick()

    def _tick(self):
        if self.demo_started is not None:
            elapsed = time.monotonic() - self.demo_started
            if elapsed < 7:
                self.speed = min(70.0, elapsed * 10.0)
            elif elapsed < 9:
                self.speed = 70.0
            else:
                self.speed = max(0.0, 70 - (elapsed - 9) * 14)
            if elapsed >= 14:
                self.speed = 0.0
                self.running = False
                self.demo_started = None
                self.message = "Auto-drive complete. Pre-stop evidence saved."
        if self.running or self.recorder.has_moved_since_stop:
            event = self.recorder.add_sample(self.speed, time.time())
            self._remember(event)
            if event is not None:
                self.message = "Stop event preserved and signed with the local demo key."

    def action(self, name: str, value=None) -> dict:
        with self.lock:
            # Never mix simulated movement with real or replayed telemetry.
            if self.external_input and name in {"start", "accelerate", "brake", "stop", "demo", "reset"}:
                raise ValueError("Simulation controls are disabled in hardware/replay mode.")
            if name == "start":
                self.running = True
                self.demo_started = None
                self.message = "Simulation started. Use Accelerate to begin driving."
            elif name == "accelerate":
                self.running = True
                self.demo_started = None
                self.speed = min(160.0, self.speed + 10.0)
                self._remember(self.recorder.add_sample(self.speed))
                self.message = f"Accelerating — {self.speed:.0f} km/h."
            elif name == "brake":
                self.demo_started = None
                self.speed = max(0.0, self.speed - 10.0)
                self._remember(self.recorder.add_sample(self.speed))
                if self.speed == 0.0:
                    self.running = False
                self.message = f"Braking — {self.speed:.0f} km/h."
            elif name == "stop":
                self.demo_started = None
                self.speed = 0.0
                self.running = False
                self._remember(self.recorder.add_sample(0.0))
                self.message = "Vehicle stopped. A stop event was saved if movement occurred."
            elif name == "demo":
                self.speed = 0.0
                self.recorder.clear_live_history()
                self.running = True
                self.demo_started = time.monotonic()
                self.message = "Auto-drive running: accelerate, cruise, brake, stop."
            elif name == "reset":
                self.speed = 0.0
                self.running = False
                self.demo_started = None
                self.recorder.clear_live_history()
                self.message = "Live readings cleared. Previously saved events were kept."
            elif name == "tamper":
                self.recorder.log_tamper()
                self.message = "Simulated enclosure opening: tamper event recorded."
            elif name == "set_limit":
                if isinstance(value, bool) or not isinstance(value, (int, float)):
                    raise ValueError("Provide a numeric limit from 1 to 200 km/h.")
                if not math.isfinite(value) or not 1 <= value <= 200:
                    raise ValueError("Speed limit must be between 1 and 200 km/h.")
                self.recorder.speed_limit_kmh = float(value)
                self.message = f"Speed limit updated to {value:g} km/h."
            elif name == "verify_latest":
                valid = bool(self.recorder.last_event_path and
                             self.recorder.verify_file(self.recorder.last_event_path))
                return {"ok": True, "verified": valid,
                        "message": "Latest record integrity: PASS." if valid else
                                   "No valid latest record (or the file/key changed)."}
            elif name == "verify":
                path = self._safe_event(value)
                if not path.is_file():
                    raise FileNotFoundError("Event file not found")
                valid = self.recorder.verify_file(path)
                return {"ok": True, "verified": valid,
                        "message": "Record integrity: PASS." if valid else "Record integrity: FAIL."}
            else:
                raise ValueError("Unknown simulator action.")
            return {"ok": True, "message": self.message}

    def _safe_event(self, name):
        if not isinstance(name, str) or not EVENT_NAME.fullmatch(name):
            raise ValueError("Invalid event filename")
        return self.recorder.events_dir / name

    def event_path(self, name):
        with self.lock:
            path = self._safe_event(name)
            if not path.is_file():
                raise FileNotFoundError("Event file not found")
            return path

    def events(self):
        with self.lock:
            files = sorted(self.recorder.events_dir.glob("*.json"), reverse=True)[:24]
            result = []
            for path in files:
                if not EVENT_NAME.fullmatch(path.name):
                    continue
                try:
                    record = json.loads(path.read_text(encoding="utf-8"))
                    payload = record["payload"]
                    result.append({
                        "filename": path.name,
                        "type": payload.get("event_type", "UNKNOWN"),
                        "recorded_at_utc": payload.get("recorded_at_utc", ""),
                        "speed_before_stop_kmh": payload.get("speed_before_stop_kmh"),
                        "maximum_recent_speed_kmh": payload.get("maximum_recent_speed_kmh"),
                        "overspeed_detected": payload.get("overspeed_detected", False),
                        "integrity": "PASS" if self.recorder.verify_file(path) else "FAIL",
                    })
                except (OSError, ValueError, KeyError, TypeError):
                    result.append({"filename": path.name, "type": "INVALID", "integrity": "FAIL"})
            return result

    def ingest_hardware(self, message: dict, source: str = "HARDWARE") -> dict:
        """Validate USB sensor telemetry or a clearly labelled offline replay."""
        if source not in {"HARDWARE", "REPLAY"}:
            raise ValueError("Unrecognized feed source")
        if not isinstance(message, dict) or message.get("type") != "telemetry":
            raise ValueError("Packet type must be telemetry")

        def number(name, low, high, required=False):
            value = message.get(name)
            if value is None and not required:
                return None
            if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
                raise ValueError(f"{name} must be a finite number")
            if not low <= value <= high:
                raise ValueError(f"{name} must be between {low} and {high}")
            return float(value)

        speed = number("speed_kmh", 0, 250, required=True)
        gps_valid = message.get("gps_valid", False)
        imu_valid = message.get("imu_valid", False)
        if not isinstance(gps_valid, bool) or not isinstance(imu_valid, bool):
            raise ValueError("gps_valid and imu_valid must be booleans")
        gps_speed = number("gps_speed_kmh", 0, 250) if gps_valid else None
        if gps_valid and gps_speed is None:
            raise ValueError("gps_speed_kmh is required when gps_valid is true")
        accel = {name: number(name, -16, 16) for name in ("imu_ax_g", "imu_ay_g", "imu_az_g")}
        pulses = message.get("wheel_pulses")
        if pulses is not None and (isinstance(pulses, bool) or not isinstance(pulses, int) or pulses < 0):
            raise ValueError("wheel_pulses must be a nonnegative integer")
        # A GPS-only comparison is a *diagnostic*, not validated sensor fusion.
        if gps_speed is None:
            consistency = "GPS_UNAVAILABLE"
        elif abs(gps_speed - speed) <= max(8.0, 0.20 * max(speed, gps_speed)):
            consistency = "GPS_AGREES"
        else:
            consistency = "GPS_DISAGREES"
        with self.lock:
            self.hardware_connected = source == "HARDWARE"
            self.hardware_last_seen = time.time()
            self.feed_source = source
            self.hardware_status = ("ESP32 USB packets received" if source == "HARDWARE"
                                    else "Offline replay running; no hardware attached")
            self.sensor_snapshot = {
                "wheel_pulses": pulses,
                "imu_valid": imu_valid,
                "imu_ax_g": accel["imu_ax_g"] if imu_valid else None,
                "imu_ay_g": accel["imu_ay_g"] if imu_valid else None,
                "imu_az_g": accel["imu_az_g"] if imu_valid else None,
                "gps_valid": gps_valid,
                "gps_speed_kmh": gps_speed,
            }
            self.sensor_consistency = consistency
            self.speed = speed
            self.running = speed > self.recorder.stop_threshold_kmh
            event = self.recorder.add_sample(speed, source=source)
            self._remember(event)
            self.message = ("Stop event recorded from " + source.lower() + " data."
                            if event else "Receiving " + source.lower() + " telemetry.")
            return {"ok": True, "speed_kmh": round(speed, 2), "source": source,
                    "gps_consistency": consistency}

    def state(self):
        with self.lock:
            history = list(self.recorder.history)
            peak = max((item["speed_kmh"] for item in history), default=0.0)
            latest = self.latest_stop_event["payload"] if self.latest_stop_event else None
            fresh = bool(self.hardware_last_seen and time.time() - self.hardware_last_seen < 3)
            if fresh and self.feed_source in {"HARDWARE", "REPLAY"}:
                mode = self.feed_source
            elif self.external_input:
                mode = "WAITING_FOR_HARDWARE" if self.feed_source != "REPLAY" else "REPLAY_COMPLETE"
            else:
                mode = "SIMULATION"
            return {
                "mode": mode,
                "hardware_connected": mode == "HARDWARE",
                "feed_active": fresh and self.feed_source in {"HARDWARE", "REPLAY"},
                "feed_source": self.feed_source,
                "sensor_snapshot": self.sensor_snapshot,
                "gps_consistency": self.sensor_consistency,
                "feed_age_seconds": round(time.time() - self.hardware_last_seen, 1) if self.hardware_last_seen else None,
                "hardware_status": self.hardware_status,
                "hardware_last_seen": self.hardware_last_seen,
                "running": self.running,
                "demo_running": self.demo_started is not None,
                "speed_kmh": round(self.speed, 1),
                "limit_kmh": self.recorder.speed_limit_kmh,
                "peak_recent_kmh": round(peak, 1),
                "overspeed_now": self.speed > self.recorder.speed_limit_kmh,
                "overspeed_recent": peak > self.recorder.speed_limit_kmh,
                "history_seconds": self.recorder.history_seconds,
                "history": [{"t": p["t"], "speed": p["speed_kmh"]} for p in history],
                "latest_event": latest,
                "latest_integrity": bool(self.latest_stop_path and
                                         self.recorder.verify_file(self.latest_stop_path)),
                "message": self.message,
                "uptime_seconds": round(time.time() - self.started_at),
                "version": "1.0.0",
            }


class DriveTraceHandler(BaseHTTPRequestHandler):
    engine: SimulationEngine
    server_version = "DriveTrace/1.0"

    def _send(self, status: int, content: bytes, content_type: str, extra: dict | None = None):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(content)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("X-Frame-Options", "DENY")
        self.send_header("Referrer-Policy", "no-referrer")
        if extra:
            for key, val in extra.items():
                self.send_header(key, val)
        self.end_headers()
        self.wfile.write(content)

    def _json(self, status: int, obj: dict | list):
        self._send(status, json.dumps(obj, ensure_ascii=False).encode("utf-8"),
                   "application/json; charset=utf-8")

    def do_GET(self):
        path = urlsplit(self.path).path
        if path == "/api/state":
            return self._json(200, self.engine.state())
        if path == "/api/events":
            return self._json(200, self.engine.events())
        if path == "/api/health":
            return self._json(200, {"ok": True, "mode": self.engine.state()["mode"]})
        if path.startswith("/api/event/"):
            try:
                name = unquote(path.removeprefix("/api/event/"))
                file = self.engine.event_path(name)
                return self._send(200, file.read_bytes(), "application/json; charset=utf-8",
                                  {"Content-Disposition": f'attachment; filename="{file.name}"'})
            except (ValueError, FileNotFoundError, OSError):
                return self._json(404, {"ok": False, "error": "Event not found"})
        if path in ALLOWED_ASSETS:
            name = ALLOWED_ASSETS[path]
            source = STATIC / name
            if source.is_file():
                typ = mimetypes.guess_type(source.name)[0] or "application/octet-stream"
                if typ.startswith("text/") or typ in {"image/svg+xml", "application/javascript"}:
                    typ += "; charset=utf-8"
                return self._send(200, source.read_bytes(), typ)
        return self._json(404, {"ok": False, "error": "Not found"})

    def do_POST(self):
        route = urlsplit(self.path).path
        if route == "/api/telemetry":
            # USB SerialBridge feeds the recorder in-process. Unauthenticated
            # HTTP posts must never be accepted as trusted vehicle telemetry.
            return self._json(403, {"ok": False, "error": "HTTP telemetry disabled; use serial input"})
        if route != "/api/action":
            return self._json(404, {"ok": False, "error": "Not found"})
        # The server binds to loopback only; require JSON and an explicit custom header.
        if self.headers.get("X-DriveTrace-Client") != "dashboard":
            return self._json(403, {"ok": False, "error": "Dashboard header required"})
        if self.headers.get("Content-Type", "").split(";")[0].strip() != "application/json":
            return self._json(415, {"ok": False, "error": "JSON content type required"})
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if length < 1 or length > 4096:
                raise ValueError("Invalid request size")
            body = json.loads(self.rfile.read(length))
            if not isinstance(body, dict):
                raise ValueError("Expected a JSON object")
            self._json(200, self.engine.action(body.get("action"), body.get("value")))
        except FileNotFoundError as exc:
            self._json(404, {"ok": False, "error": str(exc)})
        except (ValueError, TypeError, json.JSONDecodeError) as exc:
            self._json(400, {"ok": False, "error": str(exc)})

    def log_message(self, format: str, *args):
        # Avoid noisy logs for the frequent polling requests.
        if self.path not in {"/api/state", "/api/events"}:
            super().log_message(format, *args)


def create_server(host="127.0.0.1", port=8765, data_dir=None):
    engine = SimulationEngine(data_dir)
    handler = type("ProjectHandler", (DriveTraceHandler,), {"engine": engine})
    server = ThreadingHTTPServer((host, port), handler)
    server.daemon_threads = True
    server.engine = engine
    return server


class SerialBridge:
    """Optional USB serial reader. Install pyserial only when an ESP32 is attached."""
    def __init__(self, engine: SimulationEngine, port: str, baud: int = 115200):
        self.engine, self.port, self.baud = engine, port, baud
        self.stop_event = threading.Event()
        self.thread = threading.Thread(target=self._run, daemon=True, name="drivetrace-serial")
        self.error = None

    def start(self):
        self.thread.start()

    def stop(self):
        self.stop_event.set()
        self.thread.join(timeout=2)

    def _run(self):
        try:
            import serial  # type: ignore
        except ImportError:
            self.error = "pyserial is missing. Run: py -m pip install -r requirements-hardware.txt"
            self.engine.hardware_status = self.error
            return
        try:
            with serial.Serial(self.port, self.baud, timeout=1) as device:
                self.engine.hardware_status = f"Listening on {self.port}"
                while not self.stop_event.is_set():
                    raw = device.readline()
                    if not raw:
                        continue
                    try:
                        packet = json.loads(raw.decode("utf-8", errors="strict").strip())
                        self.engine.ingest_hardware(packet)
                    except (UnicodeDecodeError, json.JSONDecodeError, ValueError) as exc:
                        self.error = f"Ignored invalid sensor packet: {exc}"
        except Exception as exc:
            self.error = f"Serial connection failed: {exc}"
            self.engine.hardware_status = self.error


class HardwareReplay:
    """Offline integration demo through the exact telemetry ingestion path."""
    def __init__(self, engine: SimulationEngine, path: Path | None = None, interval: float = 0.2):
        self.engine = engine
        self.path = path or ROOT / "samples" / "hardware_replay.jsonl"
        self.interval = interval
        self.stop_event = threading.Event()
        self.thread = threading.Thread(target=self._run, daemon=True, name="drivetrace-replay")
        self.error = None

    def start(self):
        self.thread.start()

    def stop(self):
        self.stop_event.set()
        self.thread.join(timeout=2)

    def _run(self):
        try:
            with self.path.open(encoding="utf-8") as stream:
                for line in stream:
                    if self.stop_event.is_set():
                        break
                    if not line.strip():
                        continue
                    self.engine.ingest_hardware(json.loads(line), source="REPLAY")
                    if self.stop_event.wait(self.interval):
                        break
        except (OSError, ValueError, json.JSONDecodeError) as exc:
            self.error = str(exc)
            self.engine.hardware_status = "Replay error: " + str(exc)


def main():
    parser = argparse.ArgumentParser(description="DriveTrace dashboard — simulation or ESP32 serial input")
    parser.add_argument("--port", type=int, default=8765, help="Local port (default: 8765)")
    parser.add_argument("--no-browser", action="store_true", help="Do not open browser automatically")
    input_modes = parser.add_mutually_exclusive_group()
    input_modes.add_argument("--serial-port", help="ESP32 serial port, e.g. COM5 (requires pyserial)")
    input_modes.add_argument("--replay-hardware", action="store_true",
                             help="Replay built-in sensor packets without hardware or extra packages")
    parser.add_argument("--baud", type=int, default=115200, help="Serial baud rate")
    args = parser.parse_args()
    try:
        server = create_server(port=args.port)
    except OSError as exc:
        raise SystemExit(f"Cannot start DriveTrace on port {args.port}: {exc}") from exc
    url = f"http://127.0.0.1:{args.port}"
    server.engine.external_input = bool(args.serial_port or args.replay_hardware)
    server.engine.start_worker()
    bridge = SerialBridge(server.engine, args.serial_port, args.baud) if args.serial_port else None
    if bridge:
        bridge.start()
    if args.replay_hardware:
        replay = HardwareReplay(server.engine)
        replay.start()
    else:
        replay = None
    mode_line = (f"USB hardware input: {args.serial_port}" if bridge else
                 "OFFLINE hardware packet replay (NOT a real sensor)" if replay else
                 "SIMULATED data only")
    print(f"\nDriveTrace — {mode_line}\nOpen: {url}\nPress Ctrl+C to quit.\n")
    if not args.no_browser:
        threading.Timer(0.7, lambda: webbrowser.open(url)).start()
    try:
        server.serve_forever(poll_interval=0.2)
    except KeyboardInterrupt:
        pass
    finally:
        server.shutdown()
        server.server_close()
        server.engine.shutdown()
        if bridge:
            bridge.stop()
        if replay:
            replay.stop()


if __name__ == "__main__":
    main()
