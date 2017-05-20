#include <espressif/esp_common.h>
#include <esp/uart.h>

#include "sntp_sync.h"
#include "FreeRTOS.h"
#include "task.h"

#include <lwip/err.h>
#include <lwip/sockets.h>
#include <lwip/sys.h>
#include <lwip/netdb.h>
#include <lwip/dns.h>

#include "sntp/sntp.h"
#include <time.h>

void sntpInit()
{
	char *servers[] = {"0.pl.pool.ntp.org", "1.pl.pool.ntp.org", "2.pl.pool.ntp.org", "3.pl.pool.ntp.org"};
	printf("Starting SNTP...\n\r");

	/* SNTP will request an update each 5 minutes */
	sntp_set_update_delay(SNTP_REQUEST_DELAY);

	/* Set GMT+1 zone, daylight savings off */
	const struct timezone tz = {1*60, 0};
	/* SNTP initialization */
	sntp_initialize(&tz);
	/* Servers must be configured right after initialization */
	sntp_set_servers(servers, sizeof(servers) / sizeof(char*));
}

void sntpTestTask(void *pvParameters)
{
	char *servers[] = {"0.pl.pool.ntp.org", "1.pl.pool.ntp.org", "2.pl.pool.ntp.org", "3.pl.pool.ntp.org"};
	(void)(pvParameters);

	sdk_wifi_station_connect();

	/* Wait until we have joined AP and are assigned an IP */
	while (sdk_wifi_station_get_connect_status() != STATION_GOT_IP) {
		vTaskDelay(pdMS_TO_TICKS(1000));
		printf("Waiting for connect\n");
	}

	/* Start SNTP */
	printf("Starting SNTP...\n");
	/* SNTP will request an update each 5 minutes */
	sntp_set_update_delay(5*60000);
	/* Set GMT+1 zone, daylight savings off */
	const struct timezone tz = {1*60, 0};
	/* SNTP initialization */
	sntp_initialize(&tz);
	/* Servers must be configured right after initialization */
	sntp_set_servers(servers, sizeof(servers) / sizeof(char*));
	printf("DONE!\n");

	/* Print date and time each 5 seconds */
	while(1) {
		vTaskDelay(pdMS_TO_TICKS(5000));
		time_t ts = time(NULL);
		printf("TIME: %s", ctime(&ts));
	}
}
