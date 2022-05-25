#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h" 
#include "project_config.h"
#include "def_consts.h"
#include "rLog.h"
#include "reEsp32.h"
#include "rStrings.h"
#include "reTgSend.h"
#include "reEvents.h"
#include "reStates.h"
#include "esp_wifi.h" 
#include "esp_tls.h"
#include "sys/queue.h"
#include "esp_http_client.h"
#if CONFIG_PINGER_ENABLE
#include "rePinger.h"
#endif // CONFIG_PINGER_ENABLE

#define API_TELEGRAM_HOST "api.telegram.org"
#define API_TELEGRAM_PORT 443
#define API_TELEGRAM_TIMEOUT_MS 60000
#define API_TELEGRAM_BOT_PATH "/bot" CONFIG_TELEGRAM_TOKEN
#define API_TELEGRAM_SEND_MESSAGE API_TELEGRAM_BOT_PATH "/sendMessage"
#define API_TELEGRAM_TMPL_MESSAGE_TITLED "{\"chat_id\":%s,\"parse_mode\":\"HTML\",\"disable_notification\":%s,\"text\":\"<b>%s</b>\r\n\r\n%s\r\n\r\n<code>%s</code>\"}"
#define API_TELEGRAM_TMPL_MESSAGE_SIMPLE "{\"chat_id\":%s,\"parse_mode\":\"HTML\",\"disable_notification\":%s,\"text\":\"%s\r\n\r\n<code>%s</code>\"}"
#define API_TELEGRAM_HEADER_CTYPE "Content-Type"
#define API_TELEGRAM_HEADER_AJSON "application/json"
#define API_TELEGRAM_FALSE "false"
#define API_TELEGRAM_TRUE "true"

typedef struct {
  msg_options_t options;
  time_t timestamp;
  #if CONFIG_TELEGRAM_TITLE_ENABLED
  char* title;
  #endif // CONFIG_TELEGRAM_TITLE_ENABLED
  char* message;
} tgMessage_t;

#define TELEGRAM_QUEUE_ITEM_SIZE sizeof(tgMessage_t*)

typedef struct tgQueue_t {
  tgMessage_t* msg = nullptr;
  TAILQ_ENTRY(tgQueue_t) next;
} tgQueue_t;
typedef struct tgQueue_t *tgQueueHandle_t;

TAILQ_HEAD(tgQueueHead_t, tgQueue_t);
typedef struct tgQueueHead_t *tgQueueHeadHandle_t;

TaskHandle_t _tgTask;
QueueHandle_t _tgQueue = NULL;
static bool _tgEventHablers = false;

static const char* logTAG = "TG";
static const char* tgTaskName = "tg_send";

#if CONFIG_TELEGRAM_STATIC_ALLOCATION
StaticQueue_t _tgQueueBuffer;
StaticTask_t _tgTaskBuffer;
StackType_t _tgTaskStack[CONFIG_TELEGRAM_STACK_SIZE];
uint8_t _tgQueueStorage [CONFIG_TELEGRAM_QUEUE_SIZE * TELEGRAM_QUEUE_ITEM_SIZE];
#endif // CONFIG_TELEGRAM_STATIC_ALLOCATION

char* tgNotifyApi(tgMessage_t* tgMsg)
{
  if (decMsgOptionsNotify(tgMsg->options))
    return (char*)API_TELEGRAM_FALSE;
  else 
    return (char*)API_TELEGRAM_TRUE;
}

api_status_t tgSendApi(tgMessage_t* tgMsg)
{
  rlog_i(logTAG, "Send message: %s", tgMsg->message);

  api_status_t _result = API_OK;
  struct tm timeinfo;
  static char buffer_timestamp[20];

  // Determine chat ID
  const char* chat_id;
  switch (decMsgOptionsKind(tgMsg->options)) {
    case MK_SERVICE:
      #ifdef CONFIG_TELEGRAM_CHAT_ID_SERVICE
        chat_id = CONFIG_TELEGRAM_CHAT_ID_SERVICE;
      #else
        chat_id = CONFIG_TELEGRAM_CHAT_ID_MAIN;
      #endif // CONFIG_TELEGRAM_CHAT_ID_SERVICE
      break;

    case MK_PARAMS:
      #ifdef CONFIG_TELEGRAM_CHAT_ID_PARAMS
        chat_id = CONFIG_TELEGRAM_CHAT_ID_PARAMS;
      #else
        chat_id = CONFIG_TELEGRAM_CHAT_ID_MAIN;
      #endif // CONFIG_TELEGRAM_CHAT_ID_PARAMS
      break;

    case MK_SECURITY:
      #ifdef CONFIG_TELEGRAM_CHAT_ID_SECURITY
        chat_id = CONFIG_TELEGRAM_CHAT_ID_SECURITY;
      #else
        chat_id = CONFIG_TELEGRAM_CHAT_ID_MAIN;
      #endif // CONFIG_TELEGRAM_CHAT_ID_SECURITY
      break;

    default:
      chat_id = CONFIG_TELEGRAM_CHAT_ID_MAIN;
      break;
  }
  
  if (strcmp(chat_id, "") != 0) {
    // Formation of the request text (message)
    localtime_r(&tgMsg->timestamp, &timeinfo);
    strftime(buffer_timestamp, sizeof(buffer_timestamp), CONFIG_FORMAT_DTS, &timeinfo);
    char* json = nullptr;
    #if CONFIG_TELEGRAM_TITLE_ENABLED
      if (tgMsg->title) {
        json = malloc_stringf(API_TELEGRAM_TMPL_MESSAGE_TITLED, 
          chat_id, tgNotifyApi(tgMsg), tgMsg->title, tgMsg->message, buffer_timestamp);
      } else {
        json = malloc_stringf(API_TELEGRAM_TMPL_MESSAGE_SIMPLE, 
          chat_id, tgNotifyApi(tgMsg), tgMsg->message, buffer_timestamp);
      };
    #else
      json = malloc_stringf(API_TELEGRAM_TMPL_MESSAGE_SIMPLE, 
        chat_id, tgNotifyApi(tgMsg), tgMsg->message, buffer_timestamp);
    #endif // CONFIG_TELEGRAM_TITLE_ENABLED

    // Send request
    if (json) {
      // Configuring request parameters
      esp_http_client_config_t cfgHttp;
      memset(&cfgHttp, 0, sizeof(cfgHttp));
      cfgHttp.method = HTTP_METHOD_POST;
      cfgHttp.host = API_TELEGRAM_HOST;
      cfgHttp.port = API_TELEGRAM_PORT;
      cfgHttp.path = API_TELEGRAM_SEND_MESSAGE;
      cfgHttp.timeout_ms = API_TELEGRAM_TIMEOUT_MS;
      cfgHttp.transport_type = HTTP_TRANSPORT_OVER_SSL;
      cfgHttp.use_global_ca_store = true;
      cfgHttp.skip_cert_common_name_check = false;
      cfgHttp.is_async = false;

      // Making a request to the Telegram API
      esp_http_client_handle_t client = esp_http_client_init(&cfgHttp);
      if (client) {
        esp_http_client_set_header(client, API_TELEGRAM_HEADER_CTYPE, API_TELEGRAM_HEADER_AJSON);
        esp_http_client_set_post_field(client, json, strlen(json));
        esp_err_t err = esp_http_client_perform(client);
        if (err == ESP_OK) {
          int retCode = esp_http_client_get_status_code(client);
          if ((retCode >= HttpStatus_Ok) && (retCode <= HttpStatus_BadRequest)) {
            _result = API_OK;
            eventLoopPostSystem(RE_SYS_TELEGRAM_ERROR, RE_SYS_CLEAR, false);
          } else if (retCode == HttpStatus_Forbidden) {
            _result = API_ERROR_WAIT;
            eventLoopPostSystem(RE_SYS_TELEGRAM_ERROR, RE_SYS_SET, false);
            rlog_w(logTAG, "Failed to send message, too many messages, please wait");
          } else {
            _result = API_ERROR_API;
            eventLoopPostSystem(RE_SYS_TELEGRAM_ERROR, RE_SYS_SET, false);
            rlog_e(logTAG, "Failed to send message, API error code: #%d!", retCode);
          };
          #if !defined(CONFIG_TELEGRAM_SYSLED_ACTIVITY) || CONFIG_TELEGRAM_SYSLED_ACTIVITY
            // Flashing system LED
            ledSysActivity();
          #endif // CONFIG_TELEGRAM_SYSLED_ACTIVITY
        } else {
          _result = API_ERROR_HTTP;
          rlog_e(logTAG, "Failed to complete request to Telegram API, error code: 0x%x!", err);
        };
        esp_http_client_cleanup(client);
      } else {
        _result = API_ERROR_HTTP;
        rlog_e(logTAG, "Failed to complete request to Telegram API!");
      };
      // Free buffer
      free(json);
    } else {
      _result = API_ERROR;
      rlog_e(logTAG, "Failed to create request text to Telegram API");
    };
  } else {
    rlog_d(logTAG, "Chat ID not set, message ignored");
  };

  return _result;
}

bool tgSendMsg(msg_options_t msgOptions, const char* msgTitle, const char* msgText, ...)
{
  if (_tgQueue) {
    tgMessage_t* tgMsg = (tgMessage_t*)esp_calloc(1, sizeof(tgMessage_t));
    if (tgMsg) {
      tgMsg->options = msgOptions;
      tgMsg->timestamp = time(nullptr);

      // Allocating memory for the message header
      #if CONFIG_TELEGRAM_TITLE_ENABLED
        if (msgTitle) {
          uint32_t lenTitle = snprintf(nullptr, 0, msgTitle);
          tgMsg->title = (char*)esp_calloc(1, lenTitle+1);
          if (tgMsg->title) {
            snprintf(tgMsg->title, lenTitle+1, msgTitle);
          } else {
            rlog_e(logTAG, "Failed to allocate memory for message header");
            goto error;
          };
        } else {
          tgMsg->title = nullptr;
        };
      #endif // CONFIG_TELEGRAM_TITLE_ENABLED
      
      // Allocate memory for the message text and format it
      va_list msgArgs;
      va_start(msgArgs, msgText);
      uint32_t lenText = vsnprintf(nullptr, 0, msgText, msgArgs);
      tgMsg->message = (char*)esp_calloc(1, lenText+1);
      if (tgMsg->message) {
        vsnprintf(tgMsg->message, lenText+1, msgText, msgArgs);
      } else {
        rlog_e(logTAG, "Failed to allocate memory for message text");
        va_end(msgArgs);
        goto error;
      };
      va_end(msgArgs);

      // Add a message to the send queue
      if (xQueueSend(_tgQueue, &tgMsg, pdMS_TO_TICKS(CONFIG_TELEGRAM_QUEUE_WAIT)) == pdPASS) {
        return true;
      } else {
        rloga_e("Failed to adding message to queue [ %s ]!", tgTaskName);
        eventLoopPostSystem(RE_SYS_TELEGRAM_ERROR, RE_SYS_SET, false);
        goto error;
      };
    } else {
      rlog_e(logTAG, "Failed to allocate memory for message");
    };
  error:
    // Deallocate resources from heap
    if (tgMsg) {
      #if CONFIG_TELEGRAM_TITLE_ENABLED
      if (tgMsg->title) free(tgMsg->title);
      #endif // CONFIG_TELEGRAM_TITLE_ENABLED
      if (tgMsg->message) free(tgMsg->message);
      free(tgMsg);
    };
  };
  return false;
}

// -----------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------- Task routines ----------------------------------------------------
// -----------------------------------------------------------------------------------------------------------------------

void tgTaskExec(void *pvParameters)
{
  static tgQueueHeadHandle_t sendQueue = nullptr;
  sendQueue = (tgQueueHeadHandle_t)esp_malloc(sizeof(tgQueueHead_t));
  RE_MEM_CHECK(sendQueue, return);
  TAILQ_INIT(sendQueue);

  tgMessage_t *inMsg;
  api_status_t resSend = API_OK;
  TickType_t waitIncoming = portMAX_DELAY;
  while (true) {
    // Calculate the timeout for an incoming message
    if (TAILQ_EMPTY(sendQueue)) {
      waitIncoming = portMAX_DELAY;
    } else {
      if (statesInetIsAvailabled()) {
        // If the API denied service last time, you'll have to wait longer
        if (resSend == API_ERROR_WAIT) {
          waitIncoming = pdMS_TO_TICKS(CONFIG_TELEGRAM_FORBIDDEN_INTERVAL);
        } else {
          waitIncoming = pdMS_TO_TICKS(CONFIG_TELEGRAM_SEND_INTERVAL);
        };
      } else {
        waitIncoming = pdMS_TO_TICKS(CONFIG_TELEGRAM_INTERNET_INTERVAL);
      };
    };

    // Waiting for an incoming message
    if (xQueueReceive(_tgQueue, &inMsg, waitIncoming) == pdPASS) {
      // New message received, need to be added to the queue
      rlog_v(logTAG, "New message received: %s", inMsg->message);

      // Checking if a message can be added to the queue
      bool msgAccept = true;
      double heap_free_size = ((double)heap_caps_get_free_size(MALLOC_CAP_DEFAULT) / (double)heap_caps_get_total_size(MALLOC_CAP_DEFAULT)) * 100.0;
      // rlog_d(logTAG, "Heap free size: %d, %d, %.2f", heap_caps_get_free_size(MALLOC_CAP_DEFAULT), heap_caps_get_total_size(MALLOC_CAP_DEFAULT), heap_free_size);
      while (!TAILQ_EMPTY(sendQueue) && (heap_free_size < CONFIG_TELEGRAM_HEAP_LIMIT)) {
        tgQueueHandle_t item, rmvd;
        msg_priority_t inPriority = decMsgOptionsPriority(inMsg->options);
        rmvd = nullptr; 
        TAILQ_FOREACH(item, sendQueue, next) {
          // Search for a message in the queue with a lower priority
          if (decMsgOptionsPriority(item->msg->options) < inPriority) {
            // Search for the first message in the queue with a lower priority than already found
            if ((rmvd == nullptr) || (decMsgOptionsPriority(item->msg->options) < decMsgOptionsPriority(rmvd->msg->options))) {
              rmvd = item;
            };
          };
        };

        // If you can find a message with a lower priority, delete it
        if (rmvd != nullptr) {
          TAILQ_REMOVE(sendQueue, rmvd, next);
          rlog_w(logTAG, "Message dropped from send queue: %s", rmvd->msg->message);
          if (rmvd->msg->message) free(rmvd->msg->message);
          #if CONFIG_TELEGRAM_TITLE_ENABLED
          if (rmvd->msg->title) free(rmvd->msg->title);
          #endif // CONFIG_TELEGRAM_TITLE_ENABLED
          free(rmvd->msg);
          free(rmvd);
        } else {
          msgAccept = false;
          break;
        };
      };

      // Add a message to the tail of the queue
      if (msgAccept) {
        tgQueueHandle_t item = (tgQueueHandle_t)esp_calloc(1, sizeof(tgQueue_t));
        if (item) {
          item->msg = inMsg;
          TAILQ_INSERT_TAIL(sendQueue, item, next);
          rlog_v(logTAG, "Message inserted to send queue: %s", item->msg->message);
        } else {
          rlog_e(logTAG, "Failed to insert message to send queue: memory exhausted");
          if (inMsg->message) free(inMsg->message);
          #if CONFIG_TELEGRAM_TITLE_ENABLED
          if (inMsg->title) free(inMsg->title);
          #endif // CONFIG_TELEGRAM_TITLE_ENABLED
          free(inMsg);
        };
      } else {
        rlog_e(logTAG, "Failed to insert message to send queue: queue is full");
        if (inMsg->message) free(inMsg->message);
        #if CONFIG_TELEGRAM_TITLE_ENABLED
        if (inMsg->title) free(inMsg->title);
        #endif // CONFIG_TELEGRAM_TITLE_ENABLED
        free(inMsg);
      };
      
      inMsg = nullptr;
    };

    // Sending queued messages, if any  
    if (statesInetIsAvailabled()) {
      tgQueueHandle_t send = TAILQ_FIRST(sendQueue);
      if (send != nullptr) {
        resSend = tgSendApi(send->msg);
        // Message sent successfully or API returned an error (probably a "bad" message)
        if ((resSend == API_OK) || (resSend == API_ERROR_API) || (resSend == API_ERROR)) {
          // Remove message data from the heap
          rlog_v(logTAG, "Message was successfully sent and removed from queue: %s", send->msg->message);
          TAILQ_REMOVE(sendQueue, send, next);
          if (send->msg->message) free(send->msg->message);
          #if CONFIG_TELEGRAM_TITLE_ENABLED
          if (send->msg->title) free(send->msg->title);
          #endif // CONFIG_TELEGRAM_TITLE_ENABLED
          free(send->msg);
          free(send);
        };
      };
    };
  };

  // Delete task
  if (sendQueue) {
    tgQueueHandle_t item, tmp;
    TAILQ_FOREACH_SAFE(item, sendQueue, next, tmp) {
      TAILQ_REMOVE(sendQueue, item, next);
      if (item->msg) {
        if (item->msg->message) free(item->msg->message);
        #if CONFIG_TELEGRAM_TITLE_ENABLED
        if (item->msg->title) free(item->msg->title);
        #endif // CONFIG_TELEGRAM_TITLE_ENABLED
        free(item->msg);
      };
      free(item);
    };
    free(sendQueue);
  };
  tgTaskDelete();
}

bool tgTaskCreate() 
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
      rloga_i("Task [ %s ] has been successfully started", tgTaskName);
      eventLoopPostSystem(RE_SYS_TELEGRAM_ERROR, RE_SYS_CLEAR, false);
      return true;
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

