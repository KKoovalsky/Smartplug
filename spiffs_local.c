#include "espressif/esp_common.h"
#include "esp/uart.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "esp8266.h"

#include <string.h>
#include "fcntl.h"
#include "unistd.h"

#include "spiffs.h"
#include "esp_spiffs.h"
#include "spiffs_local.h"
#include "http_server.h"
#include "system.h"
#include "plc.h"

const char clientStr[] = "CLIENT";
const char brokerStr[] = "BROKER";

/* TODO:    1. Use O_RDWR in place of O_WRONLY (problems occured when using o_RDWR)
            2. Delete rubbish at the end of file, 
                which are leftovers from past config data, which was longer than now. */

static void getConfigFileContent(char *buffer, int size);

int initFileSystem()
{
	esp_spiffs_init();

	if (esp_spiffs_mount() != SPIFFS_OK)
	{
		printf("Error mounting SPIFFS\n");
		return -1;
	}

	return 0;
}

void saveConfigDataToFile(PermConfData_s *configData)
{
	int fd = open("smartplug.conf", O_WRONLY, 0);
	if (fd < 0)
	{
		printf("Error opening configuration file\n");
		return;
	}

	lseek(fd, 0, SEEK_SET);

	if (configData->mode == BROKER_CONF)
		write(fd, brokerStr, sizeof(brokerStr) - 1);
	else
		write(fd, clientStr, sizeof(clientStr) - 1);

	write(fd, "\n", 1);
	write(fd, configData->ssid, configData->ssidLen);
	write(fd, "\n", 1);
	write(fd, configData->password, configData->passwordLen);
	write(fd, "\n", 1);
	write(fd, configData->tbToken, 20);
	write(fd, "\n", 1);
	write(fd, configData->plcPhyAddr, 16);
	write(fd, "\n", 1);

	close(fd);
}

int getDeviceModeFromFile(char *buf)
{
	// Read file which contains mode of operation.
	int fd = open("smartplug.conf", O_RDONLY, 0);
	if (fd < 0)
	{
		printf("Error opening configuration file.\n");
		*buf = '\0';
		return -1;
	}

	lseek(fd, 0, SEEK_SET);
	read(fd, buf, 6);
	close(fd);

	return 0;
}

static void getConfigFileContent(char *buffer, int size)
{
	// Read file which contains mode of operation.
	int fd = open("smartplug.conf", O_RDONLY, 0);
	if (fd < 0)
	{
		printf("Error opening configuration file.\n");
		*buffer = '\0';
		return;
	}

	lseek(fd, 0, SEEK_SET);
	read(fd, buffer, size);
	close(fd);
}

void getTbTokenAndBrokerPlcPhyAddrFromFile(char *tbToken, char *plcPhyAddr)
{
	char buffer[144];
	getConfigFileContent(buffer, sizeof(buffer));

	char *p = strtok(buffer, "\n");
	p = strtok(NULL, "\n");
	p = strtok(NULL, "\n");
	p = strtok(NULL, "\n");
	if (tbToken)
		memcpy(tbToken, p, 20);
	p = strtok(NULL, "\n");
	if (plcPhyAddr)
		memcpy(plcPhyAddr, p, 16);
}

void printFileContent()
{
	char buffer[146];
	memset(buffer, 0, sizeof(buffer));

	int fd = open("smartplug.conf", O_RDONLY, 0);
	if (fd < 0)
	{
		printf("Error opening file\n");
		return;
	}

	lseek(fd, 0, SEEK_SET);
	read(fd, buffer, sizeof(buffer));
	printf("%s\n", buffer);

	close(fd);
}