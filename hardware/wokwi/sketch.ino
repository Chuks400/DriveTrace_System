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
// Wokwi-only visible wheel rotation indicator; not bicycle propulsion.
constexpr int STEPPER_STEP = 38;
constexpr int STEPPER_DIR = 39;
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
uint32_t nextMotorStepUs = 0, motorStepReleaseUs = 0;
bool motorStepHigh = false;
float animatedCarX = 12.0f, animatedWheelPhase = 0.0f;
uint32_t lastCarAdvanceMs = 0;
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
    if (pulseLow) digitalWrite(WHEEL_PULSE_OUT, HIGH);
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

void driveWheelMotor() {
  // Wokwi A4988 pulses rotate the wheel indicator, not a physical vehicle.
  uint32_t now = micros();
  if (motorStepHigh && (int32_t)(now - motorStepReleaseUs) >= 0) {
    digitalWrite(STEPPER_STEP, LOW);
    motorStepHigh = false;
  }
  if (virtualTargetKmh <= 0) {
    if (!motorStepHigh) nextMotorStepUs = now;
    return;
  }
  if (!motorStepHigh && (int32_t)(now - nextMotorStepUs) >= 0) {
    digitalWrite(STEPPER_STEP, HIGH);
    motorStepHigh = true;
    motorStepReleaseUs = now + 120;
    uint32_t periodUs = (uint32_t)(200000.0f / max(1.0f, virtualTargetKmh));
    nextMotorStepUs = now + max((uint32_t)1800, periodUs);
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

void textOnTFT(int x, int y, const String &value, int size, uint16_t color,
               uint16_t background = ILI9341_NAVY) {
  tft.setTextSize(size);
  tft.setTextColor(color, background);
  tft.setCursor(x, y);
  tft.print(value);
}

// WOKWI_PERF_V2: static background, partial redraw and less busy looping.
void setupTFT() {
  // Wokwi's ILI9341 board is shown in portrait orientation, 240 x 320.
  tft.begin();
  tft.setRotation(0);
  tft.fillScreen(ILI9341_NAVY);
  tft.fillRect(0, 0, 240, 28, ILI9341_BLUE);
  textOnTFT(8, 7, "DriveTrace", 2, ILI9341_WHITE, ILI9341_BLUE);
  textOnTFT(5, 110, "SIMULATED E-BIKE / CAR", 1, ILI9341_CYAN);
  textOnTFT(166, 58, "km/h", 2, ILI9341_WHITE);
  textOnTFT(7, 91, "LIMIT " + String(SPEED_LIMIT_KMH, 0) + " km/h", 1, ILI9341_YELLOW);
  tft.fillRect(0, 135, 240, 41, ILI9341_CYAN);
  tft.fillRect(0, 176, 240, 53, ILI9341_DARKGREY);
  for (int x = 0; x < 240; x += 52) tft.fillRect(x, 211, 26, 3, ILI9341_YELLOW);
}

void drawVehicle() {
  static int previousX = -1;
  bool driving = measuredKmh > 0.5f;
  uint32_t nowMs = millis();
  if (lastCarAdvanceMs == 0) lastCarAdvanceMs = nowMs;
  uint32_t elapsed = min((uint32_t)1000, nowMs - lastCarAdvanceMs);
  lastCarAdvanceMs = nowMs;
  // The TFT car advances only when wheel pulses yield a measured speed.
  if (driving) {
    animatedCarX += measuredKmh * elapsed / 1600.0f;
    if (animatedCarX > 150.0f) animatedCarX = 8.0f;
    animatedWheelPhase += measuredKmh * elapsed / 2200.0f;
  }
  int x = (int)animatedCarX;
  // Erase only the previous car footprint, preserving the static road and sky.
  if (previousX >= 0) {
    tft.fillRect(previousX, 160, 82, 16, ILI9341_CYAN);
    tft.fillRect(previousX, 176, 82, 36, ILI9341_DARKGREY);
  }
  previousX = x;
  // Vehicle body and wheels share the same x offset.
  tft.fillRoundRect(x, 174, 79, 20, 4, ILI9341_BLUE);
  tft.fillRoundRect(x + 16, 161, 43, 17, 4, ILI9341_BLUE);
  tft.fillRect(x + 21, 164, 15, 11, ILI9341_CYAN);
  tft.fillRect(x + 39, 164, 14, 11, ILI9341_CYAN);
  tft.fillRect(x + 71, 179, 6, 6, ILI9341_YELLOW);
  for (int wheel = 0; wheel < 2; ++wheel) {
    int cx = x + (wheel == 0 ? 18 : 61);
    int cy = 193;
    tft.fillCircle(cx, cy, 10, ILI9341_BLACK);
    tft.fillCircle(cx, cy, 6, ILI9341_WHITE);
    float a = animatedWheelPhase;
    int dx = (int)(5.0f * cosf(a));
    int dy = (int)(5.0f * sinf(a));
    tft.drawLine(cx - dx, cy - dy, cx + dx, cy + dy, ILI9341_DARKGREY);
    tft.drawLine(cx + dy, cy - dx, cx - dy, cy + dx, ILI9341_DARKGREY);
    tft.fillCircle(cx, cy, 2, ILI9341_BLACK);
  }
  textOnTFT(6, 220, driving ? "MOVING" : "STOPPED", 1,
            driving ? ILI9341_GREEN : ILI9341_WHITE, ILI9341_DARKGREY);
}

void refreshTFT() {
  // Redraw readings only when changed; the moving car redraws its own footprint.
  static int speed10 = -1, peak10 = -1, pre10 = -1, statusOld = -1;
  static bool previousMoving = false, carDrawn = false;
  int speedNow = (int)roundf(measuredKmh * 10.0f);
  int peakNow = (int)roundf(peakKmh * 10.0f);
  int preNow = (int)roundf(preStopKmh * 10.0f);
  int status = overspeedSeen ? 2 : recording ? 1 : 0;
  bool moving = measuredKmh > 0.5f;
  if (speedNow != speed10) {
    tft.fillRect(7, 37, 155, 49, ILI9341_NAVY);
    textOnTFT(9, 39, String(measuredKmh, 1), 4, ILI9341_WHITE);
    speed10 = speedNow;
  }
  if (peakNow != peak10) {
    tft.fillRect(7, 233, 230, 21, ILI9341_NAVY);
    textOnTFT(7, 235, "PEAK " + String(peakKmh, 1) + " km/h", 2, ILI9341_WHITE);
    peak10 = peakNow;
  }
  if (preNow != pre10) {
    tft.fillRect(7, 257, 230, 22, ILI9341_NAVY);
    textOnTFT(7, 258, "PRE-STOP " + String(preStopKmh, 1), 2, ILI9341_CYAN);
    pre10 = preNow;
  }
  if (moving || moving != previousMoving || !carDrawn) {
    drawVehicle();
    if (moving != previousMoving || !carDrawn) {
      tft.fillRect(0, 217, 240, 12, ILI9341_DARKGREY);
      textOnTFT(6, 220, moving ? "MOVING" : "STOPPED", 1,
                moving ? ILI9341_GREEN : ILI9341_WHITE, ILI9341_DARKGREY);
    }
    previousMoving = moving;
    carDrawn = true;
  }
  if (status != statusOld) {
    uint16_t bg = status == 2 ? ILI9341_RED : status == 1 ? ILI9341_DARKGREEN : ILI9341_DARKGREY;
    tft.fillRect(0, 287, 240, 33, bg);
    textOnTFT(7, 294, status == 2 ? "OVER SPEED!" : status == 1 ? "RECORDING" : "STANDBY", 2, ILI9341_WHITE, bg);
    statusOld = status;
  }
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
  pinMode(STEPPER_STEP, OUTPUT); digitalWrite(STEPPER_STEP, LOW);
  pinMode(STEPPER_DIR, OUTPUT); digitalWrite(STEPPER_DIR, HIGH);
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B); Wire.write(0);
  imuReady = (Wire.endTransmission() == 0);
  rtcReady = rtc.begin();
  if (rtcReady && !rtc.isrunning()) rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
  pinMode(TFT_CS, OUTPUT); digitalWrite(TFT_CS, HIGH);
  pinMode(SD_CS, OUTPUT); digitalWrite(SD_CS, HIGH);
  setupTFT();
  sdReady = SD.begin(SD_CS, SPI, 10000000);
  writeHeaderIfNeeded("/RIDE.CSV",
     "timestamp_utc,speed_kmh,source,gps_speed_kmh,wheel_pulses,imu_ax_g,imu_ay_g,imu_az_g");
  writeHeaderIfNeeded("/STOPS.CSV",
     "timestamp_utc,speed_before_stop_kmh,peak_speed_kmh,overspeed");
  refreshTFT();
  Serial.println("DriveTrace Wokwi ESP32-S3 virtual hardware started");
  Serial.println("ACCEL +10 | BRAKE -10 | STOP | AUTO DEMO buttons on the circuit");
  Serial.println("Wheel GPIO6 -> GPIO4: virtual Hall-compatible magnetic pulses");
  Serial.println("Wokwi wheel indicator motor: STEP GPIO38, DIR GPIO39");
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
  driveWheelMotor();
  uint32_t now = millis();
  static uint32_t lastWheelReadMs = 0;
  if (now - lastWheelReadMs >= 40) {
    lastWheelReadMs = now;
    readWheelSpeed();
  }
  if (now - lastLogMs >= 1000) {
    lastLogMs = now;
    readImu();
    logSample();
  }
  if (now - lastSerialMs >= 2000) {
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
  if (now - lastScreenMs >= (virtualTargetKmh > 0 ? 350U : 1100U)) {
    lastScreenMs = now;
    refreshTFT();
  }
  // Wokwi recommends sleeping between loop iterations to reduce emulator load.
  delay(5);
}
