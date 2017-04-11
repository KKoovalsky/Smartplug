#ifndef HTTP_SERVER_H_
#define HTTP_SERVER_H_

#include <FreeRTOS.h>
#include "task.h"

#define LED_PIN 2

extern TaskHandle_t xHTTPServerTask;

void httpd_task(void *pvParameters);

#endif