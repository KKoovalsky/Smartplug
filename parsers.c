#include "parsers.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "client.h"
#include "cloud.h"

uint8_t getUint8FromHexChar(char c)
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

void convertPlcPhyAddressToRaw(uint8_t *rawDest, char *asciiSrc)
{
	for (int i = 0; i < 8; i++)
	{
		rawDest[i] = (uint8_t)(getUint8FromHexChar(*asciiSrc) << 4) + (getUint8FromHexChar(*(asciiSrc + 1)));
		asciiSrc += 2;
	}
}

void convertPlcPhyAddressToString(char *asciiDst, uint8_t *rawSrc)
{
	snprintf(asciiDst, 17, "%02X%02X%02X%02X%02X%02X%02X%02X", PLCPHY2STR(rawSrc));
}

void copyString(char *dst, char *src)
{
	int strLen = strlen(src);
	memcpy(dst, src, strLen);
	dst[strLen] = '\0';
}

int composeJsonFromTelemetryData(char *buf, TelemetryData *telemetryData)
{
	char deviceName[33];
	getDeviceNameByPlcPhyAddr(deviceName, telemetryData->clientPhyAddr);
	int index = sprintf(buf, "{\"%s\":[", deviceName);
	uint8_t *data = telemetryData->data;
	uint8_t *end = data + telemetryData->len;
	int ts;
	memcpy(&ts, data, sizeof(time_t));
	data += 4;
	while (data != end)
	{
		int samplesInSeries = (int)*data >> 5;
		if(!samplesInSeries) samplesInSeries = 1;
		int tsMs = (((int)*data & 0x1F) << 8) | (*(data + 1));
		data += 2;
		for(int i = 0 ; i < samplesInSeries; i ++)
		{
			int power;
			memcpy(&power, data, sizeof(int));
			data += 4;
			index += sprintf(buf + index, "{\"ts\":%d%03d,\"values\":{\"power\":%d}},", 
							ts + tsMs/1000, tsMs % 1000, power);
			tsMs += 250;
		}
	}
	index += sprintf(buf + index - 1, "]}");
	return index;
}
