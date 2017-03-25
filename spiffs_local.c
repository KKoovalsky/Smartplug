#include "espressif/esp_common.h"
#include "esp/uart.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "esp8266.h"

#include "fcntl.h"
#include "unistd.h"

#include "spiffs.h"
#include "esp_spiffs.h"
#include "spiffs_local.h"

TaskHandle_t xSPIFFSTask;
QueueHandle_t xSPIFFSQueue;

const char wifiSSIDStr[] = "SSID=";
const char wifiPasswordStr[] = "PASSWORD=";
const char plcPhyAddrStr[] = "PLC_PHY_ADDR=";

/* TODO:    1. Use O_RDWR in place of O_WRONLY (problems occured when using o_RDWR)
            2. Delete rubbish at the end of file, 
                which are leftovers from past config data, which was longer than now. */

/*  buf is a buffer where string str will be searched and any data after found str string 
 *  will be copied into value buffer.
 */
static int retrieveValue(char *buf, const char *str, char *value);

static void getFileContent(char *buf, int bufLen);

void spiffsTask(void *pvParameters)
{
    esp_spiffs_init();

    if (esp_spiffs_mount() != SPIFFS_OK)
    {
        printf("Error mounting SPIFFS\n");
        vTaskDelete(NULL);
    }

    for (;;)
    {
        PermConfData_s configData;
        xQueueReceive(xSPIFFSQueue, &configData, portMAX_DELAY);

        printf("Configuring...\n");

        if (configData.mode == SPIFFS_WRITE_WIFI_CONF)
        {
            //(Local strings = 26 + max SSID length = 32 + max wifi passwd = 64 + PLC Phy address = 8) = 130 (+ margin)
            char buffer[146];
            getFileContent(buffer, sizeof(buffer));

            int fd = open("data", O_WRONLY, 0);
            if (fd < 0)
            {
                printf("Error opening file\n");
                continue;
            }

            lseek(fd, 0, SEEK_SET);
            write(fd, wifiSSIDStr, sizeof(wifiSSIDStr) - 1);
            write(fd, configData.SSID, strlen(configData.SSID));
            write(fd, "\n", 1);

            write(fd, wifiPasswordStr, sizeof(wifiPasswordStr) - 1);
            write(fd, configData.password, strlen(configData.password));
            write(fd, "\n", 1);

            printf("%s %s\n", configData.SSID, configData.password);

            char plcPhyAddr[24];
            memset(plcPhyAddr, 0, sizeof(plcPhyAddr));
            int phyAddrLen = retrieveValue(buffer, plcPhyAddrStr, plcPhyAddr);

            write(fd, plcPhyAddrStr, sizeof(plcPhyAddrStr) - 1);
            write(fd, plcPhyAddr, phyAddrLen);

            close(fd);

            vPortFree(configData.SSID);
            vPortFree(configData.password);
        }
        else if (configData.mode == SPIFFS_WRITE_PLC_CONF)
        {
            char buffer[146];
            getFileContent(buffer, sizeof(buffer));

            int fd = open("data", O_WRONLY, 0);
            if (fd < 0)
            {
                printf("Error opening file\n");
                continue;
            }

            char wifiData[72];
            int ssidLen = retrieveValue(buffer, wifiSSIDStr, wifiData);

            lseek(fd, 0, SEEK_SET);
            write(fd, wifiSSIDStr, sizeof(wifiSSIDStr) - 1);
            write(fd, wifiData, ssidLen);

            int passwordLen = retrieveValue(buffer, wifiPasswordStr, wifiData);

            write(fd, wifiPasswordStr, sizeof(wifiPasswordStr) - 1);
            write(fd, wifiData, passwordLen);

            write(fd, plcPhyAddrStr, sizeof(plcPhyAddrStr) - 1);
            write(fd, configData.PLCPhyAddr, strlen(configData.PLCPhyAddr));
            write(fd, "\n", 1);

            close(fd);

            vPortFree(configData.PLCPhyAddr);
        }
        else if (configData.mode == SPIFFS_READ)
        {
        }
        else
        {
            printf("Unknown SPIFFS mode\n");
        }

        checkFileContent();
    }
}

void checkFileContent()
{
    char buffer[146];
    memset(buffer, 0, sizeof(buffer));

    int fd = open("data", O_RDONLY, 0);
    if (fd < 0)
    {
        printf("Error opening file\n");
        return;
    }

    lseek(fd, 0, SEEK_SET);
    read(fd, buffer, sizeof(buffer));
    printf("%s\n", buffer);

    close(fd);
}

static int retrieveValue(char *buf, const char *str, char *value)
{
    char *p = strstr(buf, str);
    p += strlen(str);
    char *pEnd = strchr(p, '\n');

    int strLen = pEnd - p;

    memcpy(value, p, strLen);
    value[strLen] = '\n';

    return strLen + 1;
}

static void getFileContent(char *buf, int bufLen)
{
    memset(buf, 0, bufLen);

    int fd = open("data", O_RDONLY, 0);
    if (fd < 0)
    {
        printf("Error opening file\n");
        return;
    }

    lseek(fd, 0, SEEK_SET);
    read(fd, buf, bufLen);

    close(fd);
}
