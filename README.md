# DriveTrace — Embedded Speed History and Pre-Stop Event Recorder

**Project topic:** DriveTrace: Multi-Sensor Tamper-Evident Speed History and Pre-Stop Event Recorder

DriveTrace is an ESP32-S3-based embedded-systems project designed to measure vehicle speed, preserve what happened before a vehicle stopped, flag configured speed-limit exceedances, and retain journey records for later review. The current **Phase 4 software demonstrator** runs on a Windows PC without physical hardware. A proposed on-bike display and standalone logger are represented by software and untested firmware examples.

**Current status:** PC dashboard, simulation, stop-event recording, CSV journey history, CSV import/export, sample hardware-packet replay, and browser TFT preview are implemented. **Real ESP32-S3 hardware operation is not yet validated.**

## How the complete system is intended to work

1. **Measure movement:** A Hall-effect sensor stays fixed to the bike's fork/frame. A small magnet turns with the wheel and passes the sensor once per revolution (or as configured). The ESP32-S3 calculates speed using the measured wheel circumference and pulse timing/counts. No sensor wire rotates with the wheel.
2. **Cross-check conditions:** The IMU reports acceleration/movement; outdoor GPS provides an independent speed estimate when it has a valid fix. Disagreement is a diagnostic, not proof of tampering.
3. **Show information on the bike:** The proposed 2.4-inch TFT shows current speed, set speed limit, peak speed, pre-stop information, and recording status. This is currently **a functional browser preview**, not a tested physical TFT.
4. **Preserve stop events:** When a stop occurs, DriveTrace saves the preceding speed, recent peak speed, timestamp, overspeed status, and a short speed-history window.
5. **Record whole journeys:** The intended standalone ESP32-S3 writes timestamped samples to microSD while the bike travels, even when far away from the PC. The current PC prototype logs journeys from simulated or received USB speed samples.
6. **Review later on the PC:** After a ride, a microSD CSV can be imported into the DriveTrace dashboard to display the full speed graph, duration, estimated distance and peak. Imported external files are explicitly marked **unverified**.

The laptop is not required in the **planned standalone design**, but that functionality needs real electronics, firmware compilation, power testing, mounting and calibration before it can be demonstrated on an e-bike. Journey length is limited by practical storage, battery life and reliability—not a fixed minimum number of kilometres.

## Run the current software on Windows

**One consolidated project folder:**

```text
E:\Downloads\all embedded\DriveTrace\DriveTrace_System
```

1. Open that folder in File Explorer.
2. Click the address bar, type `powershell`, and press Enter.
3. Run:

   ```powershell
   py main.py
   ```

4. Open **http://127.0.0.1:8765** in Chrome.
5. Keep PowerShell open while using the dashboard. Stop the application with **Ctrl+C**.

Simulation and sample replay use Python's standard library and do not require an internet connection, database or ESP32. Do **not** use the previously removed `.cmd` launcher: Windows Smart App Control may block downloaded scripts. Only one process can listen on port 8765 at a time.

To test the synthetic ESP32 sensor-data path instead, stop the server and run:

```powershell
py main.py --replay-hardware
```

This displays **OFFLINE REPLAY** and never claims that real sensors are attached. The optional physical USB receiver uses `py main.py --serial-port COM5` (replace COM5 with the actual device port) and requires `pip install -r requirements-hardware.txt`. It has **not** been validated with a real board.

## Teacher demonstration — approximately 3 minutes

| Step | What to show | What it demonstrates |
|---|---|---|
| 1 | Open the dashboard at `http://127.0.0.1:8765` | Phase 4 software prototype; labelled SIMULATION MODE |
| 2 | Click **Run automatic demonstration** | Synthetic acceleration, a 70 km/h peak, and exceedance of the example 50 km/h configured threshold |
| 3 | Open **Latest stop evidence** and **Event history** | Pre-stop speed, timestamp, recent peak, overspeed and local event-integrity check |
| 4 | Show **Speed history** | The recent 30-second speed trace and configured limit |
| 5 | Scroll to **On-bike TFT screen preview** | Proposed display with current speed, peak, limit, pre-stop value and recording status |
| 6 | Open **Complete journey history** | Select a saved PC journey for complete graph, duration, estimated distance and **Export CSV** |
| 7 | Click **Load sample ride** | Import a fictional microSD-style 2 minute 30 second ride; show 70 km/h peak and approximately 2.0 km estimated distance |
| 8 | Point out the **Unverified import** label | External CSV files are displayed but not authenticated as physical measurements |

**Presentation explanation:** "DriveTrace is designed to record vehicle speed while moving and retain speed evidence even after the bike stops. It also keeps an entire ride for later PC review. Today I am demonstrating the tested software and simulated sensor input. The final step is to connect and calibrate the ESP32-S3 hardware."

For speaking notes, see [docs/TEACHER_DEMO.md](docs/TEACHER_DEMO.md).

## What is implemented vs what is pending

| Function | Current status |
|---|---|
| Browser live-speed gauge and limit alert | Implemented; simulation tested |
| Recent 30-second chart and pre-stop records | Implemented; automated tests |
| Local event HMAC-SHA256 integrity check | Implemented for PC-generated records; demo protection only |
| Full PC journey CSV recording and history view | Implemented; automated tests |
| microSD-format CSV import and export | Implemented; external imports marked unverified |
| TFT screen in browser | Implemented preview only |
| Synthetic Hall, IMU and GPS packet replay | Implemented; no hardware needed |
| ESP32-S3 USB serial / Hall + IMU + GPS starter | Source included; real board testing pending |
| Standalone ESP32 TFT + RTC + microSD example | Source included; compilation/wiring/physical tests pending |
| Actual e-bike calibration, power and tamper detection | Pending |

**Integrity limitation:** The current HMAC verifies PC-generated records using a key stored locally under `data/secret.key`. It cannot establish that an imported CSV is genuine, and it is not certified evidence of vehicle speed. Device-side cryptographic protections and secure key management are future engineering work.

## Data, CSV import and storage

- `data/events/`: locally saved stop and simulated tamper events (JSON).
- `data/journeys/`: local journey CSV files and their JSON summaries.
- `data/secret.key`: local demonstration HMAC key, **never upload or commit**.
- `samples/teacher_ride_sdcard.csv`: fictional ride sample for **Load sample ride**.
- External CSV files are validated on import and displayed as **UNVERIFIED_IMPORT**.

Basic CSV headers:

```csv
timestamp_utc,speed_kmh,source,gps_speed_kmh,wheel_pulses,imu_ax_g,imu_ay_g,imu_az_g
2026-11-23T10:30:00+00:00,12.50,HARDWARE,12.20,5,0.02,0.00,0.98
```

The importer requires timezone-aware `timestamp_utc` and nonnegative `speed_kmh`. CSV import accepts at most 8 MB and 200,000 records; timestamps must be chronological. Estimated distance integrates sampled speeds over time; it is not yet a calibrated odometer reading.

**Privacy and Git:** The entire `data/` directory, including journey logs and the HMAC key, is excluded by `.gitignore`. No ride histories or secrets should be published.

## Project files

| File or directory | Role |
|---|---|
| `main.py` | Local HTTP server, simulation and telemetry API |
| `drivetrace_core.py` | Stop-event logic and integrity checks |
| `journey_store.py` | Whole-journey CSV and summary storage |
| `static/` | Responsive dashboard, JavaScript and styling |
| `hardware/drivetrace_esp32s3.ino` | ESP32-S3 USB sensor telemetry starter |
| `hardware/standalone_esp32s3_tft_sd/` | Untested standalone TFT + microSD example |
| `docs/WIRING_PHASE3.md` | Provisional wiring plan |
| `docs/PHASE4_HARDWARE_NOTES.md` | Hardware limitations and integration details |
| `docs/TEACHER_DEMO.md` | Short classroom demonstration script |
| `HARDWARE_LIST.md` and `DriveTrace_Hardware.pdf` | Teacher hardware purchase list |
| `tests/` | Python automated tests |
| `samples/` | Fictional replay and CSV files |

**Hardware safety:** ESP32-S3 pins use 3.3 V logic. The wheel magnet rotates; the wired Hall sensor must remain on the stationary frame. Confirm the exact board pinout and voltage compatibility before wiring. The MPU6050 and DS3231 may both use I²C address 0x68 unless configured differently. Bench-test everything before mounting it on a bicycle.

## Test the application

From the project directory:

```powershell
py -m unittest discover -s tests -v
```

The consolidated application passed **24 automated tests** locally on 23 November 2026, covering the recorder, telemetry replay, CSV journeys, import validation and HTTP APIs. These tests do not substitute for physical sensor calibration or device safety tests.

## Source control

This is the **single maintained local project**. Older Phase 1/2/3/4 duplicate folders were removed after merging the Phase 4 features. Keep working in:

```text
E:\Downloads\all embedded\DriveTrace\DriveTrace_System
```

GitHub repository: **[Chuks400/DriveTrace_System](https://github.com/Chuks400/DriveTrace_System)**

Branch: `main`. Use GitHub Desktop to commit and push subsequent changes. The repository is already initialized and connected to `origin`; **do not create another repository**. Keep `data/` private.

## Next milestone

1. Receive the teacher-supplied ESP32-S3, Hall sensor, magnet, IMU, GPS, TFT, microSD, RTC and power components.
2. Confirm actual pinouts, I²C addresses, display interface and power requirements.
3. Compile and flash the ESP32 sketches; test each component on a bench.
4. Calibrate wheel circumference and compare speed sources on a stationary test wheel.
5. Validate standalone logging, stop detection, timestamps and PC import.
6. Mount securely on the e-bike and perform cautious, low-speed tests in a controlled location.

**Project state:** Ready for a software demonstration; physical embedded integration remains to be completed.

## Virtual hardware simulation (Wokwi)

The existing repository includes the Wokwi ESP32-S3 circuit in [hardware/wokwi/](hardware/wokwi/). Its `sketch.ino` is the firmware, `diagram.json` is the circuit wiring, and `libraries.txt` lists Arduino libraries. See the [simulator guide](hardware/wokwi/README.md). This is not another DriveTrace application.

To run: open [Wokwi ESP32-S3](https://wokwi.com/projects/new/esp32-s3), copy `sketch.ino` and `diagram.json` into their matching editor tabs, install the three libraries in `libraries.txt`, then press Start Simulation. Use the circuit's **Auto Demo**, **Accelerate +10**, **Brake -10**, and **Stop** buttons. GPIO6 generates synthetic wheel-magnet pulses read by the ESP32 interrupt on GPIO4. The virtual TFT shows measured wheel speed, the peak, pre-stop speed, limit and warning. The MPU6050, RTC stand-in and microSD provide additional sensor and logging interfaces.

**Test status:** The Wokwi CLI diagram linter passed on 23 November 2026 with one informational notice about the S3 board. Wokwi accepted a simulator build into its queue, but compilation and firmware execution are **not yet confirmed**. The virtual wheel is a simulated electrical pulse source rather than an animated car or a calibrated physical speed sensor. The PC dashboard at `http://127.0.0.1:8765` is separate and does not automatically connect to the Wokwi browser.
