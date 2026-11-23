# Phase 4 — Hardware bring-up cautions

This is a student demonstration design, not a road-certified speedometer or tamper-proof black box. Do **not** begin real vehicle use without a bench test of each module, an appropriately fused/protected power source, and solid mounts out of the steering/braking/wheel mechanisms.

## Sensor positions

- A3144 Hall sensor: mounted to a *stationary* fork or frame bracket, fixed wires secured along frame.
- Magnet: attached securely to a wheel spoke, passing the fixed sensor at the gap specified by the actual module and magnet strength; confirm reading before riding.
- ESP32-S3 / TFT / microSD / IMU / RTC: inside a protected enclosure secured to the vehicle frame. IMU should have known orientation.
- GPS antenna: clear outdoor sky view.
- Never attach wiring to a moving wheel or moving drivetrain.

## Power and logic

- ESP32-S3 GPIO is **3.3 V** logic. Sensor modules and SPI display/SD modules must be compatible, or use appropriate level shifting.
- Choose an adequately protected battery, regulator, fuse and charging solution matched to actual module currents/voltages. Do not use a guessed 12 V supply directly on low-voltage electronics.
- A DS3231 and MPU6050 may **both default to 0x68**; set MPU6050 `AD0` to HIGH for `0x69` in the standalone example, confirm with I²C scanner, and check pull-up levels.
- Some DS3231 modules have battery charging circuits unsuitable for a standard non-rechargeable CR2032. Check the exact RTC board before installing a cell.
- Sharing SPI bus between TFT and SD requires separate CS pins, no floating CS, adequate supply power and tested module compatibility.

## Example ESP32-S3 pin assignments (not universal)

| Connection | S3 GPIO (example) |
|---|---|
| Hall input | 4 |
| SDA / SCL to MPU6050 and RTC | 8 / 9 |
| GPS UART RX/TX | 16 / 17 |
| SPI SCK/MISO/MOSI (shared) | 12 / 13 / 11 |
| TFT chip select / data-command / reset | 10 / 14 / 15 |
| SD chip select | 5 |

Verify every pin on the exact board. Board flash, boot, power, and USB settings can differ.

## Firmware steps after components arrive

1. Install ESP32 board support + Adafruit GFX and ILI9341 libraries.
2. Test Hall alone, calibrate wheel circumference, and record its pulses on a stationary test wheel.
3. Test I²C MPU and DS3231 addresses, set RTC to **UTC**, then test GPS outdoors.
4. Test SPI TFT and microSD one at a time, then together. Confirm logging after power loss.
5. Record a stationary bench spin with `standalone_esp32s3_tft_sd.ino` and safely remove the card.
6. Import `RIDE001.CSV` into DriveTrace PC dashboard, inspect duration/speed and check for gaps.
7. Only then conduct low-speed tests in a traffic-free area; inspect mounting, wiring, braking clearance, sensor stability and battery safety frequently.

## Design limitations

- ESP32 standalone code is **uncompiled example code** that still requires debugging and physical calibration.
- `WHEEL_CIRCUMFERENCE_M = 0.210` is **only a model-wheel placeholder**. Set the measured circumference for the selected wheel.
- Time is taken from RTC assuming UTC; invalid/missing time prevents standalone CSV logging.
- Independent device-side HMAC signing, physical tamper switch and tamper events, secure deletion protection and power-loss recovery need additional hardware development.
- The laptop import verifies only the PC-generated records with a local demo key. Imported external files remain unverified.
