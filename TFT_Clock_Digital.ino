#include <TFT_eSPI.h> // Graphics and font library for ST7735 driver chip
#include "Fonts/Custom/Silver_16.h"
#include <SPI.h>
#include <WiFi.h>
#include "time.h"
#include "DHTesp.h"
#include <Ticker.h>
#include <HTTPClient.h>
#include <Arduino_JSON.h>

/*
**Upload settings**
Board: ESP32 Dev Module
Partition Scheme: Huge APP
Flash Size: 4MB
Upload Speed: 921600
*/

//**WiFi**
const char *ssid = "ML-WiFi";
const char *password = "95089608";
//**WiFi**

//**NTP**
const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 28800; // GMT+8
const int daylightOffset_sec = 0;
struct tm timeinfo;
byte secPrevious, minPrevious, dayPrevious;
//**NTP**

//**TFT**
TFT_eSPI tft = TFT_eSPI(); // Invoke library, pins defined in User_Setup.h
// GND=GND VCC=3V3 SCL=G14 SDA=G15 RES=G33 DC=G27 CS=G5 BL，=G22
byte xpos = 10;
byte ypos = 10;
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
// Pin number for DHT11 data pin
int dhtPin = 17; // VCC=5V
// Update duration
int dhtUpdateDuration = 5;
void DHTTask(void *pvParameters);
bool getTemperature();
void TriggerGetDHT();
// Task handle for the light value read task
TaskHandle_t dhtTaskHandle = NULL;
// Ticker for temperature reading
Ticker dhtTicker;
// Comfort profile
ComfortState cf;
// Flag if task should run
bool tasksEnabled = false;
// DHT info
float temperature = 0;
float humidity = 0;
float temperaturePrevious = -1;
float humidityPrevious = -1;
//**DHT**

//**Player API**
const char *getPlayerInfoQuery = "http://192.168.0.100:8880/api/query?player=true&trcolumns=%25artist%25,%25title%25,%25album%25";
String apiResponse = "";
bool isPlayerApiAvailable = false;
//**Player API**

//**Player info**
String artistName = "";
String songName = "";
String albumName = "";
bool isPlayerPlaying = false;
bool isPlayerPlayingPrevious = false;
int playerPlayingIndex = -1;
int playerPlayingIndexPrevious = -1;
//**Player info**

int screenState = 0; // 0 = main, 1 = player
int screenStatePrevious = -1;

void setup()
{
  Serial.begin(115200);

  tft.init();
  tft.setRotation(-1);
  tft.fillScreen(TFT_BLACK);

  ClearScreen(0, 160, 5);

  tft.setTextColor(TFT_YELLOW, TFT_BLACK); // Note: the new fonts do not draw the background colour
  // connect to wifi
  tft.setCursor(0, 5);
  tft.println("[WiFi]Connecting to: ");
  tft.print("[WiFi]" + String(ssid));
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    tft.print(".");
  }
  tft.println();
  tft.println("[WiFi]Connected!");

  // setup ntp server
  tft.println("[NTP server]Setting up...");
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  tft.println("[NTP server]Done.");

  // setup dht11
  tft.println("[DHT11]Setting up...");
  initTemp();
  tasksEnabled = true;
  tft.println("[DHT11]Done.");

  // setup complete
  tft.println("[System]Welcome!");
  delay(5000);
  ClearScreen(0, 160, 5);
}

void loop()
{
  // get time info
  getLocalTime(&timeinfo);

  // update by sec
  if (timeinfo.tm_sec != secPrevious)
  {
    // try to get player info by api
    apiResponse = HttpGETRequest(getPlayerInfoQuery);
    if (apiResponse != "{}")
    {
      JSONVar jsonObj = JSON.parse(apiResponse);
      isPlayerPlaying = JSON.stringify(jsonObj["player"]["playbackState"]) != "\"stopped\"";
      if (isPlayerPlaying)
      {
        playerPlayingIndex = jsonObj["player"]["activeItem"]["index"];
        artistName = JSON.stringify(jsonObj["player"]["activeItem"]["columns"][0]);
        artistName = artistName.substring(1, artistName.length() - 1);
        songName = JSON.stringify(jsonObj["player"]["activeItem"]["columns"][1]);
        songName = songName.substring(1, songName.length() - 1);
        albumName = JSON.stringify(jsonObj["player"]["activeItem"]["columns"][2]);
        albumName = albumName.substring(1, albumName.length() - 1);
      }
      isPlayerApiAvailable = true;
    }
    else
    {
      isPlayerApiAvailable = false;
      playerPlayingIndex = -1;
    }
  }

  // change screenState
  if (isPlayerApiAvailable)
  {
    screenState = 1;
  }
  else
  {
    screenState = 0;
  }

  // force clear screen and previous states when screenState changed
  if (screenStatePrevious != screenState)
  {
    ClearScreen(0, 160, 5);
    dayPrevious = -1;
    minPrevious = -1;
    secPrevious = -1;

    switch (screenState)
    {
    case 0: // main
      tft.setTextColor(0xFFFF, TFT_BLACK);
      // write temperature title
      tft.drawString("Temperature " + String(temperature < 10 ? "0" : ""), xpos + 10, ypos + 80, 2);
      // write humidity title
      tft.drawString("Humidity     " + String(humidity < 10 ? "0" : ""), xpos + 10, ypos + 100, 2);

      // write temperature
      tft.setTextColor(TextColorByTemperature(temperature), TFT_BLACK);
      tft.drawString(String(temperature, 1) + "C", xpos + 95, ypos + 80, 2);
      // write humidity
      tft.setTextColor(TextColorByHumidity(humidity), TFT_BLACK);
      tft.drawString(String(humidity, 1) + "%", xpos + 95, ypos + 100, 2);
      break;
    case 1: // player
      TFTPrintPlayerInfo();
      break;
    }

    screenStatePrevious = screenState;
  }

  switch (screenState)
  {
  case 0: // main
    ShowMainScreen();
    break;
  case 1: // player
    ShowPlayerScreen();
    break;
  }
}

void ShowMainScreen()
{
  // update by day
  if (timeinfo.tm_mday != dayPrevious)
  {
    TFTPrintDate();
    dayPrevious = timeinfo.tm_mday;
  }

  // update by min
  if (timeinfo.tm_min != minPrevious)
  {
    TFTPrintTime();
    // update previous state
    minPrevious = timeinfo.tm_min;
  }

  // update by sec
  if (timeinfo.tm_sec != secPrevious)
  {
    // print ":" (blink it)
    tft.setTextColor(0x39C4, TFT_BLACK);
    tft.drawString(":", 74, ypos, 7);
    tft.setTextColor(0xFFFF);
    tft.drawChar(timeinfo.tm_sec % 2 == 0 ? ':' : ' ', 74, ypos, 7);

    // print dht info
    TFTPrintDHTInfo();

    // update previous state
    secPrevious = timeinfo.tm_sec;
  }
}

void ShowPlayerScreen()
{
  // update by day
  if (timeinfo.tm_mday != dayPrevious)
  {
    TFTPrintDate();
    dayPrevious = timeinfo.tm_mday;
  }

  // update by min
  if (timeinfo.tm_min != minPrevious)
  {
    TFTPrintTime();
    // update previous state
    minPrevious = timeinfo.tm_min;
  }

  // update by sec
  if (timeinfo.tm_sec != secPrevious)
  {
    // check if player current song was updated
    if (playerPlayingIndexPrevious != playerPlayingIndex)
    {
      TFTPrintPlayerInfo();
      // update pervious state
      playerPlayingIndexPrevious = playerPlayingIndex;
    }
    
    // blink ":"
    tft.setTextColor(0xFFFF, TFT_BLACK);
    tft.drawChar(timeinfo.tm_sec % 2 == 0 ? ':' : ' ', 17, 5, 1);

    // update previous state
    secPrevious = timeinfo.tm_sec;
  }
}

void TFTPrintTime()
{
  int xposTime = xpos;
  int yposTime = ypos;
  if (screenState == 0)
  {
    // print time
    tft.setTextColor(0x39C4, TFT_BLACK);
    tft.drawString("88 88", xposTime, yposTime, 7);
    tft.setTextColor(0xFFFF);
    if (timeinfo.tm_hour < 10)
      xposTime += tft.drawChar('0', xposTime, yposTime, 7);
    xposTime += tft.drawNumber(timeinfo.tm_hour, xposTime, yposTime, 7);
    xposTime += tft.drawChar(' ', xposTime, yposTime, 7);
    if (timeinfo.tm_min < 10)
      xposTime += tft.drawChar('0', xposTime, yposTime, 7);
    xposTime += tft.drawNumber(timeinfo.tm_min, xposTime, yposTime, 7);
  }
  else
  {
    tft.setTextColor(0xFFFF, TFT_BLACK);
    tft.drawString((timeinfo.tm_hour < 10 ? "0" : "") + String(timeinfo.tm_hour) + " " +
                       (timeinfo.tm_min < 10 ? "0" : "") + String(timeinfo.tm_min),
                   1, 0, 2);
  }
}

void TFTPrintDate()
{
  tft.setTextColor(0xFFFF, TFT_BLACK);
  String dayOfWeekStr;
  switch (timeinfo.tm_wday)
  {
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
  if (screenState == 0)
  {
    tft.drawString("    " + String(timeinfo.tm_year + 1900) + "/" +
                       (timeinfo.tm_mon < 9 ? "0" : "") + String(timeinfo.tm_mon + 1) + "/" +
                       (timeinfo.tm_mday < 10 ? "0" : "") + String(timeinfo.tm_mday) + " " + dayOfWeekStr + "  ",
                   0, ypos + 58, 2);
  }
  else
  {
    tft.drawString(String(timeinfo.tm_year + 1900) + "/" +
                       (timeinfo.tm_mon < 9 ? "0" : "") + String(timeinfo.tm_mon + 1) + "/" +
                       (timeinfo.tm_mday < 10 ? "0" : "") + String(timeinfo.tm_mday) + " " + dayOfWeekStr,
                   49, 0, 2);
  }
}

void TFTPrintDHTInfo()
{
  // check if temperature or humidity was updated
  if (temperature != temperaturePrevious)
  {
    tft.setTextColor(TextColorByTemperature(temperature), TFT_BLACK);
    // write temperature
    tft.drawString(String(temperature, 1) + "C", xpos + 95, ypos + 80, 2);
    // update pervious state
    temperaturePrevious = temperature;
  }
  if (humidity != humidityPrevious)
  {
    tft.setTextColor(TextColorByHumidity(humidity), TFT_BLACK);
    // write humidity
    tft.drawString(String(humidity, 1) + "%", xpos + 95, ypos + 100, 2);
    // update pervious state
    humidityPrevious = humidity;
  }
}

int TextColorByTemperature(float temp)
{
  if (temp >= 31)
  {
    return 0xF800; // red
  }
  else if (temp < 31 && temp >= 27)
  {
    return 0xFC00; // orange
  }
  else if (temp < 27 && temp >= 24)
  {
    return 0xFFE0; // yellow
  }
  else if (temp < 24 && temp >= 20)
  {
    return 0x07E0; // green
  }
  else if (temp < 20 && temp >= 16)
  {
    return 0x07F0; // blue green
  }
  else if (temp < 16 && temp >= 12)
  {
    return 0x001F; // blue
  }
  else if (temp < 12)
  {
    return 0x07FF; // light blue
  }
}

int TextColorByHumidity(float humi)
{
  if (humi >= 75)
  {
    return 0xF800; // red
  }
  else if (humi < 75 && humi >= 50)
  {
    return 0xFC00; // orange
  }
  else if (humi < 50 && humi >= 25)
  {
    return 0x07E0; // green
  }
  else if (humi < 25)
  {
    return 0x001F; // blue
  }
}

void TFTPrintPlayerInfo()
{
  // clear screen
  ClearScreen(80, 160, 5);

  tft.setTextColor(isPlayerPlaying ? 0xFFFF : 0xF800, TFT_BLACK);

  // load han character
  tft.loadFont(Silver_16);

  // print artist name
  char artistNameArr[artistName.length()];
  artistName.toCharArray(artistNameArr, artistName.length());
  tft.drawString((utf8len(artistNameArr) > 20 ? artistName.substring(0, artistName.indexOf(utf8index(artistNameArr, 17))) + "..." : artistName), xpos, ypos + 80);

  // print album name
  char albumNameArr[albumName.length()];
  albumName.toCharArray(albumNameArr, albumName.length());
  tft.drawString((utf8len(albumNameArr) > 20 ? albumName.substring(0, albumName.indexOf(utf8index(albumNameArr, 17))) + "..." : albumName), xpos, ypos + 95);

  // print song name
  char songNameArr[songName.length()];
  songName.toCharArray(songNameArr, songName.length());
  tft.drawString((utf8len(songNameArr) > 20 ? songName.substring(0, songName.indexOf(utf8index(songNameArr, 17))) + "..." : songName), xpos, ypos + 110);

  // unload han character
  tft.unloadFont();
}

size_t utf8len(char *s)
{
  size_t len = 0;
  for (; *s; ++s)
    if ((*s & 0xC0) != 0x80)
      ++len;
  return len;
}

char *utf8index(char *s, size_t pos)
{
  ++pos;
  for (; *s; ++s)
  {
    if ((*s & 0xC0) != 0x80)
      --pos;
    if (pos == 0)
      return s;
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
bool initTemp()
{
  byte resultValue = 0;
  // Initialize temperature sensor
  dht.setup(dhtPin, DHTesp::DHT11);
  tft.println("[DHT11]Initiated.");

  // Start task to get temperature
  xTaskCreatePinnedToCore(
      DHTTask,        /* Function to implement the task */
      "DHTTask ",     /* Name of the task */
      4000,           /* Stack size in words */
      NULL,           /* Task input parameter */
      5,              /* Priority of the task */
      &dhtTaskHandle, /* Task handle. */
      1);             /* Core where the task should run */

  if (dhtTaskHandle == NULL)
  {
    tft.println("[DHT11]Failed to start task for temperature update");
    return false;
  }
  else
  {
    // Start update of environment data every XX seconds
    dhtTicker.attach(dhtUpdateDuration, TriggerGetDHT);
  }
  getTemperature();
  return true;
}

/**
   triggerGetTemp
   Sets flag dhtUpdated to true for handling in loop()
   called by Ticker getTempTimer
*/
void TriggerGetDHT()
{
  if (dhtTaskHandle != NULL)
  {
    xTaskResumeFromISR(dhtTaskHandle);
  }
}

/**
   Task to reads temperature from DHT11 sensor
   @param pvParameters
      pointer to task parameters
*/
void DHTTask(void *pvParameters)
{
  tft.println("[DHT11]Starts to get value.");
  while (1) // tempTask loop
  {
    if (tasksEnabled)
    {
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
bool getTemperature()
{
  // Reading temperature for humidity takes about 250 milliseconds!
  // Sensor readings may also be up to 2 seconds 'old' (it's a very slow sensor)
  TempAndHumidity newValues = dht.getTempAndHumidity();
  // Check if any reads failed and exit early (to try again).
  if (dht.getStatus() != 0)
  {
    tft.println("[DHT11]Error status: " + String(dht.getStatusString()));
    return false;
  }

  float heatIndex = dht.computeHeatIndex(newValues.temperature, newValues.humidity);
  temperature = newValues.temperature;
  humidity = newValues.humidity;
  return true;
}

String HttpGETRequest(const char *requestAddress)
{
  WiFiClient client;
  HTTPClient http;
  http.setConnectTimeout(100);
  client.setTimeout(100);

  // Your Domain name with URL path or IP address with path
  http.begin(client, requestAddress);

  // If you need Node-RED/server authentication, insert user and password below
  // http.setAuthorization("REPLACE_WITH_SERVER_USERNAME", "REPLACE_WITH_SERVER_PASSWORD");

  // Send HTTP POST request
  int httpResponseCode = http.GET();

  String payload = "{}";

  if (httpResponseCode > 0)
  {
    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);
    payload = http.getString();
  }
  else
  {
    Serial.print("Error code: ");
    Serial.println(httpResponseCode);
  }
  // Free resources
  http.end();

  return payload;
}

void ClearScreen(int startPoint, int endPoint, int perUnit)
{
  tft.setTextColor(0xFFFF, TFT_BLACK);
  for (int i = startPoint; i <= endPoint; i += perUnit)
  {
    tft.setCursor(0, i);
    tft.println("                                   ");
  }
  tft.setCursor(0, 0);
}
