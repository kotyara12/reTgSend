#include "reTgSend.h"
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h" 
#include "esp_wifi.h" 
#include "esp_http_client.h"
#include "mbedtls/ssl.h"

#define API_TELEGRAM_HOST "api.telegram.org"
#define API_TELEGRAM_PORT 443
#define API_TELEGRAM_TIMEOUT_MS 60000
#define API_TELEGRAM_BOT_PATH "/bot" CONFIG_TELEGRAM_TOKEN
#define API_TELEGRAM_SEND_MESSAGE API_TELEGRAM_BOT_PATH "/sendMessage"
#define API_TELEGRAM_TMPL_MESSAGE "{\"chat_id\":%s,\"parse_mode\":\"HTML\",\"disable_notification\":%s,\"text\":\"%s\r\n\r\n<code>%s</code>\"}"
#define API_TELEGRAM_TMPL_TITLE "<b>%s</b>\r\n\r\n%s"
#define API_TELEGRAM_JSON_SIZE 256
#define API_TELEGRAM_HEADER_CTYPE "Content-Type"
#define API_TELEGRAM_HEADER_AJSON "application/json"
#define API_TELEGRAM_FALSE "false"
#define API_TELEGRAM_TRUE "true"

typedef struct {
  char* message;
  msg_options_t options;
  time_t timestamp;
} tgMessage_t;

typedef struct {
  #if CONFIG_TELEGRAM_OUTBOX_ENABLE
    bool queued;
  #endif // CONFIG_TELEGRAM_OUTBOX_ENABLE
  #if CONFIG_TELEGRAM_STATIC_MESSAGE_BUFFER
    char message[CONFIG_TELEGRAM_MESSAGE_SIZE];
  #else
    char* message;
  #endif // CONFIG_TELEGRAM_MESSAGE_SIZE
  msg_options_t options;
  time_t timestamp;
} tgMessageItem_t;

#define TELEGRAM_QUEUE_ITEM_SIZE sizeof(tgMessage_t*)

TaskHandle_t _tgTask;
QueueHandle_t _tgQueue = nullptr;
#if CONFIG_TELEGRAM_OUTBOX_ENABLE
static tgMessageItem_t _tgOutbox[CONFIG_TELEGRAM_OUTBOX_SIZE];
#endif // CONFIG_TELEGRAM_OUTBOX_ENABLE

static const char* logTAG = "TG";
static const char* tgTaskName = "tg_send";

#ifndef CONFIG_TELEGRAM_TLS_PEM_STORAGE
  #define CONFIG_TELEGRAM_TLS_PEM_STORAGE TLS_CERT_BUFFER
#endif // CONFIG_TELEGRAM_TLS_PEM_STORAGE

#if (CONFIG_TELEGRAM_TLS_PEM_STORAGE == TLS_CERT_BUFFER)
  extern const char api_telegram_org_pem_start[] asm(CONFIG_TELEGRAM_TLS_PEM_START);
  extern const char api_telegram_org_pem_end[]   asm(CONFIG_TELEGRAM_TLS_PEM_END);  
#endif // CONFIG_TELEGRAM_TLS_PEM_STORAGE

#if CONFIG_TELEGRAM_STATIC_ALLOCATION
StaticQueue_t _tgQueueBuffer;
StaticTask_t _tgTaskBuffer;
StackType_t _tgTaskStack[CONFIG_TELEGRAM_STACK_SIZE];
uint8_t _tgQueueStorage [CONFIG_TELEGRAM_QUEUE_SIZE * TELEGRAM_QUEUE_ITEM_SIZE];
#endif // CONFIG_TELEGRAM_STATIC_ALLOCATION

char* tgNotifyApi(tgMessageItem_t* tgMsg)
{
  if (decMsgOptionsNotify(tgMsg->options))
    return (char*)API_TELEGRAM_FALSE;
  else 
    return (char*)API_TELEGRAM_TRUE;
}

esp_err_t tgSendApi(tgMessageItem_t* tgMsg)
{
  rlog_i(logTAG, "Send message: %s", tgMsg->message);

  struct tm timeinfo;
  static char buffer_timestamp[CONFIG_BUFFER_LEN_INT64_RADIX10];
  #if CONFIG_TELEGRAM_STATIC_MESSAGE_BUFFER
    static char buffer_json[CONFIG_TELEGRAM_MESSAGE_SIZE+API_TELEGRAM_JSON_SIZE];
  #else 
    char * buffer_json = nullptr;
  #endif // CONFIG_TELEGRAM_MESSAGE_SIZE

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
  };
  if (strcmp(chat_id, "") == 0) {
    rlog_d(logTAG, "Chat ID not set, message ignored");
    return ESP_OK;
  };

  // Preparing JSON to send
  localtime_r(&tgMsg->timestamp, &timeinfo);
  strftime(buffer_timestamp, sizeof(buffer_timestamp), CONFIG_FORMAT_DTS, &timeinfo);
  #if CONFIG_TELEGRAM_STATIC_MESSAGE_BUFFER
    uint16_t size = format_string(buffer_json, CONFIG_TELEGRAM_MESSAGE_SIZE+API_TELEGRAM_JSON_SIZE, API_TELEGRAM_TMPL_MESSAGE, 
      chat_id, tgNotifyApi(tgMsg), tgMsg->message, buffer_timestamp);
    if (size == 0) {
      rlog_e(logTAG, "Failed to create json request to Telegram API");
      return ESP_ERR_NO_MEM;
    };
  #else 
    buffer_json = malloc_stringf(API_TELEGRAM_TMPL_MESSAGE, 
      chat_id, tgNotifyApi(tgMsg), tgMsg->message, buffer_timestamp);
    if (buffer_json == nullptr) {
      rlog_e(logTAG, "Failed to create json request to Telegram API");
      return ESP_ERR_NO_MEM;
    };
  #endif // CONFIG_TELEGRAM_MESSAGE_SIZE

  // Configuring request parameters
  esp_err_t ret = ESP_FAIL;
  esp_http_client_config_t cfgHttp;
  memset(&cfgHttp, 0, sizeof(cfgHttp));
  cfgHttp.method = HTTP_METHOD_POST;
  cfgHttp.host = API_TELEGRAM_HOST;
  cfgHttp.port = API_TELEGRAM_PORT;
  cfgHttp.path = API_TELEGRAM_SEND_MESSAGE;
  cfgHttp.timeout_ms = API_TELEGRAM_TIMEOUT_MS;
  cfgHttp.transport_type = HTTP_TRANSPORT_OVER_SSL;
  #if CONFIG_TELEGRAM_TLS_PEM_STORAGE == TLS_CERT_BUFFER
    cfgHttp.cert_pem = api_telegram_org_pem_start;
    cfgHttp.use_global_ca_store = false;
  #elif CONFIG_TELEGRAM_TLS_PEM_STORAGE == TLS_CERT_GLOBAL
    cfgHttp.use_global_ca_store = true;
  #elif CONFIG_TELEGRAM_TLS_PEM_STORAGE == TLS_CERT_BUNGLE
    cfgHttp.crt_bundle_attach = esp_crt_bundle_attach;
    cfgHttp.use_global_ca_store = false;
  #endif // CONFIG_TELEGRAM_TLS_PEM_STORAGE
  cfgHttp.skip_cert_common_name_check = false;
  cfgHttp.is_async = false;

  // Make request to Telegram API
  esp_http_client_handle_t client = esp_http_client_init(&cfgHttp);
  if (client) {
    esp_http_client_set_header(client, API_TELEGRAM_HEADER_CTYPE, API_TELEGRAM_HEADER_AJSON);
    esp_http_client_set_post_field(client, buffer_json, strlen(buffer_json));
    ret = esp_http_client_perform(client);
    if (ret == ESP_OK) {
      int retCode = esp_http_client_get_status_code(client);
      if (retCode == HttpStatus_Ok) {
        ret = ESP_OK;
        rlog_v(logTAG, "Message sent: %s", tgMsg->message);
      } else if (retCode == HttpStatus_Forbidden) {
        ret = ESP_ERR_INVALID_RESPONSE;
        rlog_w(logTAG, "Failed to send message, too many messages, please wait");
      } else {
        ret = ESP_ERR_INVALID_ARG;
        rlog_e(logTAG, "Failed to send message, API error code: #%d!", retCode);
      };
      #if !defined(CONFIG_TELEGRAM_SYSLED_ACTIVITY) || CONFIG_TELEGRAM_SYSLED_ACTIVITY
        // Flashing system LED
        ledSysActivity();
      #endif // CONFIG_TELEGRAM_SYSLED_ACTIVITY
    } else {
      rlog_e(logTAG, "Failed to complete request to Telegram API, error code: 0x%x!", ret);
    };
    esp_http_client_cleanup(client);
  } else {
    ret = ESP_ERR_INVALID_STATE;
    rlog_e(logTAG, "Failed to complete request to Telegram API!");
  };

  // Free buffer
  #if !CONFIG_TELEGRAM_STATIC_MESSAGE_BUFFER
    if (buffer_json != nullptr) free(buffer_json);
  #endif // CONFIG_TELEGRAM_MESSAGE_SIZE

  return ret;
}

bool tgSendMsg(msg_options_t msgOptions, const char* msgTitle, const char* msgText, ...)
{
  if (_tgQueue) {
    tgMessage_t* tgMsg = (tgMessage_t*)esp_calloc(1, sizeof(tgMessage_t));
    if (tgMsg) {
      tgMsg->options = msgOptions;
      tgMsg->timestamp = time(nullptr);
      tgMsg->message = nullptr;

      // Allocate memory for the message text and format it
      va_list args;
      va_start(args, msgText);
      uint16_t len = vsnprintf(nullptr, 0, msgText, args);
      tgMsg->message = (char*)esp_calloc(1, len+1);
      if (tgMsg->message) {
        vsnprintf(tgMsg->message, len+1, msgText, args);
      } else {
        rlog_e(logTAG, "Failed to allocate memory for message text");
        va_end(args);
        goto error;
      };
      va_end(args);

      // Add title if available
      #if CONFIG_TELEGRAM_TITLE_ENABLED
      if (msgTitle) {
        char * temp_message = tgMsg->message;
        tgMsg->message = malloc_stringf(API_TELEGRAM_TMPL_TITLE, msgTitle, temp_message);
        if (tgMsg->message == nullptr) {
          rlog_e(logTAG, "Failed to allocate memory for message text");
          goto error;
        };
        free(temp_message);
      };
      #endif // CONFIG_TELEGRAM_TITLE_ENABLED

      // Put a message to the task queue
      if (xQueueSend(_tgQueue, &tgMsg, pdMS_TO_TICKS(CONFIG_TELEGRAM_QUEUE_WAIT)) == pdPASS) {
        return true;
      } else {
        rloga_e("Failed to adding message to queue [ %s ]!", tgTaskName);
        eventLoopPostError(RE_SYS_TELEGRAM_ERROR, ESP_ERR_NO_MEM);
        goto error;
      };
    } else {
      rlog_e(logTAG, "Failed to allocate memory for message");
    };
  error:
    // Deallocate resources from heap
    if (tgMsg) {
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
  tgMessage_t *inMsg;
  esp_err_t resLast = ESP_OK;

  // Init outgoing message queue
  #if CONFIG_TELEGRAM_OUTBOX_ENABLE
    static uint8_t _tgOutboxSize = 0;
    TickType_t waitIncoming = portMAX_DELAY;

    rlog_d(logTAG, "Initialize telegram outbox...");
    memset(_tgOutbox, 0, sizeof(tgMessageItem_t) * CONFIG_TELEGRAM_OUTBOX_SIZE);
  #endif // CONFIG_TELEGRAM_OUTBOX_ENABLE

  while (true) {
    // Outbox mode
    #if CONFIG_TELEGRAM_OUTBOX_ENABLE
      // Calculate the timeout for an incoming message
      if (_tgOutboxSize > 0) {
        if (statesWiFiIsConnected()) {
          // If the API denied service last time, you'll have to wait longer
          if (resLast == ESP_ERR_INVALID_RESPONSE) {
            waitIncoming = pdMS_TO_TICKS(CONFIG_TELEGRAM_FORBIDDEN_INTERVAL);
          } else {
            waitIncoming = pdMS_TO_TICKS(CONFIG_TELEGRAM_SEND_INTERVAL);
          };
        } else {
          waitIncoming = pdMS_TO_TICKS(CONFIG_TELEGRAM_INTERNET_INTERVAL);
        };
      } else {
        waitIncoming = portMAX_DELAY;
      };

      // Waiting for an incoming message
      if (xQueueReceive(_tgQueue, &inMsg, waitIncoming) == pdPASS) {
        rlog_d(logTAG, "New message received (outbox size: %d): %s", _tgOutboxSize, inMsg->message);

        // Search for a lower priority message that could be deleted
        if (_tgOutboxSize >= CONFIG_TELEGRAM_OUTBOX_SIZE) {
          msg_priority_t inPriority = decMsgOptionsPriority(inMsg->options);
          for (uint8_t i = 0; i < CONFIG_TELEGRAM_OUTBOX_SIZE; i++) {
            if (decMsgOptionsPriority(_tgOutbox[i].options) < inPriority) {
              rlog_w(logTAG, "Message dropped from send outbox (size: %d, index: %d): %s", _tgOutboxSize, i, _tgOutbox[i].message);
              #if !CONFIG_TELEGRAM_STATIC_MESSAGE_BUFFER
                if (_tgOutbox[i].message) free(_tgOutbox[i].message);
                _tgOutbox[i].message = nullptr;
              #endif // CONFIG_TELEGRAM_MESSAGE_SIZE
              _tgOutbox[i].queued = false;
              _tgOutboxSize--;
              break;
            };
          };
        };
        
        // Insert new message to outbox
        if (_tgOutboxSize < CONFIG_TELEGRAM_OUTBOX_SIZE) {
          for (uint8_t i = 0; i < CONFIG_TELEGRAM_OUTBOX_SIZE; i++) {
            if (!_tgOutbox[i].queued) {
              #if CONFIG_TELEGRAM_STATIC_MESSAGE_BUFFER
                memset(_tgOutbox[i].message, 0, CONFIG_TELEGRAM_MESSAGE_SIZE);
                strncpy(_tgOutbox[i].message, inMsg->message, CONFIG_TELEGRAM_MESSAGE_SIZE-1);
              #else
                _tgOutbox[i].message = inMsg->message;
                inMsg->message = nullptr;
              #endif // CONFIG_TELEGRAM_MESSAGE_SIZE
              _tgOutbox[i].queued = true;
              _tgOutbox[i].options = inMsg->options;
              _tgOutbox[i].timestamp = inMsg->timestamp;
              _tgOutboxSize++;
              rlog_d(logTAG, "Message inserted to send outbox (size: %d, index: %d): %s", _tgOutboxSize, i, _tgOutbox[i].message);
              break;
            };
          };
        } else {
          rlog_e(logTAG, "Failed to insert message to send outbox (outbox size: %d): queue is full", _tgOutboxSize);
        };
        
        // Delete the resources used for the message
        if (inMsg->message) free(inMsg->message);
        free(inMsg);
        inMsg = nullptr;
      };

      // Search for the first message in the outbox
      if (statesWiFiIsConnected()) {
        _tgOutboxSize = 0;
        uint8_t first_msg = 0xFF;
        time_t first_time = 0;
        for (uint8_t i = 0; i < CONFIG_TELEGRAM_OUTBOX_SIZE; i++) {
          if (_tgOutbox[i].queued) {
            if ((first_time == 0) || (_tgOutbox[i].timestamp < first_time)) {
              first_msg = i;
              first_time = _tgOutbox[i].timestamp;
            };
            _tgOutboxSize++;
          };
        };
        
        // If the queue is not empty, send the first found message
        if ((_tgOutboxSize > 0) && (first_msg < CONFIG_TELEGRAM_OUTBOX_SIZE)) {
          esp_err_t resSend = tgSendApi(&_tgOutbox[first_msg]);
          // If the send status has changed, send an event to the event loop
          if (resSend != resLast) {
            resLast = resSend;
            eventLoopPostError(RE_SYS_TELEGRAM_ERROR, resLast);
          };
          // If the message is sent (ESP_OK) or too long (ESP_ERR_NO_MEM) or an API error (ESP_ERR_INVALID_ARG - bad message?), then remove it from the queue
          if ((resSend == ESP_OK) || (resSend == ESP_ERR_NO_MEM) || (resSend == ESP_ERR_INVALID_ARG)) {
            #if !CONFIG_TELEGRAM_STATIC_MESSAGE_BUFFER
              if (_tgOutbox[first_msg].message) free(_tgOutbox[first_msg].message);
              _tgOutbox[first_msg].message = nullptr;
            #endif // CONFIG_TELEGRAM_MESSAGE_SIZE
            _tgOutbox[first_msg].queued = false;
            _tgOutboxSize--;
            rlog_d(logTAG, "Message #%d removed from queue, outbox size: %d", first_msg, _tgOutboxSize);
          };
        };
      };
    #else
      // Direct send
      if (xQueueReceive(_tgQueue, &inMsg, portMAX_DELAY) == pdPASS) {
        rlog_v(logTAG, "New message received: %s", inMsg->message);

        tgMessageItem_t tgMsg;
        memset(&tgMsg, 0, sizeof(tgMessageItem_t));
        #if CONFIG_TELEGRAM_STATIC_MESSAGE_BUFFER
          memset(tgMsg.message, 0, CONFIG_TELEGRAM_MESSAGE_SIZE);
          strncpy(tgMsg.message, inMsg->message, CONFIG_TELEGRAM_MESSAGE_SIZE-1);
          if (inMsg->message) free(inMsg->message);
        #else
          tgMsg.message = inMsg->message;
        #endif // CONFIG_TELEGRAM_MESSAGE_SIZE
        tgMsg.options = inMsg->options;
        tgMsg.timestamp = inMsg->timestamp;
        free(inMsg);
        inMsg = nullptr;
        
        // Trying to send a message to the Telegram API
        uint16_t trySend = 0;
        // Waiting for internet access
        while (statesInetWait(portMAX_DELAY)) {
          trySend++;
          esp_err_t resSend = tgSendApi(&tgMsg);
          // If the send status has changed, send an event to the event loop
          if (resSend != resLast) {
            resLast = resSend;
            eventLoopPostError(RE_SYS_TELEGRAM_ERROR, resLast);
          };
          // If the message is sent (ESP_OK) or too long (ESP_ERR_NO_MEM) or an API error (ESP_ERR_INVALID_ARG - bad message?), then remove it from the heap
          if (resSend == ESP_OK) {
            #if !CONFIG_TELEGRAM_STATIC_MESSAGE_BUFFER
              if (tgMsg.message) free(tgMsg.message);
              tgMsg.message = nullptr;
            #endif // CONFIG_TELEGRAM_MESSAGE_SIZE
            break;
          } else {
            if (trySend <= CONFIG_TELEGRAM_MAX_ATTEMPTS) {
              if (resLast == ESP_ERR_INVALID_RESPONSE) {
                vTaskDelay(pdMS_TO_TICKS(CONFIG_TELEGRAM_FORBIDDEN_INTERVAL));
              } else {
                vTaskDelay(pdMS_TO_TICKS(CONFIG_TELEGRAM_SEND_INTERVAL));
              };
            } else {
              rlog_e(logTAG, "Failed to send message %s", tgMsg.message);
              #if !CONFIG_TELEGRAM_STATIC_MESSAGE_BUFFER
                if (tgMsg.message) free(tgMsg.message);
                tgMsg.message = nullptr;
              #endif // CONFIG_TELEGRAM_MESSAGE_SIZE
              break;
            };
          };
        };
      };
    #endif // CONFIG_TELEGRAM_OUTBOX_ENABLE
  };

  // Delete task
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
        eventLoopPostError(RE_SYS_TELEGRAM_ERROR, ESP_FAIL);
        return false;
      };
    };
    
    #if CONFIG_TELEGRAM_STATIC_ALLOCATION
    _tgTask = xTaskCreateStaticPinnedToCore(tgTaskExec, tgTaskName, CONFIG_TELEGRAM_STACK_SIZE, nullptr, CONFIG_TASK_PRIORITY_TELEGRAM, _tgTaskStack, &_tgTaskBuffer, CONFIG_TASK_CORE_TELEGRAM); 
    #else
    xTaskCreatePinnedToCore(tgTaskExec, tgTaskName, CONFIG_TELEGRAM_STACK_SIZE, nullptr, CONFIG_TASK_PRIORITY_TELEGRAM, &_tgTask, CONFIG_TASK_CORE_TELEGRAM); 
    #endif // CONFIG_TELEGRAM_STATIC_ALLOCATION
    if (!_tgTask) {
      vQueueDelete(_tgQueue);
      rloga_e("Failed to create task for sending notifications to Telegram!");
      eventLoopPostError(RE_SYS_TELEGRAM_ERROR, ESP_FAIL);
      return false;
    }
    else {
      rloga_i("Task [ %s ] has been successfully started", tgTaskName);
      eventLoopPostError(RE_SYS_TELEGRAM_ERROR, ESP_OK);
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
      eventLoopPostError(RE_SYS_TELEGRAM_ERROR, ESP_ERR_NOT_SUPPORTED);
      return true;
    } else {
      rloga_e("Failed to suspend task [ %s ]!", tgTaskName);
      eventLoopPostError(RE_SYS_TELEGRAM_ERROR, ESP_FAIL);
    };
  };
  return false;
}

bool tgTaskResume()
{
  if ((_tgTask) && (eTaskGetState(_tgTask) == eSuspended)) {
    vTaskResume(_tgTask);
    if (eTaskGetState(_tgTask) != eSuspended) {
      rloga_i("Task [ %s ] has been successfully resumed", tgTaskName);
      eventLoopPostError(RE_SYS_TELEGRAM_ERROR, ESP_OK);
      return true;
    } else {
      rloga_e("Failed to resume task [ %s ]!", tgTaskName);
      eventLoopPostError(RE_SYS_TELEGRAM_ERROR, ESP_FAIL);
    };
  };
  return false;
}

bool tgTaskDelete()
{
  if (_tgQueue) {
    vQueueDelete(_tgQueue);
    rloga_v("The queue for sending notifications in Telegram has been deleted");
    _tgQueue = nullptr;
  };

  if (_tgTask) {
    vTaskDelete(_tgTask);
    _tgTask = nullptr;
    rloga_d("Task [ %s ] was deleted", tgTaskName);
  };
  
  return true;
}

