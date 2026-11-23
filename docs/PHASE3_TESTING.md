# DriveTrace Phase 3 — Test without any purchased hardware

**This release preserves the existing simulator and saved-event format.** It adds
an offline replay through the exact same `ingest_hardware()` function the USB
SerialBridge calls. It is **synthetic data**, not a sensor reading.

## Recommended first test (no hardware)

In the new Phase 3 folder, with Phase 2 closed (`Ctrl+C` in the old terminal):

```powershell
py main.py --replay-hardware
```

Open `http://127.0.0.1:8765`. The 15-second replay drives the dashboard,
briefly reaches **70 km/h**, then stops. The header says **OFFLINE REPLAY**.
The latest record source should say `REPLAY` in its exported JSON. The record
should show a 70 km/h peak and `Verified` integrity; **hardware connected
must remain false**. Use the dashboard's **Sensor feed diagnostics** section
to see synthetic Hall pulses, IMU accelerometer and GPS values. If the
replay has finished, the dashboard labels it as completed.

For the original manual simulation instead, stop the replay server and run:

```powershell
py main.py
```

For tests:

```powershell
py -m unittest discover -s tests -v
```

## Physical USB test — after buying ESP32-S3 and sensors

1. Confirm exact board pin mapping and **3.3 V input compatibility**.
2. Open `hardware/drivetrace_esp32s3.ino` using Arduino IDE 2.x, install the
   Espressif ESP32 board support package, and select the correct ESP32-S3.
   ESP32-S3 USB CDC settings depend on your board/USB connector.
3. Measure the wheel circumference and set `WHEEL_CIRCUMFERENCE_M`. Use one
   magnet per revolution (or adjust firmware pulse count accordingly).
4. If GPS is unavailable indoors, `gps_valid` stays `false`. This is expected.
5. Install the Python serial dependency, only when the real board is connected:

   ```powershell
   py -m pip install -r requirements-hardware.txt
   ```

6. Close Arduino Serial Monitor, check Device Manager for your board's COM port,
   and start (replace `COM5` with your port):

   ```powershell
   py main.py --serial-port COM5
   ```

7. Rotate the test wheel and observe Hall pulses and live speed. Bring it to
   rest and ensure a single STOP event appears. Rotate at a known rate to
   estimate error. Compare with GPS **outdoors only** after a valid fix.

**Limitations:** The Arduino firmware is a reference implementation, not yet
compiled or tested on your exact ESP32-S3; the board and sensor modules have
not been purchased. Raw IMU acceleration includes gravity and must be
calibrated/oriented before being used for braking verification. The GPS/wheel
agreement check is a heuristic diagnostic, not a forensic or safety claim.

## Preserve earlier event files

Your old Phase 2 folder is not overwritten. If you want prior events in
Phase 3, stop both programs, then copy the **entire** `data` folder from
Phase 2 to the new Phase 3 folder *before* creating Phase 3 records. This
preserves the original `secret.key` used to verify those events. Keep this key
private; never commit it to GitHub. Back up both folders first.
