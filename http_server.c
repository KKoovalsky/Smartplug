#include <espressif/esp_common.h>
#include <esp8266.h>
#include <esp/uart.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <FreeRTOS.h>
#include <task.h>
#include "queue.h"
#include <httpd/httpd.h>
#include "http_server.h"
#include "jsmn.h"
#include "plc.h"
#include "spiffs_local.h"

typedef enum {
	NONE,
	SET_CONFIG,
	PLC_FUNCTION
} WebsocketClbkUse_e;

TaskHandle_t xHTTPServerTask;
TaskHandle_t xWSGetAckTaskHandle = NULL;

volatile WebsocketClbkUse_e websocketClbkUse = NONE;
volatile struct tcp_pcb *wsPCB;

const uint8_t wifiConnectionFailedJson[] = "{\"data\":\"enableButtons\",\"msg\":\"Could not connect.\"}";
const uint8_t wifiWrongPasswordJson[] = "{\"data\":\"enableButtons\",\"msg\":\"Wrong wifi password\"}";
const uint8_t wifiNoAPFoundJson[] = "{\"data\":\"enableButtons\",\"msg\":\"No AP found.\"}";

uint8_t wifiConnectionSuccessJson[] =
	"{\"data\":\"stopWs\",\"msg\":\"Connection successful. Closing access point. \
Save the PLC Phy Address: ----------------\"}";

uint8_t wifiConnectionSuccessJsonLen = sizeof(wifiConnectionSuccessJson) - 1;

// Messages correspond to enumerated type in esp_sta.h (STATION_IDLE, STATION_CONNECTING...)/
const uint8_t *wifiJsonStrings[] = {
	wifiConnectionFailedJson,
	wifiConnectionFailedJson,
	wifiWrongPasswordJson,
	wifiNoAPFoundJson,
	wifiConnectionFailedJson};

// Length of above strings (null character -> so '-1'')
const uint8_t wifiJsonStringsLen[] = {
	sizeof(wifiConnectionFailedJson) - 1,
	sizeof(wifiConnectionFailedJson) - 1,
	sizeof(wifiWrongPasswordJson) - 1,
	sizeof(wifiNoAPFoundJson) - 1,
	sizeof(wifiConnectionFailedJson) - 1};

const uint8_t plcJsonRegisSuccessStr[] =
	"{\"data\":\"stopWs\",\"msg\":\"Succesfully registered client. Closing access point.\"}";
const uint8_t plcJsonRegisUnsuccessStr[] =
	"{\"data\":\"enableButtons\",\"msg\":\"Client registration error. Please, check PLC Phy address.\"}";
const uint8_t plcJsonTooShortPhyAddrStr[] =
	"{\"data\":\"enableButtons\",\"msg\":\"Client registration error. Too short PLC Phy address\"}";

const uint8_t plcJsonRegisSuccessStrLen = sizeof(plcJsonRegisSuccessStr) - 1;
const uint8_t plcJsonRegisUnsuccessStrLen = sizeof(plcJsonRegisUnsuccessStr) - 1;
const uint8_t plcJsonTooShortPhyAddrStrLen = sizeof(plcJsonTooShortPhyAddrStr) - 1;

const char *plcFunctionNames[] =
	{
		"readPLCregister",
		"readPLCregisters",
		"writePLCregister",
		"writePLCregisters",
		"setPLCtxAddrType",
		"setPLCtxDA",
		"setPLCnodeLA",
		"setPLCnodeGA",
		"getPLCrxAddrType",
		"getPLCrxSA",
		"readPLCrxPacket",
		"readPLCintRegister",
		"initPLCdevice"};

static void JSONAddInt(char *buf, int *len, uint32_t val)
{
	sprintf(buf + *len, "\"%d\"", val);
	*len = strlen(buf);
}

static void JSONAddString(char *buf, int *len, char *str)
{
	sprintf(buf + *len, "%s", str);
	*len = strlen(buf);
}

static void JSONAddByteArray(char *buf, int *len, uint8_t *array, int num)
{
	if (!num)
		return;
	int length = *len;
	buf[length++] = '[';
	printf("%.*s\n", length, buf);
	for (int i = 0; i < num; i++)
	{
		sprintf(buf + length, "\"%d\",", (uint32_t)array[i] & 0x000000FF);
		length = strlen(buf);
	}
	// Last comma should be overwritten.
	buf[length - 1] = ']';
	*len = length;
}

static uint32_t getRegFromInput(char *start, char *end)
{
	uint32_t temp;
	char *e;
	temp = strtol(start, &e, 10);
	if (e != end)
	{
		printf("Invalid argument in getRegFromInput\n");
		return -1;
	}
	return temp;
}

void setConfig(char *data, u16_t len, struct tcp_pcb *pcb)
{
	char txBuffer[32];
	int txBufferLen = 0;

	jsmn_parser jsmnParser;
	jsmntok_t t[8];
	jsmn_init(&jsmnParser);
	int r = jsmn_parse(&jsmnParser, data, len, t, sizeof(t) / sizeof(t[0]));

	if (r < 0)
	{
		printf("JSON Parsing failed\n");
		return;
	}

	JSONAddString(txBuffer, &txBufferLen, "{\"data\":");

	char *configStr = data + t[1].start;
	int configStrLen = t[1].end - t[1].start;

	PermConfData_s configData;

	printf("%.*s\n", configStrLen, configStr);

	if (!strncmp(configStr, "ssid", configStrLen))
	{
		char *SSID = data + t[2].start;
		int SSIDStrLen = t[2].end - t[2].start;

		char *password = data + t[4].start;
		int passwordLen = t[4].end - t[4].start;

		char *tbToken = data + t[6].start;
		int tbTokenLen = t[6].end - t[6].start;

		configData.SSID = (char *)pvPortMalloc(sizeof(char) * (SSIDStrLen + 1));
		configData.password = (char *)pvPortMalloc(sizeof(char) * (passwordLen + 1));
		configData.tbToken = (char *)pvPortMalloc(sizeof(char) * (tbTokenLen + 1));

		memcpy(configData.SSID, SSID, SSIDStrLen);
		memcpy(configData.password, password, passwordLen);
		memcpy(configData.tbToken, tbToken, tbTokenLen);

		configData.SSID[SSIDStrLen] = configData.password[passwordLen] = configData.tbToken[tbTokenLen] = '\0';

		configData.SSIDLen = (uint8_t)SSIDStrLen;
		configData.passwordLen = (uint8_t)passwordLen;
		configData.tbTokenLen = (uint8_t)tbTokenLen;

		printf("%s %s %s\n", configData.SSID, configData.password, configData.tbToken);

		configData.mode = WRITE_WIFI_CONF;

		// Let other task handle this data (this handler should be left asap - it is said by http server documentation)
		xQueueSend(xInitializerQueue, &configData, 0);
	}
	else if (!strncmp(configStr, "phyaddr", configStrLen))
	{
		char *phyAddr = data + t[2].start;
		int phyAddrLen = t[2].end - t[2].start;

		char *tbToken = data + t[4].start;
		int tbTokenLen = t[4].end - t[4].start;

		configData.PLCPhyAddr = (char *)pvPortMalloc(sizeof(char) * (phyAddrLen + 1));
		configData.tbToken = (char *)pvPortMalloc(sizeof(char) * (tbTokenLen + 1));

		memcpy(configData.PLCPhyAddr, phyAddr, phyAddrLen);
		memcpy(configData.tbToken, tbToken, tbTokenLen);

		configData.PLCPhyAddr[phyAddrLen] = configData.tbToken[tbTokenLen] = '\0';

		printf("%s %s\n", configData.PLCPhyAddr, configData.tbToken);

		configData.PLCPhyAddrLen = (uint8_t)phyAddrLen;
		configData.tbTokenLen = (uint8_t)tbTokenLen;

		configData.mode = WRITE_PLC_CONF;
		xQueueSend(xInitializerQueue, &configData, 0);
	}
	else
	{
		printf("Undefined config command\n");
	}
}

void plcFunction(char *data, u16_t len, struct tcp_pcb *pcb)
{
	char txBuffer[64];
	int txBufferLen = 0;
	jsmn_parser jsmnParser;
	jsmntok_t t[16];
	jsmn_init(&jsmnParser);
	int r = jsmn_parse(&jsmnParser, data, len, t, sizeof(t) / sizeof(t[0]));

	if (r < 0)
	{
		printf("JSON Parsing failed.\n");
		return;
	}

	JSONAddString(txBuffer, &txBufferLen, "{\"data\":");

	char *functionName = data + t[2].start;
	char functionNameLen = t[2].end - t[2].start;

	printf("%.*s\n", functionNameLen, functionName);

	if (!strncmp("readPLCregister", functionName, functionNameLen))
	{
		uint32_t reg = getRegFromInput(data + t[5].start, data + t[5].end);
		if (reg == -1)
			return;
		printf("%d\n", reg);
		uint32_t res = (uint32_t)readPLCregister((uint8_t)reg);
		JSONAddInt(txBuffer, &txBufferLen, res);
	}
	else if (!strncmp("readPLCregisters", functionName, functionNameLen))
	{
		uint32_t reg = getRegFromInput(data + t[5].start, data + t[5].end);
		if (reg == -1)
			return;
		uint32_t num = getRegFromInput(data + t[6].start, data + t[6].end);
		if (num == -1)
			return;
		printf("%d %d\n", reg, num);
		uint8_t buffer[32];
		readPLCregisters(reg, buffer, num);
		JSONAddByteArray(txBuffer, &txBufferLen, buffer, num);
	}
	else if (!strncmp("writePLCregister", functionName, functionNameLen))
	{
		uint32_t reg = getRegFromInput(data + t[5].start, data + t[5].end);
		if (reg == -1)
			return;
		uint32_t val = getRegFromInput(data + t[6].start, data + t[6].end);
		if (val == -1)
			return;
		printf("%d %d\n", reg, val);
		writePLCregister((uint8_t)reg, (uint8_t)val);
		return;
	}
	else if (!strncmp("writePLCregisters", functionName, functionNameLen))
	{
		uint8_t buffer[33];
		uint32_t reg = getRegFromInput(data + t[5].start, data + t[5].end);
		if (reg == -1)
			return;
		buffer[0] = (uint8_t)reg;
		int j = 1;
		for (int i = 6; t[i].type == JSMN_STRING; i++)
		{
			uint32_t temp = getRegFromInput(data + t[i].start, data + t[i].end);
			if (temp == -1)
				return;
			buffer[j++] = (uint8_t)temp;
		}
		for (int i = 0; i < j; i++)
			printf("%d ", buffer[i]);
		printf("\n");
		writePLCregisters(buffer, (uint8_t)j);
		return;
	}
	else if (!strncmp("setPLCtxAddrType", functionName, functionNameLen))
	{
		uint32_t reg = getRegFromInput(data + t[5].start, data + t[5].end);
		if (reg == -1)
			return;
		uint32_t val = getRegFromInput(data + t[6].start, data + t[6].end);
		if (val == -1)
			return;
		printf("%d %d\n", reg, val);
		setPLCtxAddrType((uint8_t)reg, (uint8_t)val);
		return;
	}
	else if (!strncmp("setPLCtxDA", functionName, functionNameLen))
	{
		uint8_t buffer[32];
		uint32_t reg = getRegFromInput(data + t[5].start, data + t[5].end);
		if (reg == -1)
			return;
		int j = 0;
		for (int i = 6; t[i].type == JSMN_STRING; i++)
		{
			uint32_t temp = getRegFromInput(data + t[i].start, data + t[i].end);
			if (temp == -1)
				return;
			buffer[j++] = (uint8_t)temp;
		}
		for (int i = 0; i < j; i++)
			printf("%d ", buffer[i]);
		printf("\n");
		setPLCtxDA((uint8_t)reg, buffer);
		return;
	}
	else if (!strncmp("setPLCnodeLA", functionName, functionNameLen))
	{
		uint32_t reg = getRegFromInput(data + t[5].start, data + t[5].end);
		if (reg == -1)
			return;
		printf("%d\n", reg);
		setPLCnodeLA((uint8_t)reg);
		return;
	}
	else if (!strncmp("setPLCnodeGA", functionName, functionNameLen))
	{
		uint32_t reg = getRegFromInput(data + t[5].start, data + t[5].end);
		if (reg == -1)
			return;
		printf("%d\n", reg);
		setPLCnodeGA((uint8_t)reg);
		return;
	}
	else if (!strncmp("getPLCrxAddrType", functionName, functionNameLen))
	{
		uint8_t buffer[2];
		getPLCrxAddrType(&buffer[0], &buffer[1]);
		JSONAddByteArray(txBuffer, &txBufferLen, buffer, 2);
	}
	else if (!strncmp("getPLCrxSA", functionName, functionNameLen))
	{
		uint8_t buffer[8];
		getPLCrxSA(buffer);
		JSONAddByteArray(txBuffer, &txBufferLen, buffer, 8);
	}
	else if (!strncmp("readPLCrxPacket", functionName, functionNameLen))
	{
		uint8_t buffer[36];
		readPLCrxPacket(&buffer[0], &buffer[2], &buffer[1]);
		JSONAddByteArray(txBuffer, &txBufferLen, buffer, buffer[1]);
	}
	else if (!strncmp("readPLCintRegister", functionName, functionNameLen))
	{
		uint32_t intReg = (uint32_t)readPLCintRegister();
		JSONAddInt(txBuffer, &txBufferLen, intReg);
	}
	else if (!strncmp("initPLCdevice", functionName, functionNameLen))
	{
		uint32_t reg = getRegFromInput(data + t[5].start, data + t[5].end);
		if (reg == -1)
			return;
		printf("%d\n", reg);
		initPLCdevice((uint8_t)reg);
		return;
	}
	else
	{
		printf("Wrong function name\n");
	}

	JSONAddString(txBuffer, &txBufferLen, "}");
	websocket_write(pcb, (uint8_t *)txBuffer, txBufferLen, WS_TEXT_MODE);
	printf("%s\n", txBuffer);
}

char *index_cgi_handler(int iIndex, int iNumParams, char *pcParam[], char *pcValue[])
{
	return "/index.html";
}

char *plc_cgi_handler(int iIndex, int iNumParams, char *pcParam[], char *pcValue[])
{
	return "/plc.html";
}
/**
 * This function is called when websocket frame is received.
 *
 * Note: this function is executed on TCP thread and should return as soon
 * as possible.
 */
void websocket_cb(struct tcp_pcb *pcb, uint8_t *data, u16_t data_len, uint8_t mode)
{
	printf("[websocket_callback]:\n%.*s\n", (int)data_len, (char *)data);

	if (!strncmp((char *)data, "ACK", 3))
	{
		if (xWSGetAckTaskHandle)
		{
			xTaskNotifyGive(xWSGetAckTaskHandle);
			xWSGetAckTaskHandle = NULL;
		}
		return;
	}

	if (websocketClbkUse == SET_CONFIG)
	{
		setConfig((char *)data, data_len, pcb);
	}
	else if (websocketClbkUse == PLC_FUNCTION)
	{
		plcFunction((char *)data, data_len, pcb);
	}
}

/**
 * This function is called when new websocket is open.
 */
void websocket_open_cb(struct tcp_pcb *pcb, const char *uri)
{
	printf("WS URI: %s\n", uri);

	if (!strcmp("/set-config", uri))
	{
		websocketClbkUse = SET_CONFIG;
		printf("Set config\n");
		wsPCB = pcb;
	}
	else if (!strcmp("/plc-function", uri))
	{
		websocketClbkUse = PLC_FUNCTION;
		printf("PLC function\n");
		wsPCB = pcb;
	}
	else
	{
		websocketClbkUse = NONE;
		printf("Uri not found\n\r");
	}
}

void httpd_task(void *pvParameters)
{
	tCGI pCGIs[] = {
		{"/index", (tCGIHandler)index_cgi_handler},
		{"/plc", (tCGIHandler)plc_cgi_handler}};

	/* register handlers and start the server */
	http_set_cgi_handlers(pCGIs, sizeof(pCGIs) / sizeof(pCGIs[0]));
	websocket_register_callbacks((tWsOpenHandler)websocket_open_cb, (tWsHandler)websocket_cb);
	httpd_init();

	for (;;)
		;
}

void sendWsResponse(const uint8_t *msg, int len)
{
	err_t result = websocket_write((struct tcp_pcb *)wsPCB, msg, len, WS_TEXT_MODE);
	printf("Sending WS response result: %d\n\r of data %s\n\r with len %d\n\r", result, msg, len);
}
