#ifndef HTTP_SERVER_H_
#define HTTP_SERVER_H_

#include <FreeRTOS.h>

#define WIFI_PASS "huehuehue"
#define WIFI_SSID "I_TAK_NIE_MAM_NETA"

#define LED_PIN 2

enum {
    SSI_UPTIME,
    SSI_FREE_HEAP,
    SSI_LED_STATE
};

void httpd_task(void *pvParameters);

#endif