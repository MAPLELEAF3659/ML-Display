#include <TFT_eSPI.h> // Graphics and font library for ST7735 driver chip
#include "Fonts/Custom/Silver_16.h"
#include <SPI.h>
#include <WiFi.h>
#include "time.h"
#include "DHTesp.h"
#include <queue>

using namespace std;

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
int dhtUpdateInterval = 5000; // ms
// DHT info
float temperature = 0;
float humidity = 0;
float temperaturePrevious = -1;
float humidityPrevious = -1;
//**DHT**

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
  PlaybackState,
  LyricCurrent
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
String songCurrentLyric = "";
// for tft print
int songMetadataYPosOffset = 53;
//**Player info**

//**Timer**
queue<TimerHandle_t> timers;
//**Timer**

enum ScreenState
{
  NoneScreen = -1,
  MainScreen,
  PlayerScreen
};
ScreenState screenState = NoneScreen;

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
  // tft.println("[WiFi]IP: "+WiFi.localIP());

  // setup ntp server
  tft.println("[NTP server]Setting up...");
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  StartTimer("timerNTP", 500, NTPGetTime);
  tft.println("[NTP server]Done.");

  // setup dht11
  tft.println("[DHT11]Setting up...");
  dht.setup(dhtPin, DHTesp::DHT11);
  StartTimer("timerDHT", 5000, DHTGetTempAndHumi);
  tft.println("[DHT11]Done.");

  // setup complete
  tft.println("[System]Welcome!");
  delay(5000);
  ChangeScreenState(MainScreen);
}

void StartTimer(char timerName[], int timerInterval, TimerCallbackFunction_t function)
{
  // create timer
  TimerHandle_t timer = xTimerCreate(
      timerName,
      pdMS_TO_TICKS(timerInterval),
      pdTRUE,
      (void *)timers.size(),
      function);

  timers.push(timer);

  // start timer to get ntp time info
  xTimerStart(timer, 0);
}

void StopAllTimer()
{
  while (timers.size() > 0)
  {
    xTimerStop(timers.front(), 0);
    xTimerDelete(timers.front(), 0);
    timers.pop();
  }
}

void NTPGetTime(TimerHandle_t xTimer)
{
  // get time info
  getLocalTime(&timeinfo);
}

void DHTGetTempAndHumi(TimerHandle_t xTimer)
{
  // Reading temperature for humidity takes about 250 milliseconds!
  // Sensor readings may also be up to 2 seconds 'old' (it's a very slow sensor)
  TempAndHumidity newValues = dht.getTempAndHumidity();
  // Check if any reads failed and exit early (to try again).
  if (dht.getStatus() != 0)
  {
    Serial.println("[DHT11]Error status: " + String(dht.getStatusString()));
  }

  float heatIndex = dht.computeHeatIndex(newValues.temperature, newValues.humidity);
  temperature = newValues.temperature;
  humidity = newValues.humidity;
}

void loop()
{
  if (Serial.available())
  {
    String message = Serial.readStringUntil('\n');

    ScreenState targetScreenState = (ScreenState)message.substring(0, message.indexOf(',')).toInt();
    ChangeScreenState(targetScreenState);

    switch (targetScreenState)
    {
    case PlayerScreen:
      PlayerInfoId playerInfoId = (PlayerInfoId)(message.substring(message.indexOf(',') + 1, message.lastIndexOf(',')).toInt());
      String value = message.substring(message.lastIndexOf(',') + 1);
      PlayerInfoUpdate(playerInfoId, value);
      break;
    }
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

void ChangeScreenState(ScreenState targetScreenState)
{
  if (screenState == targetScreenState)
  {
    return;
  }

  screenState = targetScreenState;

  // force clear screen and previous states when screenState changed
  ClearScreen(0, 160, 5);
  dayPrevious = -1;
  minPrevious = -1;
  secPrevious = -1;

  // print first screen
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
    TFTPrintPlayerState();
    TFTPrintPlayerSongDuration();
    TFTPrintPlayerSongPosition();
    TFTPrintPlayerSongGeneralInfo();
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
    tft.setTextColor(0xFFFF, TFT_BLACK);
    tft.drawChar(timeinfo.tm_sec % 2 == 0 ? ':' : ' ', 142, 123, 1);

    // update previous state
    secPrevious = timeinfo.tm_sec;
  }
}

void PlayerInfoUpdate(PlayerInfoId infoId, String value)
{
  switch (infoId)
  {
  case Artist:
    songArtist = value;
    TFTPrintPlayerSongMetadata(songArtist, 0);
    break;
  case Album:
    songAlbum = value;
    TFTPrintPlayerSongMetadata(songAlbum, 1);
    break;
  case Title:
    songTitle = value;
    TFTPrintPlayerSongMetadata(songTitle, 2);
    break;
  case BitDepth:
    songBitDepth = value;
    TFTPrintPlayerSongGeneralInfo();
    break;
  case Bitrate:
    songBitrate = value;
    TFTPrintPlayerSongGeneralInfo();
    break;
  case SampleRate:
    songSampleRate = value;
    TFTPrintPlayerSongGeneralInfo();
    break;
  case Codec:
    songCodec = value;
    TFTPrintPlayerSongCodec();
    break;
  case Duration:
    songDuration = value.toFloat();
    TFTPrintPlayerSongDuration();
    break;
  case Position:
    songPostion = value.toFloat();
    TFTPrintPlayerSongPosition();
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
    TFTPrintPlayerState();
    break;
  case LyricCurrent:
    songCurrentLyric = value;
    TFTPrintPlayerSongCurrentLyric();
    break;
  default:
    break;
  }
}

void TFTPrintTime()
{
  int xposTime = xpos;
  int yposTime = ypos;
  if (screenState == MainScreen)
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
                   130, 123, 1);
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
  tft.drawString("    " + String(timeinfo.tm_year + 1900) + "/" +
                     (timeinfo.tm_mon < 9 ? "0" : "") + String(timeinfo.tm_mon + 1) + "/" +
                     (timeinfo.tm_mday < 10 ? "0" : "") + String(timeinfo.tm_mday) + " " + dayOfWeekStr + "  ",
                 0, ypos + 58, 2);
}

void TFTPrintDHTInfo()
{
  // check if temperature was updated
  if (temperature != temperaturePrevious)
  {
    tft.setTextColor(TextColorByTemperature(temperature), TFT_BLACK);
    // write temperature
    tft.drawString(String(temperature, 1) + "C", xpos + 95, ypos + 80, 2);
    // update pervious state
    temperaturePrevious = temperature;
  }

  // check if humidity was updated
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

void TFTPrintPlayerState()
{
  // clear player state screen area
  tft.setTextColor(0xFFFF, TFT_BLACK);
  tft.drawString("           ", xpos, 5, 2);
  switch (playerState)
  {
  case 0:
    tft.setTextColor(0x07E0, TFT_BLACK);
    tft.drawString("Playing >", xpos, 5, 2);
    break;
  case 1:
    tft.setTextColor(0x001F, TFT_BLACK);
    tft.drawString("Pause ||", xpos, 5, 2);
    break;
  case 2:
    tft.setTextColor(0xF800, TFT_BLACK);
    tft.drawString("Stop []", xpos, 5, 2);
    break;
  }
}

void TFTPrintPlayerSongCodec()
{
  // clear song codec screen area
  tft.setTextColor(0xFFFF, TFT_BLACK);
  tft.drawString("            ", xpos + 80, 5, 2);

  tft.setTextColor(0xFFFF, TextBackgroundColorByCodec(songCodec));
  // print song codec
  int xposCodec = xpos + 120;
  tft.drawString(" " + songCodec + " ", xposCodec + (songCodec.length() * -5.2), 5, 2);
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

void TFTPrintPlayerSongDuration()
{
  // set color
  tft.setTextColor(0xFFFF, TFT_BLACK);

  // print duration in 00:00 format
  tft.drawString(((songDuration / 60 < 10) ? "0" : "") + String(songDuration / 60) + ":" +
                     ((songDuration % 60 < 10) ? "0" : "") + String(songDuration % 60),
                 xpos + 113, 27, 1);
}

void TFTPrintPlayerSongPosition()
{
  // set color
  tft.setTextColor(0xFFFF, TFT_BLACK);

  // print position in 00:00 format
  tft.drawString(((songPostion / 60) < 10 ? "0" : "") + String(songPostion / 60) + ":" +
                     ((songPostion % 60) < 10 ? "0" : "") + String(songPostion % 60),
                 xpos, 27, 1);

  // draw position bar
  int xposSong = xpos;
  int xIndexPlayingPosition = map(((float)songPostion / (float)songDuration) * 100, 0, 100, 0, 23);
  for (int i = 0; i < 24; i++)
  {
    xposSong += tft.drawString(i == xIndexPlayingPosition ? "+" : "-", xposSong, 37, 1);
  }
}

void TFTPrintPlayerSongGeneralInfo()
{
  // clear song general info screen area
  tft.setTextColor(0xFFFF, TFT_BLACK);
  tft.drawString("                             ", 0, 49, 1);

  // print song general info
  float sampleRateF = songSampleRate.toFloat() / 1000;
  tft.drawString(songBitDepth + "bits " +
                     String(sampleRateF, 1) + "kHz " +
                     songBitrate + "kbps",
                 xpos, 49, 1);
}

void TFTPrintPlayerSongMetadata(String value, int lineIndex)
{
  // clear screen
  tft.setTextColor(0xFFFF, TFT_BLACK);
  tft.drawString("                               ", 0, ypos + songMetadataYPosOffset - 2 + lineIndex * 15, 2);

  // load han character
  tft.loadFont(Silver_16);

  // print artist/album/title name
  tft.drawString(value, xpos, ypos + songMetadataYPosOffset + lineIndex * 15);

  // unload han character
  tft.unloadFont();
}

void TFTPrintPlayerSongCurrentLyric()
{
  // clear screen
  tft.setTextColor(0xFFFF, TFT_BLACK);
  tft.drawString("                               ", 0, 107, 2);

  // load han character
  tft.loadFont(Silver_16);

  // print lyric
  tft.drawString(songCurrentLyric, xpos, 110);

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
