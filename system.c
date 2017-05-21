#include "system.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp8266.h"
#include "espressif/esp_common.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "timers.h"
#include <dhcpserver.h>
#include "spiffs_local.h"
#include "http_server.h"
#include "cloud.h"
#include "plc.h"
#include "spiffs.h"
#include "esp_spiffs.h"

QueueHandle_t xConfiguratorQueue;
TaskHandle_t xConfiguratorTask;

volatile int devType;
volatile char myTbToken[20];

// TODO: Check if Thingsboard token is correct
// TODO: Get rid of small mallocs.

static void startBrokerMode(bool isBoot);
static void startClientMode(bool isBoot);

static inline void setMyTbToken(char *tbToken);
static void setStationAPMode();
static void connectToStation(char *SSID, char *password, int SSIDLen, int passwordLen);
static inline void fillJsonConnectionSuccessStringWithPlcPhyAddr();

void initDeviceByMode()
{
	char buffer[8];
	getDeviceModeFromFile(buffer);

	// Check if device is already configured as client or broker, otherwise start HTTP server to get configuration.
	if (!strncmp(buffer, clientStr, sizeof(clientStr) - 1))
		startClientMode(true);
	else if (!strncmp(buffer, brokerStr, sizeof(brokerStr) - 1))
		startBrokerMode(true);
	else // If its first run of this device, then start HTTP server to get configuration
	{
		printf("First run of the device\n");
		setStationAPMode();
		xTaskCreate(httpd_task, "HTTP Daemon", 128, NULL, 2, &xHTTPServerTask);
		xTaskCreate(configuratorTask, "configConnect", 1536, NULL, 4, &xConfiguratorTask);
	}
}

void configuratorTask(void *pvParameters)
{
	for (;;)
	{
		PermConfData_s configData;
		xQueueReceive(xConfiguratorQueue, &configData, portMAX_DELAY);

		if (configData.mode == WRITE_WIFI_CONF)
		{
			connectToStation(configData.SSID, configData.password, configData.SSIDLen, configData.passwordLen);
			int retries = MAX_RETRIES;
			unsigned int status = (unsigned int)sdk_wifi_station_get_connect_status();

			while (status == STATION_CONNECTING && retries)
			{
				printf("WiFi: connecting...\r\n");
				vTaskDelay(pdMS_TO_TICKS(3000));
				status = (unsigned int)sdk_wifi_station_get_connect_status();
				retries--;
			}

			if (!retries)
				printf("Too long time spent on connecting. Leaving...\n\r");

			if (status == STATION_GOT_IP)
			{
				// Set global Thingsboard token, which was passed by user.
				setMyTbToken(configData.tbToken);

				// Add PLC phy address to enable client connection to broker in the future.
				fillJsonConnectionSuccessStringWithPlcPhyAddr();

				// Subscribe to get notification when ACK via websocket will be sent.
				xWSGetAckTaskHandle = xConfiguratorTask;

				// Send JSON data that connection was successful
				sendWsResponse(wifiConnectionSuccessJson, wifiConnectionSuccessJsonLen);

				printf("WiFi: succesfully connected to AP\r\n");

				// Save the data to be available after reset/power down.
				saveBrokerConfigDataToFile(&configData);

				// Wait for ACK from websocket
				ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10000));

				startBrokerMode(false);

				// Free memory and delete this task
				vPortFree(configData.SSID);
				vPortFree(configData.password);
				vPortFree(configData.tbToken);
				vTaskDelete(NULL);
			}

			sdk_wifi_station_disconnect();
			vPortFree(configData.SSID);
			vPortFree(configData.password);
			vPortFree(configData.tbToken);

			sendWsResponse(wifiJsonStrings[status], wifiJsonStringsLen[status]);
		}
		else if (configData.mode == WRITE_PLC_CONF)
		{
			if (configData.PLCPhyAddrLen < 16)
			{
				printf("Client registration unsuccessful.");
				vPortFree(configData.PLCPhyAddr);
				vPortFree(configData.tbToken);
				sendWsResponse(plcJsonTooShortPhyAddrStr, plcJsonTooShortPhyAddrStrLen);
				continue;
			}

			uint8_t destPhyAddr[8];
			parsePLCPhyAddress((char *)configData.PLCPhyAddr, destPhyAddr);
			printf("Connecting to PLC broker: %X%X%X%X%X%X%X%X\n\r", PLCPHY2STR(destPhyAddr));

			if (registerClient((char *)destPhyAddr, configData.tbToken) >= 0)
			{
				printf("Client registration successful.\n\r");
				memcpy((char *)plcPhyAddr, destPhyAddr, 8);
				setMyTbToken(configData.tbToken);

				xWSGetAckTaskHandle = xConfiguratorTask;

				sendWsResponse(plcJsonRegisSuccessStr, plcJsonRegisSuccessStrLen);

				saveClientConfigDataToFile(&configData);

				ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10000));

				startClientMode(false);

				vPortFree(configData.PLCPhyAddr);
				vPortFree(configData.tbToken);
				vTaskDelete(NULL);
			}
			else
			{
				printf("Client registration unsuccessful.\n\r");
				vPortFree(configData.PLCPhyAddr);
				vPortFree(configData.tbToken);
				sendWsResponse(plcJsonRegisUnsuccessStr, plcJsonRegisUnsuccessStrLen);
			}
		}
		else
			printf("Unknown spiffs mode\n");
	}
}

static uint8_t getUint8FromHexChar(char c)
{
	if (c >= '0' && c <= '9')
		c -= '0';
	else if (c >= 'A' && c <= 'Z')
		c -= ('A' - 10);
	else if (c >= 'a' && c <= 'z')
		c -= ('a' - 10);
	else
		c = 0;

	return (uint8_t)c;
}

void parsePLCPhyAddress(char *asciiSrc, uint8_t *binDest)
{
	for (int i = 0; i < 8; i++)
	{
		binDest[i] = (uint8_t)(getUint8FromHexChar(*asciiSrc) << 4) + (getUint8FromHexChar(*(asciiSrc + 1)));
		asciiSrc += 2;
	}
}

static void startBrokerMode(bool isBoot)
{
	devType = BROKER;
	if (isBoot)
		setBrokerTbTokenFromFile();
	sdk_wifi_set_opmode(STATION_MODE);
	sdk_wifi_station_connect();
	xTaskNotifyGive(xMqttTask);
	printFileContent();
	printf("Starting broker mode\n");
}

static void startClientMode(bool isBoot)
{
	devType = CLIENT;

	printFileContent();
	if (isBoot)
		setClientPlcPhyAddrOfBrokerAndTbTokenFromFile();

	sdk_wifi_set_opmode(STATION_MODE);
	sdk_wifi_station_disconnect();
}

static inline void setMyTbToken(char *tbToken)
{
	memcpy((char *)myTbToken, tbToken, 20);
}

static void setStationAPMode()
{
	sdk_wifi_set_opmode(STATIONAP_MODE);
	struct ip_info ap_ip;
	IP4_ADDR(&ap_ip.ip, 192, 168, 1, 1);
	IP4_ADDR(&ap_ip.gw, 0, 0, 0, 0);
	IP4_ADDR(&ap_ip.netmask, 255, 255, 255, 0);
	sdk_wifi_set_ip_info(1, &ap_ip);

	struct sdk_softap_config ap_config = {
		.ssid = WIFI_SSID,
		.ssid_len = strlen(WIFI_SSID),
		.ssid_hidden = 0,
		.channel = 3,
		.authmode = AUTH_WPA_WPA2_PSK,
		.password = WIFI_PASS,
		.max_connection = 3,
		.beacon_interval = 100,
	};

	char macAddr[6], macAddrStr[20];
	sdk_wifi_get_macaddr(STATION_IF, (uint8_t *)macAddr);
	snprintf(macAddrStr, sizeof(macAddrStr), "%02x%02x%02x%02x%02x%02x", MAC2STR(macAddr));
	memcpy(ap_config.ssid + ap_config.ssid_len, macAddrStr, 12);
	ap_config.ssid_len += 12;
	ap_config.ssid[ap_config.ssid_len] = '\0';

	printf("Starting AP with SSID: %s\n", ap_config.ssid);
	sdk_wifi_softap_set_config(&ap_config);

	ip_addr_t first_client_ip;
	IP4_ADDR(&first_client_ip, 192, 168, 1, 2);
	dhcpserver_start(&first_client_ip, 3);
}

static void connectToStation(char *SSID, char *password, int SSIDLen, int passwordLen)
{
	struct sdk_station_config config;
	fillStationConfig(&config, SSID, password, SSIDLen, passwordLen); 

	sdk_wifi_station_set_config(&config);
	sdk_wifi_station_connect();
}

static inline void fillJsonConnectionSuccessStringWithPlcPhyAddr()
{
	/* Offset is set at 18 position from end of string because it should omit
	 * one '}', one '"' and sixteen '-' characters.
	 */
	char plcPhyAddrStr[20];
	snprintf((char *)plcPhyAddrStr, sizeof(plcPhyAddrStr), "%02X%02X%02X%02X%02X%02X%02X%02X",
			 PLCPHY2STR(plcPhyAddr));
	memcpy((uint8_t *)(wifiConnectionSuccessJson + wifiConnectionSuccessJsonLen - 18),
		   plcPhyAddrStr, 16);
}

void fillStationConfig(struct sdk_station_config *config, char *ssid, char *password, 
	uint8_t ssidLen, uint8_t passwordLen)
{
	memcpy(config->ssid, ssid, ssidLen);
	memcpy(config->password, password, passwordLen);

	config->ssid[ssidLen] = '\0';
	config->password[passwordLen] = '\0';
}
