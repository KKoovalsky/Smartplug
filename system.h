#ifndef SYSTEM_H
#define SYSTEM_H

#define WIFI_PASS "huehuehue"

//#define PLC_TX_TEST

#ifdef PLC_TX_TEST
#define WIFI_SSID "I_TAK_NIE_MAM_NET"
#else
#define WIFI_SSID "MAM_NETA_ALE_SIE_WTOREK"
#endif

#define MAX_RETRIES 15

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

extern QueueHandle_t xConnectWhileConfigQueue;
extern TaskHandle_t xConnectWhileConfigTask;

void startBrokerMode();
void startClientMode();

void setSoftAP();
void setAP_STA();

void connectToStation(char *SSID, char *password, int SSIDLen, int passwordLen);

void connectWhileConfigTask(void *pvParameters);

#endif