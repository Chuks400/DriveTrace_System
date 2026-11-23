# Example Phase 3 wiring — verify actual ESP32-S3 board pins

See [`WIRING_PHASE3.svg`](WIRING_PHASE3.svg) for a visual block diagram.

| Device | Device pin | ESP32-S3 sample pin | Notes |
|---|---|---|---|
| Hall-effect module | OUT | GPIO4 | Digital Hall output; only 3.3 V-safe logic |
| Hall-effect module | VCC | 3V3 | **Only if your purchased module supports 3.3 V supply** |
| Hall-effect module | GND | GND | Shared ground |
| MPU6050 breakout | SDA | GPIO8 | I2C data, 3.3 V-safe breakout required |
| MPU6050 breakout | SCL | GPIO9 | I2C clock, 3.3 V-safe breakout required |
| MPU6050 breakout | VCC | 3V3 | Confirm module input voltage and pull-ups |
| MPU6050 breakout | GND | GND | Shared ground |
| NEO-M8N breakout | TX | GPIO16 (RX) | GPS UART TX -> ESP RX; check signal levels |
| NEO-M8N breakout | RX | GPIO17 (TX) | Optional; needed only for GPS configuration |
| NEO-M8N breakout | VCC | Per module label | **Do not assume 3.3 V supply works for every breakout** |
| NEO-M8N breakout | GND | GND | Shared ground |
| ESP32-S3 dev board | USB | Computer USB | Programming and UART dashboard bridge |

### Important

- GPIO4, GPIO8, GPIO9, GPIO16, GPIO17 are **example** pins, not universal.
  Different ESP32-S3 boards reserve and expose different pins.
- No battery power, vehicle wiring, or road testing yet. Start from the
  computer USB power only and use a small manually rotated test wheel.
- Keep the magnet secure on the wheel and the Hall sensor safely clear of
  moving parts. The supplied calculation assumes **one pulse per revolution**.
- GPIO inputs on ESP32-S3 are not 5 V tolerant. Do not connect any 5 V
  signal directly, even if a breakout accepts 5 V power.
- GPS parsing supports checksum-validated `$GPRMC` and `$GNRMC` sentences;
  speed is converted from knots to km/h. A fresh fix is required; other
  NMEA sentences are intentionally ignored at this stage.
- No TFT, microSD, RTC, case switch or battery are connected in Phase 3.
