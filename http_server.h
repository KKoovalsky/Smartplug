#ifndef HTTP_SERVER_H_
#define HTTP_SERVER_H_

#include <FreeRTOS.h>
#include "task.h"

#define LED_PIN 2

extern TaskHandle_t xHTTPServerTask;

extern const uint8_t *wifiJsonStrings[];
extern const uint8_t wifiJsonStringsLen [];

void httpd_task(void *pvParameters);

void sendWsResponse(const uint8_t *msg, int len);

#endif