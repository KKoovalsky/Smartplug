#ifndef SPIFFS_LOCAL_H_
#define SPIFFS_LOCAL_H_

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "client.h"

typedef struct PermConfData PermConfData_s;

extern const char clientStr[7];
extern const char brokerStr[7];

int initFileSystem();

void saveConfigDataToFile(PermConfData_s *);
int getDeviceModeFromFile(char *);
void getCredentialsFromFile(char *ssid, char *wifiPassword, char *tbToken, char *plcPhyAddr, char *deviceName);
void saveClientDataToFile(client_s *newClient);
void retrieveClientListFromFile();
void printFileContent();

#endif