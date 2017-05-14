#ifndef CLIENT_LIST_H
#define CLIENT_LIST_H

#include <stdio.h>
#include <stdint.h>

typedef struct client {
	struct client *next;
	char plcPhyAddr[8];
	char tbToken[20];
	uint8_t plcLogicAddr;
} client_s;

void addClient(client_s *client);


#endif