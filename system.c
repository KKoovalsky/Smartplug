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

QueueHandle_t xConnectWhileConfigQueue;
TaskHandle_t xConnectWhileConfigTask;

volatile int devType;
volatile char myTbToken[20];

// TODO: Check if Thingsboard token is correct
// TODO: Get rid of small mallocs.

static void startBrokerMode();
static void startClientMode();

static void initPlcTimerClbk(TimerHandle_t xTimer);
static void initBrokersPlcPhyAddrFromDev(TimerHandle_t xTimer);

static inline void setMyTbToken(char *tbToken);
static inline void setPlcPhyAddrFromDev();
static void setAP_STA();
static void connectToStation(char *SSID, char *password, int SSIDLen, int passwordLen);

void connectWhileConfigTask(void *pvParameters)
{
	if (initFileSystem() < 0)
		vTaskDelete(NULL);

	char buffer[8];
	if (getDeviceModeFromFile(buffer) < 0)
		vTaskDelete(NULL);

	if (!strncmp(buffer, clientStr, sizeof(clientStr) - 1))
	{
		checkFileContent();
		setClientPlcPhyAddrOfBrokerAndTbToken();
		startClientMode();
		vTaskDelete(NULL);
	}
	else if (!strncmp(buffer, brokerStr, sizeof(brokerStr) - 1))
	{
		checkFileContent();
		setBrokerTbTokenFromFile();
		startBrokerMode();
		sdk_wifi_station_connect();

		TimerHandle_t plcPhyAddrInitTimer = xTimerCreate("PLC init", pdMS_TO_TICKS(2000), pdFALSE, 0, 
			initBrokersPlcPhyAddrFromDev);
		xTimerStart(plcPhyAddrInitTimer, 0);

		vTaskDelete(NULL);
	}
	else // If its first run of this device, then start HTTP server to get configuration
	{
		printf("First run of the device\n");
		setAP_STA();
		xTaskCreate(httpd_task, "HTTP Daemon", 128, NULL, 2, &xHTTPServerTask);
	}

	for (;;)
	{
		PermConfData_s configData;
		xQueueReceive(xConnectWhileConfigQueue, &configData, portMAX_DELAY);

		if (configData.mode == SPIFFS_WRITE_WIFI_CONF)
		{
			connectToStation(configData.SSID, configData.password, configData.SSIDLen, configData.passwordLen);
			bool abort = false;
			int retries = MAX_RETRIES;
			unsigned int status;
			while (1)
			{
				vTaskDelay(pdMS_TO_TICKS(3000));
				status = (unsigned int)sdk_wifi_station_get_connect_status();
				if (status == STATION_WRONG_PASSWORD)
				{
					printf("WiFi: wrong password\n\r");
					abort = true;
					break;
				}
				else if (status == STATION_NO_AP_FOUND)
				{
					printf("WiFi: AP not found\n\r");
					abort = true;
					break;
				}
				else if (status == STATION_CONNECT_FAIL)
				{
					printf("WiFi: connection failed\r\n");
					abort = true;
					break;
				}
				else if (status == STATION_CONNECTING)
				{
					printf("WiFi: connecting...\r\n");
					if (!retries)
					{
						printf("WiFi: could not connect to device.\r\n");
						abort = true;
						break;
					}
					--retries;
					continue;
				}
				else if (status == STATION_IDLE)
				{
					printf("WiFi: idle mode\r\n");
					abort = true;
					break;
				}
				else
				{
					setPlcPhyAddrFromDev();
					setMyTbToken(configData.tbToken);
					/* Add PLC phy address to enable client connection to broker in the future.
					 * Offset is set at 18 position from end of string because it should omit
					 * one '}', one '"' and sixteen '-' characters. 
					 */
					char plcPhyAddrStr[20];
					snprintf((char *)plcPhyAddrStr, sizeof(plcPhyAddrStr), "%02x%02x%02x%02x%02x%02x%02x%02x",
							 PLCPHY2STR(plcPhyAddr));
					memcpy((uint8_t *)(wifiConnectionSuccessJson + wifiConnectionSuccessJsonLen - 18),
						   plcPhyAddrStr, 16);

					xWSGetAckTaskHandle = xConnectWhileConfigTask;

					sendWsResponse(wifiConnectionSuccessJson, wifiConnectionSuccessJsonLen);

					printf("WiFi: succesfully connected to AP\r\n");

					saveBrokerConfigDataToFile(&configData);

					ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10000));

					startBrokerMode();

					// Free memory and delete this task
					vPortFree(configData.SSID);
					vPortFree(configData.password);
					vPortFree(configData.tbToken);
					vTaskDelete(NULL);
				}
			}

			if (abort)
			{
				sdk_wifi_station_disconnect();
				vPortFree(configData.SSID);
				vPortFree(configData.password);
				vPortFree(configData.tbToken);
			}

			sendWsResponse(wifiJsonStrings[status], wifiJsonStringsLen[status]);
		}
		else if (configData.mode == SPIFFS_WRITE_PLC_CONF)
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
			parsePLCPhyAddress((char *) configData.PLCPhyAddr, (char *) destPhyAddr);

			if (registerClient((char *) destPhyAddr, configData.tbToken) >= 0)
			{
				printf("Client registration successful.");
				memcpy((char *)plcPhyAddr, destPhyAddr, 8);
				setMyTbToken(configData.tbToken);

				xWSGetAckTaskHandle = xConnectWhileConfigTask;

				sendWsResponse(plcJsonRegisSuccessStr, plcJsonRegisSuccessStrLen);

				saveClientConfigDataToFile(&configData);

				ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10000));

				startClientMode(0);

				vPortFree(configData.PLCPhyAddr);
				vPortFree(configData.tbToken);
				vTaskDelete(NULL);
			}
			else
			{
				printf("Client registration unsuccessful.");
				vPortFree(configData.PLCPhyAddr);
				vPortFree(configData.tbToken);
				sendWsResponse(plcJsonRegisUnsuccessStr, plcJsonRegisUnsuccessStrLen);
			}
		}
		else
			printf("Unknown spiffs mode\n");
	}
}

void parsePLCPhyAddress(char *asciiSrc, char *binDest)
{
	for (int i = 0; i < 8; i++)
	{
		sscanf(asciiSrc, "%2hhx", &binDest[i]);
		asciiSrc += 2;
	}
}

static void startBrokerMode()
{
	devType = BROKER;

	printf("Starting broker mode\n");

	sdk_wifi_set_opmode(STATION_MODE);

	TimerHandle_t plcInitTimer = xTimerCreate("PLC init", pdMS_TO_TICKS(2000), pdFALSE, 0, initPlcTimerClbk);
	xTimerStart(plcInitTimer, 0);

	xTaskNotifyGive(xMqttTask);
}

void startClientMode()
{
	devType = CLIENT;

	sdk_wifi_set_opmode(STATION_MODE);
	sdk_wifi_station_disconnect();

	TimerHandle_t plcInitTimer = xTimerCreate("PLC init", pdMS_TO_TICKS(2000), pdFALSE, 0, initPlcTimerClbk);
	xTimerStart(plcInitTimer, 0);
}

static inline void setPlcPhyAddrFromDev()
{
	readPLCregisters(PHY_ADDR, (uint8_t *)plcPhyAddr, 8);
}

static void initPlcTimerClbk(TimerHandle_t xTimer)
{
	printf("Initializing PLC.\n\r");
	initPLCdevice(0);
	xTimerDelete(xTimer, 0);
}

static void initBrokersPlcPhyAddrFromDev(TimerHandle_t xTimer)
{
	printf("Getting PLC Phy address from CY8CPLC10\n\r");
	setPlcPhyAddrFromDev();
	xTimerDelete(xTimer, 0);
}

static inline void setMyTbToken(char *tbToken)
{
	memcpy((char *)myTbToken, tbToken, 20);
}

static void setAP_STA()
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

	memcpy(config.ssid, SSID, SSIDLen);
	memcpy(config.password, password, passwordLen);

	config.ssid[SSIDLen] = '\0';
	config.password[passwordLen] = '\0';

	sdk_wifi_station_set_config(&config);
	sdk_wifi_station_connect();
}

