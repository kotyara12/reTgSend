# reTgSend for ESP32 and ESP-IDF

Library for sending notifications to Telegram from a device to ESP32. There is a built-in queue of outgoing messages for delayed sending (for example, when there is no Internet)

## Dependencies:
  - https://github.com/kotyara12/rLog
  - https://github.com/kotyara12/rStrings
  - https://github.com/kotyara12/reLed
  - https://github.com/kotyara12/reWifi

### Notes:
  - libraries starting with the <b>re</b> prefix are only suitable for ESP32 and ESP-IDF
  - libraries starting with the <b>ra</b> prefix are only suitable for ARDUINO compatible code
  - libraries starting with the <b>r</b> prefix can be used in both cases (in ESP-IDF and in ARDUINO)