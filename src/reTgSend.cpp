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
#include "reLed.h"
#include "reLedSys.h"
#include "reWiFi.h"
#include "reTgSend.h"
#include "esp_wifi.h" 
#include "esp_tls.h"
#include "esp_http_client.h"

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

static const char* tagTG = "TG";
static const char* tgTaskName = "tgSend";

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
  ledSysOn(true);
  bool _result = true;
  struct tm timeinfo;
  static char buffer_timestamp[20];

  // Формируем текст запроса (сообщения)
  localtime_r(&tgMsg->timestamp, &timeinfo);
  strftime(buffer_timestamp, sizeof(buffer_timestamp), CONFIG_TELEGRAM_TIME_FORMAT, &timeinfo);
  char* json = malloc_stringf(API_TELEGRAM_TMPL_MESSAGE, 
    CONFIG_TELEGRAM_CHAT_ID, tgNotifyEx(tgMsg), tgMsg->title, tgMsg->message, buffer_timestamp);

  // Настраиваем параметры запроса
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

  // Выполняем запрос к Telegram API
  esp_http_client_handle_t client = esp_http_client_init(&cfgHttp);
  if (client != NULL) {
    esp_http_client_set_header(client, API_TELEGRAM_HEADER_CTYPE, API_TELEGRAM_HEADER_AJSON);
    esp_http_client_set_post_field(client, json, strlen(json));
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
      int retCode = esp_http_client_get_status_code(client);
      _result = ((retCode == 200) || (retCode == 301));
      if (!_result)
        rlog_e(tagTG, "Failed to send message, API error code: #%d!", retCode);
    }
    else {
      _result = false;
      rlog_e(tagTG, "Failed to complete request to Telegram API, error code: 0x%x!", err);
    };
    esp_http_client_cleanup(client);
  }
  else {
    _result = false;
    rlog_e(tagTG, "Failed to complete request to Telegram API!");
  };

  if (json) free(json);
  ledSysOff(true);
  return _result;
}

void tgTaskExec(void *pvParameters)
{
  tgMessage_t *tgMsg;

  while (true) {
    if (xQueuePeek(_tgQueue, &tgMsg, portMAX_DELAY) == pdPASS) {
      rlog_v(tagTG, "Message received from queue: [%d] %s :: %s", tgMsg->timestamp, tgMsg->title, tgMsg->message);

      // Ждем подключения к интернету, если его нет (а ещё можно приостановить задачу если соединение прервано, но это не обязательно)
      if (!wifiIsConnected()) {
        ledSysStateSet(SYSLED_TELEGRAM_ERROR, false);
        wifiWaitConnection(portMAX_DELAY);
        ledSysStateClear(SYSLED_TELEGRAM_ERROR, false);
      };
      
      // Пытаемся отправить сообщение в API Telegram
      uint16_t tryAttempt = 1;
      bool resAttempt = false;
      do 
      {
        resAttempt = tgSendEx(tgMsg);
        if (resAttempt) {
          ledSysStateClear(SYSLED_TELEGRAM_ERROR, false);
        }
        else {
          ledSysStateSet(SYSLED_TELEGRAM_ERROR, false);
          tryAttempt++;
          vTaskDelay(CONFIG_TELEGRAM_ATTEMPTS_INTERVAL / portTICK_RATE_MS);
        };
        // esp_task_wdt_reset();
      } while (!resAttempt && (tryAttempt <= CONFIG_TELEGRAM_MAX_ATTEMPTS));
      
      // Отладочный вывод
      if (resAttempt) {
        rlog_i(tagTG, "Message sent: [%d] %s :: %s", tgMsg->timestamp, tgMsg->title, tgMsg->message);
      }
      else {
        rlog_e(tagTG, "Failed to send message [%d] %s :: %s", tgMsg->timestamp, tgMsg->title, tgMsg->message);
      };

      // Удаляем сообщение из очереди (в любом случае)
      xQueueReceive(_tgQueue, &tgMsg, 0);
      // Freeing the memory allocated for the message
      free(tgMsg->title);
      free(tgMsg->message);
      delete tgMsg;
    };
  };

  tgTaskDelete();
}

bool tgSend(const bool msgNotify, const char* msgTitle, const char* msgText, ...)
{
  if (_tgQueue != NULL) {
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
      ledSysStateSet(SYSLED_TELEGRAM_ERROR, false);
      // Freeing the memory allocated for the message
      free(tgMsg->title);
      free(tgMsg->message);
      delete tgMsg;
    };
  };

  return false;
}

bool tgTaskSuspend()
{
  if ((_tgTask != NULL) && (eTaskGetState(_tgTask) != eSuspended)) {
    vTaskSuspend(_tgTask);
    rloga_d("Task [ %s ] has been successfully suspended", tgTaskName);
    return true;
  }
  else {
    rloga_w("Task [ %s ] not found or is already suspended", tgTaskName);
    return false;
  };
}

bool tgTaskResume()
{
  if ((_tgTask != NULL) && wifiIsConnected() && (eTaskGetState(_tgTask) == eSuspended)) {
    vTaskResume(_tgTask);
    rloga_d("Task [ %s ] has been successfully started", tgTaskName);
    return true;
  }
  else {
    rloga_w("Task [ %s ] is not found or is already running", tgTaskName);
    return false;
  };
}

bool tgTaskCreate() 
{
  if (_tgTask == NULL) {
    if (_tgQueue == NULL) {
      #if CONFIG_TELEGRAM_STATIC_ALLOCATION
      _tgQueue = xQueueCreateStatic(CONFIG_TELEGRAM_QUEUE_SIZE, TELEGRAM_QUEUE_ITEM_SIZE, &(_tgQueueStorage[0]), &_tgQueueBuffer);
      #else
      _tgQueue = xQueueCreate(CONFIG_TELEGRAM_QUEUE_SIZE, TELEGRAM_QUEUE_ITEM_SIZE);
      #endif // CONFIG_TELEGRAM_STATIC_ALLOCATION
      if (_tgQueue == NULL) {
        rloga_e("Failed to create a queue for sending notifications to Telegram!");
        ledSysStateSet(SYSLED_ERROR, false);
        return false;
      };
    };
    
    #if CONFIG_TELEGRAM_STATIC_ALLOCATION
    _tgTask = xTaskCreateStaticPinnedToCore(tgTaskExec, tgTaskName, CONFIG_TELEGRAM_STACK_SIZE, NULL, CONFIG_TELEGRAM_PRIORITY, _tgTaskStack, &_tgTaskBuffer, CONFIG_TELEGRAM_CORE); 
    #else
    xTaskCreatePinnedToCore(tgTaskExec, tgTaskName, CONFIG_TELEGRAM_STACK_SIZE, NULL, CONFIG_TELEGRAM_PRIORITY, &_tgTask, CONFIG_TELEGRAM_CORE); 
    #endif // CONFIG_TELEGRAM_STATIC_ALLOCATION
    if (_tgTask == NULL) {
      vQueueDelete(_tgQueue);
      rloga_e("Failed to create task for sending notifications to Telegram!");
      ledSysStateSet(SYSLED_ERROR, false);
      return false;
    }
    else {
      rloga_d("Task [ %s ] has been successfully started", tgTaskName);
      ledSysStateClear(SYSLED_TELEGRAM_ERROR, false);
      return true;
    };
  }
  else {
    return tgTaskResume();
  };
}

bool tgTaskDelete()
{
  if (_tgQueue != NULL) {
    vQueueDelete(_tgQueue);
    rloga_v("The queue for sending notifications in Telegram has been deleted");
    _tgQueue = NULL;
  };

  if (_tgTask != NULL) {
    vTaskDelete(_tgTask);
    _tgTask = NULL;
    rloga_d("Task [ %s ] was deleted", tgTaskName);
  };
  
  return true;
}

