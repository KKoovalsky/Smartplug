#ifndef SNTP_SYNC
#define SNTP_SYNC

#define SNTP_REQUEST_DELAY 5 * 60 * 1000

void sntpInit();

void sntpTestTask(void *pvParameters);

#endif