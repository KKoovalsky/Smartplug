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

void parsePLCPhyAddress(char *asciiSrc, uint8_t *binDest)
{
	for (int i = 0; i < 8; i++)
	{
		binDest[i] = (uint8_t)(getUint8FromHexChar(*asciiSrc) << 4) + (getUint8FromHexChar(*(asciiSrc + 1)));
		asciiSrc += 2;
	}
}