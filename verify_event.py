"""Verify a saved DriveTrace event file. Usage: python verify_event.py path/to/event.json"""
from pathlib import Path
import sys
from drivetrace_core import DriveTraceRecorder

if len(sys.argv) != 2:
    print("Usage: python verify_event.py data/events/your_event.json")
    raise SystemExit(2)

base = Path(__file__).resolve().parent
recorder = DriveTraceRecorder(data_dir=base / "data")
valid = recorder.verify_file(sys.argv[1])
print("PASS: record integrity verified" if valid else "FAIL: record modified or key mismatch")
raise SystemExit(0 if valid else 1)
