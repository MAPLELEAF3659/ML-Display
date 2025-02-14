#include <TFT_eSPI.h> // Graphics and font library for ST7735 driver chip
#include "Fonts/Custom/Cubic12.h"
#include <SPI.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "time.h"
#include <queue>
#include "secrets.h"
#include "wifi_info.h"

#define FINANCE_TOTAL_COUNT 5 // stock + currency
#define STOCK_COUNT 3

#define TIMER_COUNT 4
#define TASK_COUNT 1

#define PERIOD_MS_TWSE_UPDATE 5000
#define PERIOD_MS_OPEN_WEATHER_UPDATE 3600000
#define PERIOD_MS_CURRENCY_UPDATE 86400000
#define PERIOD_MS_FINANCE_PRINT 60000

/*
**Upload settings**
Board: ESP32 Dev Module
Partition Scheme: Custom(using partitions.csv)
Flash Size: 16MB
Upload Speed: 921600
*/

// GND=GND VCC=3V3 SCL=G14 SDA=G15 RES=G33 DC=G27 CS=G5 BL=G22

//**FreeRTOS**
TimerHandle_t timers[TIMER_COUNT]; //  twse, open_weather, currency, finance
TaskHandle_t tasks[TASK_COUNT];    // twse_all
enum RemainingMSCalculationType
{
  NextHour,
  NextMinute,
  Next9oclock,
};
typedef struct ScheduleTimerTaskParam_t
{
  TimerHandle_t timer;
  RemainingMSCalculationType scheduleType;
};

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
uint8_t secPrevious, minPrevious, hourPrevious, dayPrevious;
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
String openWeatherApiUrl = "https://opendata.cwa.gov.tw/api/v1/rest/datastore/O-A0001-001?Authorization=" + String(OPEN_WEATHER_DATA_API_KEY) + "&format=JSON&StationId=C0AI30&WeatherElement=Weather,AirTemperature,RelativeHumidity";
float weatherTemp = 0;
float weatherHumi = 0;
String weatherDesc = "";
//**Open weather data**

//**Finance data**
String twseApiUrl = "https://mis.twse.com.tw/stock/api/getStockInfo.jsp?ex_ch=tse_";
String currencyApiUrlLatest = "https://api.currencyapi.com/v3/latest?apikey=" + String(CURRENCY_API_KEY) + "&base_currency=TWD&currencies=JPY,USD";
String currencyApiUrlHistorical = "https://api.currencyapi.com/v3/historical?apikey=" + String(CURRENCY_API_KEY) + "&base_currency=TWD&currencies=JPY,USD&date=";
uint8_t financeIndex = -1;
// si_ = stock index; se_ = stock ETF; sn_ = stock normal; cu_ = currency;
String financeNumbers[FINANCE_TOTAL_COUNT] = {"si_t00", "se_0050", "sn_2330", "cu_JPY", "cu_USD"};
String financeNames[FINANCE_TOTAL_COUNT] = {"加權指數", "元大台灣50", "台積電", "日幣JPY 兌 台幣TWD", "美元USD 兌 台幣TWD"};
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

  // setup ntp server
  tft.print("[NTP] Setting up...");
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  getLocalTime(&timeinfo);
  tft.println("ok");

  // setup online data for first time
  // open weather
  tft.print("[OpenWeather] Updating...");
  HttpGetOpenWeatherInfo();
  tft.println("ok");
  // TWSE
  tft.print("[TWSE] Updating");
  xTaskCreate(UpdateTWSEInfo_All, "update_all_twse_info", 4096, (void *)1, tskIDLE_PRIORITY, &tasks[0]);
  while (financeYesterdayPrices[STOCK_COUNT - 1] == 0)
  {
    tft.print(".");
    delay(2000);
  }
  tft.println("ok");
  // currency
  tft.print("[Currency] Updating...");
  HttpGetCurrencyInfo();
  tft.println("ok");

  tft.print("[Timer] Creating timers...");
  timers[0] = xTimerCreate("update_twse", pdMS_TO_TICKS(PERIOD_MS_TWSE_UPDATE), pdTRUE, (void *)1, UpdateTWSEInfo);
  timers[1] = xTimerCreate("update_print_open_weather", pdMS_TO_TICKS(PERIOD_MS_OPEN_WEATHER_UPDATE), pdTRUE, (void *)0, UpdateAndPrintOpenWeatherInfo);
  timers[2] = xTimerCreate("update_currency", pdMS_TO_TICKS(PERIOD_MS_CURRENCY_UPDATE), pdTRUE, (void *)2, UpdateCurrencyInfo);
  timers[3] = xTimerCreate("print_finance", pdMS_TO_TICKS(PERIOD_MS_FINANCE_PRINT), pdTRUE, (void *)3, FinanceInfoPrint);
  tft.println("ok");

  // setup complete
  tft.println("Welcome to ML-Display!");
  delay(1000);
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
  }

  switch (screenState)
  {
  case MainScreen:
    ScreenUpdateMain();
    break;
  case PlayerScreen:
    if (serialMsg)
    {
      ScreenUpdatePlayer(serialMsg);
      serialMsg = "";
    }
    break;
  default:
    ClearScreen(0, 160, 5);
    break;
  }
}

void ChangeScreenState(ScreenState targetScreenState)
{
  if (screenState == targetScreenState)
  {
    return;
  }

  // update screen state
  screenState = targetScreenState;

  // stop all timers
  StopAllTimer();

  // force clear screen and previous states when screenState changed
  ClearScreen(0, 160, 5);

  // print first screen
  switch (screenState)
  {
  case MainScreen:
  {
    TFTPrintDate();
    TFTPrintTime();
    TFTPrintTimeSec();
    TFTPrintSecBlink();
    TFTPrintOpenWeatherInfo();
    financeIndex = 0;
    TFTPrintFinanceInfo(financeIndex);

    if (isTWSEOpening)
      xTimerStart(timers[0], 0); // twse

    xTimerChangePeriod(timers[1], pdMS_TO_TICKS(getRemainingMS(NextHour)), 0); // open weather
    xTimerChangePeriod(timers[2], pdMS_TO_TICKS(getRemainingMS(Next9oclock)), 0); // currency
    xTimerChangePeriod(timers[3], pdMS_TO_TICKS(getRemainingMS(NextMinute)), 0); // finance print
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

void ScreenUpdateMain()
{
  // update by day
  if (timeinfo.tm_mday != dayPrevious)
  {
    TFTPrintDate();

    dayPrevious = timeinfo.tm_mday;
  }

  // update by hour
  if (timeinfo.tm_hour != hourPrevious)
  {
    hourPrevious = timeinfo.tm_hour;
  }

  // update by min
  if (timeinfo.tm_min != minPrevious)
  {
    // print time hh:mm
    TFTPrintTime();

    if (isTWSEOpening() && xTimerIsTimerActive(timers[0]) == pdFALSE)
      xTimerStart(timers[0], 0);
    else if (!isTWSEOpening() && xTimerIsTimerActive(timers[0]) != pdFALSE)
    {
      xTimerStop(timers[0], 0);
      xTaskCreate(UpdateTWSEInfo_All, "update_all_twse_info", 4096, (void *)1, tskIDLE_PRIORITY, &tasks[0]);
    }

    // update previous state
    minPrevious = timeinfo.tm_min;
  }

  // update by sec
  if (timeinfo.tm_sec != secPrevious)
  {
    TFTPrintSecBlink();
    TFTPrintTimeSec();

    // update previous state
    secPrevious = timeinfo.tm_sec;
  }
}

void ScreenUpdatePlayer(String msg)
{
  PlayerInfoId playerInfoId = (PlayerInfoId)(msg.substring(msg.indexOf('$') + 1, msg.lastIndexOf('$')).toInt());
  String value = msg.substring(msg.lastIndexOf('$') + 1);
  PlayerInfoUpdate(playerInfoId, value);
}

// **timer & task**
void UpdateAndPrintOpenWeatherInfo(TimerHandle_t xTimer)
{
  HttpGetOpenWeatherInfo();
  TFTPrintOpenWeatherInfo();
  if (pdTICKS_TO_MS(xTimerGetPeriod(xTimer)) != PERIOD_MS_OPEN_WEATHER_UPDATE)
    xTimerChangePeriod(xTimer, PERIOD_MS_OPEN_WEATHER_UPDATE, 0);
}

void UpdateTWSEInfo(TimerHandle_t xTimer)
{
  if (financeIndex < STOCK_COUNT)
  {
    if (timeinfo.tm_sec <= 50)
    {
      HttpGetTWSEInfo(financeIndex);
      TFTPrintFinanceInfo_PriceOnly(financeIndex);
    }
    else
    {
      if (financeIndex + 1 < STOCK_COUNT)
        HttpGetTWSEInfo(financeIndex + 1);
    }
  }
  else if (financeIndex + 1 == FINANCE_TOTAL_COUNT)
  {
    if (timeinfo.tm_sec > 50)
      HttpGetTWSEInfo(0);
  }
}

void UpdateTWSEInfo_All(void *pvParameters)
{
  for (uint8_t i = 0; i < STOCK_COUNT; i++)
  {
    HttpGetTWSEInfo(i);
    vTaskDelay(pdMS_TO_TICKS(2000));
  }

  vTaskDelete(NULL);
}

void UpdateCurrencyInfo(TimerHandle_t xTimer)
{
  HttpGetCurrencyInfo();

  if (pdTICKS_TO_MS(xTimerGetPeriod(xTimer)) != PERIOD_MS_CURRENCY_UPDATE)
    xTimerChangePeriod(xTimer, PERIOD_MS_CURRENCY_UPDATE, 0);
}

void FinanceInfoPrint(TimerHandle_t xTimer)
{
  if (financeIndex >= FINANCE_TOTAL_COUNT - 1)
  {
    financeIndex = 0;
  }
  else
  {
    financeIndex++;
  }
  TFTPrintFinanceInfo(financeIndex);

  if (pdTICKS_TO_MS(xTimerGetPeriod(xTimer)) != PERIOD_MS_FINANCE_PRINT)
    xTimerChangePeriod(xTimer, PERIOD_MS_FINANCE_PRINT, 0);
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

// **HTTP Get**
void HttpGetOpenWeatherInfo()
{
  // Make an HTTP request
  HTTPClient http;
  http.begin(openWeatherApiUrl); // replace with your URL
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK)
  {
    // Parse the JSON response
    String payload = http.getString();
    DynamicJsonDocument doc(4096);
    deserializeJson(doc, payload);

    float tempTemp, tempHumi;
    String tempDesc;
    tempTemp = doc["records"]["Station"][0]["WeatherElement"]["AirTemperature"].as<float>();
    tempHumi = doc["records"]["Station"][0]["WeatherElement"]["RelativeHumidity"].as<float>();
    tempDesc = doc["records"]["Station"][0]["WeatherElement"]["Weather"].as<String>();

    if (tempTemp != -99)
      weatherTemp = tempTemp;
    if (tempHumi != -99)
      weatherHumi = tempHumi;
    if (tempDesc != "-99")
      weatherDesc = tempDesc;
  }
  else
  {
    Serial.println("Error getting JSON data");
  }

  // Turn off http client
  http.end();
}

void HttpGetTWSEInfo(int index)
{
  // Make an HTTP request
  HTTPClient http;
  http.begin(twseApiUrl + financeNumbers[index].substring(3) + ".tw"); // replace with your URL
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK)
  {
    // Parse the JSON response
    String payload = http.getString();
    DynamicJsonDocument doc(4096);
    deserializeJson(doc, payload);

    if (doc["msgArray"][0]["z"] != "-")
    {
      financePrices[index] = doc["msgArray"][0]["z"].as<float>();
    }
    financeYesterdayPrices[index] = doc["msgArray"][0]["y"].as<float>();
  }
  else
  {
    Serial.println("Error getting JSON data");
  }

  // Turn off http client
  http.end();
}

void HttpGetCurrencyInfo()
{
  HTTPClient http;
  int httpCode;

  // Make an HTTP request
  http.begin(currencyApiUrlLatest); // replace with your URL
  httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK)
  {
    // Parse the JSON response
    String payload = http.getString();
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, payload);

    for (uint8_t i = STOCK_COUNT; i < FINANCE_TOTAL_COUNT; i++)
    {
      financeYesterdayPrices[i] == financePrices[i];
      financePrices[i] = 1 / doc["data"][financeNumbers[i].substring(3)]["value"].as<float>();
    }
  }
  else
  {
    Serial.println("Error getting JSON data");
  }

  // Turn off http client
  http.end();

  // for first time setup
  if (financeYesterdayPrices[STOCK_COUNT] == 0)
  {
    struct tm previousTM = timeinfo;
    time_t previousEpoch = mktime(&previousTM);
    // if time is not Currency API update time(UTC+8 08:00), use the day before yesterday
    previousEpoch -= 86400 + (timeinfo.tm_hour > 8 ? 86400 : 0);
    localtime_r(&previousEpoch, &previousTM);
    char previousUpdateDateStr[11];
    strftime(previousUpdateDateStr, sizeof(previousUpdateDateStr), "%Y-%m-%d", &previousTM);

    // Make an HTTP request
    // using previous update date (yesterday or -2d)
    http.begin(currencyApiUrlHistorical + previousUpdateDateStr); // replace with your URL
    httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK)
    {
      // Parse the JSON response
      String payload = http.getString();
      DynamicJsonDocument doc(1024);
      deserializeJson(doc, payload);

      for (uint8_t i = STOCK_COUNT; i < FINANCE_TOTAL_COUNT; i++)
      {
        financeYesterdayPrices[i] = 1 / doc["data"][financeNumbers[i].substring(3)]["value"].as<float>();
      }
    }
    else
    {
      Serial.println("Error getting JSON data");
    }

    // Turn off http client
    http.end();
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

  // print temperature
  tft.setTextColor(0xFFFF, TFT_BLACK);
  tft.drawString(weatherDesc, x_pad + 5, y_pad + 72);

  tft.setTextColor(TextColorByTemperature(weatherTemp), TFT_BLACK);
  tft.drawString((weatherTemp >= 10 ? "" : " ") + String(weatherTemp, 1) + "℃", x_pad + 70, y_pad + 72);

  // print humidity
  tft.setTextColor(TextColorByHumidity(weatherHumi), TFT_BLACK);
  tft.drawString((weatherHumi >= 10 ? "" : " ") + String(weatherHumi, 1) + "%", x_pad + 110, y_pad + 72);

  tft.unloadFont();
}

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
  else if (temp < 14)
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
  else if (humi < 20)
  {
    return 0xF800; // red
  }
}

// **Finance**
void TFTPrintFinanceInfo(uint8_t index)
{
  tft.setTextColor(0xFFFF, TFT_BLACK);
  tft.drawString("                                              ", x_pad, y_pad + 90, 2);
  tft.drawString("                                              ", x_pad, y_pad + 110, 2);

  tft.loadFont(Cubic12);

  // print name
  String number, type;
  // if number is not index or currency, then print its stock number
  if (!(financeNumbers[index].startsWith("si_") || financeNumbers[index].startsWith("cu_")))
  {
    number = financeNumbers[index].substring(3) + "  ";
  }
  if (financeNumbers[index].startsWith("se_"))
  {
    type = "ETF";
  }
  else if (financeNumbers[index].startsWith("si_"))
  {
    type = "INDEX";
  }
  tft.setTextColor(0xFFFF, TFT_BLACK);
  tft.drawString(financeNames[index] + "  " + number + type, x_pad + 5, y_pad + 92);

  // print price
  tft.setTextColor(0xFFFF, TFT_BLACK);
  tft.drawString(String(financePrices[index], index < STOCK_COUNT ? 2 : 4), x_pad + 5, y_pad + 112);

  // print price change
  float changeAmount = financePrices[index] - financeYesterdayPrices[index];
  float changePercent = (financePrices[index] / financeYesterdayPrices[index] - 1.0) * 100;
  tft.setTextColor(TextColorByAmount(changeAmount), TFT_BLACK);
  tft.drawString((changeAmount >= 0 ? "+" : "") + String(changeAmount, index < STOCK_COUNT ? 2 : 4) + "(" + String(abs(changePercent), changePercent >= 10 ? 1 : 2) + "%)",
                 x_pad + 65, y_pad + 112);

  tft.unloadFont();
}

void TFTPrintFinanceInfo_PriceOnly(uint8_t index)
{
  tft.setTextColor(0xFFFF, TFT_BLACK);
  tft.drawString("                                              ", x_pad, y_pad + 110, 2);

  tft.loadFont(Cubic12);

  // print price
  tft.setTextColor(0xFFFF, TFT_BLACK);
  tft.drawString(String(financePrices[index], index < STOCK_COUNT ? 2 : 4), x_pad + 5, y_pad + 112);

  // print price change
  float changeAmount = financePrices[index] - financeYesterdayPrices[index];
  float changePercent = (financePrices[index] / financeYesterdayPrices[index] - 1.0) * 100;
  tft.setTextColor(TextColorByAmount(changeAmount), TFT_BLACK);
  tft.drawString((changeAmount >= 0 ? "+" : "") + String(changeAmount, index < STOCK_COUNT ? 2 : 4) + "(" + String(abs(changePercent), changePercent >= 10 ? 1 : 2) + "%)",
                 x_pad + 65, y_pad + 112);

  tft.unloadFont();
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
  tft.drawString("                               ", 0, y_pad + songMetadataYPosOffset - 2 + lineIndex * 17, 2);

  // load han character
  tft.loadFont(Cubic12);

  // print artist/album/title name
  tft.setTextColor(0xFFFF, TFT_BLACK);
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

void StopAllTimer()
{
  for (uint8_t i = 0; i < TIMER_COUNT; i++)
  {
    if (timers[i] != NULL)
      xTimerStop(timers[i], 0);
  }
}

bool isTWSEOpening()
{
  return ((timeinfo.tm_wday > 0 && timeinfo.tm_wday < 6) &&
          ((timeinfo.tm_hour >= 9 && timeinfo.tm_hour < 13) ||
           (timeinfo.tm_hour == 13 && timeinfo.tm_min <= 30)));
}

double getRemainingMS(RemainingMSCalculationType type)
{
  getLocalTime(&timeinfo);
  time_t nowEpoch = mktime(&timeinfo);
  struct tm nextTM = timeinfo;
  switch (type)
  {
  case NextMinute:
  {
    nextTM.tm_min += 1;
    nextTM.tm_sec = 0;
  }
  break;
  case NextHour:
  {
    nextTM.tm_hour += 1;
    nextTM.tm_min = 0;
    nextTM.tm_sec = 0;
  }
  break;
  case Next9oclock:
  {
    if (timeinfo.tm_hour >= 9)
    {
      nextTM.tm_mday += 1;
    }
    nextTM.tm_hour = 9;
    nextTM.tm_min = 0;
    nextTM.tm_sec = 0;
  }
  break;
  }
  time_t nextEpoch = mktime(&nextTM);
  return difftime(nextEpoch, nowEpoch) * 1000UL;
}