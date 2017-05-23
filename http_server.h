#ifndef HTTP_SERVER_H_
#define HTTP_SERVER_H_

#include <FreeRTOS.h>
#include "task.h"

#define LED_PIN 2

extern TaskHandle_t xHTTPServerTask;

extern const uint8_t *wifiJsonStrings[];
extern const uint8_t wifiJsonStringsLen [];

extern uint8_t wifiConnectionSuccessJson[];
extern uint8_t wifiConnectionSuccessJsonLen;

extern const uint8_t plcJsonRegisSuccessStr[];
extern const uint8_t plcJsonRegisUnsuccessStr[];
extern const uint8_t plcJsonTooShortPhyAddrStr[];

extern const uint8_t plcJsonRegisSuccessStrLen;
extern const uint8_t plcJsonRegisUnsuccessStrLen;
extern const uint8_t plcJsonTooShortPhyAddrStrLen;

void httpd_task(void *pvParameters);

void sendWsResponse(const uint8_t *msg, int len);
void sendWsResponseAndWaitForAck(const uint8_t *msg, int len);
#endif