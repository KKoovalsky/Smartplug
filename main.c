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
#include <dhcpserver.h>

#include "plc.h"
#include "spiffs_local.h"

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
    dhcpserver_start(&first_client_ip, 4);

    xSPIFFSQueue = xQueueCreate(1, sizeof(PermConfData_s));

    xTaskCreate(blinkTask, "Blink", 256, NULL, 2, NULL);
    xTaskCreate(plcTask, "PLC", 256, NULL, 3, &xPLCTask);
    xTaskCreate(httpd_task, "HTTP Daemon", 128, NULL, 2, NULL);
    xTaskCreate(spiffsTask, "SPIFFS", 512, NULL, 2, NULL);
}