#ifndef CLOUD_H
#define CLOUD_H

#include <FreeRTOS.h>
#include <queue.h>

#define MQTT_HOST "172.104.142.37"
#define MQTT_PORT 1883

#define MQTT_ID "ESP_TELEMETRY"
#define MQTT_PASS NULL

#include <FreeRTOS.h>
#include <task.h>

#define TELEMETRY_TYPE_DATA			1
#define TELEMETRY_TYPE_NEW_DEVICE	2

typedef struct telemetryData_s
{
	uint8_t data[32];
	uint8_t brokerPhyAddr[8];
	uint8_t dataType;
	uint8_t len;
} TelemetryData;

extern QueueHandle_t xMqttQueue;

void mqttTask(void *pvParameters);
void setTbToken(char *);
char *getTbToken();

#endif