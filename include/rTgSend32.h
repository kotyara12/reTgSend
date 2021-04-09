/* 
   EN: Module for sending notifications to Telegram from ESP32
   RU: Отправка уведомлений в Telegram из ESP32
   --------------------------
   (с) 2020-2021 Разживин Александр | Razzhivin Alexander
   kotyara12@yandex.ru | https://kotyara12.ru | tg: @kotyara1971
*/

#ifndef __RTGSEND32_H__
#define __RTGSEND32_H__

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

bool tgTaskCreate();
bool tgTaskSuspend();
bool tgTaskResume();
bool tgTaskDelete();
bool tgSend(const bool msgNotify, const char* msgTitle, const char* msgText, ...);

#ifdef __cplusplus
}
#endif

#endif // __RTGSEND32_H__
