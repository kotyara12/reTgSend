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

## Parameters of the module
The module is configured using macros located in the general project configuration file "project_config.h":

<pre>
// Allow Telegram notifications (general flag)
#define CONFIG_TELEGRAM_ENABLE 1
#if CONFIG_TELEGRAM_ENABLE

// TLS certificate for Telegram API
#define CONFIG_TELEGRAM_TLS_PEM_START "_binary_api_telegram_org_pem_start"
#define CONFIG_TELEGRAM_TLS_PEM_END "_binary_api_telegram_org_pem_end"

// Telegram API bot token
#define CONFIG_TELEGRAM_TOKEN "99999999:AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"

// Chat or group ID
#define CONFIG_TELEGRAM_CHAT_ID "-100123456789"

// Device name (published at the beginning of each message)
#define CONFIG_TELEGRAM_DEVICE "🌦 THS-DEMO"

// Time format at the end of the notification
#define CONFIG_TELEGRAM_TIME_FORMAT "%d.%m.%Y %H:%M:%S"

// Use static memory allocation for the task and queue. CONFIG_FREERTOS_SUPPORT_STATIC_ALLOCATION must be enabled!
#define CONFIG_TELEGRAM_STATIC_ALLOCATION 1

// Stack size for the task of sending notifications to Telegram
#define CONFIG_TELEGRAM_STACK_SIZE 3072

// Queue size for the task of sending notifications to Telegram
#define CONFIG_TELEGRAM_QUEUE_SIZE 16

// Priority of the task of sending notifications to Telegram
#define CONFIG_TELEGRAM_PRIORITY 3

// The processor core for the task of sending notifications to Telegram
#define CONFIG_TELEGRAM_CORE 1

// Number of attempts to send notifications to Telegram
#define CONFIG_TELEGRAM_MAX_ATTEMPTS 3

// The interval between attempts to send notifications to Telegram
#define CONFIG_TELEGRAM_ATTEMPTS_INTERVAL 3000

#endif // CONFIG_TELEGRAM_ENABLE
</pre>
