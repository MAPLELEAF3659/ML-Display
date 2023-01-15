#include <TFT_eSPI.h> // Graphics and font library for ST7735 driver chip
#include "Fonts/Custom/Silver_16.h"
#include <SPI.h>
#include <WiFi.h>
#include "time.h"
#include "DHTesp.h"
#include <Ticker.h>

//**WiFi**
const char* ssid       = "ML-WiFi";
const char* password   = "95089608";
//**WiFi**

//**NTP**
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 28800; //GMT+8
const int   daylightOffset_sec = 0;
struct tm timeinfo;
byte oldSec, oldMin, oldDay;
//**NTP**

//**TFT**
TFT_eSPI tft = TFT_eSPI();  // Invoke library, pins defined in User_Setup.h
//GND=GND VCC=3V3 SCL=G14 SDA=G15 RES=G33 DC=G27 CS=G5 BL，=G22
byte xpos;
byte ypos;
/*
  A few colour codes:

  code  color
  0x0000  Black
  0xFFFF  White
  0xBDF7  Light Gray
  0x7BEF  Dark Gray
  0xF800  Red
  0xFFE0  Yellow
  0xFBE0  Orange
  0x79E0  Brown
  0x7E0 Green
  0x7FF Cyan
  0x1F  Blue
  0xF81F  Pink

*/
//**TFT**

//**DHT**
DHTesp dht;
void tempTask(void *pvParameters);
bool getTemperature();
void triggerGetTemp();
/** Task handle for the light value read task */
TaskHandle_t tempTaskHandle = NULL;
/** Ticker for temperature reading */
Ticker tempTicker;
/** Comfort profile */
ComfortState cf;
/** Flag if task should run */
bool tasksEnabled = false;
/** Pin number for DHT11 data pin */
int dhtPin = 17; //VCC=5V
float temperature = 0;
float humidity = 0;
//**DHT**

//**Serial**
String serialRXData = "";
String oldSerialRXData = "";
String artistName = "";
String songName = "";
//**Serial**

void setup() {
  Serial.begin(115200);

  tft.init();
  tft.setRotation(-1);
  tft.fillScreen(TFT_BLACK);

  tft.setTextColor(TFT_YELLOW, TFT_BLACK); // Note: the new fonts do not draw the background colour
  ClearScreen(0, 30, 5);

  tft.setCursor(5, 5);
  tft.println("[WiFi]Connecting to:" );
  tft.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    tft.print(".");
  }
  tft.println("Connected!");

  tft.println("[NTP server]Setting up..." );
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  tft.println("[NTP server]Done." );

  tft.println("[DHT11]Setting up..." );
  initTemp();
  tasksEnabled = true;

  tft.println("[System]Welcome!" );
  delay(3000);
  ClearScreen(0, 30, 5);
}


void loop() {
  while (Serial.available()) {
    serialRXData += Serial.readString();
  }
  if (serialRXData != "") {
    artistName = serialRXData.substring(0, serialRXData.indexOf('$'));
    songName = serialRXData.substring(serialRXData.indexOf('$') + 1, serialRXData.length() - 1);
  }

  getLocalTime(&timeinfo);
  xpos = 10;
  ypos = 20;
  if (timeinfo.tm_min != oldMin) {
    tft.setTextColor(0x39C4, TFT_BLACK);
    tft.drawString("88 88", xpos, ypos, 7);
    tft.setTextColor(0xFFFF);
    if (timeinfo.tm_hour < 10) xpos += tft.drawChar('0', xpos, ypos, 7);
    xpos += tft.drawNumber(timeinfo.tm_hour, xpos, ypos, 7);
    xpos += tft.drawChar(' ', xpos, ypos, 7);
    if (timeinfo.tm_min < 10) xpos += tft.drawChar('0', xpos, ypos, 7);
    xpos += tft.drawNumber(timeinfo.tm_min, xpos, ypos, 7);
    oldMin = timeinfo.tm_min;
  }
  if (timeinfo.tm_sec != oldSec) {
    tft.setTextColor(0x39C4, TFT_BLACK);
    tft.drawString(":", 74, ypos, 7);
    tft.setTextColor(0xFFFF);
    tft.drawChar(timeinfo.tm_sec % 2 == 0 ? ':' : ' ', 74, ypos, 7);
    tftPrintTempOrMediaInfo(artistName == "");
    oldSec = timeinfo.tm_sec;
  }
  if (timeinfo.tm_mday != oldDay) {
    tftPrintDate();
    oldDay = timeinfo.tm_mday;
  }
}
void tftPrintDate() {
  tft.setTextColor(0xFFFF, TFT_BLACK);
  String dayOfWeekStr;
  switch (timeinfo.tm_wday) {
    case 0:
      dayOfWeekStr = "SUN.";
      break;
    case 1:
      dayOfWeekStr = "MON.";
      break;
    case 2:
      dayOfWeekStr = "TUE.";
      break;
    case 3:
      dayOfWeekStr = "WED.";
      break;
    case 4:
      dayOfWeekStr = "THU.";
      break;
    case 5:
      dayOfWeekStr = "FRI.";
      break;
    case 6:
      dayOfWeekStr = "SAT.";
      break;
  }
  tft.drawString("    " + String(timeinfo.tm_year + 1900) + "/" +
                 (timeinfo.tm_mon < 9 ? "0" : "") + String(timeinfo.tm_mon + 1) + "/" +
                 (timeinfo.tm_mday < 10 ? "0" : "") + String(timeinfo.tm_mday) + " " + dayOfWeekStr + "  ", 0, ypos + 58, 2);
}

void tftPrintTempOrMediaInfo(bool isTemp) {
  tft.setTextColor(0xFFFF, TFT_BLACK);
  if (isTemp) {
    if (serialRXData != "" && serialRXData != oldSerialRXData) {
      tft.drawString("                              ", 0, 95, 2);
      tft.drawString("                              ", 0, 105, 2);
      tft.drawString("                              ", 0, 115, 2);
      oldSerialRXData = serialRXData;
      serialRXData = "";
    }
    tft.drawString((temperature < 10 ? "0" : "") + String(temperature, 1) + "C | "
                   + (humidity < 10 ? "0" : "") + String(humidity, 1) + "%", 35, 105, 2);
  } else {
    if (serialRXData != "" && serialRXData != oldSerialRXData) {
      //clear screen
      tft.drawString("                              ", 0, 95, 2);
      tft.drawString("                              ", 0, 105, 2);
      tft.drawString("                              ", 0, 115, 2);
      //write han character
      tft.loadFont(Silver_16);
      tft.drawString(artistName, 10, 100);
      char artistNameArr[artistName.length()];
      artistName.toCharArray(artistNameArr, artistName.length());
      tft.drawString((utf8len(artistNameArr) > 20 ?
                      artistName.substring(0, artistName.indexOf(utf8index(artistNameArr, 17))) + "..." :
                      artistName), 10, 100);
      char songNameArr[songName.length()];
      songName.toCharArray(songNameArr, songName.length());
      tft.drawString((utf8len(songNameArr) > 20 ?
                      songName.substring(0, songName.indexOf(utf8index(songNameArr, 17))) + "..." :
                      songName), 10, 115);
      tft.unloadFont();
      oldSerialRXData = serialRXData;
      serialRXData = "";
    }
  }
}

size_t utf8len(char *s)
{
  size_t len = 0;
  for (; *s; ++s) if ((*s & 0xC0) != 0x80) ++len;
  return len;
}

char *utf8index(char *s, size_t pos)
{
  ++pos;
  for (; *s; ++s) {
    if ((*s & 0xC0) != 0x80) --pos;
    if (pos == 0) return s;
  }
  return NULL;
}

/**
   initTemp
   Setup DHT library
   Setup task and timer for repeated measurement
   @return bool
      true if task and timer are started
      false if task or timer couldn't be started
*/
bool initTemp() {
  byte resultValue = 0;
  // Initialize temperature sensor
  dht.setup(dhtPin, DHTesp::DHT11);
  tft.println("[DHT11]Initiated.");

  // Start task to get temperature
  xTaskCreatePinnedToCore(
    tempTask,                       /* Function to implement the task */
    "tempTask ",                    /* Name of the task */
    4000,                           /* Stack size in words */
    NULL,                           /* Task input parameter */
    5,                              /* Priority of the task */
    &tempTaskHandle,                /* Task handle. */
    1);                             /* Core where the task should run */

  if (tempTaskHandle == NULL) {
    tft.println("[DHT11]Failed to start task for temperature update");
    return false;
  } else {
    // Start update of environment data every XX seconds
    tempTicker.attach(5, triggerGetTemp);
  }
  return true;
}

/**
   triggerGetTemp
   Sets flag dhtUpdated to true for handling in loop()
   called by Ticker getTempTimer
*/
void triggerGetTemp() {
  if (tempTaskHandle != NULL) {
    xTaskResumeFromISR(tempTaskHandle);
  }
}

/**
   Task to reads temperature from DHT11 sensor
   @param pvParameters
      pointer to task parameters
*/
void tempTask(void *pvParameters) {
  tft.println("[DHT11]Starts to get value.");
  while (1) // tempTask loop
  {
    if (tasksEnabled) {
      // Get temperature values
      getTemperature();
    }
    // Got sleep again
    vTaskSuspend(NULL);
  }
}

/**
   getTemperature
   Reads temperature from DHT11 sensor
   @return bool
      true if temperature could be aquired
      false if aquisition failed
*/
bool getTemperature() {
  // Reading temperature for humidity takes about 250 milliseconds!
  // Sensor readings may also be up to 2 seconds 'old' (it's a very slow sensor)
  TempAndHumidity newValues = dht.getTempAndHumidity();
  // Check if any reads failed and exit early (to try again).
  if (dht.getStatus() != 0) {
    tft.println("[DHT11]Error status: " + String(dht.getStatusString()));
    return false;
  }

  float heatIndex = dht.computeHeatIndex(newValues.temperature, newValues.humidity);
  temperature = newValues.temperature;
  humidity = newValues.humidity;
  return true;
}

void ClearScreen(int startPoint, int endPoint, int perUnit) {
  //  tft.setTextSize(20);
  for (int i = startPoint ; i < endPoint; i++) {
    tft.setCursor (0, i * perUnit);
    tft.println("                                   ");
  }
  tft.setCursor(0, 0);
}
