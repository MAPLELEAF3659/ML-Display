#include <Arduino.h>
#include <TFT_eSPI.h> // Graphics and font library for ST7735 driver chip
#include "Fonts/Custom/Cubic12.h"
#include <SPI.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "time.h"
#include "secrets.h"
#include "wifi_info.h"

#define FINANCE_TOTAL_COUNT 5 // stock + currency
#define STOCK_COUNT 3
#define TWSE_PERIOD 16200000 // 09:00~13:30 -> ms

/*
**Upload settings**
Board: ESP32 Dev Module
Partition Scheme: Custom(using partitions.csv)
Flash Size: 16MB
Upload Speed: 921600
*/

// GND=GND VCC=3V3 SCL=G14 SDA=G15 RES=G33 DC=G27 CS=G5 BL=G22

//**FreeRTOS**
TaskHandle_t taskHttpGet;
QueueHandle_t queueHttpGet;
enum RequestHttpGetType
{
  Weather,
  TWSE,
  Currency
};
struct RequestHttpGet
{
  RequestHttpGetType type;
  uint8_t index;
};
RequestHttpGet httpGetReq;
Preferences preferences;
//**FreeRTOS**

//**WiFi**
const char *ssid = WIFI_SSID;
const char *password = WIFI_PASSWORD;
//**WiFi**

//**NTP**
const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 28800; // GMT+8
const int daylightOffset_sec = 0;
struct tm timeinfo;
uint8_t secPrev, minPrev, hourPrev, dayPrev;
bool isWorkingDay;
//**NTP**

//**TFT**
TFT_eSPI tft = TFT_eSPI(); // Invoke library, pins defined in User_Setup.h
enum ScreenState
{
  NoneScreen = -1,
  MainScreen,
  PlayerScreen
};
ScreenState screenState = NoneScreen;
uint8_t x_pad = 5, y_pad = 5;
//**TFT**

//**Open weather data**
String weatherApiUrl = "http://api.weatherapi.com/v1/current.json?q=Sanchung&aqi=no&lang=zh_tw&key=" + String(WEATHER_API_KEY);
bool isWeatherPrinted = true;
float weatherTemp = 0;
int weatherHumi = 0;
String weatherDesc = "";
//**Open weather data**

//**Finance data**
String currencyApiUrlLatest = "https://api.currencyapi.com/v3/latest?apikey=" + String(CURRENCY_API_KEY) + "&base_currency=TWD&currencies=JPY,USD";
String currencyApiUrlHistorical = "https://api.currencyapi.com/v3/historical?apikey=" + String(CURRENCY_API_KEY) + "&base_currency=TWD&currencies=JPY,USD&date=";
uint8_t financeIndex = 0;
uint8_t financeIndexPrev = -1;
bool isFinancePrinted = true;
bool isTWSEOpening = false;
bool isTWSEOpeningPrev = false;
int currencyUpdateDate;
// si_ = stock index; se_ = stock ETF; sn_ = stock normal; cu_ = currency;
String financeNumbers[FINANCE_TOTAL_COUNT] = {"si_t00", "se_0050", "sn_2330", "cu_JPY", "cu_USD"};
String financeNames[FINANCE_TOTAL_COUNT] = {"加權指數", "元大台灣50", "台積電", "日幣_台幣 JPY_TWD", "美元_台幣 USD_TWD"};
float financePrices[FINANCE_TOTAL_COUNT];
float financeYesterdayPrices[FINANCE_TOTAL_COUNT];
//**Finanse data**

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
int songMetadataYPosOffset = 58; // for tft print
//**Player info**

// **Utils**
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

// **Callbacks**
void vTaskHttpGetCallback(void *pvParameters)
{
  while (true)
  {
    RequestHttpGet req;
    if (xQueueReceive(queueHttpGet, &req, 1000) == pdTRUE)
    {
      HTTPClient http;
      switch (req.type)
      {
      case Weather:
      {
        http.begin(weatherApiUrl);
      }
      break;
      case TWSE:
      {
        if (req.index == 255) // 255 = update all
        {
          // queue all TWSE indexes
          for (uint8_t i = 0; i < STOCK_COUNT; i++)
          {
            req.index = i;
            xQueueSend(queueHttpGet, &req, 100);
          }
          continue;
        }
        http.begin("https://mis.twse.com.tw/stock/api/getStockInfo.jsp?ex_ch=tse_" + financeNumbers[req.index].substring(3) + ".tw");
      }
      break;
      case Currency:
      {
        if (req.index == 0)
        {
          http.begin(currencyApiUrlLatest);
        }
        else if (req.index == 1)
        {
          struct tm tempTM = timeinfo;
          tempTM.tm_hour -= 8; // gmt+8 to utc
          tempTM.tm_mday -= 2;
          mktime(&tempTM);
          char previousDateBuffer[11];
          strftime(previousDateBuffer, sizeof(previousDateBuffer), "%Y-%m-%d", &tempTM);
          http.begin(currencyApiUrlHistorical + previousDateBuffer);
        }
      }
      break;
      }

      if (http.GET() == HTTP_CODE_OK)
      {
        String payload;
        JsonDocument doc;
        payload = http.getString();
        deserializeJson(doc, payload);

        switch (req.type)
        {
        case Weather:
        {
          weatherTemp = doc["current"]["temp_c"].as<float>();
          weatherHumi = doc["current"]["humidity"].as<int>();
          weatherDesc = doc["current"]["condition"]["text"].as<String>();
          isWeatherPrinted = false;
        }
        break;
        case TWSE:
        {
          // update specific index of stock
          if (doc["msgArray"][0]["z"] != "-")
          {
            if (financePrices[req.index] != doc["msgArray"][0]["z"].as<float>())
            {
              financePrices[req.index] = doc["msgArray"][0]["z"].as<float>();
              financeYesterdayPrices[req.index] = doc["msgArray"][0]["y"].as<float>();
              isFinancePrinted = false;
            }
          }
        }
        break;
        case Currency:
        {
          preferences.begin("storage");

          for (uint8_t i = STOCK_COUNT; i < FINANCE_TOTAL_COUNT; i++)
          {
            float fetchedPrice = 1 / doc["data"][financeNumbers[i].substring(3)]["value"].as<float>();
            if (req.index == 0)
            {
              financeYesterdayPrices[i] = financePrices[i];
              financePrices[i] = fetchedPrice;
              preferences.putFloat(("c_y_" + String(i)).c_str(), financeYesterdayPrices[i]);
              preferences.putFloat(("c_" + String(i)).c_str(), financePrices[i]);
            }
            else if (req.index == 1)
            {
              financeYesterdayPrices[i] = fetchedPrice;
              preferences.putFloat(("c_y_" + String(i)).c_str(), financeYesterdayPrices[i]);
            }
            currencyUpdateDate = (timeinfo.tm_year + 1900) * 10000 + (timeinfo.tm_mon + 1) * 100 + timeinfo.tm_mday;
            preferences.putUInt("c_date", currencyUpdateDate);
            preferences.end();
          }
        }
        break;
        }
      }
      else
      {
        Serial.println("HTTP GET failed.");
      }
      http.end();
    }
  }
}

// **Text Color**
int TextColorByTemperature(float temp)
{
  if (temp >= 34)
  {
    return 0xF800; // red
  }
  else if (temp < 34 && temp >= 30)
  {
    return 0xFC00; // orange
  }
  else if (temp < 30 && temp >= 26)
  {
    return 0xFFE0; // yellow
  }
  else if (temp < 26 && temp >= 22)
  {
    return 0x07E0; // green
  }
  else if (temp < 22 && temp >= 18)
  {
    return 0x07FC; // blue green
  }
  else if (temp < 18 && temp >= 14)
  {
    return 0x067F; // light blue
  }
  else
  {
    return 0x077F; // lighter blue
  }
}

int TextColorByHumidity(float humi)
{
  if (humi >= 90)
  {
    return 0x077F; // lighter blue
  }
  else if (humi < 90 && humi >= 75)
  {
    return 0x067F; // light blue
  }
  else if (humi < 75 && humi >= 60)
  {
    return 0x07F7; // green blue
  }
  else if (humi < 60 && humi >= 40)
  {
    return 0x07E0; // green
  }
  else if (humi < 40)
  {
    return 0xFC00; // orange
  }
  else
  {
    return 0xF800; // red
  }
}

int TextColorByAmount(float amount)
{
  if (amount > 0)
  {
    return 0xF800; // red
  }
  else if (amount < 0)
  {
    return 0x07E0; // green
  }
  else
  {
    return 0xFFFF; // green blue
  }
}

int TextBackgroundColorByCodec(String codecStr)
{
  if (codecStr.startsWith("FLAC"))
  {
    return 0x0200; // dark green
  }
  else if (codecStr.startsWith("PCM"))
  {
    return 0x020C; // dark blue
  }
  else if (codecStr.startsWith("DST") || codecStr.startsWith("DSD"))
  {
    return 0x4000; // dark red
  }
  else if (codecStr.startsWith("MP3") || codecStr.startsWith("AAC"))
  {
    return 0x8B00; // orange
  }
  else
  {
    return 0x4208; // dark gray
  }
}

// **Time & Date**
void TFTPrintTime()
{
  int xposTime = x_pad + 5;
  int yposTime = y_pad + 15;

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

void TFTPrintSecBlink()
{
  // print ":" background
  tft.setTextColor(0x39C4, TFT_BLACK);
  tft.drawString(":", x_pad + 70, y_pad + 15, 7);

  // print ":" (blink it)
  tft.setTextColor(0xFFFF);
  tft.drawChar(timeinfo.tm_sec % 2 == 0 ? ':' : ' ', x_pad + 70, y_pad + 15, 7);
}

void TFTPrintTimeSec()
{
  tft.setTextColor(0xFFFF, TFT_BLACK);
  tft.drawString((timeinfo.tm_sec < 10 ? "0" : "") + String(timeinfo.tm_sec), x_pad + 130, y_pad, 1);
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
  tft.drawString(String(timeinfo.tm_year + 1900) + "/" + (timeinfo.tm_mon < 9 ? "0" : "") + String(timeinfo.tm_mon + 1) + "/" + (timeinfo.tm_mday < 10 ? "0" : "") + String(timeinfo.tm_mday) + " " + dayOfWeekStr + "  ",
                 x_pad + 5, y_pad, 1);
}

// **Weather**
void TFTPrintOpenWeatherInfo()
{
  tft.setTextColor(0xFFFF, TFT_BLACK);
  tft.drawString("                                    ", x_pad, y_pad + 70, 2);

  tft.loadFont(Cubic12);

  // print description
  tft.setTextColor(0xFFFF, TFT_BLACK);
  tft.drawString(weatherDesc, x_pad + 5, y_pad + 72);

  // print temperature
  tft.setTextColor(TextColorByTemperature(weatherTemp), TFT_BLACK);
  tft.drawString((weatherTemp >= 10 ? "" : " ") + String(weatherTemp, 1) + "℃", x_pad + 80, y_pad + 72);

  // print humidity
  tft.setTextColor(TextColorByHumidity(weatherHumi), TFT_BLACK);
  tft.drawString((weatherHumi >= 10 ? "" : " ") + String(weatherHumi) + "%", x_pad + 120, y_pad + 72);

  tft.unloadFont();

  isWeatherPrinted = true;
}

// **Finance**
void TFTPrintFinanceInfo()
{
  tft.setTextColor(0xFFFF, TFT_BLACK);
  if (financeIndex != financeIndexPrev)
    tft.drawString("                                              ", x_pad, y_pad + 90, 2);
  tft.drawString("                                              ", x_pad, y_pad + 110, 2);

  tft.loadFont(Cubic12);

  // print name
  if (financeIndex != financeIndexPrev)
  {
    String type, number;
    // if number is stock in etf or index, then print its stock type
    if (financeNumbers[financeIndex].startsWith("se_"))
    {
      type = "ETF ";
    }
    else if (financeNumbers[financeIndex].startsWith("si_"))
    {
      type = "INDEX ";
    }
    // if number is stock in normal or etf, then print its stock number
    if ((financeNumbers[financeIndex].startsWith("sn_") || financeNumbers[financeIndex].startsWith("se_")))
    {
      number = financeNumbers[financeIndex].substring(3);
    }
    tft.setTextColor(0xFFFF, TFT_BLACK);
    tft.drawString(financeNames[financeIndex] + "  " + type + number, x_pad + 5, y_pad + 92);
    financeIndexPrev = financeIndex;
  }

  // print price
  if (financePrices[financeIndex])
  {
    tft.setTextColor(0xFFFF, TFT_BLACK);
    tft.drawString(String(financePrices[financeIndex], financeIndex < STOCK_COUNT ? 2 : 4), x_pad + 5, y_pad + 112);
  }
  else
  {
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.drawString("--", x_pad + 5, y_pad + 112);
  }

  // print price change
  if (financePrices[financeIndex] && financeYesterdayPrices[financeIndex])
  {
    float changeAmount = financePrices[financeIndex] - financeYesterdayPrices[financeIndex];
    float changePercent = (financePrices[financeIndex] / financeYesterdayPrices[financeIndex] - 1.0) * 100;
    tft.setTextColor(TextColorByAmount(changeAmount), TFT_BLACK);
    tft.drawString((changeAmount >= 0 ? "+" : "") + String(changeAmount, financeIndex < STOCK_COUNT ? 2 : 4) + "(" + String(abs(changePercent), changePercent >= 10 ? 1 : 2) + "%)",
                   x_pad + 65, y_pad + 112);
  }
  else
  {
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.drawString("--", x_pad + 65, y_pad + 112);
  }

  tft.unloadFont();

  isFinancePrinted = true;
}

// **Player**
void TFTPrintPlayerState()
{
  // clear player state screen area
  tft.setTextColor(0xFFFF, TFT_BLACK);
  tft.drawString("           ", x_pad, 5, 2);
  switch (playerState)
  {
  case 0:
    tft.setTextColor(0x07E0, TFT_BLACK);
    tft.drawString("Playing >", x_pad, 5, 2);
    break;
  case 1:
    tft.setTextColor(0x001F, TFT_BLACK);
    tft.drawString("Pause ||", x_pad, 5, 2);
    break;
  case 2:
    tft.setTextColor(0xF800, TFT_BLACK);
    tft.drawString("Stop []", x_pad, 5, 2);
    break;
  }
}

void TFTPrintPlayerSongCodec()
{
  // clear song codec screen area
  tft.setTextColor(0xFFFF, TFT_BLACK);
  tft.drawString("            ", x_pad + 85, 5, 2);

  tft.setTextColor(0xFFFF, TextBackgroundColorByCodec(songCodec));
  // print song codec
  int xposCodec = x_pad + 125;
  tft.drawString(" " + songCodec + " ", xposCodec + (songCodec.length() * -5.2), 5, 2);
}

void TFTPrintPlayerSongDuration()
{
  // set color
  tft.setTextColor(0xFFFF, TFT_BLACK);

  // print duration in 00:00 format
  tft.drawString(((songDuration / 60 < 10) ? "0" : "") + String(songDuration / 60) + ":" + ((songDuration % 60 < 10) ? "0" : "") + String(songDuration % 60),
                 x_pad + 118, 27, 1);
}

void TFTPrintPlayerSongPosition()
{
  // set color
  tft.setTextColor(0xFFFF, TFT_BLACK);

  // print position in 00:00 format
  tft.drawString(((songPostion / 60) < 10 ? "0" : "") + String(songPostion / 60) + ":" + ((songPostion % 60) < 10 ? "0" : "") + String(songPostion % 60),
                 x_pad, 27, 1);

  // draw position bar
  int xposSong = x_pad;
  int xIndexPlayingPosition = map(((float)songPostion / (float)songDuration) * 100, 0, 100, 0, 24);
  for (int i = 0; i <= 24; i++)
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
  tft.drawString(songBitDepth + "bits " + songSampleRate + "Hz " + songBitrate + "kbps",
                 x_pad, 49, 1);
}

void TFTPrintPlayerSongMetadata(String value, int lineIndex)
{
  // clear screen
  tft.setTextColor(0xFFFF, TFT_BLACK);
  tft.drawString("                       ", 0, y_pad + songMetadataYPosOffset - 2 + lineIndex * 17, 2);

  // load han character
  tft.loadFont(Cubic12);

  // print artist/album/title name
  tft.setTextColor(0xFFFF);
  tft.drawString(value, x_pad, y_pad + songMetadataYPosOffset + lineIndex * 17);

  // unload han character
  tft.unloadFont();
}

void TFTPrintPlayerSongCurrentLyric()
{
  tft.setTextColor(0xFFFF, TFT_BLACK);
  tft.drawString("                           ", x_pad, 114, 2);

  // load han character
  tft.loadFont(Cubic12);

  // print lyric
  tft.setTextColor(0xFFFF, TFT_BLACK);
  tft.drawString(songCurrentLyric, x_pad, 116);

  // unload han character
  tft.unloadFont();
}

void PlayerInfoUIUpdate(PlayerInfoId infoId, String value)
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

// **UI Update**
void ScreenUIUpdateMain()
{
  // update by day
  if (timeinfo.tm_mday != dayPrev)
  {
    TFTPrintDate();
    isWorkingDay = (timeinfo.tm_wday > 0 && timeinfo.tm_wday < 6);

    dayPrev = timeinfo.tm_mday;
  }

  // update by hour
  if (timeinfo.tm_hour != hourPrev)
  {
    // update weather
    httpGetReq.type = Weather;
    httpGetReq.index = 0;
    xQueueSend(queueHttpGet, &httpGetReq, 100);

    // update currency
    if (timeinfo.tm_hour == 8)
    {
      httpGetReq.type = Currency;
      httpGetReq.index = 0;
      xQueueSend(queueHttpGet, &httpGetReq, 100);
    }

    // update TWSE
    bool isTWSEOpeningTemp = isWorkingDay && (timeinfo.tm_hour >= 9 && timeinfo.tm_hour <= 13);
    if (isTWSEOpening != isTWSEOpeningTemp)
    {
      httpGetReq.type = TWSE;
      httpGetReq.index = 255;
      xQueueSend(queueHttpGet, &httpGetReq, 100);
    }
    isTWSEOpening = isTWSEOpeningTemp;

    hourPrev = timeinfo.tm_hour;
  }

  // update by min
  if (timeinfo.tm_min != minPrev)
  {
    // print time hh:mm
    TFTPrintTime();

    if (financeIndex == FINANCE_TOTAL_COUNT - 1)
    {
      financeIndex = 0;
    }
    else
    {
      financeIndex++;
    }

    // update previous state
    minPrev = timeinfo.tm_min;
  }

  // update by sec
  if (timeinfo.tm_sec != secPrev)
  {
    TFTPrintSecBlink();
    TFTPrintTimeSec();

    if (isTWSEOpening && financeIndex < STOCK_COUNT)
    {
      if (timeinfo.tm_sec % 5 == 0)
      {
        httpGetReq.type = TWSE;
        httpGetReq.index = financeIndex;
        xQueueSend(queueHttpGet, &httpGetReq, 100);
      }
    }
    else if (timeinfo.tm_sec == 0)
    {
      isFinancePrinted = false;
    }

    // update previous state
    secPrev = timeinfo.tm_sec;
  }

  if (!isWeatherPrinted)
    TFTPrintOpenWeatherInfo();

  if (!isFinancePrinted)
    TFTPrintFinanceInfo();
}

void ScreenUIUpdatePlayer(String msg)
{
  PlayerInfoId playerInfoId = (PlayerInfoId)(msg.substring(msg.indexOf('$') + 1, msg.lastIndexOf('$')).toInt());
  String value = msg.substring(msg.lastIndexOf('$') + 1);
  PlayerInfoUIUpdate(playerInfoId, value);
}

void ChangeScreenState(ScreenState targetScreenState)
{
  if (screenState == targetScreenState)
  {
    return;
  }

  // update screen state
  screenState = targetScreenState;

  // force clear screen and previous states when screenState changed
  ClearScreen(0, 160, 5);

  // print first screen
  switch (screenState)
  {
  case MainScreen:
  {
    dayPrev = -1;
    hourPrev = -1;
    minPrev = -1;
    secPrev = -1;
    financeIndex = -1;
    financeIndexPrev = -2;
    isFinancePrinted = false;
  }
  break;
  case PlayerScreen:
  {
    TFTPrintPlayerState();
    TFTPrintPlayerSongDuration();
    TFTPrintPlayerSongPosition();
    TFTPrintPlayerSongGeneralInfo();
  }
  break;
  }
}

void setup()
{
  Serial.begin(115200);

  tft.init();
  tft.setRotation(-1);
  tft.fillScreen(TFT_BLACK);

  ClearScreen(0, 160, 5);

  tft.setTextColor(TFT_YELLOW, TFT_BLACK); // Note: the new fonts do not draw the background colour
  tft.setCursor(0, 5);

  // connect to wifi
  tft.print("[Wi-Fi] " + String(ssid));
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    tft.print(".");
  }
  tft.println("ok");
  delay(1000);

  // setup ntp server
  tft.print("[NTP] Setup...");
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  getLocalTime(&timeinfo);
  tft.println("ok");
  delay(1000);

  // setup currency data from preferences
  tft.print("[PREF] Setup currency...");
  if (preferences.begin("storage", true))
  {
    for (uint8_t i = STOCK_COUNT; i < FINANCE_TOTAL_COUNT; i++)
    {
      financePrices[i] = preferences.getFloat(("c_" + String(i)).c_str(), 0.0F);
      financeYesterdayPrices[i] = preferences.getFloat(("c_y_" + String(i)).c_str(), 0.0F);
    }
    currencyUpdateDate = preferences.getUInt("c_date", 0U);
    preferences.end();
  }
  tft.println("ok");
  delay(1000);

  // setup http get task
  tft.print("[HTTP] Create task...");
  queueHttpGet = xQueueCreate(10, sizeof(RequestHttpGet));
  xTaskCreatePinnedToCore(vTaskHttpGetCallback, "task_http_get", 8192, NULL, 1, &taskHttpGet, 0);
  tft.println("ok");

  if (currencyUpdateDate < (timeinfo.tm_year + 1900) * 10000 + (timeinfo.tm_mon + 1) * 100 + timeinfo.tm_mday)
  {
    tft.print("[HTTP] Update currency");
    httpGetReq.type = Currency;
    httpGetReq.index = 0;
    xQueueSend(queueHttpGet, &httpGetReq, 100);
    tft.print(".");
    delay(1000);
    httpGetReq.index = 1;
    xQueueSend(queueHttpGet, &httpGetReq, 100);
    tft.println(".ok");
  }

  tft.print("[HTTP] Update TWSE");
  httpGetReq.type = TWSE;
  httpGetReq.index = 255;
  xQueueSend(queueHttpGet, &httpGetReq, 100);
  while (financePrices[STOCK_COUNT - 1] <= 0)
  {
    delay(1000);
    tft.print(".");
  }
  tft.println("ok");

  // setup complete
  tft.println(" Welcome to ML-Display!");
  delay(3000);
  ChangeScreenState(MainScreen);
}

String serialMsg;
void loop()
{
  getLocalTime(&timeinfo);

  if (Serial.available())
  {
    serialMsg = Serial.readStringUntil('\n');
    ScreenState targetScreenState = (ScreenState)serialMsg.substring(0, serialMsg.indexOf('$')).toInt();
    ChangeScreenState(targetScreenState);
    if (screenState == PlayerScreen)
    {
      ScreenUIUpdatePlayer(serialMsg);
    }
    serialMsg = "";
  }

  switch (screenState)
  {
  case MainScreen:
    ScreenUIUpdateMain();
    break;
  }
}