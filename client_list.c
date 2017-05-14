#include "client_list.h"
#include <stdio.h>

volatile client_s *clientListBegin = NULL;
volatile client_s *clientListEnd = NULL;

void addClient(client_s * client)
{
	if(clientListBegin == NULL)
	{
		clientListBegin = clientListEnd = client;
		return;
	}

	clientListEnd->next = client;
	clientListEnd = client;
}

