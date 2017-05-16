#ifndef SYSTEM_H
#define SYSTEM_H

#define WIFI_SSID "Juno_"
#define WIFI_PASS "huehuehue"

#define MAX_RETRIES 10

#define CLIENT 1
#define BROKER 2

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

typedef enum
{
    WRITE_WIFI_CONF, WRITE_PLC_CONF
} InitMode_t;

typedef struct PermConfData
{
    char *SSID, *password, *PLCPhyAddr, *tbToken;
    uint8_t SSIDLen, passwordLen, PLCPhyAddrLen, tbTokenLen;
    InitMode_t mode;
} PermConfData_s;

extern QueueHandle_t xInitializerQueue;
extern TaskHandle_t xInitializerTask;

extern volatile int devType;
extern volatile char myTbToken[20];

void initializerTask(void *pvParameters);

void parsePLCPhyAddress(char *asciiSrc, uint8_t *binDest);

#endif