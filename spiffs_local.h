#ifndef SPIFFS_LOCAL_H_
#define SPIFFS_LOCAL_H_

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

typedef enum
{
    SPIFFS_WRITE_WIFI_CONF, SPIFFS_WRITE_PLC_CONF
} SpiffsMode_t;

typedef struct 
{
    char *SSID, *password, *PLCPhyAddr, *tbToken;
    uint8_t SSIDLen, passwordLen, PLCPhyAddrLen, tbTokenLen;
    SpiffsMode_t mode;
} PermConfData_s;

extern TaskHandle_t xSPIFFSTask;
extern QueueHandle_t xSPIFFSQueue;

extern const char clientStr[7];
extern const char brokerStr[7];


int initFileSystem();

void saveBrokerConfigDataToFile(PermConfData_s *);
void saveClientConfigDataToFile(PermConfData_s *);

int getDeviceModeFromFile(char *);

void setClientPlcPhyAddrOfBrokerAndTbToken();
void setBrokerTbTokenFromFile();

void checkFileContent();

#endif