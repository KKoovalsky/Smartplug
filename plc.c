#include "plc.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "i2c.h"
#include "espressif/esp_common.h"
#include "esp8266.h"

uint8_t readPLCregister(uint8_t reg)
{
	uint8_t buf;
	uint32_t attemptCnt = MAX_I2C_ATTEMPTS;
	while(attemptCnt)
	{
		if(i2c_slave_read(PLC_WRITE_ADDR, reg, &buf, 1))
			break;
		attemptCnt--;
	}
	return buf;
}

void readPLCregisters(uint8_t reg, uint8_t *buf, uint32_t len)
{
	uint32_t attemptCnt = MAX_I2C_ATTEMPTS;
	while(attemptCnt)
	{
		if(i2c_slave_read(PLC_WRITE_ADDR, reg, buf, len))
			break;
		attemptCnt--;
	}
}

void writePLCregister(uint8_t reg, uint8_t val)
{
	uint8_t data[2];
	data[0] = reg; data[1] = val;
	uint32_t attemptCnt = MAX_I2C_ATTEMPTS;
	while(attemptCnt)
	{
		if(i2c_slave_write(PLC_WRITE_ADDR, data, 2))
			break;
		attemptCnt--;
	}
} 

void writePLCregisters(uint8_t reg, uint8_t *buf, uint8_t len)
{
	uint8_t regAddr = reg;
	uint32_t attemptCnt = MAX_I2C_ATTEMPTS;
	while(attemptCnt)
	{
		if(i2c_slave_write(PLC_WRITE_ADDR, &regAddr, 1))
			if(i2c_slave_write(PLC_WRITE_ADDR, buf, len))
				break;
		attemptCnt--;
	}
}

void setPLCtxAddrType(uint8_t txSAtype, uint8_t txDAtype)
{
    uint8_t configRegValue;
    //  Odczytanie bieżącej wartości rejestru TX_CONFIG i maskowanie bitów
    //  odpowiedzialnych za typy adresów SA i DA
    configRegValue = readPLCregister(TX_CONFIG_REG) & ~(TX_ADDR_MASK);
    //  Ustawienie nowych typów adresów będących argumentami funkcji – należy
    //  używać definicji typów z pliku nagłówkowego
    writePLCregister(TX_CONFIG_REG, configRegValue | txSAtype | txDAtype);
}

void setPLCtxDA(uint8_t txDAtype, volatile uint8_t *txDA)
{
    //  W zależności od typu adresu DA zapisujemy odpowiednią
    //  ilość bajtów adresu DA
    switch (txDAtype)
    {
    case TX_DA_TYPE_LOGICAL:
    case TX_DA_TYPE_GROUP:
		writePLCregister(TX_DA_REG, (uint8_t) *txDA);
		break;
    case TX_DA_TYPE_PHYSICAL:
		writePLCregisters(TX_DA_REG, (uint8_t *) txDA, 8);
		break;
    }
}

void setPLCnodeLA(uint8_t logicalAddress)
{
    writePLCregister(LOGICAL_ADDR_LSB_REG, logicalAddress);
}

void setPLCnodeGA(uint8_t groupAddress)
{
    writePLCregister(GROUP_ADDR_REG, groupAddress);
}

void getPLCrxAddrType(uint8_t *rxSAtype, uint8_t *rxDAtype)
{
    register uint8_t messageInfo;
    //  Odczytanie bieżącej wartości rejestru RX_MESSAGE_INFO_REG
    //  i ustalenie typu adresu DA i SA otrzymanej wiadomości
    messageInfo = readPLCregister(RX_MESSAGE_INFO_REG);
    //  Zwraca typ DA odebranej wiadomości: RX_DA_TYPE_LOGICAL_PHYSICAL
    //  lub RX_DA_TYPE_GROUP

    //  Typ zapisany w bicie 6
    //  fZwraca typ SA odebranej wiadomości: RX_SA_TYPE_LOGICAL lub
    //  RX_SA_TYPE_PHYSICAL
    *rxDAtype = messageInfo & 0b01000000;
    //  Typ zapisany w bicie 5
    *rxSAtype = messageInfo & 0b00100000;
}

void getPLCrxSA(uint8_t *rxSA)
{
    //  Niezależnie od rodzaju SA otrzymanej wiadomości odczytujemy
    //  zawsze 8 bajtów. Jeśli adres urządzenia, które przesłało nam dane
    //  jest typu LA to istotny jest tylko pierwszy bajt odczytanego adresu,
    //  w przypadku PA, ważne jest całe 8 bajtów.
    readPLCregisters(RX_SA_REG, rxSA, 8);
}

void readPLCrxPacket(uint8_t *rxCommand, uint8_t *rxData, uint8_t *rxDataLength)
{
    register uint8_t infoRegister;
    //	Odczytanie bieżącej wartości rejestru RX_MESSAGE_INFO_REG i ustalenie rozmiaru otrzymanej wiadomości
    *rxDataLength = readPLCregister(RX_MESSAGE_INFO_REG) & 0x1F; // 0...31
    *rxCommand = readPLCregister(RX_COMMAND_ID_REG);
 	   //	Wczytanie do tablicy rxData będącej argumentem funkcji wszystkich otrzymanych danych
    readPLCregisters(RX_DATA_REG, rxData, *rxDataLength);
    //	Aby skasować flagi STATUS_VALUE_CHANGE, STATUS_RX_PACKET_DROPPED
    //	i STATUS_RX_DATA_AVAIBLE w rejestrze INTERRUPT_STATUS_REG musimy
    //	wyzerować flagę NEW_PACKET_RECEIVED w rejestrze RX_MESSAGE_INFO_REG
    //	(datasheet)
    infoRegister = readPLCregister(RX_MESSAGE_INFO_REG);
    writePLCregister(RX_MESSAGE_INFO_REG, infoRegister & ~NEW_PACKET_RECEIVED);
}

void initPLCdevice(uint8_t nodeLA)
{
    //	Uruchomienie i podstawowa konfiguracja modemu PLC
    writePLCregister(PLC_MODE_REG, TX_ENABLE | RX_ENABLE | RX_OVERRIDE | ENABLE_BIU | CHECK_DA | VERIFY_PACKET_CRC8);
    //	Ustawienie poziomu sygnału dla mechanizmu CSMA (Carrier Sense Multimaster Access)
    // 	zapewniającego wielodostęp do medium transmisyjnego
    writePLCregister(THRESHOLD_NOISE_REG, BIU_TRESHOLD_87DBUV);
    //	Konfiguracja parametrów transmisji
    writePLCregister(MODEM_CONFIG_REG, MODEM_BPS_2400 | MODEM_FSK_BAND_DEV_3KHZ);
    //	Uruchomienie przerwań dla wybranych zdarzeń (aktywny poziom niski na wyprowadzeniu HOST_INT)
    writePLCregister(INTERRUPT_ENABLE_REG, INT_POLARITY_LOW | INT_UNABLE_TO_TX |
		INT_TX_NO_ACK | INT_TX_NO_RESP | INT_RX_DATA_AVAILABLE | INT_TX_DATA_SENT);
    //	Ustawienie trybu potwierdzania pakietów danych oraz liczby prób
    //	transmisji = 5 (domyślne logiczne typy adresów SA i DA)
    writePLCregister(TX_CONFIG_REG, TX_SERVICE_ACKNOWLEDGED | 0x05);
    //	Ustawienie wzmocnienia dla modułu nadajnika PLC
    writePLCregister(TX_GAIN_REG, TX_GAIN_LEVEL_1550MV);
    //	Ustawienie czułości dla modułu odbiornika PLC
    writePLCregister(RX_GAIN_REG, RX_GAIN_LEVEL_250UV);
    setPLCnodeLA(nodeLA); //	Ustawienie numeru LA modemu PLC
	//	Ustawienie adresu Grupowego modemu PLC
    setPLCnodeGA(MASTER_GROUP_ADDR);

	// TODO: Set configuration of pin interrupt 
}
