#include "system.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp8266.h"
#include "espressif/esp_common.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include <dhcpserver.h>
#include "spiffs_local.h"
#include "http_server.h"

QueueHandle_t xConnectWhileConfigQueue;
TaskHandle_t xConnectWhileConfigTask;

void startBrokerMode()
{
}

void startClientMode()
{
}

void setSoftAP()
{
    sdk_wifi_set_opmode(SOFTAP_MODE);
    struct ip_info ap_ip;
    IP4_ADDR(&ap_ip.ip, 192, 168, 1, 1);
    IP4_ADDR(&ap_ip.gw, 0, 0, 0, 0);
    IP4_ADDR(&ap_ip.netmask, 255, 255, 255, 0);
    sdk_wifi_set_ip_info(1, &ap_ip);

    struct sdk_softap_config ap_config = {
        .ssid = WIFI_SSID,
        .ssid_hidden = 0,
        .channel = 3,
        .ssid_len = strlen(WIFI_SSID),
        .authmode = AUTH_WPA_WPA2_PSK,
        .password = WIFI_PASS,
        .max_connection = 3,
        .beacon_interval = 100,
    };
    sdk_wifi_softap_set_config(&ap_config);

    ip_addr_t first_client_ip;
    IP4_ADDR(&first_client_ip, 192, 168, 1, 2);
    dhcpserver_start(&first_client_ip, 3);
}

void setAP_STA()
{
    sdk_wifi_set_opmode(STATIONAP_MODE);
    struct ip_info ap_ip;
    IP4_ADDR(&ap_ip.ip, 192, 168, 1, 1);
    IP4_ADDR(&ap_ip.gw, 0, 0, 0, 0);
    IP4_ADDR(&ap_ip.netmask, 255, 255, 255, 0);
    sdk_wifi_set_ip_info(1, &ap_ip);

    struct sdk_softap_config ap_config = {
        .ssid = WIFI_SSID,
        .ssid_hidden = 0,
        .channel = 3,
        .ssid_len = strlen(WIFI_SSID),
        .authmode = AUTH_WPA_WPA2_PSK,
        .password = WIFI_PASS,
        .max_connection = 3,
        .beacon_interval = 100,
    };
    sdk_wifi_softap_set_config(&ap_config);

    ip_addr_t first_client_ip;
    IP4_ADDR(&first_client_ip, 192, 168, 1, 2);
    dhcpserver_start(&first_client_ip, 3);
}

void connectToStation(char *SSID, char *password, int SSIDLen, int passwordLen)
{
    struct sdk_station_config config;

    memcpy(config.ssid, SSID, SSIDLen);
    memcpy(config.password, password, passwordLen);

    config.ssid[SSIDLen] = '\0';
    config.password[passwordLen] = '\0';

    sdk_wifi_station_set_config(&config);
    sdk_wifi_station_connect();
}

void connectWhileConfigTask(void *pvParameters)
{
    for (;;)
    {
        PermConfData_s configData;
        xQueueReceive(xConnectWhileConfigQueue, &configData, portMAX_DELAY);

        if (configData.mode == SPIFFS_WRITE_WIFI_CONF)
        {
            connectToStation(configData.SSID, configData.password, configData.SSIDLen, configData.passwordLen);
            bool abort = false;
            int retries = MAX_RETRIES;
            unsigned int status;
            while (1)
            {
                vTaskDelay(pdMS_TO_TICKS(1000));
                status = (unsigned int)sdk_wifi_station_get_connect_status();
                if (status == STATION_WRONG_PASSWORD)
                {
                    printf("WiFi: wrong password\n\r");
                    abort = true;
                    break;
                }
                else if (status == STATION_NO_AP_FOUND)
                {
                    printf("WiFi: AP not found\n\r");
                    abort = true;
                    break;
                }
                else if (status == STATION_CONNECT_FAIL)
                {
                    printf("WiFi: connection failed\r\n");
                    abort = true;
                    break;
                }
                else if (status == STATION_CONNECTING)
                {
                    printf("WiFi: connecting...\r\n");
                    if(!retries)
                    {
                        printf("WiFi: could not connect to device.\r\n");
                        abort = true;
                        break;
                    }
                    --retries;
                    continue;
                }
                else if (status == STATION_IDLE)
                {
                    printf("WiFi: idle mode\r\n");
                    abort = true;
                    break;
                }
                else
                {
                    printf("WiFi: succesfully connected to AP\r\n");
                    startBrokerMode();
                    xQueueSend(xSPIFFSQueue, &configData, 0);
                    break;
                }
            }

            if (abort)
            {
                sdk_wifi_station_disconnect();
                //sdk_wifi_station_set_auto_connect(0);
                vPortFree(configData.SSID);
                vPortFree(configData.password);
                vPortFree(configData.devicePlugged);
            }

            sendWsResponse(wifiJsonStrings[status], wifiJsonStringsLen[status]);
        }
        else if (configData.mode == SPIFFS_WRITE_PLC_CONF)
        {
            xQueueSend(xSPIFFSQueue, &configData, 0);
        }
        else
        {
            printf("Unknown spiffs mode\n");
        }
    }
}