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
#include "cloud.h"
#include "sntp_sync.h"

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

	xPLCSendSemaphore = xSemaphoreCreateMutex();

	initFileSystem();
	initDeviceByMode();

    xTaskCreate(blinkTask, "Blink", 256, NULL, 2, NULL);
	xTaskCreate(initPlcTask, "PLC Init", 256, NULL, 3, NULL);
    xTaskCreate(plcTaskRcv, "PLC Rcv", 256, NULL, 3, &xPLCTaskRcv);
	xTaskCreate(plcTaskSend, "PLC Send", 256, NULL, 3, &xPLCTaskSend);
}