#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h" 
#include "project_config.h"
#include "rLog.h"
#include "rStrings.h"
#include "reTgSend.h"
#include "reEvents.h"
#include "reStates.h"
#include "esp_wifi.h" 
#include "esp_tls.h"
#include "esp_http_client.h"
#if CONFIG_PINGER_ENABLE
#include "rePinger.h"
#endif // CONFIG_PINGER_ENABLE

#define API_TELEGRAM_HOST "api.telegram.org"
#define API_TELEGRAM_PORT 443
#define API_TELEGRAM_BOT_PATH "/bot" CONFIG_TELEGRAM_TOKEN
#define API_TELEGRAM_SEND_MESSAGE API_TELEGRAM_BOT_PATH "/sendMessage"
#define API_TELEGRAM_TMPL_MESSAGE "{\"chat_id\":%s,\"parse_mode\":\"HTML\",\"disable_notification\":%s,\"text\":\"<b>%s</b>\r\n\r\n%s\r\n\r\n<code>%s</code>\"}"
#define API_TELEGRAM_HEADER_CTYPE "Content-Type"
#define API_TELEGRAM_HEADER_AJSON "application/json"
#define API_TELEGRAM_FALSE "false"
#define API_TELEGRAM_TRUE "true"

typedef struct {
  int64_t chat_id;
  char* title;
  char* message;
  time_t timestamp;
  bool notify;
} tgMessage_t;

#define TELEGRAM_QUEUE_ITEM_SIZE sizeof(tgMessage_t*)

TaskHandle_t _tgTask;
QueueHandle_t _tgQueue = NULL;

static const char* logTAG = "TG";
static const char* tgTaskName = "tg_send";

extern const char api_telegram_org_pem_start[] asm(CONFIG_TELEGRAM_TLS_PEM_START);
extern const char api_telegram_org_pem_end[]   asm(CONFIG_TELEGRAM_TLS_PEM_END); 

#if CONFIG_TELEGRAM_STATIC_ALLOCATION
StaticQueue_t _tgQueueBuffer;
StaticTask_t _tgTaskBuffer;
StackType_t _tgTaskStack[CONFIG_TELEGRAM_STACK_SIZE];
uint8_t _tgQueueStorage [CONFIG_TELEGRAM_QUEUE_SIZE * TELEGRAM_QUEUE_ITEM_SIZE];
#endif // CONFIG_TELEGRAM_STATIC_ALLOCATION

char* tgNotifyEx(const tgMessage_t* tgMsg)
{
  if (tgMsg->notify) 
    return (char*)API_TELEGRAM_FALSE;
  else 
    return (char*)API_TELEGRAM_TRUE;
}

bool tgSendEx(const tgMessage_t* tgMsg)
{
  bool _result = true;
  struct tm timeinfo;
  static char buffer_timestamp[20];

  // Flashing system LED
  eventLoopPostSystem(RE_SYS_SYSLED, RE_SYS_FLASH);

  // Formation of the request text (message)
  localtime_r(&tgMsg->timestamp, &timeinfo);
  strftime(buffer_timestamp, sizeof(buffer_timestamp), CONFIG_FORMAT_DTS, &timeinfo);
  char* json = malloc_stringf(API_TELEGRAM_TMPL_MESSAGE, 
    CONFIG_TELEGRAM_CHAT_ID, tgNotifyEx(tgMsg), tgMsg->title, tgMsg->message, buffer_timestamp);

  // Configuring request parameters
  esp_http_client_config_t cfgHttp;
  memset(&cfgHttp, 0, sizeof(cfgHttp));
  cfgHttp.method = HTTP_METHOD_POST;
  cfgHttp.host = API_TELEGRAM_HOST;
  cfgHttp.port = API_TELEGRAM_PORT;
  cfgHttp.path = API_TELEGRAM_SEND_MESSAGE;
  cfgHttp.use_global_ca_store = false;
  cfgHttp.transport_type = HTTP_TRANSPORT_OVER_SSL;
  cfgHttp.cert_pem = api_telegram_org_pem_start;
  cfgHttp.skip_cert_common_name_check = false;
  cfgHttp.is_async = false;

  // Making a request to the Telegram API
  esp_http_client_handle_t client = esp_http_client_init(&cfgHttp);
  if (client != NULL) {
    esp_http_client_set_header(client, API_TELEGRAM_HEADER_CTYPE, API_TELEGRAM_HEADER_AJSON);
    esp_http_client_set_post_field(client, json, strlen(json));
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
      int retCode = esp_http_client_get_status_code(client);
      _result = ((retCode == 200) || (retCode == 301));
      if (!_result)
        rlog_e(logTAG, "Failed to send message, API error code: #%d!", retCode);
    }
    else {
      _result = false;
      rlog_e(logTAG, "Failed to complete request to Telegram API, error code: 0x%x!", err);
    };
    esp_http_client_cleanup(client);
  }
  else {
    _result = false;
    rlog_e(logTAG, "Failed to complete request to Telegram API!");
  };

  if (json) free(json);
  return _result;
}

bool tgSend(const bool msgNotify, const char* msgTitle, const char* msgText, ...)
{
  if (_tgQueue) {
    uint32_t lenTitle;
    uint32_t lenText;
    va_list msgArgs;

    // Allocating memory for the message
    tgMessage_t* tgMsg = new tgMessage_t;
    tgMsg->notify = msgNotify;
    tgMsg->timestamp = time(NULL);

    // Allocating memory for the message header
    lenTitle = snprintf(NULL, 0, msgTitle);
    tgMsg->title = (char*)malloc(lenTitle+1);
    snprintf(tgMsg->title, lenTitle+1, msgTitle);
    
    // Allocate memory for the message text and format it
    va_start(msgArgs, msgText);
    lenText = vsnprintf(NULL, 0, msgText, msgArgs);
    tgMsg->message = (char*)malloc(lenText+1);
    vsnprintf(tgMsg->message, lenText+1, msgText, msgArgs);
    va_end(msgArgs);

    // Add a message to the send queue
    if (xQueueSend(_tgQueue, &tgMsg, portMAX_DELAY) == pdPASS) {
      return true;
    }
    else {
      rloga_e("Error adding message to queue [ %s ]!", tgTaskName);
      eventLoopPostSystem(RE_SYS_TELEGRAM_ERROR, RE_SYS_SET, false);
      // Freeing the memory allocated for the message
      free(tgMsg->title);
      free(tgMsg->message);
      delete tgMsg;
    };
  };

  return false;
}

// -----------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------- Task routines ----------------------------------------------------
// -----------------------------------------------------------------------------------------------------------------------

void tgTaskExec(void *pvParameters)
{
  tgMessage_t *tgMsg;

  while (true) {
    if (xQueuePeek(_tgQueue, &tgMsg, portMAX_DELAY) == pdPASS) {
      rlog_v(logTAG, "Message received from queue: [%d] %s :: %s", tgMsg->timestamp, tgMsg->title, tgMsg->message);

      // Trying to send a message to the Telegram API
      uint16_t tryAttempt = 1;
      bool resAttempt = false;
      do {
        // Checking Internet and host availability
        if (statesInetWait(pdMS_TO_TICKS(CONFIG_TELEGRAM_ATTEMPTS_INTERVAL))) {
          resAttempt = tgSendEx(tgMsg);
          if (resAttempt) {
            eventLoopPostSystem(RE_SYS_TELEGRAM_ERROR, RE_SYS_CLEAR, false);
          }
          else {
            eventLoopPostSystem(RE_SYS_TELEGRAM_ERROR, RE_SYS_SET, false);
            tryAttempt++;
            vTaskDelay(pdMS_TO_TICKS(CONFIG_TELEGRAM_ATTEMPTS_INTERVAL));
          };
        };
      } while (!resAttempt && (tryAttempt <= CONFIG_TELEGRAM_MAX_ATTEMPTS));
      
      // Debug log output
      if (resAttempt) {
        rlog_i(logTAG, "Message sent: [%d] %s :: %s", tgMsg->timestamp, tgMsg->title, tgMsg->message);
      }
      else {
        rlog_e(logTAG, "Failed to send message [%d] %s :: %s", tgMsg->timestamp, tgMsg->title, tgMsg->message);
      };

      // Removing a message from the queue (anyway)
      xQueueReceive(_tgQueue, &tgMsg, 0);
      // Freeing the memory allocated for the message
      free(tgMsg->title);
      free(tgMsg->message);
      delete tgMsg;
    };
  };

  tgTaskDelete();
}

bool tgTaskCreate(bool createSuspended) 
{
  if (!_tgTask) {
    if (!_tgQueue) {
      #if CONFIG_TELEGRAM_STATIC_ALLOCATION
      _tgQueue = xQueueCreateStatic(CONFIG_TELEGRAM_QUEUE_SIZE, TELEGRAM_QUEUE_ITEM_SIZE, &(_tgQueueStorage[0]), &_tgQueueBuffer);
      #else
      _tgQueue = xQueueCreate(CONFIG_TELEGRAM_QUEUE_SIZE, TELEGRAM_QUEUE_ITEM_SIZE);
      #endif // CONFIG_TELEGRAM_STATIC_ALLOCATION
      if (!_tgQueue) {
        rloga_e("Failed to create a queue for sending notifications to Telegram!");
        eventLoopPostSystem(RE_SYS_ERROR, RE_SYS_SET, false);
        return false;
      };
    };
    
    #if CONFIG_TELEGRAM_STATIC_ALLOCATION
    _tgTask = xTaskCreateStaticPinnedToCore(tgTaskExec, tgTaskName, CONFIG_TELEGRAM_STACK_SIZE, NULL, CONFIG_TELEGRAM_PRIORITY, _tgTaskStack, &_tgTaskBuffer, CONFIG_TELEGRAM_CORE); 
    #else
    xTaskCreatePinnedToCore(tgTaskExec, tgTaskName, CONFIG_TELEGRAM_STACK_SIZE, NULL, CONFIG_TELEGRAM_PRIORITY, &_tgTask, CONFIG_TELEGRAM_CORE); 
    #endif // CONFIG_TELEGRAM_STATIC_ALLOCATION
    if (!_tgTask) {
      vQueueDelete(_tgQueue);
      rloga_e("Failed to create task for sending notifications to Telegram!");
      eventLoopPostSystem(RE_SYS_ERROR, RE_SYS_SET, false);
      return false;
    }
    else {
      if (createSuspended) {
        rloga_i("Task [ %s ] has been successfully created", tgTaskName);
        tgTaskSuspend();
        eventLoopPostSystem(RE_SYS_TELEGRAM_ERROR, RE_SYS_SET, false);
        return tgEventHandlerRegister();
      } else {
        rloga_i("Task [ %s ] has been successfully started", tgTaskName);
        eventLoopPostSystem(RE_SYS_TELEGRAM_ERROR, RE_SYS_CLEAR, false);
        return true;
      };
    };
  }
  else {
    return tgTaskResume();
  };
}

bool tgTaskSuspend()
{
  if ((_tgTask) && (eTaskGetState(_tgTask) != eSuspended)) {
    vTaskSuspend(_tgTask);
    if (eTaskGetState(_tgTask) == eSuspended) {
      rloga_d("Task [ %s ] has been suspended", tgTaskName);
      return true;
    } else {
      rloga_e("Failed to suspend task [ %s ]!", tgTaskName);
    };
    eventLoopPostSystem(RE_SYS_TELEGRAM_ERROR, RE_SYS_SET, false);
  };
  return false;
}

bool tgTaskResume()
{
  if ((_tgTask) && (eTaskGetState(_tgTask) == eSuspended)) {
    vTaskResume(_tgTask);
    if (eTaskGetState(_tgTask) != eSuspended) {
      rloga_i("Task [ %s ] has been successfully resumed", tgTaskName);
      eventLoopPostSystem(RE_SYS_TELEGRAM_ERROR, RE_SYS_CLEAR, false);
      return true;
    } else {
      rloga_e("Failed to resume task [ %s ]!", tgTaskName);
      eventLoopPostSystem(RE_SYS_TELEGRAM_ERROR, RE_SYS_SET, false);
    };
  };
  return false;
}

bool tgTaskDelete()
{
  tgEventHandlerUnregister();

  if (_tgQueue) {
    vQueueDelete(_tgQueue);
    rloga_v("The queue for sending notifications in Telegram has been deleted");
    _tgQueue = NULL;
  };

  if (_tgTask) {
    vTaskDelete(_tgTask);
    _tgTask = NULL;
    rloga_d("Task [ %s ] was deleted", tgTaskName);
  };
  
  return true;
}

// -----------------------------------------------------------------------------------------------------------------------
// --------------------------------------------------- Event handlers ----------------------------------------------------
// -----------------------------------------------------------------------------------------------------------------------

static void tgWiFiEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
  // STA connected & Internet access enabled
  if (event_id == RE_WIFI_STA_PING_OK) {
    if (_tgTask) {
      if (tgTaskResume()) {
        eventLoopPostSystem(RE_SYS_TELEGRAM_ERROR, RE_SYS_CLEAR, false);
      };
    } else {
      if (tgTaskCreate(false)) {
        eventLoopPostSystem(RE_SYS_TELEGRAM_ERROR, RE_SYS_CLEAR, false);
      };
    };
  }
  // All other events
  else {
    tgTaskSuspend();
    eventLoopPostSystem(RE_SYS_TELEGRAM_ERROR, RE_SYS_SET, false);
  };
}

#if CONFIG_PINGER_ENABLE && defined(CONFIG_TELEGRAM_HOST_CHECK)

static void tgTelegramEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
  // Telegram API available
  if (event_id == RE_PING_TG_API_AVAILABLE) {
    if (tgTaskResume()) {
      eventLoopPostSystem(RE_SYS_TELEGRAM_ERROR, RE_SYS_CLEAR, false);
    };
  }
  // Telegram API unavailable
  else if (event_id == RE_PING_TG_API_UNAVAILABLE) {
    tgTaskSuspend();
    eventLoopPostSystem(RE_SYS_TELEGRAM_ERROR, RE_SYS_SET, false);
  };
}

#endif // CONFIG_PINGER_ENABLE && defined(CONFIG_TELEGRAM_HOST_CHECK)

bool tgEventHandlerRegister()
{
  bool ret = eventHandlerRegister(RE_WIFI_EVENTS, ESP_EVENT_ANY_ID, &tgWiFiEventHandler, nullptr);
  #if CONFIG_PINGER_ENABLE && defined(CONFIG_TELEGRAM_HOST_CHECK)
    ret = ret && eventHandlerRegister(RE_PING_EVENTS, RE_PING_TG_API_AVAILABLE, &tgTelegramEventHandler, nullptr);
    ret = ret && eventHandlerRegister(RE_PING_EVENTS, RE_PING_TG_API_UNAVAILABLE, &tgTelegramEventHandler, nullptr);
  #endif // CONFIG_PINGER_ENABLE && defined(CONFIG_TELEGRAM_HOST_CHECK)
  return ret;
}

void tgEventHandlerUnregister()
{
  eventHandlerUnregister(RE_WIFI_EVENTS, ESP_EVENT_ANY_ID, &tgWiFiEventHandler);
  #if CONFIG_PINGER_ENABLE && defined(CONFIG_TELEGRAM_HOST_CHECK)
    eventHandlerUnregister(RE_PING_EVENTS, RE_PING_TG_API_AVAILABLE, &tgTelegramEventHandler);
    eventHandlerUnregister(RE_PING_EVENTS, RE_PING_TG_API_UNAVAILABLE, &tgTelegramEventHandler);
  #endif // CONFIG_PINGER_ENABLE && defined(CONFIG_TELEGRAM_HOST_CHECK)
}