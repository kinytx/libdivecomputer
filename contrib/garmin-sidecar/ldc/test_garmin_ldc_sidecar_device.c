#include "garmin_ldc_sidecar_device.h"

#include <assert.h>
#include <stdio.h>

typedef struct capture_t {
	unsigned int writes;
	unsigned int directories;
	unsigned int dives;
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

static int dive_cb(const uint8_t *data, size_t size, const uint8_t *fingerprint, size_t fingerprint_size, void *userdata)
{
	capture_t *capture = (capture_t *) userdata;
	(void) data;
	(void) fingerprint;
	(void) fingerprint_size;
	capture->dives++;
	printf("dive bytes=%u\n", (unsigned int) size);
	return 0;
}

static void progress_cb(unsigned int current, unsigned int maximum, void *userdata)
{
	(void) userdata;
	printf("progress=%u/%u\n", current, maximum);
}

static void directory_cb(const garmin_directory_entry_t *entry, void *userdata)
{
	capture_t *capture = (capture_t *) userdata;
	capture->directories++;
	printf("dir index=%u type=%u/%u size=%u\n", entry->file_index, entry->data_type, entry->sub_type, entry->file_size);
}

int main(void)
{
	static uint8_t download_buffer[4096];
	capture_t capture = {0};
	garmin_ldc_sidecar_callbacks_t callbacks = {
		write_cb,
		dive_cb,
		progress_cb,
		directory_cb,
		&capture
	};

	garmin_ldc_sidecar_device_t device;
	assert(garmin_ldc_sidecar_open(&device, &callbacks, download_buffer, sizeof(download_buffer)) == GARMIN_OK);
	assert(garmin_ldc_sidecar_start(&device) == GARMIN_OK);
	assert(capture.writes == 1);

	uint8_t close_resp[12];
	garmin_ml_close_all(GARMIN_CLIENT_ID, close_resp);
	close_resp[1] = 6;
	assert(garmin_ldc_sidecar_notify(&device, close_resp, sizeof(close_resp)) == GARMIN_OK);
	assert(capture.writes == 2);

	uint8_t reg_resp[15] = {0};
	reg_resp[1] = 1;
	for (unsigned int i = 0; i < 8; i++)
		reg_resp[2 + i] = (uint8_t) ((GARMIN_CLIENT_ID >> (i * 8)) & 0xFF);
	reg_resp[10] = GARMIN_SERVICE_GFDI;
	reg_resp[12] = 0;
	reg_resp[13] = 2;
	reg_resp[14] = 0;
	assert(garmin_ldc_sidecar_notify(&device, reg_resp, sizeof(reg_resp)) == GARMIN_OK);
	assert(device.state == GARMIN_LDC_STATE_GFDI_READY);

	unsigned int writes_before_foreach = capture.writes;
	assert(garmin_ldc_sidecar_foreach(&device) == GARMIN_OK);
	assert(capture.writes > writes_before_foreach);

	garmin_ldc_sidecar_close(&device);
	assert(device.state == GARMIN_LDC_STATE_CLOSED);
	return 0;
}
