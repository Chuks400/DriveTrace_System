# DriveTrace — Phase 4 teacher demonstration

**A complete demonstration of journey logging, pre-stop event evidence, an onboard TFT display preview, and an offline microSD-to-PC workflow.** The software works **without hardware**. The real ESP32-S3 firmware is an uncompiled example and must be bench-tested and calibrated after the teacher supplies components.

## Run today — Windows

1. Stop the earlier DriveTrace instance in PowerShell using **Ctrl+C** (port 8765 must be free).
2. Extract the new ZIP into `E:\Downloads\all embedded\FINAL PROJECT` without overwriting the old Phase 3 folder.
3. Open PowerShell in `DriveTrace_System_Phase4`.
4. Run `py main.py` — **do not double-click any `.cmd` file** (Windows Smart App Control may block it).
5. Visit `http://127.0.0.1:8765` and keep PowerShell open.

No pip packages, internet, database, cloud accounts, or hardware are required for the main demonstration.

## Teacher demo — 3 minutes

1. Show the **Phase 4** dashboard and explain the system distinguishes simulated data from real sensor data.
2. Click **Run automatic demonstration**. Wait for acceleration, 70 km/h peak speed, overspeed at the 50 km/h configurable threshold, braking, and the saved STOP event.
3. Point to **Latest stop evidence** and **Event history**. A saved STOP event has a local demo HMAC-SHA256 integrity check.
4. Scroll to **On-bike TFT screen preview**. The current speed, last pre-stop speed, peak speed, speed limit, and logging status are designed for a small onboard display. This on-PC preview is not a connected screen.
5. Scroll to **Complete journey history**. Select the newly saved `PC` journey. Show the speed graph, duration, estimated distance, peak speed, and **Export CSV**.
6. Click **Load sample ride** to demonstrate importing a *fictional* 2-minute-30-second ride from a microSD-compatible CSV. It will appear as **Unverified import** — deliberately not presented as verified real-world speed evidence.
7. For a separate hardware-data integration demonstration, stop the app and run `py main.py --replay-hardware`. This labels the input **OFFLINE REPLAY** and shows synthetic Hall, IMU, and GPS values in **Sensor feed diagnostics**.

## What is implemented

| Capability | Implementation | Validation |
|---|---|---|
| Live speed dashboard | Browser UI with speed gauge and limits | Python + browser UI smoke tests |
| Pre-stop event recorder | Rolling 30 seconds, peak, timestamp, overspeed | Automated tests |
| Whole-journey recording | PC writes one CSV per ride to `data/journeys/`, even if the ride is long | Automated tests |
| History over time | Select journeys for a full ride chart, duration, approximate distance, speed statistics | Browser UI smoke tests |
| External microSD import | Import UTF-8 CSV in the documented format, validate, display and export | Automated tests |
| 2.4-inch TFT mock-up | Live browser preview designed for ILI9341 display | Browser UI smoke tests |
| Offline sensor replay | Synthetic Hall/IMU/GPS JSON packets fed through the receiver | Automated tests |
| USB hardware listener | Optional `--serial-port COMx` | Not tested with a physical ESP32 |
| Standalone ESP32 TFT + microSD sketch | Example `.ino` code to read sensors and log SD records | **NOT** compiled or physically tested |
| Cryptographic event integrity | Local HMAC-SHA256 on PC event and completed PC-ride summaries | Automated tests; demo only |

### Important scope distinction

**A PC with this package can log a full journey only while receiving simulated or real USB-fed speed samples.** Independent e-bike operation requires the teacher-supplied ESP32-S3, powered TFT, SD module, Hall wheel sensor, RTC, GPS and IMU to be wired, flashed, and tested. The included standalone firmware sketch is a starting implementation for that later physical phase.

The onboard display currently **does not exist as tested physical hardware**. The TFT preview is functional as an on-PC view. The standalone firmware code has not been physically run. Do not claim production readiness or evidentiary accuracy.

## Import from microSD

The dashboard accepts CSV files with headers:

```csv
timestamp_utc,speed_kmh,source,gps_speed_kmh,wheel_pulses,imu_ax_g,imu_ay_g,imu_az_g
2026-11-23T10:30:00+00:00,12.50,HARDWARE,12.20,5,0.02,0.00,0.98
2026-11-23T10:30:01+00:00,13.20,HARDWARE,13.10,6,0.03,0.00,0.98
```

- Required fields: `timestamp_utc` (timezone-aware ISO 8601), `speed_kmh` (non-negative km/h).
- Other columns are optional, but the standalone firmware writes them when available.
- Import limit: **8 MB, 200,000 records**; time must be chronological.
- Imported data is stored in `data/journeys/` and labelled **UNVERIFIED_IMPORT**, not signed as trustworthy sensor data.
- A prebuilt fictional example is available at `samples/teacher_ride_sdcard.csv`. Click **Load sample ride** to demonstrate importing without locating the file manually.
- Local PC-generated records have a demo HMAC. It is not equivalent to device-side cryptographic signing, secure key hardware or a forensic-grade log.

## Physical firmware and wiring

Two separate sketches are provided:

- `hardware/drivetrace_esp32s3.ino`: USB serial Hall + IMU + GPS stream for PC dashboard.
- `hardware/standalone_esp32s3_tft_sd/standalone_esp32s3_tft_sd.ino`: example independent logging to microSD and TFT display, using Hall, MPU6050, GPS, DS3231 RTC, SD, and ILI9341.

Read `docs/PHASE4_HARDWARE_NOTES.md` before wiring. The example pins are provisional and all GPIO must be 3.3 V logic safe. The MPU6050 and DS3231 normally share I²C address **0x68**; the standalone example requires the MPU6050 **AD0 high** to use **0x69**, or otherwise an appropriate address arrangement. **Do not mount the Hall sensor on the rotating wheel**: mount the sensor on the stationary frame and only the magnet on the rotating wheel.

### Build requirements for ESP32 example

Arduino IDE 2.x, Espressif ESP32 core, `Adafruit GFX Library`, `Adafruit ILI9341` for the standalone TFT sketch. `Wire`, `SPI`, and `SD` are from the board core. Firmware was **not compiled on a target ESP32-S3** and should not be treated as tested or ready to mount. RTC must be set to UTC (this sketch will not log if RTC is invalid); wheel circumference must be measured and calibrated, not assumed.

## Tests

```powershell
py -m unittest discover -s tests -v
```

**24 tests** presently cover the original event pipeline, Phase 3 replay, CSV generation, import rejection, signed metadata, and web API. Browser UI smoke tests were also performed with mocked API responses because browser networking to localhost is restricted in the authoring environment. Actual backend API tests were performed against a live local HTTP server.

## Runtime files and backups

- `data/events/`: stop/tamper JSON records.
- `data/journeys/`: complete CSV journeys + JSON summary metadata.
- `data/secret.key`: demo HMAC key, **do not commit or share**.
- Both folders and key are excluded by `.gitignore`.
- The ZIP has no runtime data. Preserve your Phase 3 folder and records separately.

## Source control

Use a new, private `drivetrace-embedded` GitHub repository. Never push these files into unrelated Cybte or EPISTYN repositories. No GitHub push has been performed automatically. See `docs/TEACHER_DEMO.md` for speaking notes and `docs/PHASE4_HARDWARE_NOTES.md` for physical assembly.
