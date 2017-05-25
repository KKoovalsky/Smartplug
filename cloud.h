#ifndef CLOUD_H
#define CLOUD_H

#define TEST_ACCESS_TOKEN "nKszhGeiC3BX0CglY76d"

#define MQTT_HOST "ec2-52-41-163-55.us-west-2.compute.amazonaws.com"
#define MQTT_PORT 1883

#define MQTT_ID "ESPYOLO"
#define MQTT_USER TEST_ACCESS_TOKEN
#define MQTT_PASS NULL
#define MQTT_MSG "{\"power\":\"20\",\"current\":\"11.0\",\"active\":\"false\"}"

#include <FreeRTOS.h>
#include <task.h>

void mqttTask(void *pvParameters);

#endif