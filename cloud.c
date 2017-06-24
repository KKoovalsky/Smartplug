#include "cloud.h"
#include "espressif/esp_common.h"
#include <FreeRTOS.h>
#include <task.h>
#include <string.h>
#include <paho_mqtt_c/MQTTESP8266.h>
#include <paho_mqtt_c/MQTTClient.h>
#include "client.h"
#include "parsers.h"
#include "jsmn.h"
#include "plc.h"

static volatile char *tbToken[21];
QueueHandle_t xMqttQueue;

const char telemetryTopic[] = "v1/gateway/telemetry";
const char newDeviceTopic[] = "v1/gateway/connect";
const char rpcTopicPrefix[] = "v1/devices/me/rpc/";
#define rpcTopicResponse "v1/devices/me/rpc/response/"

static inline int registerMqttClientsFromList(mqtt_client_t *mqttClient, mqtt_message_t *mqttMessage);
static void mqttRpcReceived(mqtt_message_data_t *md);
static inline void getNumberOfRequestInStringForm(uint8_t *dst, uint8_t *dstLen, char *src, int srcLen);
static inline enum RpcMethodType getRpcMethodType(mqtt_message_t *message);
static inline int composeJsonFromRelayStateOnDevices(char *buf);
inline struct RelayStateChanger *extractPinNumberAndStateFromJson(mqtt_message_t *message);

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

		mqtt_client_new(&client, &network, 5000, mqttBuf, sizeof(mqttBuf), mqttReadBuf, sizeof(mqttReadBuf));
		ret = mqtt_connect(&client, &data);
		if (ret != MQTT_SUCCESS)
		{
			printf("Error sending MQTT CONNECT: %d\n\r", ret);
			mqtt_network_disconnect(&network);
			continue;
		}

		ret = registerMqttClientsFromList(&client, &message);
		if (ret != MQTT_SUCCESS)
		{
			printf("Error registering devices\n");
			mqtt_network_disconnect(&network);
			continue;
		}

		ret = mqtt_subscribe(&client, "v1/devices/me/rpc/request/+", MQTT_QOS1, mqttRpcReceived);
		if (ret != MQTT_SUCCESS)
		{
			printf("Error occured while subscribing: %d\n", ret);
			mqtt_network_disconnect(&network);
			continue;
		}

		for (;;)
		{
			struct MqttData mqttData;
			xQueueReceive(xMqttQueue, &mqttData, portMAX_DELAY);

			char buf[128] = "";
			message.payload = buf;
			if (mqttData.dataType == TYPE_TELEMETRY)
			{
				char deviceName[33];
				getDeviceNameByPlcPhyAddr(deviceName, mqttData.gatewayPhyAddr);
				uint8_t *data = mqttData.data;
				bool restart = false;
				for (int i = 0; i < mqttData.len / 10; i++)
				{
					message.payloadlen = composeJsonFromMqttData(buf, deviceName, data);
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
					}
					data += 10;
				}
				if (restart)
					break;
			}
			else if (mqttData.dataType == TYPE_NEW_DEVICE)
			{
				message.payloadlen = composeJsonFromNewDevice(buf);
				ret = mqtt_publish(&client, newDeviceTopic, &message);
				if (ret != MQTT_SUCCESS)
				{
					printf("Error while publishing message: %d\n\r", ret);
					mqtt_network_disconnect(&network);
					break;
				}
			}
			else if (mqttData.dataType == TYPE_GPIO_STATUS_GET)
			{
				message.payloadlen = composeJsonFromRelayStateOnDevices(buf);
				char topic[48] = rpcTopicResponse;
				memcpy(topic + sizeof(rpcTopicResponse) - 1, mqttData.data, mqttData.len + 1);
				ret = mqtt_publish(&client, topic, &message);
				printf("Topic: %s payload: %.*s\n", topic, message.payloadlen, buf);
				if (ret != MQTT_SUCCESS)
				{
					printf("Error -> get gpio status -> MQTT");
					mqtt_network_disconnect(&network);
					break;
				}
				ret = mqtt_publish(&client, "v1/devices/me/attributes", &message);
				if (ret != MQTT_SUCCESS)
				{
					printf("Error -> get gpio status -> MQTT");
					mqtt_network_disconnect(&network);
					break;
				}
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
	for (struct Client *client = (struct Client *)clientListBegin; client; client = client->next)
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
	mqtt_message_t *message = md->message;

	printf("Got on topic: %.*s ", md->topic->lenstring.len, (char *)md->topic->lenstring.data);
	printf("data: %.*s\n", message->payloadlen, (char *)message->payload);

	char *topicItr = md->topic->lenstring.data + sizeof(rpcTopicPrefix) - 1;
	const char requestStr[] = "request";
	if (!strncmp(topicItr, requestStr, sizeof(requestStr) - 1))
	{
		topicItr += sizeof(requestStr);
		struct MqttData rpcData;
		getNumberOfRequestInStringForm(rpcData.data, &rpcData.len, topicItr,
									   md->topic->lenstring.len - (topicItr - md->topic->lenstring.data));
		printf("Request: %s len %d\n", rpcData.data, rpcData.len);

		enum RpcMethodType methodType = getRpcMethodType(message);
		if (methodType == GET_GPIO_STATUS)
		{
			rpcData.dataType = TYPE_GPIO_STATUS_GET;
			xQueueSend(xMqttQueue, &rpcData, 0);
		}
		else if (methodType == SET_GPIO_STATUS)
		{
			struct RelayStateChanger *r = extractPinNumberAndStateFromJson(message);
			if (r)
			{
				r->requestNumber = atoi((char *)rpcData.data);
				xTaskCreate(changeRelayStateTask, "Change GPIO", 256, (void *)r, 3, NULL);
			} else
				printf("JSON parsing fail in extract\n");
		}
		else
			printf("JSON parsing failed\n");
	}
}

static inline void getNumberOfRequestInStringForm(uint8_t *dst, uint8_t *dstLen, char *src, int srcLen)
{
	memcpy(dst, src, srcLen);
	dst[srcLen] = '\0';
	*dstLen = srcLen;
}

static inline enum RpcMethodType getRpcMethodType(mqtt_message_t *message)
{
	/*
	jsmn_parser jsmnParser;
	jsmntok_t t[4];
	jsmn_init(&jsmnParser);
	int r = jsmn_parse(&jsmnParser, message->payload, message->payloadlen, t, sizeof(t) / sizeof(t[0]));
	if (r < 0)
		return NO_METHOD;
	*/
	const char getGpioStatusMethodName[] = "getGpioStatus";
	const char setGpioStatusMethodName[] = "setGpioStatus";

	if (!strncmp(message->payload + 11, getGpioStatusMethodName, sizeof(getGpioStatusMethodName) - 1))
		return GET_GPIO_STATUS;
	else if (!strncmp(message->payload + 11, setGpioStatusMethodName, sizeof(setGpioStatusMethodName) - 1))
		return SET_GPIO_STATUS;
	else
		return NO_METHOD;
}

static inline int composeJsonFromRelayStateOnDevices(char *buf)
{
	int i = 1, index = 1;
	*buf = '{';
	for (struct Client *client = (struct Client *)clientListBegin; client; client = client->next, i++)
		index += sprintf(buf + index, "\"%d\":%s,", i, client->relayState ? "true" : "false");

	*(buf + index - 1) = '}';
	return index;
}

inline struct RelayStateChanger *extractPinNumberAndStateFromJson(mqtt_message_t *message)
{
	jsmn_parser jsmnParser;
	jsmntok_t t[10];
	jsmn_init(&jsmnParser);
	int r = jsmn_parse(&jsmnParser, message->payload, message->payloadlen, t, sizeof(t) / sizeof(t[0]));
	if (r < 0)
		return NULL;

	struct RelayStateChanger *relayStateChanger =
		(struct RelayStateChanger *)pvPortMalloc(sizeof(struct RelayStateChanger));
	
	char deviceNumberInStringForm[4];
	int deviceNumberInStringFormLen = t[6].end - t[6].start;
	memcpy(deviceNumberInStringForm, message->payload + t[6].start, deviceNumberInStringFormLen);
	deviceNumberInStringForm[deviceNumberInStringFormLen] = '\0';
	relayStateChanger->deviceNumber = (uint8_t)atoi(deviceNumberInStringForm);

	relayStateChanger->relayState = (!strncmp(message->payload + t[8].start, "true", sizeof("true") - 1 ? 1 : 0));
	return relayStateChanger;
}