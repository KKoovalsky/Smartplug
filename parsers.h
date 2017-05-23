#ifndef PARSERS_H
#define PARSERS_H

#include <stdint.h>

uint8_t getUint8FromHexChar(char c);
void parsePLCPhyAddress(char *asciiSrc, uint8_t *binDest);

#endif