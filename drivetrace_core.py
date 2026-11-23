"""DriveTrace phase 1: hardware-independent event recorder and integrity checks.

Uses only the Python standard library. All speed inputs in this phase are simulated.
"""

from __future__ import annotations

from collections import deque
from datetime import datetime, timezone
import hashlib
import hmac
import json
from pathlib import Path
import secrets
import time
import uuid


def utc_iso(timestamp: float) -> str:
    return datetime.fromtimestamp(timestamp, tz=timezone.utc).isoformat(timespec="milliseconds")


def canonical_bytes(payload: dict) -> bytes:
    return json.dumps(payload, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode("utf-8")


class DriveTraceRecorder:
    """Rolling history + pre-stop snapshots + demo HMAC verification.

    In production, HMAC keys require secure storage; a file-based key is for
    local prototyping only and does not protect against machine compromise.
    """

    def __init__(self, data_dir: str | Path = "data", speed_limit_kmh: float = 50.0,
                 history_seconds: float = 30.0, stop_threshold_kmh: float = 0.5,
                 secret_key: bytes | None = None):
        self.data_dir = Path(data_dir)
        self.events_dir = self.data_dir / "events"
        self.events_dir.mkdir(parents=True, exist_ok=True)
        self.speed_limit_kmh = float(speed_limit_kmh)
        self.history_seconds = float(history_seconds)
        self.stop_threshold_kmh = float(stop_threshold_kmh)
        self.history: deque[dict] = deque()
        self.current_speed_kmh = 0.0
        self.last_nonzero_speed_kmh = 0.0
        self.has_moved_since_stop = False
        self.last_event: dict | None = None
        self.last_event_path: Path | None = None
        self.key = secret_key if secret_key is not None else self._load_or_create_key()

    def _load_or_create_key(self) -> bytes:
        key_path = self.data_dir / "secret.key"
        if key_path.exists():
            key = key_path.read_bytes()
            if len(key) < 32:
                raise ValueError("Invalid local secret.key: expected at least 32 bytes")
            return key
        key = secrets.token_bytes(32)
        # 'x' refuses to overwrite an existing key; important for verification.
        try:
            with key_path.open("xb") as handle:
                handle.write(key)
        except FileExistsError:
            return key_path.read_bytes()
        try:
            key_path.chmod(0o600)
        except OSError:
            pass
        return key

    def add_sample(self, speed_kmh: float, timestamp: float | None = None, source: str = "SIMULATED") -> dict | None:
        timestamp = time.time() if timestamp is None else float(timestamp)
        speed_kmh = float(speed_kmh)
        if speed_kmh < 0:
            raise ValueError("Speed must be non-negative")
        self.current_speed_kmh = speed_kmh
        self.history.append({"timestamp_utc": utc_iso(timestamp), "t": round(timestamp, 3),
                             "speed_kmh": round(speed_kmh, 2), "source": source})
        cutoff = timestamp - self.history_seconds
        while self.history and self.history[0]["t"] < cutoff:
            self.history.popleft()

        if speed_kmh > self.stop_threshold_kmh:
            self.has_moved_since_stop = True
            self.last_nonzero_speed_kmh = speed_kmh
            return None

        if self.has_moved_since_stop:
            self.has_moved_since_stop = False
            event = self._save_stop(timestamp)
            self.last_nonzero_speed_kmh = 0.0
            return event
        return None

    def _save_stop(self, timestamp: float) -> dict:
        samples = list(self.history)
        max_speed = max((item["speed_kmh"] for item in samples), default=0.0)
        payload = {
            "event_type": "STOP",
            "recorded_at_utc": utc_iso(timestamp),
            "source": samples[-1].get("source", "SIMULATED") if samples else "SIMULATED",
            "speed_before_stop_kmh": round(self.last_nonzero_speed_kmh, 2),
            "maximum_recent_speed_kmh": round(max_speed, 2),
            "speed_limit_kmh": round(self.speed_limit_kmh, 2),
            "overspeed_detected": max_speed > self.speed_limit_kmh,
            "history_seconds": self.history_seconds,
            "samples": samples,
        }
        return self._persist(payload, timestamp)

    def log_tamper(self, timestamp: float | None = None) -> dict:
        timestamp = time.time() if timestamp is None else float(timestamp)
        payload = {
            "event_type": "TAMPER",
            "recorded_at_utc": utc_iso(timestamp),
            "source": "SIMULATED_SWITCH",
            "description": "Enclosure opening simulated in software",
        }
        return self._persist(payload, timestamp)

    def _persist(self, payload: dict, timestamp: float) -> dict:
        mac = hmac.new(self.key, canonical_bytes(payload), hashlib.sha256).hexdigest()
        record = {"payload": payload, "integrity": {"algorithm": "HMAC-SHA256", "value": mac}}
        name = datetime.fromtimestamp(timestamp, tz=timezone.utc).strftime("%Y%m%dT%H%M%S_%fZ")
        destination = self.events_dir / f"{name}_{uuid.uuid4().hex[:8]}.json"
        temporary = destination.with_suffix(".tmp")
        temporary.write_text(json.dumps(record, indent=2, ensure_ascii=False), encoding="utf-8")
        temporary.replace(destination)
        self.last_event = record
        self.last_event_path = destination
        return record

    def verify_file(self, path: str | Path) -> bool:
        try:
            record = json.loads(Path(path).read_text(encoding="utf-8"))
            if record["integrity"]["algorithm"] != "HMAC-SHA256":
                return False
            expected = hmac.new(self.key, canonical_bytes(record["payload"]), hashlib.sha256).hexdigest()
            return hmac.compare_digest(expected, record["integrity"]["value"])
        except (OSError, KeyError, ValueError, TypeError, json.JSONDecodeError):
            return False

    def clear_live_history(self) -> None:
        """Clear live simulator state, without deleting saved evidence."""
        self.history.clear()
        self.current_speed_kmh = 0.0
        self.last_nonzero_speed_kmh = 0.0
        self.has_moved_since_stop = False
