/*
 * DriveTrace Phase 4 - STANDALONE ESP32-S3 prototype
 * Hall wheel speed + MPU6050 + NEO-M8N GPS + DS3231 RTC + ILI9341 TFT + microSD.
 *
 * Libraries: ESP32 Arduino core, Adafruit GFX Library, Adafruit ILI9341.
 * Uses built-in Wire, SPI, SD.
 *
 * Example pins only: REVIEW YOUR BOARD PINOUT AND VOLTAGE LEVELS before wiring.
 * SD data is unverified until authenticated device-side signatures are added.
 * Example firmware has NOT been compiled on / tested with physical hardware.
 * Only use after safe bench checks, calibration and a controlled test area.
 */
#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <math.h>
#include <time.h>

// Example pins — S3 board pinout varies.
static constexpr int HALL_PIN = 4;
static constexpr int I2C_SDA = 8, I2C_SCL = 9;
static constexpr int GPS_RX = 16, GPS_TX = 17;
static constexpr int SPI_SCK = 12, SPI_MISO = 13, SPI_MOSI = 11;
static constexpr int TFT_CS = 10, TFT_DC = 14, TFT_RST = 15;
static constexpr int SD_CS = 5;
// MPU6050 is 0x68 by default; DS3231 is 0x68 too! DO NOT place both on
// the same bus without changing MPU6050 AD0 high to address 0x69.
static constexpr uint8_t MPU_ADDR = 0x69;
static constexpr uint8_t DS3231_ADDR = 0x68;

// Calibrate! Example 0.21 m = small model wheel, NOT a typical bicycle wheel.
static constexpr float WHEEL_CIRCUMFERENCE_M = 0.210f;
static constexpr uint32_t STOP_TIMEOUT_US = 3000000;
static constexpr uint32_t DEBOUNCE_US = 3000;
static constexpr uint32_t SAMPLE_MS = 200, LOG_MS = 1000;

Adafruit_ILI9341 tft(TFT_CS,TFT_DC,TFT_RST);
volatile uint32_t hallLastUs = 0, hallPeriodUs = 0, hallPulses = 0;
uint32_t lastSample=0, lastLog=0, stationarySince=0;
bool sdReady=false, imuReady=false, rideOpen=false;
char ridePath[28]={0};
float speedKmh=0, lastPreStop=0, peakRecent=0, gpsSpeedKmh=0;
float ax=0, ay=0, az=0;
bool imuValid=false, gpsValid=false, overLimit=false;
static constexpr float SPEED_LIMIT_KMH=50.0f;
static constexpr int WINDOW_SAMPLES=150; // 5 samples/s x 30 s
float recent[WINDOW_SAMPLES]={0};
int recentIndex=0, recentCount=0;
uint32_t lastGpsMs=0;
char gpsLine[128]={0}; size_t gpsLen=0;

void IRAM_ATTR pulseISR() {
  uint32_t now=micros(), delta=now-hallLastUs;
  if(hallLastUs && delta < DEBOUNCE_US)return;
  if(hallLastUs)hallPeriodUs=delta;
  hallLastUs=now; hallPulses++;
}

bool writeI2C(uint8_t addr,uint8_t reg,uint8_t val){
  Wire.beginTransmission(addr);Wire.write(reg);Wire.write(val);
  return Wire.endTransmission()==0;
}
bool readAccel(){
  Wire.beginTransmission(MPU_ADDR);Wire.write(0x3B);
  if(Wire.endTransmission(false)!=0)return false;
  if(Wire.requestFrom(int(MPU_ADDR),6,true)!=6)return false;
  int16_t x=(int16_t)((Wire.read()<<8)|Wire.read());
  int16_t y=(int16_t)((Wire.read()<<8)|Wire.read());
  int16_t z=(int16_t)((Wire.read()<<8)|Wire.read());
  ax=x/16384.0f; ay=y/16384.0f; az=z/16384.0f;
  return true;
}
uint8_t bcd(uint8_t x){return (x>>4)*10+(x&0x0f);}
bool rtcUtc(char* out,size_t cap){
  Wire.beginTransmission(DS3231_ADDR);Wire.write(0x00);
  if(Wire.endTransmission(false)!=0)return false;
  if(Wire.requestFrom(int(DS3231_ADDR),7,true)!=7)return false;
  uint8_t sec=bcd(Wire.read()&0x7f),min=bcd(Wire.read()&0x7f);
  uint8_t hour=bcd(Wire.read()&0x3f);
  Wire.read(); // weekday
  uint8_t day=bcd(Wire.read()&0x3f),month=bcd(Wire.read()&0x1f);
  uint16_t year=2000+bcd(Wire.read());
  if(year<2020||year>2099||month<1||month>12||day<1||day>31||hour>23||min>59||sec>59)return false;
  snprintf(out,cap,"%04u-%02u-%02uT%02u:%02u:%02u+00:00",year,month,day,hour,min,sec);
  return true;
}

// Read NMEA $GPRMC/$GNRMC GPS speed with checksum validation.
int hexVal(char c){if(c>='0'&&c<='9')return c-'0';if(c>='A'&&c<='F')return c-'A'+10;if(c>='a'&&c<='f')return c-'a'+10;return -1;}
const char* fieldAt(const char* str,int n){const char*p=str;while(n--){p=strchr(p,',');if(!p)return nullptr;p++;}return p;}
void parseGpsRmc(const char* str){
  if(strncmp(str,"$GNRMC,",7)!=0&&strncmp(str,"$GPRMC,",7)!=0)return;
  const char* star=strchr(str,'*');if(!star||!star[1]||!star[2])return;
  int h=hexVal(star[1]),l=hexVal(star[2]);if(h<0||l<0)return;
  uint8_t check=0;for(const char* p=str+1;p<star;p++)check^=uint8_t(*p);
  if(check!=uint8_t((h<<4)|l))return;
  const char* fix=fieldAt(str,2);const char* sp=fieldAt(str,7);
  if(!fix||*fix!='A'||!sp||*sp==',')return;
  char* end=nullptr;float knots=strtof(sp,&end);
  if(end==sp||!end||*end!=','||!isfinite(knots)||knots<0||knots>135)return;
  gpsSpeedKmh=knots*1.852f;lastGpsMs=millis();
}
void pollGps(){
  for(int i=0;i<192&&Serial1.available();i++){
    char c=(char)Serial1.read();
    if(c=='$'){gpsLen=0;gpsLine[gpsLen++]=c;}
    else if(c=='\n'){if(gpsLen){gpsLine[gpsLen]=0;parseGpsRmc(gpsLine);}gpsLen=0;}
    else if(gpsLen>0&&gpsLen<sizeof(gpsLine)-1&&c!='\r')gpsLine[gpsLen++]=c;
    else if(gpsLen>=sizeof(gpsLine)-1)gpsLen=0;
  }
  gpsValid=lastGpsMs && (millis()-lastGpsMs<3000);
}

void textAt(int x,int y,const char*txt,int size,uint16_t color){
  tft.setTextSize(size);tft.setTextColor(color,ILI9341_NAVY);tft.setCursor(x,y);tft.print(txt);
}
void drawScreen(){
  tft.fillScreen(ILI9341_NAVY);
  tft.fillRect(0,0,320,36,ILI9341_BLUE);
  textAt(12,11,"DriveTrace",2,ILI9341_WHITE);
  textAt(216,11,sdReady?"SD READY":"SD ERROR",1,sdReady?ILI9341_GREEN:ILI9341_YELLOW);
  textAt(14,49,"CURRENT SPEED",1,ILI9341_CYAN);
  tft.setTextColor(ILI9341_WHITE,ILI9341_NAVY);tft.setTextSize(5);tft.setCursor(16,65);tft.print(speedKmh,1);
  textAt(228,103,"km/h",2,ILI9341_LIGHTGREY);
  char info[36];
  snprintf(info,sizeof(info),"PEAK %.1f / LIMIT %.0f",peakRecent,SPEED_LIMIT_KMH);
  textAt(14,142,info,2,ILI9341_WHITE);
  snprintf(info,sizeof(info),"PRE-STOP %.1f km/h",lastPreStop);
  textAt(14,180,info,2,ILI9341_CYAN);
  tft.fillRect(0,214,320,26,overLimit?ILI9341_RED:ILI9341_DARKGREEN);
  tft.setTextColor(ILI9341_WHITE,overLimit?ILI9341_RED:ILI9341_DARKGREEN);
  tft.setTextSize(2);tft.setCursor(12,219);tft.print(overLimit?"OVERSPEED!":rideOpen?"RECORDING RIDE":"STANDBY");
}

bool newRideFile(){
  if(!sdReady)return false;
  for(int n=1;n<=999;n++){
    snprintf(ridePath,sizeof(ridePath),"/RIDE%03d.CSV",n);
    if(SD.exists(ridePath))continue;
    File f=SD.open(ridePath,FILE_WRITE);
    if(!f)return false;
    f.println("timestamp_utc,speed_kmh,source,gps_speed_kmh,wheel_pulses,imu_ax_g,imu_ay_g,imu_az_g");
    f.close();return true;
  }
  return false;
}
void logRide(){
  if(!rideOpen||!sdReady)return;
  char stamp[40];
  if(!rtcUtc(stamp,sizeof(stamp))){Serial.println("RTC_NOT_SET - not logging");return;}
  File f=SD.open(ridePath,FILE_APPEND);
  if(!f){sdReady=false;return;}
  f.print(stamp);f.print(',');f.print(speedKmh,2);f.print(",HARDWARE,");
  if(gpsValid)f.print(gpsSpeedKmh,2);
  f.print(',');
  noInterrupts();uint32_t pulses=hallPulses;interrupts();
  f.print(pulses);f.print(',');
  if(imuValid)f.print(ax,3);f.print(',');
  if(imuValid)f.print(ay,3);f.print(',');
  if(imuValid)f.print(az,3);
  f.println();f.flush();f.close();
}
void setup(){
  Serial.begin(115200);delay(250);
  pinMode(HALL_PIN,INPUT_PULLUP);attachInterrupt(digitalPinToInterrupt(HALL_PIN),pulseISR,FALLING);
  Wire.begin(I2C_SDA,I2C_SCL);
  // IMPORTANT: MPU6050 AD0 pin MUST be HIGH for address 0x69;
  // DS3231 remains at 0x68. Verify module I2C levels are 3.3V safe.
  imuReady=writeI2C(MPU_ADDR,0x6B,0x00);
  Serial1.begin(9600,SERIAL_8N1,GPS_RX,GPS_TX);
  SPI.begin(SPI_SCK,SPI_MISO,SPI_MOSI);
  pinMode(SD_CS,OUTPUT);digitalWrite(SD_CS,HIGH);
  pinMode(TFT_CS,OUTPUT);digitalWrite(TFT_CS,HIGH);
  tft.begin();tft.setRotation(1);
  sdReady=SD.begin(SD_CS,SPI,10000000);
  drawScreen();
  Serial.println("DriveTrace standalone bench prototype. Set RTC to UTC before riding.");
}
void loop(){
  pollGps();uint32_t now=millis();
  if(now-lastSample<SAMPLE_MS)return;
  lastSample=now;
  uint32_t prev,last,count;
  noInterrupts();prev=hallPeriodUs;last=hallLastUs;count=hallPulses;interrupts();
  uint32_t age=micros()-last;
  speedKmh=(last>0&&age<STOP_TIMEOUT_US&&prev>0)?(WHEEL_CIRCUMFERENCE_M*3600000.0f/float(prev)):0;
  if(!isfinite(speedKmh)||speedKmh<0||speedKmh>250)speedKmh=0;
  imuValid=imuReady&&readAccel();
  recent[recentIndex]=speedKmh;recentIndex=(recentIndex+1)%WINDOW_SAMPLES;
  if(recentCount<WINDOW_SAMPLES)recentCount++;
  peakRecent=0;
  for(int i=0;i<recentCount;i++)peakRecent=max(peakRecent,recent[i]);
  overLimit=peakRecent>SPEED_LIMIT_KMH;
  if(speedKmh>0.5){
    stationarySince=0;lastPreStop=speedKmh;
    if(!rideOpen){char stamp[40];if(rtcUtc(stamp,sizeof(stamp))&&newRideFile())rideOpen=true;}
  } else if(rideOpen){
    if(stationarySince==0)stationarySince=now;
    if(now-stationarySince>4000){logRide();rideOpen=false;stationarySince=0;}
  }
  if(now-lastLog>=LOG_MS){lastLog=now;logRide();}
  drawScreen();
  // Existing USB sketch supplies the real serial JSON protocol for the PC.
  // The standalone firmware's CSV is transferred via microSD after the ride.
}
