#include <TFT_eSPI.h>  // Graphics and font library for ST7735 driver chip
#include "Fonts/Custom/Cubic12.h"
#include <SPI.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "time.h"
// #include "DHTesp.h"
#include <queue>
#include "secrets.h"
#include "wifi_info.h"

#define FINANCE_TOTAL_COUNT 5 // stock + currency
#define STOCK_COUNT 3

/*
**Upload settings**
Board: ESP32 Dev Module
Partition Scheme: 16MB Flash(3MB APP/9.9MB FATFS)
Flash Size: 16MB
Upload Speed: 921600
*/

//**WiFi**
const char *ssid = WIFI_SSID;
const char *password = WIFI_PASSWORD;
//**WiFi**

//**NTP**
const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 28800;  // GMT+8
const int daylightOffset_sec = 0;
struct tm timeinfo;
uint8_t secPrevious, minPrevious, hourPrevious, dayPrevious;
//**NTP**

//**TFT**
TFT_eSPI tft = TFT_eSPI();  // Invoke library, pins defined in User_Setup.h
// GND=GND VCC=3V3 SCL=G14 SDA=G15 RES=G33 DC=G27 CS=G5 BL=G22
uint8_t xpos = 5;
uint8_t ypos = 5;
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

//**Open weather data**
String openWeatherUrl = "https://opendata.cwa.gov.tw/api/v1/rest/datastore/O-A0001-001?Authorization=" + String(OPEN_WEATHER_DATA_API_KEY) + "&format=JSON&StationId=C0AI30&WeatherElement=Weather,AirTemperature,RelativeHumidity";
bool isOpenWeatherInfoUpdated = false;
bool isOpenWeatherInfoPrinted = true;
float temperatureOpenWeather = 0;
float humidityOpenWeather = 0;
String descriptionOpenWeather = "";
//**Open weather data**

//**Finance data**
String twseUrl = "https://mis.twse.com.tw/stock/api/getStockInfo.jsp?ex_ch=tse_";
String currencyUrl = "https://open.er-api.com/v6/latest/TWD";
bool isTWSEInfoUpdated = false;
bool isCurrencyInfoUpdated = false;
bool isFinanceInfoPrinted = true;
bool isFinanceInfoPrintPriceOnly = false;
uint8_t financeIndex = -1;
uint8_t financeIndexPrevious = -1;
// si_ = stock index; se_ = stock ETF; sn_ = stock normal; cu_ = currency;
String financeNumbers[FINANCE_TOTAL_COUNT] = { "si_t00", "se_0050", "sn_2330", "cu_JPY", "cu_USD" };
String financeNames[FINANCE_TOTAL_COUNT] = { "加權指數", "元大台灣50", "台積電", "日幣JPY 兌 台幣TWD", "美元USD 兌 台幣TWD" };
float financePrices[FINANCE_TOTAL_COUNT];
float financeYesterdayPrices[FINANCE_TOTAL_COUNT];
//**Finanse data**

//**Player info**
enum PlayerInfoId {
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
enum PlayerState {
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
int songMetadataYPosOffset = 58;
//**Player info**

//**Timer**
std::queue<TimerHandle_t> timers;
//**Timer**

enum ScreenState {
  NoneScreen = -1,
  MainScreen,
  PlayerScreen
};
ScreenState screenState = NoneScreen;

void setup() {
  Serial.begin(115200);

  tft.init();
  tft.setRotation(-1);
  tft.fillScreen(TFT_BLACK);

  ClearScreen(0, 160, 5);

  tft.setTextColor(TFT_YELLOW, TFT_BLACK);  // Note: the new fonts do not draw the background colour
  tft.setCursor(0, 5);

  // connect to wifi
  tft.print("[Wi-Fi]" + String(ssid));
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    tft.print(".");
  }
  tft.println("ok");

  // setup ntp server
  tft.println("[NTP]Setup");
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  // setup online data for first time
  // open weather
  tft.print("[OpenWeather]Updating...");
  OpenWeatherGetInfo();
  tft.println("ok");
  // twse
  tft.print("[Finance]Updating");
  CurrencyGetInfo();
  for (uint8_t i = 0; i < FINANCE_TOTAL_COUNT; i++) {
    if (i < STOCK_COUNT) {
      TWSEGetInfo(i);
    } else {
      financeYesterdayPrices[i] = financePrices[i];
    }
    tft.print(".");
  }
  tft.print(".");
  tft.println("ok");

  // setup complete
  tft.println("Welcome to ML-Display!");
  delay(1000);
  ChangeScreenState(MainScreen);
}

void loop() {
  if (Serial.available()) {
    String message = Serial.readStringUntil('\n');

    ScreenState targetScreenState = (ScreenState)message.substring(0, message.indexOf('$')).toInt();
    ChangeScreenState(targetScreenState);

    switch (targetScreenState) {
      case PlayerScreen:
        PlayerInfoId playerInfoId = (PlayerInfoId)(message.substring(message.indexOf('$') + 1, message.lastIndexOf('$')).toInt());
        String value = message.substring(message.lastIndexOf('$') + 1);
        PlayerInfoUpdate(playerInfoId, value);
        break;
    }
  }

  if (!isOpenWeatherInfoUpdated) {
    OpenWeatherGetInfo();
    isOpenWeatherInfoPrinted = false;
  }

  if (!isTWSEInfoUpdated) {
    TWSEGetInfo(financeIndex);
  }

  if (!isCurrencyInfoUpdated) {
    CurrencyGetInfo();
  }
}

void ChangeScreenState(ScreenState targetScreenState) {
  if (screenState == targetScreenState) {
    return;
  }

  // update screen state
  screenState = targetScreenState;

  // stop all timers
  StopAllTimer();

  // force clear screen and previous states when screenState changed
  ClearScreen(0, 160, 5);
  dayPrevious = -1;
  hourPrevious = -1;
  minPrevious = -1;
  secPrevious = -1;

  // print first screen
  switch (screenState) {
    case MainScreen:
      isOpenWeatherInfoUpdated = false;
      StartTimer("timerNTP", 500, NTPGetTime);
      break;
    case PlayerScreen:
      TFTPrintPlayerState();
      TFTPrintPlayerSongDuration();
      TFTPrintPlayerSongPosition();
      TFTPrintPlayerSongGeneralInfo();
      break;
  }
}

// **Timer functions**
void StartTimer(char timerName[], int timerInterval, TimerCallbackFunction_t function) {
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

void StopAllTimer() {
  while (timers.size() > 0) {
    xTimerStop(timers.front(), 0);
    xTimerDelete(timers.front(), 0);
    timers.pop();
  }
}

// **Update Info**
void NTPGetTime(TimerHandle_t xTimer) {
  // get time info
  getLocalTime(&timeinfo);

  // update by day
  if (timeinfo.tm_mday != dayPrevious) {
    if (screenState == MainScreen) {
      TFTPrintDate();
    }
    dayPrevious = timeinfo.tm_mday;
  }

  // update by hour
  if (timeinfo.tm_hour != hourPrevious) {
    if (screenState == MainScreen) {
      isOpenWeatherInfoUpdated = false;
      if (timeinfo.tm_hour == 10) {
        isCurrencyInfoUpdated = false;
      }
    }

    hourPrevious = timeinfo.tm_hour;
  }

  // update by min
  if (timeinfo.tm_min != minPrevious) {
    if (screenState == MainScreen) {
      // print time hh:mm
      TFTPrintTime();

      // print Finance Info (scheduled)
      financeIndex = timeinfo.tm_min % FINANCE_TOTAL_COUNT;
      isFinanceInfoPrintPriceOnly = false;
      isFinanceInfoPrinted = false;
    }

    // update previous state
    minPrevious = timeinfo.tm_min;
  }

  // update by sec
  if (timeinfo.tm_sec != secPrevious) {
    if (screenState == MainScreen) {
      // blink ":"
      TFTPrintSecBlink();
      TFTPrintTimeSec();

      // print open weather info
      if (!isOpenWeatherInfoPrinted) {
        TFTPrintOpenWeatherInfo();
        isOpenWeatherInfoPrinted = true;
      }

      // update twse info (scheduled)
      if ((timeinfo.tm_wday > 0 && timeinfo.tm_wday < 6) && ((timeinfo.tm_hour >= 9 && timeinfo.tm_hour < 14))) {
        if (timeinfo.tm_min % FINANCE_TOTAL_COUNT < STOCK_COUNT) {
          if (timeinfo.tm_hour == 13) {
            if (timeinfo.tm_min <= 30) {
              // schedule for print on %5sec(exclude 0)
              if (timeinfo.tm_sec % 5 == 0 && timeinfo.tm_sec > 0) {
                financeIndex = timeinfo.tm_min % FINANCE_TOTAL_COUNT;
                isFinanceInfoPrintPriceOnly = true;
                isFinanceInfoPrinted = false;
              }
              // schedule for update on every %5+1sec(ex.1,6,11,11...)
              if (timeinfo.tm_sec % 5 == 1) {
                financeIndex = timeinfo.tm_min % FINANCE_TOTAL_COUNT;
                isTWSEInfoUpdated = false;
              }
            } else {
              // schedule for update on 56sec
              if (timeinfo.tm_sec == 56) {
                financeIndex = timeinfo.tm_min % FINANCE_TOTAL_COUNT;
                isTWSEInfoUpdated = false;
              }
            }
          }
        }
      }

      // print finance info
      if (!isFinanceInfoPrinted) {
        TFTPrintFinanceInfo(financeIndex);
        isFinanceInfoPrinted = true;
      }
    }

    // update previous state
    secPrevious = timeinfo.tm_sec;
  }
}

void OpenWeatherGetInfo() {
  // Make an HTTP request
  HTTPClient http;
  http.begin(openWeatherUrl);  // replace with your URL
  int httpCode = http.GET();
  float tempTemp, tempHumi;
  String tempDesc;

  if (httpCode == HTTP_CODE_OK) {
    // Parse the JSON response
    String payload = http.getString();
    DynamicJsonDocument doc(4096);
    deserializeJson(doc, payload);
    tempTemp = doc["records"]["Station"][0]["WeatherElement"]["AirTemperature"].as<float>();
    tempHumi = doc["records"]["Station"][0]["WeatherElement"]["RelativeHumidity"].as<float>();
    tempDesc = doc["records"]["Station"][0]["WeatherElement"]["Weather"].as<String>();
    if (tempTemp != -99)
      temperatureOpenWeather = tempTemp;
    if (tempHumi != -99)
      humidityOpenWeather = tempHumi;
    if (tempDesc != "-99")
      descriptionOpenWeather = tempDesc;
  } else {
    Serial.println("Error getting JSON data");
  }

  // Turn off http client
  http.end();

  isOpenWeatherInfoUpdated = true;
}

void TWSEGetInfo(int index) {
  // Make an HTTP request
  HTTPClient http;
  http.begin(twseUrl + financeNumbers[index].substring(3) + ".tw");  // replace with your URL
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    // Parse the JSON response
    String payload = http.getString();
    DynamicJsonDocument doc(4096);
    deserializeJson(doc, payload);

    if (doc["msgArray"][0]["z"] != "-") {
      financePrices[index] = doc["msgArray"][0]["z"].as<float>();
    }
    financeYesterdayPrices[index] = doc["msgArray"][0]["y"].as<float>();
  } else {
    Serial.println("Error getting JSON data");
  }

  // Turn off http client
  http.end();

  isTWSEInfoUpdated = true;
}

void CurrencyGetInfo() {
  // Make an HTTP request
  HTTPClient http;
  http.begin(currencyUrl);  // replace with your URL
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    // Parse the JSON response
    String payload = http.getString();
    DynamicJsonDocument doc(4096);
    deserializeJson(doc, payload);

    if (doc["result"].as<String>() == "success") {
      for (uint8_t i = STOCK_COUNT; i < FINANCE_TOTAL_COUNT; i++) {
        financeYesterdayPrices[i] = financePrices[i];
        financePrices[i] = doc["rates"]["TWD"].as<float>() / doc["rates"][financeNumbers[i].substring(3)].as<float>();
      }
    }
  } else {
    Serial.println("Error getting JSON data");
  }

  // Turn off http client
  http.end();

  isCurrencyInfoUpdated = true;
}

void PlayerInfoUpdate(PlayerInfoId infoId, String value) {
  switch (infoId) {
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
      switch (value[1]) {
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

// **Time & Date**
void TFTPrintTime() {
  int xposTime = xpos + 5;
  int yposTime = ypos + 15;

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

void TFTPrintSecBlink() {
  // print ":" background
  tft.setTextColor(0x39C4, TFT_BLACK);
  tft.drawString(":", xpos + 70, ypos + 15, 7);

  // print ":" (blink it)
  tft.setTextColor(0xFFFF);
  tft.drawChar(timeinfo.tm_sec % 2 == 0 ? ':' : ' ', xpos + 70, ypos + 15, 7);
}

void TFTPrintTimeSec() {
  tft.setTextColor(0xFFFF, TFT_BLACK);
  tft.drawString((timeinfo.tm_sec < 10 ? "0" : "") + String(timeinfo.tm_sec), xpos + 130, ypos, 1);
}

void TFTPrintDate() {
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
  tft.drawString(String(timeinfo.tm_year + 1900) + "/" + (timeinfo.tm_mon < 9 ? "0" : "") + String(timeinfo.tm_mon + 1) + "/" + (timeinfo.tm_mday < 10 ? "0" : "") + String(timeinfo.tm_mday) + " " + dayOfWeekStr + "  ",
                 xpos + 5, ypos, 1);
}

// **Weather**
void TFTPrintOpenWeatherInfo() {
  tft.setTextColor(0xFFFF, TFT_BLACK);
  tft.drawString("                                    ", xpos, ypos + 70, 2);

  tft.loadFont(Cubic12);

  // print temperature
  tft.setTextColor(0xFFFF, TFT_BLACK);
  tft.drawString(descriptionOpenWeather, xpos + 5, ypos + 72);

  tft.setTextColor(TextColorByTemperature(temperatureOpenWeather), TFT_BLACK);
  tft.drawString((temperatureOpenWeather >= 10 ? "" : " ") + String(temperatureOpenWeather, 1) + "℃", xpos + 70, ypos + 72);

  // print humidity
  tft.setTextColor(TextColorByHumidity(humidityOpenWeather), TFT_BLACK);
  tft.drawString((humidityOpenWeather >= 10 ? "" : " ") + String(humidityOpenWeather, 1) + "%", xpos + 110, ypos + 72);

  tft.unloadFont();
}

int TextColorByTemperature(float temp) {
  if (temp >= 34) {
    return 0xF800;  // red
  } else if (temp < 34 && temp >= 30) {
    return 0xFC00;  // orange
  } else if (temp < 30 && temp >= 26) {
    return 0xFFE0;  // yellow
  } else if (temp < 26 && temp >= 22) {
    return 0x07E0;  // green
  } else if (temp < 22 && temp >= 18) {
    return 0x07FC;  // blue green
  } else if (temp < 18 && temp >= 14) {
    return 0x067F;  // light blue
  } else if (temp < 14) {
    return 0x077F;  // lighter blue
  }
}

int TextColorByHumidity(float humi) {
  if (humi >= 90) {
    return 0x077F;  // lighter blue
  } else if (humi < 90 && humi >= 75) {
    return 0x067F;  // light blue
  } else if (humi < 75 && humi >= 60) {
    return 0x07F7;  // green blue
  } else if (humi < 60 && humi >= 40) {
    return 0x07E0;  // green
  } else if (humi < 40) {
    return 0xFC00;  // orange
  } else if (humi < 20) {
    return 0xF800;  // red
  }
}

// **Finance**
void TFTPrintFinanceInfo(uint8_t index) {
  tft.setTextColor(0xFFFF, TFT_BLACK);
  if (!isFinanceInfoPrintPriceOnly) {
    tft.drawString("                                              ", xpos, ypos + 90, 2);
  }
  tft.drawString("                                              ", xpos, ypos + 110, 2);

  tft.loadFont(Cubic12);

  // print name
  if (!isFinanceInfoPrintPriceOnly) {
    String number, type;
    // if number is not index or currency, then print its stock number
    if (!(financeNumbers[index].startsWith("si_") || financeNumbers[index].startsWith("cu_"))) {
      number = financeNumbers[index].substring(3) + "  ";
    }
    if (financeNumbers[index].startsWith("se_")) {
      type = "ETF";
    } else if (financeNumbers[index].startsWith("si_")) {
      type = "INDEX";
    }
    tft.setTextColor(0xFFFF, TFT_BLACK);
    tft.drawString(financeNames[index] + "  " + number + type, xpos + 5, ypos + 92);
  }

  // print price
  tft.setTextColor(0xFFFF, TFT_BLACK);
  tft.drawString(String(financePrices[index], index < STOCK_COUNT ? 2 : 4), xpos + 5, ypos + 112);

  float changeAmount = financePrices[index] - financeYesterdayPrices[index];
  float changePercent = (financePrices[index] / financeYesterdayPrices[index] - 1.0) * 100;
  tft.setTextColor(TextColorByAmount(changeAmount), TFT_BLACK);
  tft.drawString((changeAmount >= 0 ? "+" : "") + String(changeAmount, index < STOCK_COUNT ? 2 : 4)
                   + "(" + String(abs(changePercent), changePercent >= 10 ? 1 : 2) + "%)",
                 xpos + 65, ypos + 112);

  tft.unloadFont();
}

int TextColorByAmount(float amount) {
  if (amount > 0) {
    return 0xF800;  // red
  } else if (amount < 0) {
    return 0x07E0;  // green
  } else {
    return 0xFFFF;  // green blue
  }
}

// **Player**
void TFTPrintPlayerState() {
  // clear player state screen area
  tft.setTextColor(0xFFFF, TFT_BLACK);
  tft.drawString("           ", xpos, 5, 2);
  switch (playerState) {
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

void TFTPrintPlayerSongCodec() {
  // clear song codec screen area
  tft.setTextColor(0xFFFF, TFT_BLACK);
  tft.drawString("            ", xpos + 85, 5, 2);

  tft.setTextColor(0xFFFF, TextBackgroundColorByCodec(songCodec));
  // print song codec
  int xposCodec = xpos + 125;
  tft.drawString(" " + songCodec + " ", xposCodec + (songCodec.length() * -5.2), 5, 2);
}

int TextBackgroundColorByCodec(String codecStr) {
  if (codecStr.startsWith("FLAC")) {
    return 0x0200;  // dark green
  } else if (codecStr.startsWith("PCM")) {
    return 0x020C;  // dark blue
  } else if (codecStr.startsWith("DST") || codecStr.startsWith("DSD")) {
    return 0x4000;  // dark red
  } else if (codecStr.startsWith("MP3") || codecStr.startsWith("AAC")) {
    return 0x8B00;  // orange
  } else {
    return 0x4208;  // dark gray
  }
}

void TFTPrintPlayerSongDuration() {
  // set color
  tft.setTextColor(0xFFFF, TFT_BLACK);

  // print duration in 00:00 format
  tft.drawString(((songDuration / 60 < 10) ? "0" : "") + String(songDuration / 60) + ":" + ((songDuration % 60 < 10) ? "0" : "") + String(songDuration % 60),
                 xpos + 118, 27, 1);
}

void TFTPrintPlayerSongPosition() {
  // set color
  tft.setTextColor(0xFFFF, TFT_BLACK);

  // print position in 00:00 format
  tft.drawString(((songPostion / 60) < 10 ? "0" : "") + String(songPostion / 60) + ":" + ((songPostion % 60) < 10 ? "0" : "") + String(songPostion % 60),
                 xpos, 27, 1);

  // draw position bar
  int xposSong = xpos;
  int xIndexPlayingPosition = map(((float)songPostion / (float)songDuration) * 100, 0, 100, 0, 24);
  for (int i = 0; i <= 24; i++) {
    xposSong += tft.drawString(i == xIndexPlayingPosition ? "+" : "-", xposSong, 37, 1);
  }
}

void TFTPrintPlayerSongGeneralInfo() {
  // clear song general info screen area
  tft.setTextColor(0xFFFF, TFT_BLACK);
  tft.drawString("                             ", 0, 49, 1);

  // print song general info
  tft.drawString(songBitDepth + "bits " + songSampleRate + "Hz " + songBitrate + "kbps",
                 xpos, 49, 1);
}

void TFTPrintPlayerSongMetadata(String value, int lineIndex) {
  // clear screen
  tft.setTextColor(0xFFFF, TFT_BLACK);
  tft.drawString("                               ", 0, ypos + songMetadataYPosOffset - 2 + lineIndex * 17, 2);

  // load han character
  tft.loadFont(Cubic12);

  // print artist/album/title name
  tft.setTextColor(0xFFFF, TFT_BLACK);
  tft.drawString(value, xpos, ypos + songMetadataYPosOffset + lineIndex * 17);

  // unload han character
  tft.unloadFont();
}

void TFTPrintPlayerSongCurrentLyric() {
  tft.setTextColor(0xFFFF, TFT_BLACK);
  tft.drawString("                           ", xpos, 114, 2);

  // load han character
  tft.loadFont(Cubic12);

  // print lyric
  tft.setTextColor(0xFFFF, TFT_BLACK);
  tft.drawString(songCurrentLyric, xpos, 116);

  // unload han character
  tft.unloadFont();
}

void ClearScreen(int startPoint, int endPoint, int perUnit) {
  tft.setTextColor(0xFFFF, TFT_BLACK);
  for (int i = startPoint; i <= endPoint; i += perUnit) {
    tft.setCursor(0, i);
    tft.println("                                   ");
  }
  tft.setCursor(0, 0);
}
