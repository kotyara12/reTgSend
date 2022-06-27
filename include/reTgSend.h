/* 
   EN: Module for sending notifications to Telegram from ESP32
   RU: Отправка уведомлений в Telegram из ESP32
   --------------------------
   (с) 2020-2021 Разживин Александр | Razzhivin Alexander
   kotyara12@yandex.ru | https://kotyara12.ru | tg: @kotyara1971
*/

#ifndef __RE_TGSEND_H__
#define __RE_TGSEND_H__

#include <stdio.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include "project_config.h"
#include "def_consts.h"
#include "rLog.h"
#include "rTypes.h"
#include "rStrings.h"
#include "reEsp32.h"
#include "reTgSend.h"
#include "reEvents.h"
#include "reStates.h"

#if defined(CONFIG_TELEGRAM_MESSAGE_SIZE) && (CONFIG_TELEGRAM_MESSAGE_SIZE > 0)
  #define CONFIG_TELEGRAM_STATIC_MESSAGE_BUFFER 1
#else
  #define CONFIG_TELEGRAM_STATIC_MESSAGE_BUFFER 0
#endif // CONFIG_TELEGRAM_MESSAGE_SIZE

#if defined(CONFIG_TELEGRAM_OUTBOX_SIZE) && (CONFIG_TELEGRAM_OUTBOX_SIZE > 0)
  #define CONFIG_TELEGRAM_OUTBOX_ENABLE 1
#else
  #define CONFIG_TELEGRAM_OUTBOX_ENABLE 0
#endif // CONFIG_TELEGRAM_OUTBOX_SIZE


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

/**
 * @brief Delete the task (for example, before restarting the device)
 * @return true - successful, false - failure
 * */
bool tgTaskDelete();

/**
 * Add a message to the send queue
 * @brief Add a message to the send queue. If the Internet is available, an attempt will be made to send a message almost immediately
 * @param msgOptions - message options (kind, priority and notification)
 * @param msgTitle - message header
 * @param msgText - message text or formatting template
 * @param ... - formatting options
 * @return true - successful, false - failure
 * */
bool tgSendMsg(msg_options_t msgOptions, const char* msgTitle, const char* msgText, ...);

/**
 * Easier adding a message to the send queue
 * @brief Easier adding a message to the send queue (no need to code options)
 * @param msgKind - kind of message, depending on this message can be sent to different chats or groups
 * @param msgPriority - sending priority - in case of problems with the Internet, messages with low priority may not be sent
 * @param msgNotify - send a notification message
 * @param msgTitle - message header
 * @param msgText - message text or formatting template
 * @param ... - formatting options
 * @return true - successful, false - failure
 * */
#define tgSend(msgKind, msgPriority, msgNotify, msgTitle, msgText, ...) tgSendMsg(encMsgOptions(msgKind, msgNotify, msgPriority), msgTitle, msgText, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif // __RE_TGSEND_H__
