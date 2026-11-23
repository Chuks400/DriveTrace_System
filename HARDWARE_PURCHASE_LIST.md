# DriveTrace — Hardware Purchase List

Based on the uploaded five-page DriveTrace proposal, with purchase order and safety/compatibility notes added.

## Order A — Core parts for first physical prototype

| Qty | Component | What to look for | Why |
|---:|---|---|---|
| 1 | ESP32-S3 development board | Prefer **ESP32-S3-DevKitC-1-N8R8** or a well-documented ESP32-S3 board with pre-soldered headers | Main controller |
| 1 | Hall-effect wheel-speed sensor | A3144 **or equivalent** module; check ESP32-compatible **3.3 V signal** levels | Wheel pulse measurement |
| 1–2 | Small neodymium magnets | Small wheel-mountable magnets | Trigger Hall sensor each revolution |
| 1 | MPU6050 GY-521 IMU | 3.3 V-compatible I2C module | Detect acceleration/braking |
| 1 | 2.4-inch SPI TFT | **ILI9341**, 240×320, documented 3.3 V logic, SPI pins exposed | Device display |
| 1 | MicroSD breakout | SPI breakout that works with **3.3 V ESP32 signals** | Event logging |
| 1 | 8–32 GB microSD card | Genuine card from reliable seller | Data storage |
| 1 | DS3231 RTC | I2C real-time clock module | Event timestamps |
| 1 | Backup coin cell | **Only the type recommended for the chosen RTC module** | Clock backup |
| 1 | Tamper microswitch | Small lever/limit switch | Detect case opening |
| 1 | Active buzzer | Low-current, ESP32-compatible module | Alerts |
| 2–3 | LEDs | Red and green; amber optional | Status indication |
| 3–5 | 220–330 Ω resistors | Through-hole | LED current limiting |
| 1 | Breadboard | Full or half size | Initial assembly |
| 1 set | Male-to-male and male-to-female jumper wires | Mixed lengths | Connections |
| 1 | USB data cable | Match the **actual ESP32 board port** (Micro-USB or USB-C) | Programming and bench power |
| 1 | Model wheel/RC car or bicycle | Wheel with known circumference | Real movement demo |
| 1 | Hall sensor bracket | Adjustable mount | Sensor placement |

## Order B — Complete multi-sensor and portable version

| Qty | Component | Notes |
|---:|---|---|
| 1 | NEO-M8N GPS receiver + antenna | Outdoor speed cross-check; check UART logic voltage and supply requirements |
| 1 | Power switch | For portable enclosure |
| 1 | Suitable battery pack/power source | A certified 5 V USB power bank is a simpler early option |
| 1 | Battery connector/holder | Only if using a separate battery pack |
| 1 | LM2596 buck converter | **Only if needed** for the selected battery/input voltage; set and measure output before connection |
| 1 | Perfboard/prototype PCB | Final wiring |
| 1 set | Hook-up wire and pin headers/connectors | Permanent connections |
| 1 | Project enclosure | 3D printed or off-the-shelf |
| 1 set | Screws, nuts, standoffs, spacers | Mounting |
| 1 set | Cable ties, Velcro, double-sided tape | Secure parts to vehicle |

## Tools (buy only if you do not already have them)

- Digital multimeter
- Soldering iron and solder
- Wire cutter/stripper
- Screwdrivers
- Heat-shrink tubing or hot glue
- Laptop with Python now, then Arduino IDE or PlatformIO

## Compatibility checks before paying

1. ESP32-S3 GPIOs use **3.3 V logic**; do not feed 5 V signals into GPIOs. Some A3144 boards and TFT/SD modules have different voltage requirements; check the exact listing/datasheet.
2. Check the ESP32 board's USB connector type and whether headers are included.
3. Check whether the chosen RTC board is designed for a rechargeable or non-rechargeable backup cell; do not assume every DS3231 breakout safely supports a CR2032.
4. Check whether the GPS module includes its antenna and a regulator/level-shifting circuitry.
5. For initial testing, power the ESP32 via USB. Do not wire a bare lithium battery directly to ESP32 power pins.
6. Select wheel circumference from the actual wheel, then calibrate the speed equation: **speed_kmh = rotations_per_second × circumference_m × 3.6**.
7. IMU acceleration integration drifts; use Hall pulses as the primary speed source and GPS as an optional outdoor check, as stated in the proposal.
