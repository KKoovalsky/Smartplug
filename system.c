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
#include "sntp_sync.h"
#include "client.h"
#include "parsers.h"

QueueHandle_t xConfiguratorQueue;
TaskHandle_t xConfiguratorTask;

volatile int devType;

// TODO: Check if Thingsboard token is correct
// TODO: Clean up function placement.

static void initCommonOpts();
static void startBrokerMode();
static void startClientMode();
static void setStationAPMode();
static void stationAndSntpStartup(void *pvParameters);
static void setBrokerPlcPhyAddressTask(void *pvParameters);
static void connectToStation(char *ssid, char *password, int ssidLen, int passwordLen);
static inline void fillJsonConnectionSuccessStringWithPlcPhyAddr(char *plcPhyAddrStr);

// TODO: Add makefile define NULL = THIS (when task self deletion use vTaskDelete(THIS) instead of vTaskDelete(NULL))
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
		xConfiguratorQueue = xQueueCreate(1, sizeof(PermConfData_s));
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
		if (configData.mode == BROKER_CONF)
		{
			connectToStation(configData.ssid, configData.password, configData.ssidLen, configData.passwordLen);
			int retries = MAX_RETRIES;
			unsigned int status = (unsigned int)sdk_wifi_station_get_connect_status();

			while (status == STATION_CONNECTING && retries)
			{
				printf("WiFi: connecting...\r\n");
				vTaskDelay(pdMS_TO_TICKS(3000));
				status = (unsigned int)sdk_wifi_station_get_connect_status();
				retries--;
			}

			if (status == STATION_GOT_IP)
			{
				devType = BROKER;
				uint8_t rawPlcPhyAddr[8];
				setPlcPhyAddrFromPLCChip(rawPlcPhyAddr);
				convertPlcPhyAddressToString(configData.plcPhyAddr, rawPlcPhyAddr);
				fillJsonConnectionSuccessStringWithPlcPhyAddr(configData.plcPhyAddr);
				sendWsResponseAndWaitForAck(wifiConnectionSuccessJson, wifiConnectionSuccessJsonLen);
				setTbToken(configData.tbToken);
				sdk_wifi_set_opmode(STATION_MODE);
				sdk_wifi_station_connect();
				sntpInit();
				addClient(createClient(rawPlcPhyAddr, configData.deviceName, configData.deviceNameLen));
				saveConfigDataToFile(&configData);
				xMqttQueue = xQueueCreate(8, sizeof(TelemetryData));
				xTaskCreate(mqttTask, "MQTT", 1536, NULL, 2, NULL);
				vQueueDelete(xConfiguratorQueue);
				vTaskDelete(NULL);
			}

			sdk_wifi_station_disconnect();
			sendWsResponse(wifiJsonStrings[status], wifiJsonStringsLen[status]);
		}
		else if (configData.mode == CLIENT_CONF)
		{
			if (registerClient(&configData) >= 0)
			{
				devType = CLIENT;
				addClient(createClientFromAscii(configData.plcPhyAddr, configData.deviceName, 
					configData.deviceNameLen));
				sendWsResponseAndWaitForAck(plcJsonRegisSuccessStr, plcJsonRegisSuccessStrLen);
				setTbToken(configData.tbToken);
				sdk_wifi_set_opmode(STATION_MODE);
				connectToStation(configData.ssid, configData.password, configData.ssidLen, configData.passwordLen);
				sntpInit();
				saveConfigDataToFile(&configData);
				vQueueDelete(xConfiguratorQueue);
				vTaskDelete(NULL);
			}
			sendWsResponse(plcJsonRegisUnsuccessStr, plcJsonRegisUnsuccessStrLen);
		}
	}
}

static void stationAndSntpStartup(void *pvParameters)
{
	sdk_wifi_station_connect();
	sntpInit();
	vTaskDelete(NULL);
}

static void setBrokerPlcPhyAddressTask(void *pvParameters)
{
	vTaskDelay(pdMS_TO_TICKS(2000));
	setPLCtxDA(TX_DA_TYPE_PHYSICAL, (uint8_t *)clientListBegin->plcPhyAddr);
	vTaskDelete(NULL);
}


static void initCommonOpts()
{
	sdk_wifi_set_opmode(STATION_MODE);
	xTaskCreate(stationAndSntpStartup, "StartUp", 512, NULL, 2, NULL);

	char brokerDeviceName[33], brokerPlcPhyAddr[17];
	getCredentialsFromFile(NULL, NULL, NULL, brokerPlcPhyAddr, brokerDeviceName);
	addClient(createClientFromAscii(brokerPlcPhyAddr, brokerDeviceName, strlen(brokerDeviceName)));

	printFileContent();
}

static void startBrokerMode()
{
	devType = BROKER;
	initCommonOpts();
	retrieveClientListFromFile();
	xMqttQueue = xQueueCreate(8, sizeof(TelemetryData));
	xTaskCreate(mqttTask, "MQTT", 1536, NULL, 2, NULL);
	printf("Starting broker mode\n");
}

static void startClientMode()
{
	devType = CLIENT;
	initCommonOpts();
	xTaskCreate(setBrokerPlcPhyAddressTask, "BrokerAddr", 128, NULL, 2, NULL);
	printf("Starting client mode\n");
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

	printf("Starting AP with ssid: %s\n", ap_config.ssid);
	sdk_wifi_softap_set_config(&ap_config);

	ip_addr_t first_client_ip;
	IP4_ADDR(&first_client_ip, 192, 168, 1, 2);
	dhcpserver_start(&first_client_ip, 3);
}

static void connectToStation(char *ssid, char *password, int ssidLen, int passwordLen)
{
	struct sdk_station_config config;
	fillStationConfig(&config, ssid, password, ssidLen, passwordLen);

	sdk_wifi_station_set_config(&config);
	sdk_wifi_station_connect();
}

static inline void fillJsonConnectionSuccessStringWithPlcPhyAddr(char *plcPhyAddrStr)
{
	/* Offset is set at 18 position from end of string because it should omit
	 * one '}', one '"' and sixteen '-' characters.
	 */
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
