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
#include "http_server.h"
#include "system.h"

const char clientStr[] = "CLIENT";
const char brokerStr[] = "BROKER";

TaskHandle_t xSPIFFSTask;
QueueHandle_t xSPIFFSQueue;

/* TODO:    1. Use O_RDWR in place of O_WRONLY (problems occured when using o_RDWR)
            2. Delete rubbish at the end of file, 
                which are leftovers from past config data, which was longer than now. 
            3. Create one file for configuration data instead of wifi.conf and plc.conf*/

void spiffsTask(void *pvParameters)
{
    esp_spiffs_init();

    if (esp_spiffs_mount() != SPIFFS_OK)
    {
        printf("Error mounting SPIFFS\n");
        vTaskDelete(NULL);
    }

    // Read file which contains mode of operation.
    int fd = open("mode.conf", O_RDONLY, 0);
    if (fd < 0)
    {
        printf("Error opening file mode.conf\n");
        vTaskDelete(NULL);
    }

    char buffer[8];
    lseek(fd, 0, SEEK_SET);
    read(fd, buffer, sizeof(buffer));
    if (!strncmp(buffer, clientStr, sizeof(clientStr)))
    {
        startClientMode();
    }
    else if (!strncmp(buffer, brokerStr, sizeof(brokerStr)))
    {
        startBrokerMode();
    }
    else // If its first run of this device, then start HTTP server to get configuration
    {
        printf("First run of the device\n");
        //Disable auto connect
        setAP_STA();
        xTaskCreate(httpd_task, "HTTP Daemon", 128, NULL, 2, &xHTTPServerTask);
    }

    for (;;)
    {
        PermConfData_s configData;
        xQueueReceive(xSPIFFSQueue, &configData, portMAX_DELAY);

        printf("Configuring...\n");

        // Set device plugged name.
        int fd = open("id.conf", O_WRONLY, 0);
        if (fd < 0)
        {
            printf("Error opening file wifi.conf\n");
            continue;
        }
        lseek(fd, 0, SEEK_SET);
        write(fd, configData.devicePlugged, strlen(configData.devicePlugged));
        write(fd, "\n", 1);
        close(fd);

        if (configData.mode == SPIFFS_WRITE_WIFI_CONF)
        {
            int fd = open("wifi.conf", O_WRONLY, 0);
            if (fd < 0)
            {
                printf("Error opening file wifi.conf\n");
                continue;
            }

            lseek(fd, 0, SEEK_SET);
            write(fd, configData.SSID, configData.SSIDLen);
            write(fd, "\n", 1);
            write(fd, configData.password, configData.passwordLen);
            write(fd, "\n", 1);

            printf("%s %s\n", configData.SSID, configData.password);

            close(fd);

            vPortFree(configData.SSID);
            vPortFree(configData.password);
        }
        else if (configData.mode == SPIFFS_WRITE_PLC_CONF)
        {
            int fd = open("plc.conf", O_WRONLY, 0);
            if (fd < 0)
            {
                printf("Error opening file plc.conf\n");
                continue;
            }

            lseek(fd, 0, SEEK_SET);

            write(fd, configData.PLCPhyAddr, configData.PLCPhyAddrLen);
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