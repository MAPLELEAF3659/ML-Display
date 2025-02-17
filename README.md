# ML Display

## Installation

1. Create ```secrets.h``` in ```include``` folder

    ```c
    #define WEATHER_API_KEY "weatherapi.com key"
    #define CURRENCY_API_KEY "app.currencyapi.com key"
    ```

1. Create ```wifi_info.h``` in ```include``` folder

    ```c
    #define WIFI_SSID "SSID"
    #define WIFI_PASSWORD "PASSWORD"
    ```

1. Set upload settings
    - Board: ESP32 Dev Module
    - Partition Scheme: Custom(partitions.csv)
    - Flash Size: 16MB
    - Upload Speed: 921600
1. Build & upload to ESP32
