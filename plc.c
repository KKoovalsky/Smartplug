#include "plc.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "i2c.h"
#include "espressif/esp_common.h"
#include "esp8266.h"
#include "esp/uart.h"
#include "system.h"

#define DEBUG_PLC
#define HOST_INT_PIN 13

TaskHandle_t xPLCTask;

static void hostIntPinHandler(uint8_t pin);

uint8_t readPLCregister(uint8_t reg)
{
    uint8_t buf;
    uint32_t attemptCnt = MAX_I2C_ATTEMPTS;
    while (attemptCnt)
    {
        if (i2c_slave_read(PLC_WRITE_ADDR, reg, &buf, 1))
            break;
        attemptCnt--;
    }

#ifdef DEBUG_PLC
    if (!attemptCnt)
        printf("Read PLC register failed\n\r");
#endif

    return buf;
}

void readPLCregisters(uint8_t reg, uint8_t *buf, uint32_t len)
{
    uint32_t attemptCnt = MAX_I2C_ATTEMPTS;
    while (attemptCnt)
    {
        if (i2c_slave_read(PLC_WRITE_ADDR, reg, buf, len))
            break;
        attemptCnt--;
    }

#ifdef DEBUG_PLC
    if (!attemptCnt)
        printf("Read PLC registers failed\n\r");
#endif
}

void writePLCregister(uint8_t reg, uint8_t val)
{
    uint8_t data[2];
    data[0] = reg;
    data[1] = val;
    uint32_t attemptCnt = MAX_I2C_ATTEMPTS;
    while (attemptCnt)
    {
        if (i2c_slave_write(PLC_WRITE_ADDR, data, 2))
            break;
        attemptCnt--;
    }

#ifdef DEBUG_PLC
    if (!attemptCnt)
        printf("Write PLC register failed\n\r");
#endif
}

void writePLCregisters(uint8_t *buf, uint8_t len)
{
    uint32_t attemptCnt = MAX_I2C_ATTEMPTS;
    while (attemptCnt)
    {
        if (i2c_slave_write(PLC_WRITE_ADDR, buf, len))
            break;
        attemptCnt--;
    }

#ifdef DEBUG_PLC
    if (!attemptCnt)
        printf("Write PLC registers failed\n\r");
#endif
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
            writePLCregister(TX_DA_REG, (uint8_t)*txDA);
            break;
        case TX_DA_TYPE_PHYSICAL:
        {
            uint8_t buffer[9];
            buffer[0] = TX_DA_REG;
            memcpy(buffer + 1, (uint8_t *)txDA, 8);
            writePLCregisters(buffer, 9);
            break;
        }
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

uint8_t readPLCintRegister(void)
{
    register uint8_t intStatusReg, intEnableReg;
    //  Odczyt rejestru statusu przerwań PLC by ustalić rodzaj zdarzenia
    //  jakie zostało zgłoszone
    intStatusReg = readPLCregister(INTERRUPT_STATUS_REG);
    //  Po odczycie powyższego rejestru należy wyzerować bit INT_CLEAR
    //  w rejestrze INTERRUPT_ENABLE_REG (datasheet, page 7.)
    intEnableReg = readPLCregister(INTERRUPT_ENABLE_REG) & ~INT_CLEAR_PLC;
    writePLCregister(INTERRUPT_ENABLE_REG, intEnableReg);
    return intStatusReg;
}

void initPLCdevice(uint8_t nodeLA)
{
    //  Konfiguracja pinu HOST_INT
    gpio_enable(HOST_INT_PIN, GPIO_INPUT);
    gpio_set_interrupt(HOST_INT_PIN, GPIO_INTTYPE_EDGE_NEG, hostIntPinHandler);

    //	Uruchomienie i podstawowa konfiguracja modemu PLC
    writePLCregister(PLC_MODE_REG, TX_ENABLE | RX_ENABLE | RX_OVERRIDE | ENABLE_BIU | CHECK_DA | VERIFY_PACKET_CRC8);
    //	Ustawienie poziomu sygnału dla mechanizmu CSMA (Carrier Sense Multimaster Access)
    // 	zapewniającego wielodostęp do medium transmisyjnego
    writePLCregister(THRESHOLD_NOISE_REG, BIU_TRESHOLD_70DBUV);
    //	Konfiguracja parametrów transmisji
    writePLCregister(MODEM_CONFIG_REG, MODEM_BPS_2400 | MODEM_FSK_BAND_DEV_3KHZ);
    //	Uruchomienie przerwań dla wybranych zdarzeń (aktywny poziom niski na wyprowadzeniu HOST_INT)
    writePLCregister(INTERRUPT_ENABLE_REG, INT_POLARITY_LOW | INT_UNABLE_TO_TX |
                                               INT_TX_NO_ACK | INT_TX_NO_RESP | INT_RX_DATA_AVAILABLE | INT_TX_DATA_SENT);
    //	Ustawienie trybu potwierdzania pakietów danych oraz liczby prób
    //	transmisji = 5 (domyślne logiczne typy adresów SA i DA)
    writePLCregister(TX_CONFIG_REG, TX_SERVICE_ACKNOWLEDGED | 0x05);
    //	Ustawienie wzmocnienia dla modułu nadajnika PLC
    writePLCregister(TX_GAIN_REG, TX_GAIN_LEVEL_3500MV);
    //	Ustawienie czułości dla modułu odbiornika PLC
    writePLCregister(RX_GAIN_REG, RX_GAIN_LEVEL_250UV);
    setPLCnodeLA(nodeLA); //	Ustawienie numeru LA modemu PLC
                          //	Ustawienie adresu Grupowego modemu PLC
    setPLCnodeGA(MASTER_GROUP_ADDR);
}

void fillPLCTxData(uint8_t *buf, uint8_t len)
{
	if(len > 32)
	{
		// TODO: Jeżeli ten warunek jest spełniony należy podzielić wiadomość.
		printf("Internal CY8CPLC10 buffer is shorter than len\n\r");
		return;
	}

	uint8_t buffer[33];
    // Wypełnij bufor nadawczy modemu PLC
	buffer[0] = TX_DATA_REG;
	memcpy(buffer + 1, buf, len);
	writePLCregisters(buffer, len + 1);
}

// TODO: Zrobić inline z onelinerów.
void sendPLCData(uint8_t len)
{
	// Po wypełnieniu bufora nakaż wysłać dane poprzez PLC
	writePLCregister(TX_MESSAGE_LENGTH_REG, SEND_MESSAGE | (len & (32 - 1)));
}

static void hostIntPinHandler(uint8_t pin)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    vTaskNotifyGiveFromISR(xPLCTask, &xHigherPriorityTaskWoken);
    portEND_SWITCHING_ISR(xHigherPriorityTaskWoken);
}

void plcTask(void *pvParameters)
{
    vTaskDelay(pdMS_TO_TICKS(2 * 1000));
    printf("Initializing PLC at address %d\n", PLC_WRITE_ADDR);

#ifdef PLC_TX_TEST
    initPLCdevice(120);
#else
	initPLCdevice(119);
#endif
    // Read Physical Address
    uint8_t phyAddr[8];
    readPLCregisters(0x6A, phyAddr, 8);
    printf("%d %d %d %d %d %d %d %d\n\r", phyAddr[0], phyAddr[1], phyAddr[2], phyAddr[3], phyAddr[4],
           phyAddr[5], phyAddr[6], phyAddr[7]);

    for (;;)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        unsigned int temp = readPLCintRegister();
        printf("Got some data from PLC: %d\n\r", temp);
    }
}

#ifdef PLC_TX_TEST
void plcTestTxTask(void *pvParameters)
{
	vTaskDelay(pdMS_TO_TICKS(4 * 1000));

	printf("Deske lau\n\r");

	setPLCtxAddrType(TX_SA_TYPE_LOGICAL, TX_DA_TYPE_LOGICAL);

	uint8_t txDA = 119;
	setPLCtxDA(TX_DA_TYPE_LOGICAL, &txDA);

	uint8_t len = 5;
	uint8_t testData[len];
	testData[0] = 48;
	testData[1] = 49;
	testData[2] = 50;
	testData[3] = 51;
	testData[4] = 52; 

	fillPLCTxData(testData, len);

	for( ; ; )
	{
		sendPLCData(len);
		vTaskDelay(pdMS_TO_TICKS(1000));
	}

}

#endif