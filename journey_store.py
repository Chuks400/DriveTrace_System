"""DriveTrace whole-ride CSV logger and offline microSD import.

This PC logger is a demonstration implementation. Local HMAC requires secure key
storage for any stronger integrity claim. Imported microSD data is UNVERIFIED.
"""
from __future__ import annotations

import csv
from datetime import datetime, timezone
import hashlib
import hmac
import io
import json
import math
from pathlib import Path
import re
import time
import uuid

from drivetrace_core import canonical_bytes, utc_iso

CSV_COLUMNS = ("timestamp_utc", "speed_kmh", "source", "gps_speed_kmh", "wheel_pulses",
               "imu_ax_g", "imu_ay_g", "imu_az_g")
ID_PATTERN = re.compile(r"^R[0-9]{8}T[0-9]{6}_[0-9a-f]{8}$")
MAX_CSV_BYTES = 8 * 1024 * 1024
MAX_ROWS = 200000


def parse_time(raw: str) -> float:
    try:
        result = datetime.fromisoformat(raw.strip().replace("Z", "+00:00"))
        if result.tzinfo is None:
            raise ValueError("timestamp must include timezone")
        value = result.timestamp()
        if not math.isfinite(value) or not 946684800 <= value <= 4102444800:
            raise ValueError("timestamp out of supported range")
        return value
    except (OverflowError, TypeError) as exc:
        raise ValueError("Invalid timestamp") from exc


def _numeric(raw, label, lo=0.0, hi=250.0, optional=False):
    if raw is None or raw == "":
        if optional:
            return None
        raise ValueError(f"Missing {label}")
    try:
        value = float(raw)
    except (ValueError, TypeError) as exc:
        raise ValueError(f"Invalid {label}") from exc
    if not math.isfinite(value) or not lo <= value <= hi:
        raise ValueError(f"{label} out of range")
    return value


def sample_to_row(timestamp: float, speed: float, source: str, sensor=None) -> dict:
    sensor = sensor or {}
    return {
        "timestamp_utc": utc_iso(timestamp),
        "speed_kmh": f"{speed:.2f}",
        "source": source,
        "gps_speed_kmh": "" if sensor.get("gps_speed_kmh") is None else f"{sensor['gps_speed_kmh']:.2f}",
        "wheel_pulses": "" if sensor.get("wheel_pulses") is None else str(sensor["wheel_pulses"]),
        "imu_ax_g": "" if sensor.get("imu_ax_g") is None else str(sensor["imu_ax_g"]),
        "imu_ay_g": "" if sensor.get("imu_ay_g") is None else str(sensor["imu_ay_g"]),
        "imu_az_g": "" if sensor.get("imu_az_g") is None else str(sensor["imu_az_g"]),
    }


def read_csv(source: str | Path | io.TextIOBase, max_rows: int = MAX_ROWS) -> list[dict]:
    if isinstance(source, Path):
        handle = source.open("r", newline="", encoding="utf-8-sig")
        close = True
    elif isinstance(source, str):
        handle = io.StringIO(source)
        close = True
    else:
        handle = source
        close = False
    try:
        reader = csv.DictReader(handle)
        if not reader.fieldnames or not {"timestamp_utc", "speed_kmh"}.issubset(reader.fieldnames):
            raise ValueError("CSV needs timestamp_utc and speed_kmh columns")
        entries = []
        previous = None
        for i, row in enumerate(reader):
            if i >= max_rows:
                raise ValueError("CSV has too many rows")
            if None in row:
                raise ValueError(f"Unexpected extra CSV fields on row {i+2}")
            ts = parse_time(row["timestamp_utc"])
            if previous is not None and ts < previous:
                raise ValueError("CSV timestamps must be ordered")
            previous = ts
            speed = _numeric(row["speed_kmh"], "speed_kmh")
            gps = _numeric(row.get("gps_speed_kmh"), "gps_speed_kmh", optional=True)
            pulse = _numeric(row.get("wheel_pulses"), "wheel_pulses", 0, 1e12, optional=True)
            imu = [_numeric(row.get(name), name, -16, 16, optional=True)
                   for name in ("imu_ax_g", "imu_ay_g", "imu_az_g")]
            entries.append({"timestamp_utc": utc_iso(ts), "t": ts, "speed_kmh": speed,
                            "gps_speed_kmh": gps, "wheel_pulses": pulse,
                            "imu_ax_g": imu[0], "imu_ay_g": imu[1], "imu_az_g": imu[2]})
        if not entries:
            raise ValueError("CSV contains no speed samples")
        return entries
    finally:
        if close:
            handle.close()


def compute_summary(entries: list[dict], limit: float) -> dict:
    start, end = entries[0]["t"], entries[-1]["t"]
    distance_km = 0.0
    overspeed = 0
    peak = 0.0
    for i, row in enumerate(entries):
        speed = row["speed_kmh"]
        peak = max(peak, speed)
        overspeed += speed > limit
        if i:
            prev = entries[i-1]
            dt = max(0, min(row["t"] - prev["t"], 60))
            distance_km += (prev["speed_kmh"] + speed) / 2 * (dt / 3600)
    return {"start_utc": utc_iso(start), "end_utc": utc_iso(end),
            "duration_seconds": round(end - start, 1),
            "distance_km_estimate": round(distance_km, 3),
            "peak_kmh": round(peak, 2), "overspeed_sample_count": int(overspeed),
            "limit_kmh": round(limit, 2), "samples": len(entries)}


class JourneyStore:
    """1 CSV per ride, durable after stopping, plus safe import and JSON summaries."""
    def __init__(self, root: Path | str, local_hmac_key: bytes):
        self.root = Path(root)
        self.root.mkdir(parents=True, exist_ok=True)
        self.key = local_hmac_key
        self.active_id = None
        self.active_file = None
        self.active_writer = None
        self.active_entries = []
        self.active_limit = 50.0

    def _new_id(self) -> str:
        return "R" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S") + "_" + uuid.uuid4().hex[:8]

    def _paths(self, name):
        if not isinstance(name, str) or not ID_PATTERN.fullmatch(name):
            raise ValueError("Invalid journey ID")
        return self.root / f"{name}.csv", self.root / f"{name}.json"

    def record(self, speed: float, timestamp: float | None = None, source="SIMULATED", sensor=None, limit=50.0):
        timestamp = time.time() if timestamp is None else timestamp
        if not math.isfinite(speed) or not 0 <= speed <= 250:
            raise ValueError("Invalid speed")
        if self.active_id is None:
            if speed <= 0.5:
                return None
            self.active_id = self._new_id()
            csv_path, _ = self._paths(self.active_id)
            self.active_file = csv_path.open("x", newline="", encoding="utf-8")
            self.active_writer = csv.DictWriter(self.active_file, fieldnames=CSV_COLUMNS)
            self.active_writer.writeheader()
            self.active_limit = float(limit)
            self.active_entries = []
        row = sample_to_row(timestamp, speed, source, sensor)
        self.active_writer.writerow(row)
        self.active_file.flush()
        self.active_entries.append({"t": timestamp, "speed_kmh": speed})
        if speed <= 0.5:
            return self.finish()
        return None

    def finish(self):
        if self.active_id is None:
            return None
        name = self.active_id
        self.active_file.flush()
        self.active_file.close()
        csv_path, json_path = self._paths(name)
        entries = read_csv(csv_path)
        summary = compute_summary(entries, self.active_limit)
        summary.update({"id": name, "origin": "PC_LOGGER", "status": "COMPLETE",
                        "integrity": "LOCAL_HMAC", "source": "Local simulation / USB / replay"})
        summary["csv_sha256"] = hashlib.sha256(csv_path.read_bytes()).hexdigest()
        summary["signature"] = hmac.new(self.key, canonical_bytes(summary), hashlib.sha256).hexdigest()
        json_path.write_text(json.dumps(summary, indent=2), encoding="utf-8")
        self.active_id = self.active_file = self.active_writer = None
        self.active_entries = []
        return summary

    def import_csv(self, raw: bytes, limit: float = 50.0):
        if len(raw) > MAX_CSV_BYTES:
            raise ValueError("CSV exceeds 8 MB limit")
        try:
            content = raw.decode("utf-8-sig")
        except UnicodeDecodeError as exc:
            raise ValueError("CSV must be UTF-8") from exc
        entries = read_csv(content)
        name = self._new_id()
        csv_path, json_path = self._paths(name)
        summary = compute_summary(entries, limit)
        summary.update({"id": name, "origin": "MICROSD_IMPORT", "status": "COMPLETE",
                        "integrity": "UNVERIFIED_IMPORT", "source": "External CSV; authenticity not established"})
        # Re-serialize validated fields; ignore arbitrary/untrusted input columns.
        with csv_path.open("x", newline="", encoding="utf-8") as target:
            writer = csv.DictWriter(target, fieldnames=CSV_COLUMNS)
            writer.writeheader()
            for item in entries:
                sensor = {k: item[k] for k in CSV_COLUMNS[3:]}
                writer.writerow(sample_to_row(item["t"], item["speed_kmh"], "MICROSD_IMPORT", sensor))
        json_path.write_text(json.dumps(summary, indent=2), encoding="utf-8")
        return summary

    def _load(self, name):
        csv_path, json_path = self._paths(name)
        if not csv_path.is_file() or not json_path.is_file():
            raise FileNotFoundError("Journey not found")
        meta = json.loads(json_path.read_text(encoding="utf-8"))
        if not isinstance(meta, dict) or meta.get("id") != name:
            raise ValueError("Corrupt journey summary")
        if meta.get("integrity") == "LOCAL_HMAC":
            claimed = meta.pop("signature", None)
            trusted = claimed is not None and hmac.compare_digest(
                hmac.new(self.key, canonical_bytes(meta), hashlib.sha256).hexdigest(), claimed)
            trusted = trusted and hmac.compare_digest(hashlib.sha256(csv_path.read_bytes()).hexdigest(),
                                                      str(meta.get("csv_sha256", "")))
            meta["signature"] = claimed
            meta["verification"] = "VERIFIED_LOCAL" if trusted else "CHANGED_OR_KEY_MISSING"
        else:
            meta["verification"] = "UNVERIFIED_IMPORT"
        return meta

    def list(self):
        summaries = []
        for path in sorted(self.root.glob("R*.json"), reverse=True)[:100]:
            try:
                summaries.append(self._load(path.stem))
            except (ValueError, FileNotFoundError, OSError, KeyError):
                continue
        if self.active_id is not None:
            active = compute_summary(self.active_entries, self.active_limit)
            active.update({"id": self.active_id, "status": "RECORDING", "origin": "PC_LOGGER",
                           "verification": "RECORDING", "source": "Live local"})
            summaries.insert(0, active)
        return summaries

    def detail(self, name):
        if name == self.active_id:
            samples = list(self.active_entries)
            meta = next(x for x in self.list() if x["id"] == name)
        else:
            meta = self._load(name)
            csv_path, _ = self._paths(name)
            samples = read_csv(csv_path)
        step = max(1, len(samples) // 350)
        chart = [{"t": round(sample["t"] - samples[0]["t"], 1),
                  "speed": round(sample["speed_kmh"], 2)} for sample in samples[::step]]
        if chart[-1]["t"] != round(samples[-1]["t"] - samples[0]["t"], 1):
            chart.append({"t": round(samples[-1]["t"] - samples[0]["t"], 1),
                          "speed": round(samples[-1]["speed_kmh"], 2)})
        return {"summary": meta, "samples": chart}

    def csv_path(self, name):
        if name == self.active_id and self.active_file:
            self.active_file.flush()
        csv_path, _ = self._paths(name)
        if not csv_path.is_file():
            raise FileNotFoundError("Journey not found")
        return csv_path
