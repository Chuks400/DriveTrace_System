/*
 DriveTrace Wokwi hardware simulation (ESP32-S3)
 Virtual wheel: GPIO6 generates 3.3V magnetic pulses into GPIO4 Hall input.
 This is a simulator signal, not a real Hall sensor or moving car.
 Buttons: ACCEL +10 / BRAKE -10 / STOP / AUTO DEMO.
 TFT, MPU6050, DS1307 (simulator stand-in for DS3231), microSD, overspeed LED.
 Requires: Adafruit GFX Library, Adafruit ILI9341, RTClib.
*/
#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <RTClib.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <math.h>

constexpr int HALL_INPUT = 4;
constexpr int WHEEL_PULSE_OUT = 6;
constexpr int BTN_ACCEL = 18;
constexpr int BTN_BRAKE = 17;
constexpr int BTN_STOP = 16;
constexpr int BTN_DEMO = 21;
constexpr int ALERT_LED = 7;
constexpr int TFT_CS = 10, TFT_DC = 14;
constexpr int SD_CS = 5;
constexpr int SPI_SCK = 12, SPI_MISO = 13, SPI_MOSI = 11;
constexpr int I2C_SDA = 8, I2C_SCL = 9;
constexpr uint8_t MPU_ADDR = 0x69; // AD0 connected to 3.3V; DS1307 remains 0x68.
constexpr float WHEEL_CIRCUMFERENCE_M = 2.10f; // SIMULATED e-bike wheel, set for display only.
constexpr float SPEED_LIMIT_KMH = 50.0f;
constexpr uint32_t HALL_DEBOUNCE_US = 3500;
constexpr uint32_t HALL_STALE_US = 2200000;

Adafruit_ILI9341 tft(TFT_CS, TFT_DC);
RTC_DS1307 rtc;
bool rtcReady = false, sdReady = false, imuReady = false;
bool recording = false, overspeedSeen = false, demoActive = false;
float virtualTargetKmh = 0.0f;
float measuredKmh = 0.0f, peakKmh = 0.0f, preStopKmh = 0.0f;
float imuAx = 0.0f, imuAy = 0.0f, imuAz = 0.0f;
uint32_t lastScreenMs = 0, lastLogMs = 0, lastSerialMs = 0, demoStartMs = 0;
uint32_t nextPulseUs = 0, pulseReleaseUs = 0;
bool pulseLow = false;
volatile uint32_t lastHallUs = 0, hallPeriodUs = 0, hallPulses = 0;
bool lastAccel = HIGH, lastBrake = HIGH, lastStop = HIGH, lastDemo = HIGH;
uint32_t debounceAccel = 0, debounceBrake = 0, debounceStop = 0, debounceDemo = 0;
String commandBuffer;

void IRAM_ATTR onHall() {
  uint32_t now = micros();
  uint32_t elapsed = now - lastHallUs;
  if (lastHallUs && elapsed < HALL_DEBOUNCE_US) return;
  if (lastHallUs) hallPeriodUs = elapsed;
  lastHallUs = now;
  hallPulses++;
}

bool pressEdge(int pin, bool &previous, uint32_t &lastMs) {
  bool level = digitalRead(pin);
  bool hit = previous == HIGH && level == LOW && millis() - lastMs >= 120;
  previous = level;
  if (hit) lastMs = millis();
  return hit;
}

void setTarget(float newKmh) {
  if (newKmh < 0) newKmh = 0;
  if (newKmh > 90) newKmh = 90;
  if (newKmh > 0 && virtualTargetKmh == 0) {
    recording = true;
    overspeedSeen = false;
    peakKmh = 0;
  }
  virtualTargetKmh = newKmh;
  nextPulseUs = micros(); // Inject a new magnet pass when target speed changes.
  Serial.print("SIMULATED_WHEEL_TARGET_KMH ");
  Serial.println(virtualTargetKmh, 1);
}

void pulseWheelSignal() {
  uint32_t now = micros();
  if (virtualTargetKmh <= 0) {
    digitalWrite(WHEEL_PULSE_OUT, HIGH);
    pulseLow = false;
    nextPulseUs = now;
    return;
  }
  if (pulseLow && (int32_t)(now - pulseReleaseUs) >= 0) {
    digitalWrite(WHEEL_PULSE_OUT, HIGH);
    pulseLow = false;
  }
  if (!pulseLow && (int32_t)(now - nextPulseUs) >= 0) {
    digitalWrite(WHEEL_PULSE_OUT, LOW);
    pulseLow = true;
    pulseReleaseUs = now + 2000;
    float interval = WHEEL_CIRCUMFERENCE_M * 3600000.0f / virtualTargetKmh;
    nextPulseUs = now + (uint32_t)interval;
  }
}

void readWheelSpeed() {
  uint32_t period, last, count;
  noInterrupts();
  period = hallPeriodUs;
  last = lastHallUs;
  count = hallPulses;
  interrupts();
  // Intentionally show 0 on STOP. In real hardware the stale timeout is also used.
  if (virtualTargetKmh <= 0 || !last || !period || micros() - last > HALL_STALE_US) {
    measuredKmh = 0;
  } else {
    measuredKmh = WHEEL_CIRCUMFERENCE_M * 3600000.0f / period;
    if (!isfinite(measuredKmh) || measuredKmh > 130) measuredKmh = 0;
  }
  if (recording && measuredKmh > peakKmh) peakKmh = measuredKmh;
  if (recording && measuredKmh > SPEED_LIMIT_KMH) overspeedSeen = true;
  digitalWrite(ALERT_LED, overspeedSeen ? HIGH : LOW);
}

bool readImu() {
  if (!imuReady) return false;
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)MPU_ADDR, 6, true) != 6) return false;
  int16_t x = (int16_t)((Wire.read() << 8) | Wire.read());
  int16_t y = (int16_t)((Wire.read() << 8) | Wire.read());
  int16_t z = (int16_t)((Wire.read() << 8) | Wire.read());
  imuAx = x / 16384.0f;
  imuAy = y / 16384.0f;
  imuAz = z / 16384.0f;
  return true;
}

String currentUTC() {
  if (!rtcReady) return "NO_RTC";
  DateTime dt = rtc.now();
  char stamp[40];
  snprintf(stamp, sizeof(stamp), "%04u-%02u-%02uT%02u:%02u:%02u+00:00",
           dt.year(), dt.month(), dt.day(), dt.hour(), dt.minute(), dt.second());
  return String(stamp);
}

void writeHeaderIfNeeded(const char *name, const char *header) {
  if (!sdReady || SD.exists(name)) return;
  File f = SD.open(name, FILE_WRITE);
  if (f) {
    f.println(header);
    f.close();
  }
}

void logSample() {
  if (!recording || !sdReady || !rtcReady) return;
  File f = SD.open("/RIDE.CSV", FILE_APPEND);
  if (!f) { Serial.println("SD_WRITE_FAILED"); return; }
  f.print(currentUTC()); f.print(',');
  f.print(measuredKmh, 2); f.print(",SIMULATED_WOKWI,,");
  noInterrupts(); uint32_t pulses = hallPulses; interrupts();
  f.print(pulses); f.print(',');
  f.print(imuAx, 3); f.print(',');
  f.print(imuAy, 3); f.print(',');
  f.println(imuAz, 3);
  f.close();
}

void stopVehicle() {
  if (virtualTargetKmh <= 0 && !recording) return;
  // Preserve the measured speed *before* commanded stop.
  preStopKmh = measuredKmh;
  if (virtualTargetKmh > 0 && preStopKmh < 0.1f) {
    // No valid second Hall pulse yet: event remains 0 instead of inventing a reading.
    preStopKmh = 0;
  }
  virtualTargetKmh = 0;
  measuredKmh = 0;
  digitalWrite(WHEEL_PULSE_OUT, HIGH);
  pulseLow = false;
  if (sdReady && rtcReady) {
    File f = SD.open("/STOPS.CSV", FILE_APPEND);
    if (f) {
      f.print(currentUTC()); f.print(',');
      f.print(preStopKmh, 2); f.print(',');
      f.print(peakKmh, 2); f.print(',');
      f.println(overspeedSeen ? "YES" : "NO");
      f.close();
    }
    logSample();
  }
  recording = false;
  Serial.print("STOP_EVENT simulated pre_stop=");
  Serial.print(preStopKmh, 1);
  Serial.print(" peak=");
  Serial.print(peakKmh, 1);
  Serial.print(" overspeed=");
  Serial.println(overspeedSeen ? "YES" : "NO");
}

void handleButtons() {
  if (pressEdge(BTN_ACCEL, lastAccel, debounceAccel)) {
    demoActive = false;
    setTarget(virtualTargetKmh + 10);
  }
  if (pressEdge(BTN_BRAKE, lastBrake, debounceBrake)) {
    demoActive = false;
    float next = virtualTargetKmh - 10;
    if (next <= 0) stopVehicle();
    else setTarget(next);
  }
  if (pressEdge(BTN_STOP, lastStop, debounceStop)) {
    demoActive = false;
    stopVehicle();
  }
  if (pressEdge(BTN_DEMO, lastDemo, debounceDemo)) {
    demoActive = true;
    demoStartMs = millis();
    if (recording) stopVehicle();
    setTarget(10);
    Serial.println("AUTOMATIC_DEMONSTRATION_START");
  }
}

void runDemo() {
  if (!demoActive) return;
  uint32_t elapsed = millis() - demoStartMs;
  float desired = 0;
  if (elapsed < 1000) desired = 10;
  else if (elapsed < 7000) desired = 10.0f * (1 + elapsed / 1000);
  else if (elapsed < 10000) desired = 70;
  else if (elapsed < 16000) desired = 70.0f - 10.0f * (1 + (elapsed - 10000) / 1000);
  if (desired <= 0) {
    stopVehicle();
    demoActive = false;
    Serial.println("AUTOMATIC_DEMONSTRATION_COMPLETE");
  } else if (desired != virtualTargetKmh) {
    setTarget(desired);
  }
}

void drawText(int x, int y, const char *text, uint8_t sz, uint16_t color) {
  tft.setTextSize(sz);
  tft.setTextColor(color, ILI9341_NAVY);
  tft.setCursor(x, y);
  tft.print(text);
}

void refreshTFT() {
  // Redraw only 1.5 times per second for responsive simulator performance.
  tft.fillScreen(ILI9341_NAVY);
  tft.fillRect(0, 0, 320, 35, ILI9341_BLUE);
  tft.setTextSize(2); tft.setTextColor(ILI9341_WHITE, ILI9341_BLUE);
  tft.setCursor(10, 9); tft.print("DriveTrace");
  drawText(215, 13, sdReady ? "SD OK" : "SD ERR", 1,
           sdReady ? ILI9341_GREEN : ILI9341_YELLOW);
  drawText(12, 43, "ESP32-S3  |  WOKWI SIMULATION", 1, ILI9341_CYAN);
  char b[50];
  snprintf(b, sizeof(b), "%.1f", measuredKmh);
  drawText(18, 67, b, 5, ILI9341_WHITE);
  drawText(235, 101, "km/h", 2, ILI9341_WHITE);
  snprintf(b, sizeof(b), "PEAK %.1f  LIMIT %.0f", peakKmh, SPEED_LIMIT_KMH);
  drawText(12, 141, b, 2, ILI9341_WHITE);
  snprintf(b, sizeof(b), "PRE-STOP %.1f", preStopKmh);
  drawText(12, 173, b, 2, ILI9341_CYAN);
  drawText(12, 199, imuReady ? "IMU OK" : "IMU OFF", 1, ILI9341_GREEN);
  tft.fillRect(0, 216, 320, 24,
               overspeedSeen ? ILI9341_RED : recording ? ILI9341_DARKGREEN : ILI9341_DARKGREY);
  tft.setTextSize(2);
  tft.setTextColor(ILI9341_WHITE);
  tft.setCursor(9, 220);
  tft.print(overspeedSeen ? "OVERSPEED EVENT" : recording ? "RECORDING RIDE" : "STANDBY");
}

void dumpRide() {
  if (!sdReady) { Serial.println("NO_SD_CARD"); return; }
  File f = SD.open("/RIDE.CSV", FILE_READ);
  if (!f) { Serial.println("NO_RIDE_YET"); return; }
  Serial.println("CSV_BEGIN");
  while (f.available()) Serial.write(f.read());
  Serial.println("CSV_END");
  f.close();
}

void handleSerial() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      commandBuffer.trim();
      commandBuffer.toUpperCase();
      if (commandBuffer == "DUMP") dumpRide();
      else if (commandBuffer == "STOP") { demoActive = false; stopVehicle(); }
      else if (commandBuffer == "DEMO") { demoActive = true; demoStartMs = millis(); setTarget(10); }
      commandBuffer = "";
    } else if (commandBuffer.length() < 30) commandBuffer += c;
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(WHEEL_PULSE_OUT, OUTPUT); digitalWrite(WHEEL_PULSE_OUT, HIGH);
  pinMode(HALL_INPUT, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(HALL_INPUT), onHall, FALLING);
  pinMode(BTN_ACCEL, INPUT_PULLUP);
  pinMode(BTN_BRAKE, INPUT_PULLUP);
  pinMode(BTN_STOP, INPUT_PULLUP);
  pinMode(BTN_DEMO, INPUT_PULLUP);
  pinMode(ALERT_LED, OUTPUT);
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B); Wire.write(0);
  imuReady = (Wire.endTransmission() == 0);
  rtcReady = rtc.begin();
  if (rtcReady && !rtc.isrunning()) rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
  pinMode(TFT_CS, OUTPUT); digitalWrite(TFT_CS, HIGH);
  pinMode(SD_CS, OUTPUT); digitalWrite(SD_CS, HIGH);
  tft.begin(); tft.setRotation(1);
  sdReady = SD.begin(SD_CS, SPI, 10000000);
  writeHeaderIfNeeded("/RIDE.CSV",
     "timestamp_utc,speed_kmh,source,gps_speed_kmh,wheel_pulses,imu_ax_g,imu_ay_g,imu_az_g");
  writeHeaderIfNeeded("/STOPS.CSV",
     "timestamp_utc,speed_before_stop_kmh,peak_speed_kmh,overspeed");
  refreshTFT();
  Serial.println("DriveTrace Wokwi ESP32-S3 virtual hardware started");
  Serial.println("ACCEL +10 | BRAKE -10 | STOP | AUTO DEMO buttons on the circuit");
  Serial.println("Wheel GPIO6 -> GPIO4: virtual Hall-compatible magnetic pulses");
  Serial.println("Type DUMP to print RIDE.CSV to serial monitor");
  Serial.print("MPU6050="); Serial.print(imuReady ? "YES" : "NO");
  Serial.print(" RTC="); Serial.print(rtcReady ? "YES" : "NO");
  Serial.print(" SD="); Serial.println(sdReady ? "YES" : "NO");
}

void loop() {
  handleButtons();
  handleSerial();
  runDemo();
  pulseWheelSignal();
  readWheelSpeed();
  uint32_t now = millis();
  if (now - lastLogMs >= 1000) {
    lastLogMs = now;
    readImu();
    logSample();
  }
  if (now - lastSerialMs >= 1000) {
    lastSerialMs = now;
    noInterrupts(); uint32_t count = hallPulses; interrupts();
    Serial.print("{\"type\":\"telemetry\",\"simulated\":true,\"source\":\"WOKWI\",\"speed_kmh\":");
    Serial.print(measuredKmh, 2);
    Serial.print(",\"wheel_pulses\":"); Serial.print(count);
    Serial.print(",\"imu_valid\":"); Serial.print(imuReady ? "true" : "false");
    Serial.print(",\"imu_ax_g\":"); Serial.print(imuAx, 3);
    Serial.print(",\"imu_ay_g\":"); Serial.print(imuAy, 3);
    Serial.print(",\"imu_az_g\":"); Serial.print(imuAz, 3);
    Serial.println(",\"gps_valid\":false}");
  }
  if (now - lastScreenMs >= 650) {
    lastScreenMs = now;
    refreshTFT();
  }
  delay(1);
}
