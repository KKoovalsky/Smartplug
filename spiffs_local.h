#ifndef SPIFFS_LOCAL_H_
#define SPIFFS_LOCAL_H_

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

typedef struct PermConfData PermConfData_s;

extern const char clientStr[7];
extern const char brokerStr[7];

int initFileSystem();

void saveBrokerConfigDataToFile(PermConfData_s *);
void saveClientConfigDataToFile(PermConfData_s *);
void saveClientWifiCredentialsToFile(char *newWifiSsid, char *newWifiPassword, 
	uint8_t newSsidLen, uint8_t newWifiPasswordLen);

int getDeviceModeFromFile(char *);

void setClientPlcPhyAddrOfBrokerAndTbTokenFromFile();
void setBrokerTbTokenFromFile();

void printFileContent();

#endif