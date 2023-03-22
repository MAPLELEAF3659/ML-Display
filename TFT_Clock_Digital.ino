#include <TFT_eSPI.h> // Graphics and font library for ST7735 driver chip
#include "Fonts/Custom/Silver_16.h"
#include <SPI.h>
#include <WiFi.h>
#include "time.h"
#include "DHTesp.h"
#include <Ticker.h>
// #include <HTTPClient.h>
// #include <Arduino_JSON.h>

/*
**Upload settings**
Board: ESP32 Dev Module
Partition Scheme: Huge APP
Flash Size: 4MB
Upload Speed: 921600
*/

//**Serial**
String serialData;
//**Serial**

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
uint upperBarBackgroundColor = 0x2104;
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

// //**Player API**
// const char *getPlayerInfoQuery =
//     "http://192.168.0.100:8880/api/query?player=true&trcolumns=%artist%,%title%,%album%,%__bitspersample%,%bitrate%,%samplerate%,%codec%";
// String apiResponse = "";
// bool isPlayerApiAvailable = false;
// int apiErrorCount = 0;
// //**Player API**

//**Player info**
enum PlayerInfoId
{
  None = -1,
  Artist,
  Title,
  Album,
  BitDepth,
  Bitrate,
  SampleRate,
  Codec,
  Duration,
  Position,
  PlaybackState
};
enum PlayerState
{
  Playing,
  Paused,
  Stopped
};
PlayerInfoId updatePlayerInfoId = None;
PlayerState playerState = Stopped;
String songArtist = "";
String songTitle = "";
String songAlbum = "";
int songDuration = 0;
int songPostion = 0;
String songBitDepth = "0";
String songBitrate = "0";
String songSampleRate = "0";
String songCodec = "";
//**Player info**

enum ScreenState
{
  NoneScreen = -1,
  MainScreen,
  PlayerScreen
};
ScreenState screenState = MainScreen;
ScreenState screenStatePrevious = NoneScreen;

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

  // get serial data
  serialData = Serial.readStringUntil('\n');

  // change screenState
  if (serialData != "")
  {
    screenState = (ScreenState)serialData.substring(0, serialData.indexOf(',')).toInt();
    if (screenState == MainScreen)
    {
      serialData = "";
    }
  }

  // force clear screen and previous states when screenState changed
  if (screenState != screenStatePrevious)
  {
    ClearScreen(0, 160, 5);
    dayPrevious = -1;
    minPrevious = -1;
    secPrevious = -1;

    switch (screenState)
    {
    case MainScreen:
      // **force print dht info
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
    case PlayerScreen:
      // draw upper bar(time&date) background
      tft.setTextColor(0xFFFF, upperBarBackgroundColor);
      tft.drawString("                   ", 0, 0, 2);

      TFTPrintPlayerState();
      TFTPrintPlayerSongDuration();
      TFTPrintPlayerSongMetadata();
      TFTPrintPlayerSongGeneralInfo();

      break;
    }

    screenStatePrevious = screenState;
  }

  // show screen by screenState
  switch (screenState)
  {
  case MainScreen:
    ShowMainScreen();
    break;
  case PlayerScreen:
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
    // blink ":"
    tft.setTextColor(0xFFFF, upperBarBackgroundColor);
    tft.drawChar(timeinfo.tm_sec % 2 == 0 ? ':' : ' ', 17, 5, 1);

    // update previous state
    secPrevious = timeinfo.tm_sec;
  }

  if (serialData != "")
  {
    PlayerInfoId playerInfoId = (PlayerInfoId)(serialData.substring(serialData.indexOf(',') + 1, serialData.lastIndexOf(',')).toInt());
    String value = serialData.substring(serialData.lastIndexOf(',') + 1);
    PlayerInfoUpdate(playerInfoId, value);
    switch (playerInfoId)
    {
    case PlaybackState:
      TFTPrintPlayerState();
      break;
    case Duration:
    case Position:
      TFTPrintPlayerSongDuration();
      break;
    case Artist:
    case Album:
    case Title:
      TFTPrintPlayerSongMetadata();
      break;
    case BitDepth:
    case Bitrate:
    case SampleRate:
    case Codec:
      TFTPrintPlayerSongGeneralInfo();
      break;
    }

    serialData = "";
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
    tft.setTextColor(0xFFFF, upperBarBackgroundColor);
    tft.drawString((timeinfo.tm_hour < 10 ? "0" : "") + String(timeinfo.tm_hour) + " " +
                       (timeinfo.tm_min < 10 ? "0" : "") + String(timeinfo.tm_min),
                   1, 0, 2);
  }
}

void TFTPrintDate()
{
  if (screenState == 0)
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
    tft.drawString("    " + String(timeinfo.tm_year + 1900) + "/" +
                       (timeinfo.tm_mon < 9 ? "0" : "") + String(timeinfo.tm_mon + 1) + "/" +
                       (timeinfo.tm_mday < 10 ? "0" : "") + String(timeinfo.tm_mday) + " " + dayOfWeekStr + "  ",
                   0, ypos + 58, 2);
  }
  else
  {
    tft.setTextColor(0xFFFF, upperBarBackgroundColor);
    tft.drawString(String(timeinfo.tm_year + 1900) + "/" +
                       (timeinfo.tm_mon < 9 ? "0" : "") + String(timeinfo.tm_mon + 1) + "/" +
                       (timeinfo.tm_mday < 10 ? "0" : "") + String(timeinfo.tm_mday),
                   83, 0, 2);
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
  if (temp >= 34)
  {
    return 0xF800; // red
  }
  else if (temp < 34 && temp >= 30)
  {
    return 0xFFE0; // yellow
  }
  else if (temp < 30 && temp >= 26)
  {
    return 0xFC00; // orange
  }
  else if (temp < 26 && temp >= 22)
  {
    return 0x07E0; // green
  }
  else if (temp < 22 && temp >= 18)
  {
    return 0x07F0; // blue green
  }
  else if (temp < 18 && temp >= 14)
  {
    return 0x001F; // blue
  }
  else if (temp < 14)
  {
    return 0x07FF; // light blue
  }
}

int TextColorByHumidity(float humi)
{
  if (humi >= 80)
  {
    return 0xF800; // red
  }
  else if (humi < 80 && humi >= 70)
  {
    return 0xFC00; // orange
  }
  else if (humi < 70 && humi >= 60)
  {
    return 0xFFE0; // yellow
  }
  else if (humi < 60 && humi >= 40)
  {
    return 0x07E0; // green
  }
  else if (humi < 40)
  {
    return 0x001F; // blue
  }
}

void TFTPrintPlayerSongMetadata()
{
  // clear screen
  ClearScreen(80, 160, 5);

  tft.setTextColor(0xFFFF, TFT_BLACK);

  // load han character
  tft.loadFont(Silver_16);

  // print artist name
  char artistNameArr[songArtist.length()];
  songArtist.toCharArray(artistNameArr, songArtist.length());
  tft.drawString((utf8len(artistNameArr) > 20 ? songArtist.substring(0, songArtist.indexOf(utf8index(artistNameArr, 17))) + "..." : songArtist), xpos, ypos + 75);

  // print album name
  char albumNameArr[songAlbum.length()];
  songAlbum.toCharArray(albumNameArr, songAlbum.length());
  tft.drawString((utf8len(albumNameArr) > 20 ? songAlbum.substring(0, songAlbum.indexOf(utf8index(albumNameArr, 17))) + "..." : songAlbum), xpos, ypos + 90);

  // print song name
  char songNameArr[songTitle.length()];
  songTitle.toCharArray(songNameArr, songTitle.length());
  tft.drawString((utf8len(songNameArr) > 20 ? songTitle.substring(0, songTitle.indexOf(utf8index(songNameArr, 17))) + "..." : songTitle), xpos, ypos + 105);

  // unload han character
  tft.unloadFont();
}

void TFTPrintPlayerSongGeneralInfo()
{
  // clear song general info screen area
  tft.setTextColor(0xFFFF, TFT_BLACK);
  tft.drawString("                             ", 0, 67, 1);
  // print song general info
  float sampleRateF = songSampleRate.toFloat() / 1000;
  tft.drawString(songBitDepth + "bits " +
                     String(sampleRateF, 1) + "kHz " +
                     songBitrate + "kbps",
                 xpos, 67, 1);

  // clear song codec screen area
  tft.setTextColor(0xFFFF, TFT_BLACK);
  tft.drawString("            ", xpos + 80, 20, 2);

  tft.setTextColor(0xFFFF, TextBackgroundColorByCodec(songCodec));
  // print song codec
  int xposCodec = xpos + 120;
  tft.drawString(" " + songCodec + " ", xposCodec + (songCodec.length() * -5.2), 20, 2);
}

int TextBackgroundColorByCodec(String codecStr)
{
  if (codecStr == "FLAC")
  {
    return 0x0200; // dark green
  }
  else if (codecStr == "PCM")
  {
    return 0x020C; // dark blue
  }
  else if (codecStr == "DST64" || codecStr == "DSD64")
  {
    return 0x4000; // dark red
  }
  else if (codecStr == "MP3" || codecStr == "AAC")
  {
    return 0x8B00; // orange
  }
  else
  {
    return 0x4208; // dark gray
  }
}

void TFTPrintPlayerState()
{
  // clear player state screen area
  tft.setTextColor(0xFFFF, TFT_BLACK);
  tft.drawString("           ", xpos, 20, 2);
  switch (playerState)
  {
  case 0:
    tft.setTextColor(0x07E0, TFT_BLACK);
    tft.drawString("Playing >", xpos, 20, 2);
    break;
  case 1:
    tft.setTextColor(0x001F, TFT_BLACK);
    tft.drawString("Pause ||", xpos, 20, 2);
    break;
  case 2:
    tft.setTextColor(0xF800, TFT_BLACK);
    tft.drawString("Stop []", xpos, 20, 2);
    break;
  }
}

void TFTPrintPlayerSongDuration()
{
  // clear player state screen area
  tft.setTextColor(0xFFFF, TFT_BLACK);
  // tft.drawString("                             ", 0, 50, 1);

  tft.drawString(((songPostion / 60) < 10 ? "0" : "") + String(songPostion / 60) + ":" +
                     ((songPostion % 60) < 10 ? "0" : "") + String(songPostion % 60),
                 xpos, 42, 1);

  tft.drawString(((songDuration / 60 < 10) ? "0" : "") + String(songDuration / 60) + ":" +
                     ((songDuration % 60 < 10) ? "0" : "") + String(songDuration % 60),
                 xpos + 113, 42, 1);

  int xposSong = xpos;
  int xIndexPlayingPosition = map(((float)songPostion / (float)songDuration) * 100, 0, 100, 0, 23);
  for (int i = 0; i < 24; i++)
  {
    xposSong += tft.drawString(i == xIndexPlayingPosition ? "+" : "-", xposSong, 52, 1);
  }
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

void PlayerInfoUpdate(PlayerInfoId infoId, String value)
{
  switch (infoId)
  {
  case Artist:
    songArtist = value;
    break;
  case Album:
    songAlbum = value;
    break;
  case Title:
    songTitle = value;
    break;
  case BitDepth:
    songBitDepth = value;
    break;
  case Bitrate:
    songBitrate = value;
    break;
  case SampleRate:
    songSampleRate = value;
    break;
  case Codec:
    songCodec = value;
    break;
  case Duration:
    songDuration = value.toFloat();
    break;
  case Position:
    songPostion = value.toFloat();
    break;
  case PlaybackState:
    switch (value[1])
    {
    case 'l':
      playerState = Playing;
      break;
    case 'a':
      playerState = Paused;
      break;
    case 't':
      playerState = Stopped;
      break;
    }
    break;
  default:
    break;
  }
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
