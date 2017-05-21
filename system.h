#ifndef SYSTEM_H
#define SYSTEM_H

#define WIFI_SSID "Juno_"
#define WIFI_PASS "huehuehue"

#define MAX_RETRIES 10

#define CLIENT 1
#define BROKER 2

#define MAX_WAITTIME_FOR_DISCONNECT_WHEN_CHANGING_STATION 20

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

struct sdk_station_config;

extern QueueHandle_t xConfiguratorQueue;
extern TaskHandle_t xConfiguratorTask;

extern volatile int devType;
extern volatile char myTbToken[20];

void configuratorTask(void *pvParameters);

void initDeviceByMode();

void parsePLCPhyAddress(char *asciiSrc, uint8_t *binDest);

void fillStationConfig(struct sdk_station_config *config, char *ssid, char *password, 
	uint8_t ssidLen, uint8_t passwordLen);


#endif