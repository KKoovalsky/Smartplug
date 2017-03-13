#include <stdio.h>
#include <stdlib.h>
#include "i2c.h"
#include "http_server.h"
#include "espressif/esp_common.h"
#include "esp/uart.h"
#include "FreeRTOS.h"
#include "task.h"
#include "esp8266.h"

#include "plc.h"

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

void plcTask(void *pvParameters)
{
    vTaskDelay(pdMS_TO_TICKS(10 * 1000));
    printf("Initializing PLC at address %d\n", PLC_WRITE_ADDR);
    initPLCdevice(120);
    for (;;)
    {

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void user_init(void)
{
    uart_set_baud(0, 74880);
    i2c_init(SCL_PIN, SDA_PIN);

    struct sdk_station_config config = {
        .ssid = WIFI_SSID,
        .password = WIFI_PASS,
    };

        /* required to call wifi_set_opmode before station_set_config */
    sdk_wifi_set_opmode(STATION_MODE);
    sdk_wifi_station_set_config(&config);
    sdk_wifi_station_connect();


    printf("SDK version:%s\n", sdk_system_get_sdk_version());
    xTaskCreate(blinkTask, "blinkTask", 256, NULL, 2, NULL);
    xTaskCreate(plcTask, "i2cTestTask", 256, NULL, 3, NULL);
    xTaskCreate(httpd_task, "HTTP Daemon", 128, NULL, 2, NULL);
}