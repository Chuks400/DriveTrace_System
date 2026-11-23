# DriveTrace — Teacher presentation (software demonstration)

**Project topic:** DriveTrace: Multi-Sensor Tamper-Evident Speed History and Pre-Stop Event Recorder

## What problem does it solve?

A standard speed display may show zero after a vehicle stops. DriveTrace keeps the recent speed before stopping, records a speed-limit violation, and saves a complete ride so the operator can review it afterward.

## Demonstrate on the PC today

1. `py main.py` → open `http://127.0.0.1:8765`.
2. Show **Simulation Mode**; no real sensors are connected.
3. Click **Run automatic demonstration** (peak 70 km/h; limit 50 km/h).
4. Show the STOP event: pre-stop speed, 30-second peak, overspeed status, timestamp and local HMAC integrity.
5. Scroll to **On-bike TFT screen preview**, which shows the intended speedometer display.
6. Scroll to **Complete journey history**, select the PC ride, demonstrate the long-duration graph and CSV download.
7. Click **Load sample ride** and show microSD-like CSV import, ride graph, estimated distance and the unverified data warning.
8. For Hall/IMU/GPS protocol: restart with `py main.py --replay-hardware` (synthetic feed clearly labelled OFFLINE REPLAY).

## If the teacher asks about the final device

- The stationary Hall sensor counts a magnet on the rotating wheel; no wires rotate with the wheel.
- The onboard TFT will show speed and stop evidence without a laptop.
- The device will write CSV rows to microSD while riding. Back at the PC, the CSV can be imported into this dashboard.
- Hardware must be purchased, installed, calibrated, and tested; the PC demo does not prove sensor accuracy.
- USB, SD, TFT and RTC code is included, but **physical firmware compilation and board validation are outstanding**.

## Answer about security

"The current PC prototype signs its own event data with a locally stored HMAC key. Imported SD files are not cryptographically authenticated. A future device-security step needs safe, device-side key management and protection against record deletion or replacement."

## Demo completeness

Ready to show: UI, synthetic speeds, pre-stop events, overspeed, full ride CSV, full graph, simulated TFT layout, replay feed and CSV import. Not ready to claim: verified real e-bike speed, tamper-resistant standalone hardware, road-legal accuracy, end-to-end signed SD records, or remote cloud sync.
