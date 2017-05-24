#include "parsers.h"

#include <stdio.h>
#include <stdint.h>

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