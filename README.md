# DriveTrace — Embedded Speed Evidence System

**Phase 3: Dashboard + simulation + offline telemetry replay + ESP32-S3 firmware starter. No hardware required for the current test.**

DriveTrace is a multi-sensor, tamper-evident speed history and pre-stop event recorder. The final ESP32-S3 device will use wheel pulses, IMU movement checks, GPS verification, and a TFT display. The present milestone preserves the tested simulation and adds a separate offline replay path for testing the hardware packet protocol. Real sensor accuracy remains untested.

## Quick start on Windows

1. Install [Python 3.10+](https://www.python.org/downloads/) if needed; enable **Add Python to PATH**.
2. Extract `DriveTrace_System_Phase3.zip` into `E:\Downloads\all embedded\FINAL PROJECT`. The ZIP creates its own `DriveTrace_System_Phase3` folder, so your old folder is not overwritten. Do not run blocked `.cmd` scripts; use PowerShell as shown below.
3. Open PowerShell **in** the `DriveTrace_System_Phase3` folder.
4. Run:

   ```powershell
   py main.py
   ```

5. A browser should open at **http://127.0.0.1:8765**. Keep the PowerShell terminal open while using the dashboard. Press Ctrl+C to stop it.

No `pip install`, cloud account, Node, database or internet access is required for simulation or replay. Stop the old Phase 2 server first to free port 8765.

## First test — simulation

- Press **Run automatic demonstration**. In about 14 seconds, the simulated vehicle accelerates to **70 km/h**, cruises, brakes and stops. With the default 50 km/h threshold, a stop event will be stored and marked overspeed.
- Or press **Start → Accelerate +10** multiple times → **Brake −10** → **Stop vehicle**.
- Observe the **Current speed**, **Speed before stop**, **Peak speed**, **Rolling 30-second graph**, and **Latest stop evidence**.
- Press **Verify latest event integrity** to check the saved HMAC-SHA256.
- In **Recorded events**, use **↓ JSON** to export a record.
- Press **Simulate tamper** to generate a simulated enclosure-opening event. This is not a physical sensor.
- Press **Reset live readings** to clear only the live samples; saved events remain.

### Simulate a tamper event

On Windows PowerShell (while dashboard is running):

```powershell
Invoke-RestMethod -Uri http://127.0.0.1:8765/api/action -Method Post -ContentType application/json -Headers @{'X-DriveTrace-Client'='dashboard'} -Body '{"action":"tamper"}'
```

## Offline hardware-packet replay (new in Phase 3)

With the Phase 2 server stopped, run `py main.py --replay-hardware`. The dashboard will show **OFFLINE REPLAY** and use test packets from `samples/hardware_replay.jsonl`. This runs without an ESP32 and creates a signed stop record with source `REPLAY`. See [docs/PHASE3_TESTING.md](docs/PHASE3_TESTING.md) for the full test sequence.

## Files

| Path | What it does |
|---|---|
| `main.py` | Local HTTP server, simulated vehicle controller and REST API |
| `drivetrace_core.py` | Hardware-independent rolling speed recorder and HMAC integrity |
| `static/index.html` | Responsive dashboard layout |
| `static/style.css` | Light-mode dashboard styling |
| `static/app.js` | Live graph, speed gauge, controls, record table and downloads |
| `tests/` | Automated Python tests |
| `verify_event.py` | Command-line verification of exported JSON |
| `hardware/drivetrace_esp32s3.ino` | First ESP32-S3 Hall + MPU6050 bench firmware |
| `docs/HARDWARE_INTEGRATION.md` | Wiring checks, serial protocol and bench validation |
| `requirements-hardware.txt` | Optional USB serial dependency (pyserial) |
| `samples/hardware_replay.jsonl` | Synthetic Hall, IMU and GPS packet sequence |
| `docs/PHASE3_TESTING.md` | Replay and future bench testing steps |
| `docs/WIRING_PHASE3.md` and `.svg` | Example board wiring and safety checks |
| `HARDWARE_PURCHASE_LIST.md` | Components, quantities, buying order and cautions |
| `DriveTrace_Proposal.pdf` | Final project proposal |
| `data/events/*.json` | Runtime event records (generated on first run; ignored by Git) |
| `data/secret.key` | Local HMAC demo key (generated; **never push to GitHub**) |

## Run automated tests

```powershell
py -m unittest discover -s tests -v
```

To verify an exported file *using this installation's local key*:

```powershell
py verify_event.py "data/events/ACTUAL_EVENT_FILENAME.json"
```

## Safety and scope

- **No hardware is connected.** All readings and case-opening events are simulated; results cannot establish a real vehicle's speed.
- Event files use **HMAC-SHA256** with a locally stored key. This detects modifications when the key is protected. It is not forensic-grade proof if someone can alter both the record and key.
- Server binds to **127.0.0.1** only, not the public network. Do not expose it to the internet.
- For actual speed measurement, calibrate the wheel circumference and verify the Hall sensor and module voltage with ESP32-S3 3.3 V logic.
- The initial USB serial integration scaffold is included. It still requires the exact board pinout to be checked, firmware upload, wheel calibration and physical validation. GPS, SD, RTC, TFT, tamper switch and vehicle power integration remain future work.

## Development milestones

1. ✅ Phase 1: Local dashboard and simulated evidence engine.
2. ◐ Phase 2: ESP32-S3 + Hall firmware and serial bridge written; board testing pending.
3. ◐ Phase 3: GPS UART, IMU raw readings, GPS agreement diagnostic, and replay tests written; physical validation pending.
4. ☐ Phase 4: MicroSD + RTC + storage on ESP32.
5. ☐ Phase 5: TFT display + tamper switch + hardware integrity design.
6. ☐ Phase 6: RC vehicle integration, calibration, test report.

## GitHub publishing (new, dedicated repository)

**Do not push this project to the unrelated Cybte or EPISTYN repositories.** Create a new **private** GitHub repository, e.g. `drivetrace-embedded`, and from this folder run:

```powershell
git init
git add .
git commit -m "Build DriveTrace Phase 3 sensor bridge and diagnostics"
git branch -M main
git remote add origin https://github.com/YOUR_USERNAME/drivetrace-embedded.git
git push -u origin main
```

Replace `YOUR_USERNAME` with your GitHub username. The `.gitignore` excludes local runtime data and keys. Do not upload saved evidence or secret keys.
