# DriveTrace hardware integration — Phase 2 bench setup

## Architecture

For the first physical prototype, the ESP32-S3 measures wheel rotation using a Hall-effect sensor and magnet. It also reads raw acceleration from an MPU6050 over I2C. The ESP32 sends newline-delimited JSON over USB serial to the local Python application. The dashboard continues to run in the browser on the same computer.

`Hall sensor + wheel magnet + MPU6050 → ESP32-S3 → USB serial → Python serial bridge → recorder → dashboard`

GPS, microSD, RTC, TFT, buzzer and tamper switch are later integration steps. The IMU is initially used as a movement/braking cross-check; it is not the primary speed source because double integration of acceleration drifts. GPS is an independent outdoor cross-check, not a guaranteed source in tunnels or poor reception.

## Before wiring

1. Identify the exact ESP32-S3 board model and verify its pinout.
2. Confirm the Hall module output is 3.3 V-safe. Never connect a 5 V signal directly to an ESP32-S3 GPIO.
3. Confirm the MPU6050 module supply and I2C logic levels. Use 3.3 V-compatible wiring.
4. Keep the first test on a small bench wheel or RC wheel, not a road vehicle.
5. Measure the wheel circumference in metres and set `WHEEL_CIRCUMFERENCE_M` in the firmware.

## Example pin assignment (verify against your board)

| Signal | ESP32-S3 pin in sample | Notes |
|---|---:|---|
| Hall sensor OUT | GPIO 4 | Digital input with pull-up; check module output type |
| MPU6050 SDA | GPIO 8 | I2C data; board pin must be available |
| MPU6050 SCL | GPIO 9 | I2C clock; board pin must be available |
| Sensor VCC/GND | 3V3/GND | Only if each module supports 3.3 V supply |

These are example GPIO assignments, not universal ESP32-S3 pinouts. Some development boards reserve pins or route them differently.

## Install the serial bridge dependency on Windows

From the DriveTrace project folder:

```powershell
py -m pip install -r requirements-hardware.txt
```

Find the ESP32 COM port in Windows Device Manager → Ports (COM & LPT). Close Arduino Serial Monitor before starting DriveTrace; only one program can normally own the serial port at a time.

Start the dashboard with the actual port, for example:

```powershell
py main.py --serial-port COM5
```

Replace `COM5` with the port shown on your computer. The dashboard URL remains `http://127.0.0.1:8765`. Without `--serial-port`, it stays in simulation mode.

## Serial protocol

The firmware sends one UTF-8 JSON object per line at 115200 baud:

```json
{"type":"telemetry","speed_kmh":12.34,"wheel_pulses":42,"imu_valid":true,"imu_ax_g":0.012,"imu_ay_g":-0.034,"imu_az_g":0.998}
```

The Python bridge accepts `type=telemetry` and a numeric `speed_kmh` from 0 to 250. Samples are recorded with source `HARDWARE`. Other fields are retained in the serial packet for later diagnostics but are not yet used for sensor fusion.

## Validation checklist

- [ ] Confirm the board enumerates as a COM port.
- [ ] Upload `hardware/drivetrace_esp32s3.ino` using Arduino IDE with an ESP32-S3 board package.
- [ ] Confirm the serial status line reports `imu_ready:true`.
- [ ] Rotate the test wheel slowly and confirm speed rises in the dashboard.
- [ ] Stop the wheel and confirm a STOP event is recorded with `source=HARDWARE`.
- [ ] Compare calculated speed against a measured rotation rate; adjust circumference and magnet count assumptions.
- [ ] Check that a stationary wheel reports 0 km/h after the pulse timeout.
- [ ] Repeat at several speeds and document error before relying on the measurement.

## Current limitations

This is an initial bench integration scaffold. It is not yet a complete production device: no GPS input, SD-card logging, RTC timestamp, TFT, tamper switch, calibrated sensor fusion, enclosure, vehicle power conditioning, or road testing has been implemented. The sample firmware and serial bridge have not been compiled against your exact board or physically tested yet. Do not use it for law enforcement, road-safety decisions, or legal evidence.
