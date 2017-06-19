#ifndef CLIENT_H
#define CLIENT_H

#include <stdio.h>
#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"

typedef struct client {
	struct client *next;
	uint8_t plcPhyAddr[8];		// PLC Physical address is binary data.
	char deviceName[32 + 1]; 	// Thingsboard token is an ascii type string so null termination is required
} client_s;

extern volatile client_s *clientListBegin;
extern volatile client_s *clientListEnd;
extern volatile int clientCnt;

void addClient(client_s *client);
client_s *createClient(uint8_t *plcPhyAddr, char *deviceName, int deviceNameLen);
client_s *createClientFromString(char *plcPhyAddr, char *deviceName, int deviceNameLen);
void getDeviceNameByPlcPhyAddr(char *destDeviceName, uint8_t *srcPlcPhyAddr);

#endif