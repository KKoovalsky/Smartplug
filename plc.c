#include "plc.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "i2c.h"
#include "esp8266.h"
#include "espressif/esp_common.h"
#include "espressif/esp_sta.h"
#include "esp/uart.h"
#include "system.h"
#include "client.h"
#include "timers.h"
#include "sntp_sync.h"
#include "spiffs_local.h"
#include "parsers.h"
#include "cloud.h"

#define DEBUG_PLC
#define HOST_INT_PIN 13

TaskHandle_t xPLCTaskRcv;
TaskHandle_t xPLCTaskSend;
TaskHandle_t xTaskNewClientRegis;
static volatile TaskHandle_t xWifiCredsTaskHandle = NULL;
SemaphoreHandle_t xPLCSendSemaphore;

plcTxRecord_s plcTxBuf[PLC_TX_BUF_SIZE];
int plcTxBufHead, plcTxBufTail;

volatile uint8_t *newWifiSsid = NULL, *newWifiPassword = NULL;

static void hostIntPinHandler(uint8_t pin);
static inline void handleReceivedDataBasingOnCommandReceived();
static void initPlcTimerClbk(TimerHandle_t xTimer);

void initPlcWithDelay()
{
	// PLC Chip initialization after two seconds to wait for device startup.
	TimerHandle_t plcInitTimer = xTimerCreate("PLC init", pdMS_TO_TICKS(2000), pdFALSE, 0, initPlcTimerClbk);
	xTimerStart(plcInitTimer, 0);
}

static void initPlcTimerClbk(TimerHandle_t xTimer)
{
	printf("Initializing PLC.\n\r");
	initPLCdevice(0);
	xTimerDelete(xTimer, 0);
}

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
	// Logical and group addresses are one byte and physical address is eight byte.
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
	uint8_t len = readPLCregister(RX_MESSAGE_INFO_REG) & 0x1F; // 0...31
	if (rxCommand)
		*rxCommand = readPLCregister(RX_COMMAND_ID_REG);
	//	Wczytanie do tablicy rxData będącej argumentem funkcji wszystkich otrzymanych danych
	readPLCregisters(RX_DATA_REG, rxData, len);
	//	Aby skasować flagi STATUS_VALUE_CHANGE, STATUS_RX_PACKET_DROPPED
	//	i STATUS_RX_DATA_AVAIBLE w rejestrze INTERRUPT_STATUS_REG musimy
	//	wyzerować flagę NEW_PACKET_RECEIVED w rejestrze RX_MESSAGE_INFO_REG
	//	(datasheet)
	infoRegister = readPLCregister(RX_MESSAGE_INFO_REG);
	writePLCregister(RX_MESSAGE_INFO_REG, infoRegister & ~NEW_PACKET_RECEIVED);

	if (rxDataLength)
		*rxDataLength = len;
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
	writePLCregister(INTERRUPT_ENABLE_REG, INT_POLARITY_LOW | INT_UNABLE_TO_TX | INT_TX_NO_ACK | INT_TX_NO_RESP | INT_RX_DATA_AVAILABLE | INT_TX_DATA_SENT);
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
			handleReceivedDataBasingOnCommandReceived();
			break;
		}
		case STATUS_TX_NO_ACK:
		{
			static int nackCnt = MAX_NACK_RCV;
			nackCnt--;
			if (!nackCnt)
			{
				plcTxRecord_s *txRec = &plcTxBuf[plcTxBufTail];
				if (txRec->taskToNotify)
					xTaskNotify(txRec->taskToNotify, PLC_ERR_NO_ACK, eSetValueWithoutOverwrite);

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
			if (!noRespCnt)
			{
				plcTxRecord_s *txRec = &plcTxBuf[plcTxBufTail];
				if (txRec->taskToNotify)
					xTaskNotify(txRec->taskToNotify, PLC_ERR_NO_RESP, eSetValueWithoutOverwrite);

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
				xTaskNotify(txRec->taskToNotify, PLC_ERR_OK, eSetValueWithoutOverwrite);

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

static inline void handleReceivedDataBasingOnCommandReceived()
{
	unsigned int cmdReg = readPLCregister(RX_COMMAND_ID_REG);
	printf("PLC: New RX data available.\n\r");
	PlcErr_e result = PLC_ERR_IDLE;
	switch (cmdReg)
	{
	case REGISTER_NEW_DEV:
		if (devType == BROKER)
			xTaskCreate(registerNewClientTask, "Regis", 256, NULL, 4, &xTaskNewClientRegis);
		break;
	case REGISTRATION_SUCCESS:
		if (xTaskNewClientRegis)
			xTaskNotify(xTaskNewClientRegis, 1, eSetValueWithoutOverwrite);
		break;
	case REGISTRATION_FAILED:
		result = PLC_ERR_REGISTRATION_FAILED;
		break;
	case NEW_WIFI_PASSWORD:
		result = PLC_ERR_NEW_PASSWORD;
		break;
	case NEW_WIFI_SSID:
		result = PLC_ERR_NEW_SSID;
		break;
	case NEW_TB_TOKEN:
		result = PLC_ERR_NEW_TB_TOKEN;
		break;
	}

	if ((result != PLC_ERR_IDLE) && xWifiCredsTaskHandle)
		xTaskNotify(xWifiCredsTaskHandle, result, eSetValueWithoutOverwrite);
}

void plcTaskSend(void *pvParameters)
{
	initPlcWithDelay();
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

				if (txRec->isPhyAddrNew)
					setPLCtxDA(TX_DA_TYPE_PHYSICAL, txRec->phyAddr);

				writePLCregister(TX_COMMAND_ID_REG, txRec->command);

				writePLCregister(TX_MESSAGE_LENGTH_REG, txRec->len | SEND_MESSAGE);
			}
		}
	}
}

// TODO: byte type signedness standarization
// TODO: split into different files client side and broker side functions.
PlcErr_e registerClient(PermConfData_s *configData)
{
	uint8_t rawPlcPhyAddr[8];
	convertPlcPhyAddressToRaw(rawPlcPhyAddr, configData->plcPhyAddr);

	TaskHandle_t currentTask = xTaskGetCurrentTaskHandle();
	PlcErr_e result = sendPLCData((uint8_t *)configData->deviceName, rawPlcPhyAddr, currentTask,
								  REGISTER_NEW_DEV, (uint8_t)configData->deviceNameLen, 1);

	if (result >= 0)
	{
		xWifiCredsTaskHandle = currentTask;
		if (xTaskNotifyWait(0, 0xFFFFFFFF, (uint32_t *)&result, pdMS_TO_TICKS(3000)) != pdTRUE)
			result = PLC_ERR_TIMEOUT;

		if (result == PLC_ERR_NEW_SSID)
		{
			readPLCrxPacket(NULL, (uint8_t *)configData->ssid, &configData->ssidLen);
			configData->ssid[configData->ssidLen] = '\0';

			if (xTaskNotifyWait(0, 0xFFFFFFFF, (uint32_t *)&result, pdMS_TO_TICKS(3000)) != pdTRUE)
				result = PLC_ERR_TIMEOUT;

			if (result == PLC_ERR_NEW_PASSWORD)
			{
				readPLCrxPacket(NULL, (uint8_t *)configData->password, &configData->passwordLen);
				configData->password[configData->passwordLen] = '\0';

				if (xTaskNotifyWait(0, 0xFFFFFFFF, (uint32_t *)&result, pdMS_TO_TICKS(3000)) != pdTRUE)
					result = PLC_ERR_TIMEOUT;

				if(result == PLC_ERR_NEW_TB_TOKEN)
				{
					uint8_t packetLen;
					readPLCrxPacket(NULL, (uint8_t *)configData->tbToken, &packetLen);
					configData->tbToken[20] = '\0';

					if(packetLen == 20)
						result = sendPLCData(NULL, NULL, NULL, REGISTRATION_SUCCESS, 0, 0);			
				} else
					result = PLC_ERR_NOT_WIFI_CREDS;
			}
			else
				result = PLC_ERR_NOT_WIFI_CREDS;
		}
		else
			result = PLC_ERR_NOT_WIFI_CREDS;
	}

	xWifiCredsTaskHandle = NULL;
	return result;
}

void registerNewClientTask(void *pvParameters)
{
	uint8_t packetLen, command;
	client_s *newClient = (client_s *)pvPortMalloc(sizeof(client_s));
	readPLCrxPacket(&command, (uint8_t *)newClient->deviceName, &packetLen);
	newClient->deviceName[packetLen] = '\0';
	getPLCrxSA(newClient->plcPhyAddr);

	// TODO: Send reason of registration failure.
	PlcErr_e result = PLC_ERR_OK;

	if (result >= 0)
	{
		struct sdk_station_config config;
		sdk_wifi_station_get_config(&config);
		int ssidLen = strlen((char *)config.ssid);
		int passwordLen = strlen((char *)config.password);

		result = sendPLCData(config.ssid, newClient->plcPhyAddr, xTaskNewClientRegis, NEW_WIFI_SSID, ssidLen, 1);
		if (result >= 0)
		{
			result = sendPLCData(config.password, NULL, xTaskNewClientRegis, NEW_WIFI_PASSWORD, passwordLen, 0);
			if (result >= 0)
			{
				result = sendPLCData((uint8_t *) getTbToken(), NULL, xTaskNewClientRegis, NEW_TB_TOKEN, 20, 0);
				if (result >= 0)
				{
					if (xTaskNotifyWait(0, 0xFFFFFFFF, (uint32_t *)&result, pdMS_TO_TICKS(4000)) != pdTRUE)
						result = PLC_ERR_TIMEOUT;
					else
					{
						printf("Registration successful\n");
						addClient(newClient);
						saveClientDataToFile(newClient);
					}
				}
			}
		}
	}

	if (result < 0)
	{
		vPortFree(newClient);
		sendPLCData(NULL, NULL, NULL, REGISTRATION_FAILED, 0, 0);
		printf("Registation unsuccessful: %d\n", result);
	}

	vTaskDelete(NULL);
}

PlcErr_e sendPLCData(uint8_t *data, uint8_t *phyAddr, TaskHandle_t taskToNotify,
					 uint8_t command, uint8_t len, uint8_t isPhyAddrNew)
{
	plcTxRecord_s *txRec = &plcTxBuf[plcTxBufHead];
	txRec->len = len;
	txRec->command = command;
	txRec->taskToNotify = taskToNotify;

	if (isPhyAddrNew)
	{
		txRec->isPhyAddrNew = 1;
		memcpy(txRec->phyAddr, phyAddr, 8);
	}

	if (len)
		memcpy(txRec->data, data, len);

	plcTxBufHead = (plcTxBufHead + 1) & PLC_TX_BUF_MASK;
	xTaskNotifyGive(xPLCTaskSend);

	PlcErr_e result = PLC_ERR_OK;
	if (taskToNotify)
	{
		if (xTaskNotifyWait(0, 0xFFFFFFFF, (uint32_t *)&result, pdMS_TO_TICKS(3000)) != pdTRUE)
		{
			txRec->taskToNotify = NULL;
			result = PLC_ERR_TIMEOUT;
		}
	}

	return result;
}
