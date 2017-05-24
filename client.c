#include "client.h"
#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "parsers.h"

// TODO: Parse PLC Phy address to two integers composing a key
volatile client_s *clientListBegin = NULL;
volatile client_s *clientListEnd = NULL;

client_s *createClient(uint8_t *plcPhyAddr, char *tbToken)
{
	client_s *newClient = (client_s *)pvPortMalloc(sizeof(client_s));
	memcpy(newClient->plcPhyAddr, plcPhyAddr, 8);
	memcpy(newClient->tbToken, tbToken, 20);
	newClient->tbToken[20] = '\0';
	return newClient;
}

client_s *createClientFromAscii(char *plcPhyAddr, char *tbToken)
{
	uint8_t rawPlcPhyAddr[8];
	convertPlcPhyAddressToRaw(rawPlcPhyAddr, plcPhyAddr);
	return createClient(rawPlcPhyAddr, tbToken);
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
