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

## Parameters (preprocessor macros) in the "project_config.h" file that control the operation of the module
The module is configured using macros located in the general project configuration file "project_config.h":

<pre>
// EN: Allow Telegram notifications (general flag)<br/>
#define CONFIG_TELEGRAM_ENABLE 1<br/>
#if CONFIG_TELEGRAM_ENABLE<br/>
// TLS certificate for Telegram API<br/>
#define CONFIG_TELEGRAM_TLS_PEM_START "_binary_api_telegram_org_pem_start"<br/>
#define CONFIG_TELEGRAM_TLS_PEM_END "_binary_api_telegram_org_pem_end"<br/>
// Telegram API bot token<br/>
#define CONFIG_TELEGRAM_TOKEN "99999999:AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"<br/>
// Chat or group ID<br/>
#define CONFIG_TELEGRAM_CHAT_ID "-100123456789"<br/>
// Device name (published at the beginning of each message)<br/>
#define CONFIG_TELEGRAM_DEVICE "🌦 THS-DEMO"<br/>
// Time format at the end of the notification<br/>
#define CONFIG_TELEGRAM_TIME_FORMAT "%d.%m.%Y %H:%M:%S"<br/>
// Use static memory allocation for the task and queue. CONFIG_FREERTOS_SUPPORT_STATIC_ALLOCATION must be enabled!<br/>
#define CONFIG_TELEGRAM_STATIC_ALLOCATION 1<br/>
// Stack size for the task of sending notifications to Telegram<br/>
#define CONFIG_TELEGRAM_STACK_SIZE 3072<br/>
// Queue size for the task of sending notifications to Telegram<br/>
#define CONFIG_TELEGRAM_QUEUE_SIZE 16<br/>
// Priority of the task of sending notifications to Telegram<br/>
#define CONFIG_TELEGRAM_PRIORITY 3<br/>
// The processor core for the task of sending notifications to Telegram<br/>
#define CONFIG_TELEGRAM_CORE 1<br/>
// Number of attempts to send notifications to Telegram<br/>
#define CONFIG_TELEGRAM_MAX_ATTEMPTS 3<br/>
// The interval between attempts to send notifications to Telegram<br/>
#define CONFIG_TELEGRAM_ATTEMPTS_INTERVAL 3000<br/>
#endif // CONFIG_TELEGRAM_ENABLE<br/>
</pre>
