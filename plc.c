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
#include "client_list.h"

#define DEBUG_PLC
#define HOST_INT_PIN 13

TaskHandle_t xPLCTaskRcv;
TaskHandle_t xPLCTaskSend;
TaskHandle_t xTaskNewClientRegis;
SemaphoreHandle_t xPLCSendSemaphore;

plcTxRecord_s plcTxBuf[PLC_TX_BUF_SIZE];
int plcTxBufHead, plcTxBufTail;

volatile uint8_t plcPhyAddr[8];

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

void setPLCtxDA(uint8_t txDAtype, uint8_t *txDA)
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
	intStatusReg = readPLCregister(INTERRUPT_STATUS_REG) & ~STATUS_VALUE_CHANGE;
	//  Po odczycie powyższego rejestru należy wyzerować bit INT_CLEAR
	//  w rejestrze INTERRUPT_ENABLE_REG (datasheet, page 7.)
	intEnableReg = readPLCregister(INTERRUPT_ENABLE_REG) & ~INT_CLEAR_PLC;
	writePLCregister(INTERRUPT_ENABLE_REG, intEnableReg);
	return intStatusReg;
}

//TODO: Change comments to english
void initPLCdevice(uint8_t nodeLA)
{
	//  Konfiguracja pinu HOST_INT
	gpio_enable(HOST_INT_PIN, GPIO_INPUT);
	gpio_set_interrupt(HOST_INT_PIN, GPIO_INTTYPE_EDGE_NEG, hostIntPinHandler);

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
	writePLCregister(TX_GAIN_REG, TX_GAIN_LEVEL_3000MV);
	//	Ustawienie czułości dla modułu odbiornika PLC
	writePLCregister(RX_GAIN_REG, RX_GAIN_LEVEL_250UV);
	//	Ustawienie numeru LA modemu PLC
	setPLCnodeLA(nodeLA);
	//	Ustawienie adresu Grupowego modemu PLC
	setPLCnodeGA(MASTER_GROUP_ADDR);
	setPLCtxAddrType(TX_SA_TYPE_PHYSICAL, TX_DA_TYPE_PHYSICAL);
}

void fillPLCTxData(uint8_t *buf, uint8_t len)
{
	if (len > 32)
	{
		// TODO: Jeżeli ten warunek jest spełniony należy podzielić wiadomość.
		printf("Internal CY8CPLC10 buffer is shorter than len\n\r");
		return;
	}

	if (len == 0)
		return;

	uint8_t buffer[33];
	// Wypełnij bufor nadawczy modemu PLC
	buffer[0] = TX_DATA_REG;
	memcpy(buffer + 1, buf, len);
	writePLCregisters(buffer, len + 1);
}

// TODO: Zrobić inline z onelinerów.
void sendPLCData(uint8_t *buf, uint8_t len)
{
	fillPLCTxData(buf, len);
	writePLCregister(TX_COMMAND_ID_REG, SEND_REMOTE_DATA);
	// Po wypełnieniu bufora nakaż wysłać dane poprzez PLC
	writePLCregister(TX_MESSAGE_LENGTH_REG, SEND_MESSAGE | (len & (32 - 1)));
}

static void hostIntPinHandler(uint8_t pin)
{
	BaseType_t xHigherPriorityTaskWoken = pdFALSE;
	if (xPLCTaskRcv)
		vTaskNotifyGiveFromISR(xPLCTaskRcv, &xHigherPriorityTaskWoken);
	portEND_SWITCHING_ISR(xHigherPriorityTaskWoken);
}

void plcTaskRcv(void *pvParameters)
{
	for (;;)
	{
		ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
		unsigned int intRegContent = readPLCintRegister();
		printf("Got some data from PLC: %d\n\r", intRegContent);
		switch (intRegContent)
		{
		case STATUS_RX_DATA_AVAILABLE:
		{
			int result = 0;
			unsigned int cmdReg = readPLCregister(RX_COMMAND_ID_REG);
			printf("PLC: New RX data available.\n\r");
			switch (cmdReg)
			{
			case CUSTOM_CMD_REGISTER_NEW_DEV:
				if (devType == BROKER)
					xTaskCreate(registerNewClientTask, "Regis", 256, NULL, 4, &xTaskNewClientRegis);
				break;
			case CUSTOM_CMD_REGISTRATION_FAILED:
				result = -2;
			case CUSTOM_CMD_REGISTRATION_SUCCESS:
				if (xInitializerTask)
					xTaskNotify(xInitializerTask, result, eSetValueWithoutOverwrite);
				break;
			}
			break;
		}
		case STATUS_TX_NO_ACK:
		{
			static int nackCnt = MAX_NACK_RCV;
			nackCnt--;
			printf("PLC: No ACK received.\n\r");
			if (!nackCnt)
			{
				plcTxRecord_s *txRec = &plcTxBuf[plcTxBufTail];
				if (txRec->taskToNotify)
					xTaskNotify(txRec->taskToNotify, -1, eSetValueWithoutOverwrite);

				nackCnt = MAX_NACK_RCV;
				plcTxBufTail = (plcTxBufTail + 1) & PLC_TX_BUF_MASK;
			}
			xSemaphoreGive(xPLCSendSemaphore);
			break;
		}
		case STATUS_TX_NO_RESP:
		{
			static int noRespCnt = MAX_REMOTE_CMD_RETRIES;
			noRespCnt--;
			printf("PLC: No response received.\n\r");
			if (!noRespCnt)
			{
				plcTxRecord_s *txRec = &plcTxBuf[plcTxBufTail];
				if (txRec->taskToNotify)
					xTaskNotify(txRec->taskToNotify, -3, eSetValueWithoutOverwrite);

				noRespCnt = MAX_REMOTE_CMD_RETRIES;
				plcTxBufTail = (plcTxBufTail + 1) & PLC_TX_BUF_MASK;
			}
			xSemaphoreGive(xPLCSendSemaphore);
			break;
		}
		case STATUS_TX_DATA_SENT:
		{
			plcTxRecord_s *txRec = &plcTxBuf[plcTxBufTail];
			if (txRec->taskToNotify)
				xTaskNotify(txRec->taskToNotify, 0, eSetValueWithoutOverwrite);

			plcTxBufTail = (plcTxBufTail + 1) & PLC_TX_BUF_MASK;

			printf("PLC: Data sent.\n\r");
			xSemaphoreGive(xPLCSendSemaphore);
			break;
		}
		default:
			printf("Wrong PLC Interrupt register content: %d\n\r", intRegContent);
			break;
		}
	}
}

void plcTaskSend(void *pvParameters)
{
	xSemaphoreGive(xPLCSendSemaphore);

	for (;;)
	{
		// Task notification receiving implemented to speed up sending of data.
		ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10));

		if (plcTxBufHead != plcTxBufTail)
		{
			if (xSemaphoreTake(xPLCSendSemaphore, 0))
			{
				plcTxRecord_s *txRec = &plcTxBuf[plcTxBufTail];

				printf("Sending PLC data \"%.*s\" of len %d with command 0x%X\n\r",
					   txRec->len, txRec->data, txRec->len, txRec->command);

				if (txRec->len)
					fillPLCTxData(txRec->data, txRec->len);

				writePLCregister(TX_COMMAND_ID_REG, txRec->command);

				writePLCregister(TX_MESSAGE_LENGTH_REG, txRec->len | SEND_MESSAGE);
			}
		}
	}
}

// TODO: byte type signedness standarization
// TODO: split into different files client side and broker side functions.
int registerClient(char *brokerPhyAddr, char *tbToken)
{
	plcTxRecord_s *txRec = &plcTxBuf[plcTxBufHead];
	txRec->len = 20;
	txRec->command = CUSTOM_CMD_REGISTER_NEW_DEV;
	txRec->taskToNotify = xInitializerTask;

	memcpy(txRec->phyAddr, brokerPhyAddr, 8);
	memcpy(txRec->data, tbToken, 20);

	plcTxBufHead = (plcTxBufHead + 1) & PLC_TX_BUF_MASK;

	xTaskNotifyGive(xPLCTaskSend);

	// First notification received with information if TX data acknowledged.
	uint32_t result;
	if (xTaskNotifyWait(0, 0xFFFFFFFF, &result, pdMS_TO_TICKS(20000)) != pdTRUE)
	{
		result = -1;
		txRec->taskToNotify = NULL;
	}

	if (result == 0)
	{
		if (xTaskNotifyWait(0, 0xFFFFFFFF, &result, pdMS_TO_TICKS(20000)) != pdTRUE)
			result = -1;
	}

	return result;
}

void registerNewClientTask(void *pvParameters)
{
	uint8_t packetLen, command;
	client_s *newClient = (client_s *)pvPortMalloc(sizeof(client_s));
	readPLCrxPacket(&command, (uint8_t *)newClient->tbToken, &packetLen);

	plcTxRecord_s *txRec = &plcTxBuf[plcTxBufHead];
	getPLCrxSA(txRec->phyAddr);
	memcpy(newClient->plcPhyAddr, txRec->phyAddr, 8);

	if (packetLen != 20)
	{
		vPortFree(newClient);
		txRec->len = 0;
		txRec->command = CUSTOM_CMD_REGISTRATION_FAILED;
		txRec->taskToNotify = NULL;

		plcTxBufHead = (plcTxBufHead + 1) & PLC_TX_BUF_MASK;
		xTaskNotifyGive(xPLCTaskSend);
	}
	else
	{
		// TODO: Check now if Thingsboard token is valid
		txRec->len = 0;
		txRec->command = CUSTOM_CMD_REGISTRATION_SUCCESS;
		txRec->taskToNotify = xTaskNewClientRegis;

		plcTxBufHead = (plcTxBufHead + 1) & PLC_TX_BUF_MASK;
		xTaskNotifyGive(xPLCTaskSend);

		uint32_t result;
		if (xTaskNotifyWait(0, 0xFFFFFFFF, &result, pdMS_TO_TICKS(5000)) != pdTRUE)
		{
			result = -1;
			txRec->taskToNotify = NULL;
		}

		if (result < 0)
		{
			vPortFree(newClient);
			// TODO: Here can be checked what happened exactly (switch(result))
			switch (result)
			{
			case -1:
				printf("ERROR: Registration confirmation not acknowledged. Registration aborted.\n\r");
				break;
			case -3:
				printf("ERROR: Response not received. Registration aborted.\n\r");
				break;
			default:
				printf("ERROR: Undefined: %d\n\r", result);
			}
			printf("ERROR: Registration confirmation not acknowledged. Not registering.\n\r");
		}
		else
		{
			printf("Registration successful\n\r");
			addClient(newClient);
		}
	}

	vTaskDelete(NULL);
}