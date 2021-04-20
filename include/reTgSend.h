/* 
   EN: Module for sending notifications to Telegram from ESP32
   RU: Отправка уведомлений в Telegram из ESP32
   --------------------------
   (с) 2020-2021 Разживин Александр | Razzhivin Alexander
   kotyara12@yandex.ru | https://kotyara12.ru | tg: @kotyara1971
*/

#ifndef __RE_TGSEND_H__
#define __RE_TGSEND_H__

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief We create and launch a task (and a queue) to send notifications.
 * @return true - successful, false - failure
 * */
bool tgTaskCreate();

/**
 * @brief We pause the task for sending notifications. For example, when disconnecting from WiFi.
 * @return true - successful, false - failure
 * */
bool tgTaskSuspend();
bool tgTaskResume();
bool tgTaskDelete();
bool tgSend(const bool msgNotify, const char* msgTitle, const char* msgText, ...);

#ifdef __cplusplus
}
#endif

#endif // __RE_TGSEND_H__
