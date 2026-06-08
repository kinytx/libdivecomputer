#include "garmin_ldc_sidecar_adapter.h"

#include <assert.h>
#include <stdio.h>

typedef struct capture_t {
	unsigned int writes;
	unsigned int events;
} capture_t;

static int write_cb(const uint8_t *data, size_t size, void *userdata)
{
	capture_t *capture = (capture_t *) userdata;
	capture->writes++;
	for (size_t i = 0; i < size; i++)
		printf("%s%02x", i ? " " : "", data[i]);
	printf("\n");
	return 0;
}

static int event_cb(const garmin_event_t *event, void *userdata)
{
	capture_t *capture = (capture_t *) userdata;
	capture->events++;
	printf("event=%d service=%u message=%u\n", event->type, event->service, event->message_id);
	return 0;
}

int main(void)
{
	capture_t capture = {0};
	garmin_ldc_adapter_t adapter;
	garmin_ldc_adapter_init(&adapter, write_cb, event_cb, &capture);
	assert(garmin_ldc_adapter_start(&adapter) == GARMIN_OK);
	assert(capture.writes == 1);

	uint8_t close_resp[12];
	garmin_ml_close_all(GARMIN_CLIENT_ID, close_resp);
	close_resp[1] = 6;
	assert(garmin_ldc_adapter_notify(&adapter, close_resp, sizeof(close_resp)) == GARMIN_OK);
	assert(capture.writes == 2);
	assert(capture.events == 1);

	return 0;
}
