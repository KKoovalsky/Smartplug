#include "client.h"
#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "plc.h"

// TODO: Parse PLC Phy address to two integers composing a key
volatile client_s *clientListBegin = NULL;
volatile client_s *clientListEnd = NULL;

client_s *createClientLocal(char *tbToken)
{
	client_s *newClient = (client_s *)pvPortMalloc(sizeof(client_s));
	setPlcPhyAddrFromPLCChip(newClient->plcPhyAddr);
	memcpy(newClient->tbToken, tbToken, 20);
	newClient->tbToken[20] = '\0';
	return newClient;
}

client_s *createClient(uint8_t *plcPhyAddr, char *tbToken)
{
	client_s *newClient = (client_s *)pvPortMalloc(sizeof(client_s));
	memcpy(newClient->plcPhyAddr, plcPhyAddr, 8);
	memcpy(newClient->tbToken, tbToken, 20);
	newClient->tbToken[20] = '\0';
	return newClient;
}

void addClient(client_s *client)
{
	if (clientListBegin == NULL)
	{
		clientListBegin = clientListEnd = client;
		return;
	}

	clientListEnd->next = client;
	clientListEnd = client;
}
