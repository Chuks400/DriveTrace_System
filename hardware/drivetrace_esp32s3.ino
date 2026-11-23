/*
 * DriveTrace Phase 3 — ESP32-S3 bench firmware (Hall + MPU6050 + GPS).
 * One newline-delimited JSON telemetry packet at 5 Hz over USB serial.
 *
 * Pins are EXAMPLES. Check your exact ESP32-S3 dev board pinout.
 * ONLY attach 3.3 V-safe logic to ESP32-S3 pins.
 * Demo/educational use; not calibrated or road-ready.
 *
 * Compile using Arduino IDE 2.x + Espressif ESP32 board support.
 * No additional Arduino library is necessary (uses Wire + built-in UART).
 */
#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static constexpr int HALL_PIN = 4;
static constexpr int I2C_SDA_PIN = 8;
static constexpr int I2C_SCL_PIN = 9;
static constexpr int GPS_RX_PIN = 16; // ESP32 RX <- GPS module TX (3.3 V UART)
static constexpr int GPS_TX_PIN = 17; // ESP32 TX -> GPS RX (optional, module permitting)
static constexpr uint8_t MPU6050_ADDR = 0x68;

// REPLACE after measuring your specific test wheel. One magnet = one pulse per revolution.
static constexpr float WHEEL_CIRCUMFERENCE_M = 0.210f;
static constexpr uint32_t WHEEL_STOP_TIMEOUT_US = 1500000;
static constexpr uint32_t HALL_DEBOUNCE_US = 3000;
static constexpr uint32_t SEND_INTERVAL_MS = 200;
static constexpr uint32_t GPS_FIX_TIMEOUT_MS = 3000;

volatile uint32_t lastPulseUs = 0;
volatile uint32_t pulseIntervalUs = 0;
volatile uint32_t pulseCount = 0;
uint32_t lastSendMs = 0;
bool imuReady = false;

char nmeaLine[128] = {0};
size_t nmeaLength = 0;
float gpsSpeedKmh = 0.0f;
uint32_t lastGpsValidMs = 0;
bool gpsEverValid = false;

void IRAM_ATTR onHallPulse() {
  const uint32_t now = micros();
  const uint32_t previous = lastPulseUs;
  if (previous && (uint32_t)(now - previous) < HALL_DEBOUNCE_US) return;
  if (previous) pulseIntervalUs = now - previous;
  lastPulseUs = now;
  pulseCount++;
}

bool writeMpu(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool readMpuAcceleration(float &ax, float &ay, float &az) {
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x3B);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)MPU6050_ADDR, 6, true) != 6) return false;
  const int16_t x = (int16_t)((Wire.read() << 8) | Wire.read());
  const int16_t y = (int16_t)((Wire.read() << 8) | Wire.read());
  const int16_t z = (int16_t)((Wire.read() << 8) | Wire.read());
  // +/-2g default scale; readings include gravity until calibrated/oriented.
  ax = x / 16384.0f;
  ay = y / 16384.0f;
  az = z / 16384.0f;
  return true;
}

int hexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

// Return the pointer to the nth comma-separated field of an NMEA sentence.
// Field index 0 is the message type ($GPRMC), 2 is RMC fix status, 7 speed (knots).
const char* nmeaField(const char* line, unsigned index) {
  const char* p = line;
  for (unsigned i = 0; i < index; ++i) {
    p = strchr(p, ',');
    if (!p) return nullptr;
    ++p;
  }
  return p;
}

void parseRmc(const char* line) {
  if (strncmp(line, "$GPRMC,", 7) != 0 && strncmp(line, "$GNRMC,", 7) != 0) return;
  const char* star = strchr(line, '*');
  if (!star || !star[1] || !star[2]) return;
  const int h = hexDigit(star[1]);
  const int l = hexDigit(star[2]);
  if (h < 0 || l < 0) return;
  uint8_t xorValue = 0;
  for (const char* p = line + 1; p < star; ++p) xorValue ^= (uint8_t)*p;
  if (xorValue != (uint8_t)((h << 4) | l)) return;

  const char* status = nmeaField(line, 2);
  const char* speedText = nmeaField(line, 7);
  if (!status || *status != 'A' || !speedText || *speedText == ',') return;
  char* end = nullptr;
  const float knots = strtof(speedText, &end);
  if (end == speedText || !end || *end != ',' || !isfinite(knots) || knots < 0 || knots > 135) return;
  gpsSpeedKmh = knots * 1.852f;
  lastGpsValidMs = millis();
  gpsEverValid = true;
}

void pollGps() {
  // Limit work per loop so GPS noise cannot stall sensor updates.
  for (int n = 0; n < 192 && Serial1.available(); ++n) {
    const char c = (char)Serial1.read();
    if (c == '$') { nmeaLength = 0; nmeaLine[nmeaLength++] = c; }
    else if (c == '\n') {
      if (nmeaLength > 0) { nmeaLine[nmeaLength] = '\0'; parseRmc(nmeaLine); }
      nmeaLength = 0;
    } else if (nmeaLength > 0 && nmeaLength < sizeof(nmeaLine)-1 && c != '\r') {
      nmeaLine[nmeaLength++] = c;
    } else if (nmeaLength == sizeof(nmeaLine)-1) {
      nmeaLength = 0; // discard unexpectedly long line
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(350);
  pinMode(HALL_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(HALL_PIN), onHallPulse, FALLING);
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  imuReady = writeMpu(0x6B, 0x00);
  Serial1.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  Serial.print("{\"type\":\"status\",\"device\":\"DriveTrace ESP32-S3\",\"imu_ready\":");
  Serial.print(imuReady ? "true" : "false");
  Serial.println(",\"gps_uart\":true}");
}

void loop() {
  pollGps();
  const uint32_t nowMs = millis();
  if ((uint32_t)(nowMs - lastSendMs) < SEND_INTERVAL_MS) return;
  lastSendMs = nowMs;

  uint32_t intervalCopy, lastPulseCopy, countCopy;
  noInterrupts();
  intervalCopy = pulseIntervalUs;
  lastPulseCopy = lastPulseUs;
  countCopy = pulseCount;
  interrupts();

  const uint32_t nowUs = micros();
  float speedKmh = 0.0f;
  if (lastPulseCopy && (uint32_t)(nowUs - lastPulseCopy) <= WHEEL_STOP_TIMEOUT_US && intervalCopy) {
    speedKmh = WHEEL_CIRCUMFERENCE_M * 3600000.0f / intervalCopy;
  }
  float ax = 0.0f, ay = 0.0f, az = 0.0f;
  const bool accelValid = imuReady && readMpuAcceleration(ax, ay, az);
  const bool gpsValid = gpsEverValid && (uint32_t)(nowMs - lastGpsValidMs) <= GPS_FIX_TIMEOUT_MS;

  Serial.print("{\"type\":\"telemetry\",\"speed_kmh\":");
  Serial.print(speedKmh, 2);
  Serial.print(",\"wheel_pulses\":"); Serial.print(countCopy);
  Serial.print(",\"imu_valid\":"); Serial.print(accelValid ? "true" : "false");
  Serial.print(",\"imu_ax_g\":"); Serial.print(ax, 3);
  Serial.print(",\"imu_ay_g\":"); Serial.print(ay, 3);
  Serial.print(",\"imu_az_g\":"); Serial.print(az, 3);
  Serial.print(",\"gps_valid\":"); Serial.print(gpsValid ? "true" : "false");
  if (gpsValid) {
    Serial.print(",\"gps_speed_kmh\":"); Serial.print(gpsSpeedKmh, 2);
  }
  Serial.println("}");
}
