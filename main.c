#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "i2c.h"
#include "http_server.h"
#include "espressif/esp_common.h"
#include "esp/uart.h"
#include "FreeRTOS.h"
#include "task.h"
#include "esp8266.h"

#include "plc.h"
#include "spiffs_local.h"
#include "system.h"

#define SCL_PIN 5
#define SDA_PIN 4

const int gpio = 2;

/*  This task uses the high level GPIO API (esp_gpio.h) to blink an LED.
 *  Used for debug purposes.
 */
void blinkTask(void *pvParameters)
{
    gpio_enable(gpio, GPIO_OUTPUT);
    while (1)
    {
        gpio_write(gpio, 1);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        gpio_write(gpio, 0);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

void user_init(void)
{
    uart_set_baud(0, 74880);
    printf("SDK version:%s\n", sdk_system_get_sdk_version());

    i2c_init(SCL_PIN, SDA_PIN);

    sdk_wifi_station_set_auto_connect(0);

    xSPIFFSQueue = xQueueCreate(1, sizeof(PermConfData_s));
    xConnectWhileConfigQueue = xQueueCreate(1, sizeof(PermConfData_s));

    xTaskCreate(blinkTask, "Blink", 256, NULL, 2, NULL);
    xTaskCreate(plcTask, "PLC", 256, NULL, 3, &xPLCTask);
    xTaskCreate(spiffsTask, "SPIFFS", 512, NULL, 2, NULL);
    xTaskCreate(connectWhileConfigTask, "configConnect", 512, NULL, 2, &xConnectWhileConfigTask);

#ifdef PLC_TX_TEST
    if (xTaskCreate(plcTestTxTask, "PLC_TX", 256, NULL, 2, NULL) == pdPASS)
        printf("YEP\n\r");
    else
        printf("NOPE\n\r");
#endif
}