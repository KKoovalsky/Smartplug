#ifndef SYSTEM_H
#define SYSTEM_H

#define WIFI_SSID "Juno_"
#define WIFI_PASS "huehuehue"

#define MAX_RETRIES 15

#define CLIENT 1
#define BROKER 2

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

extern QueueHandle_t xConnectWhileConfigQueue;
extern TaskHandle_t xConnectWhileConfigTask;

extern volatile int devType;
extern volatile char myTbToken[20];

void connectWhileConfigTask(void *pvParameters);

void parsePLCPhyAddress(char *asciiSrc, char *binDest);

#endif