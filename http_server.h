#ifndef HTTP_SERVER_H_
#define HTTP_SERVER_H_

#include <FreeRTOS.h>
#include "plc.h"

#define WIFI_PASS "huehuehue"

#ifdef PLC_TX_TEST
#define WIFI_SSID "I_TAK_NIE_MAM_NETA"
#else
#define WIFI_SSID "MAM_NETA_ALE_SIE_NIEDZIELE"
#endif

#define LED_PIN 2

void httpd_task(void *pvParameters);

#endif