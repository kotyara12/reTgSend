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
// -----------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------- EN - Telegram notify ---------------------------------------------------
// -----------------------------------------------------------------------------------------------------------------------
// Allow Telegram notifications (general flag)
#define CONFIG_TELEGRAM_ENABLE 1
#if CONFIG_TELEGRAM_ENABLE
// TLS certificate for Telegram API
#define CONFIG_TELEGRAM_TLS_PEM_START "_binary_api_telegram_org_pem_start"
#define CONFIG_TELEGRAM_TLS_PEM_END "_binary_api_telegram_org_pem_end"
// EN: Telegram API bot token
// RU: Токен бота API Telegram
#define CONFIG_TELEGRAM_TOKEN "99999999999:AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
// EN: Chat or group ID
// RU: Идентификатор чата или группы
#define CONFIG_TELEGRAM_CHAT_ID "-100123456789"
// EN: Device name (published at the beginning of each message)
// RU: Название устройства (публикуется в начале каждого сообщения)
#define CONFIG_TELEGRAM_DEVICE "🌦 THS-DEMO"
// EN: Time format at the end of the notification
// RU: Формат времени в конце уведомления
#define CONFIG_TELEGRAM_TIME_FORMAT "%d.%m.%Y %H:%M:%S"
// EN: Use static memory allocation for the task and queue. CONFIG_FREERTOS_SUPPORT_STATIC_ALLOCATION must be enabled!
// RU: Использовать статическое выделение памяти под задачу и очередь. Должен быть включен параметр CONFIG_FREERTOS_SUPPORT_STATIC_ALLOCATION!
#define CONFIG_TELEGRAM_STATIC_ALLOCATION 1
// EN: Stack size for the task of sending notifications to Telegram
// RU: Размер стека для задачи отправки уведомлений в Telegram
#define CONFIG_TELEGRAM_STACK_SIZE 3072
// EN: Queue size for the task of sending notifications to Telegram
// RU: Размер очереди для задачи отправки уведомлений в Telegram
#define CONFIG_TELEGRAM_QUEUE_SIZE 16
// EN: Priority of the task of sending notifications to Telegram
// RU: Приоритет задачи отправки уведомлений в Telegram
#define CONFIG_TELEGRAM_PRIORITY 3
// EN: The processor core for the task of sending notifications to Telegram
// RU: Ядро процессора для задачи отправки уведомлений в Telegram
#define CONFIG_TELEGRAM_CORE 1
// EN: Number of attempts to send notifications to Telegram
// RU: Количество попыток отправки уведомлений в Telegram
#define CONFIG_TELEGRAM_MAX_ATTEMPTS 3
// EN: The interval between attempts to send notifications to Telegram
// RU: Интервал между попытками отправки уведомлений в Telegram
#define CONFIG_TELEGRAM_ATTEMPTS_INTERVAL 3000
// EN: Send a notification to Telegram when a parameter is changed
// RU: Отправить уведомление в Telegram при изменении параметра
#define CONFIG_TELEGRAM_PARAM_CHANGE_NOTIFY 1
// EN: Send a notification to Telegram when a command is received
// RU: Отправить уведомление в Telegram при получении команды
#define CONFIG_TELEGRAM_COMMAND_NOTIFY 1
// EN: Send a notification to Telegram when receiving an OTA update
// RU: Отправить уведомление в Telegram при получении OTA обновления
#define CONFIG_TELEGRAM_OTA_NOTIFY 1
#endif // CONFIG_TELEGRAM_ENABLE
