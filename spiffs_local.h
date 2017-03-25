#ifndef SPIFFS_LOCAL_H_
#define SPIFFS_LOCAL_H_

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

typedef enum
{
    SPIFFS_READ = 1, SPIFFS_WRITE_WIFI_CONF, SPIFFS_WRITE_PLC_CONF
} SpiffsMode_t;

typedef struct 
{
    char *SSID, *password, *PLCPhyAddr;
    SpiffsMode_t mode;
} PermConfData_s;

extern TaskHandle_t xSPIFFSTask;
extern QueueHandle_t xSPIFFSQueue;

void spiffsTask(void *pvParameters);
void checkFileContent();

#endif