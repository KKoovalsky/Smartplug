#include "cloud.h"
#include "espressif/esp_common.h"
#include <FreeRTOS.h>
#include <task.h>
#include <string.h>
#include <paho_mqtt_c/MQTTESP8266.h>
#include <paho_mqtt_c/MQTTClient.h>
#include "client.h"
#include "parsers.h"

static volatile char *tbToken[21];
QueueHandle_t xMqttQueue;

const char telemetryTopic[] = "v1/gateway/telemetry";
const char newDeviceTopic[] = "v1/gateway/connect";

static inline int registerMqttClientsFromList(mqtt_client_t *mqttClient, mqtt_message_t *mqttMessage);
static void mqttRpcReceived(mqtt_message_data_t *md);

void mqttTask(void *pvParameters)
{
	struct mqtt_network network;
	mqtt_client_t client = mqtt_client_default;
	uint8_t mqttBuf[128];
	uint8_t mqttReadBuf[128];
	mqtt_packet_connect_data_t data = mqtt_packet_connect_data_initializer;

	data.willFlag = 0;
	data.MQTTVersion = 3;
	data.clientID.cstring = MQTT_ID;
	data.username.cstring = (char *)tbToken;
	data.password.cstring = MQTT_PASS;
	data.keepAliveInterval = 10;
	data.cleansession = 0;

	mqtt_message_t message;
	message.dup = 0;
	message.qos = MQTT_QOS1;
	message.retained = 0;

	mqtt_network_new(&network);

	int ret;

	while (1)
	{
		printf("Establishing MQTT connection...\n\r");
		ret = mqtt_network_connect(&network, MQTT_HOST, MQTT_PORT);
		if (ret != MQTT_SUCCESS)
		{
			printf("Error connecting to MQTT server: %d\n\r", ret);
			vTaskDelay(pdMS_TO_TICKS(2000));
			continue;
		}
		else
			printf("Successfully connected to MQTT server\n\r");

		mqtt_client_new(&client, &network, 5000, mqttBuf, sizeof(mqttBuf), mqttReadBuf, sizeof(mqttReadBuf));
		ret = mqtt_connect(&client, &data);
		if (ret != MQTT_SUCCESS)
		{
			printf("Error sending MQTT CONNECT: %d\n\r", ret);
			mqtt_network_disconnect(&network);
			continue;
		}
		else
			printf("MQTT CONNECT sent successfully\n\r");

		ret = registerMqttClientsFromList(&client, &message);
		if (ret != MQTT_SUCCESS)
		{
			printf("Error registering devices\n");
			mqtt_network_disconnect(&network);
			continue;
		}
		else
			printf("Succesfull device connect\n");

		ret = mqtt_subscribe(&client, "v1/gateway/rpc", MQTT_QOS1, mqttRpcReceived);
		if (ret != MQTT_SUCCESS)
		{
			printf("Error occured while subscribing: %d\n", ret);
			mqtt_network_disconnect(&network);
			continue;
		}
		else
			printf("Succesfull subscription\n");

		for (;;)
		{
			TelemetryData telemetryData;
			xQueueReceive(xMqttQueue, &telemetryData, portMAX_DELAY);

			char buf[128] = "";
			message.payload = buf;
			if (telemetryData.dataType == TELEMETRY_TYPE_DATA)
			{
				char deviceName[33];
				getDeviceNameByPlcPhyAddr(deviceName, telemetryData.brokerPhyAddr);
				uint8_t *data = telemetryData.data;
				bool restart = false;
				for (int i = 0; i < telemetryData.len / 10; i++)
				{
					message.payloadlen = composeJsonFromTelemetryData(buf, deviceName, data);
					printf("%s %d\n", (char *)message.payload, message.payloadlen);
					if (message.payloadlen > 0)
					{
						ret = mqtt_publish(&client, telemetryTopic, &message);
						if (ret != MQTT_SUCCESS)
						{
							printf("Error while publishing message: %d\n\r", ret);
							mqtt_network_disconnect(&network);
							restart = true;
							break;
						}
						else
							printf("MQTT Publishing successful\n\r");
					}
					data += 10;
				}
				if(restart)
					break;
			}
			else
			{
				message.payloadlen = composeJsonFromNewDevice(buf);
				ret = mqtt_publish(&client, newDeviceTopic, &message);
				if (ret != MQTT_SUCCESS)
				{
					printf("Error while publishing message: %d\n\r", ret);
					mqtt_network_disconnect(&network);
					break;
				}
				else
					printf("MQTT Publishing successful\n\r");
			}
		}
	}
}

void setTbToken(char *newTbToken)
{
	memcpy(tbToken, newTbToken, 20);
	tbToken[20] = '\0';
}

char *getTbToken()
{
	return (char *)tbToken;
}

static inline int registerMqttClientsFromList(mqtt_client_t *mqttClient, mqtt_message_t *mqttMessage)
{
	char buf[52] = "{\"device\":\"";
	int ret = MQTT_FAILURE;
	const int firstTxtLen = strlen(buf);
	for (client_s *client = (client_s *)clientListBegin; client; client = client->next)
	{
		mqttMessage->payloadlen = sprintf(buf + firstTxtLen, "%s\"}", client->deviceName) + firstTxtLen;
		mqttMessage->payload = buf;
		ret = mqtt_publish(mqttClient, newDeviceTopic, mqttMessage);
		if (ret != MQTT_SUCCESS)
			break;
		else
			printf("Device connected: %s\n", buf);
	}
	return ret;
}

static void mqttRpcReceived(mqtt_message_data_t *md)
{
	int i;
	mqtt_message_t *message = md->message;
	printf("Received: ");
	for (i = 0; i < md->topic->lenstring.len; ++i)
		printf("%c", md->topic->lenstring.data[i]);

	printf(" = ");
	for (i = 0; i < (int)message->payloadlen; ++i)
		printf("%c", ((char *)(message->payload))[i]);

	printf("\r\n");
}