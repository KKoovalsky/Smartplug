#ifndef CLIENT_H
#define CLIENT_H

#include <stdio.h>
#include <stdint.h>

typedef struct client {
	struct client *next;
	uint8_t plcPhyAddr[8];		// PLC Physical address is binary data.
	char tbToken[20 + 1]; 	// Thingsboard token is an ascii type string so null termination is required
} client_s;

extern volatile client_s *clientListBegin;
extern volatile client_s *clientListEnd;

void addClient(client_s *client);
client_s *createClient(uint8_t *plcPhyAddr, char *tbToken);
client_s *createClientFromAscii(char *plcPhyAddr, char *tbToken);


#endif