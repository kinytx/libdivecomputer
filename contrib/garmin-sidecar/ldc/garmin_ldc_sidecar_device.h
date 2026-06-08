#ifndef GARMIN_LDC_SIDECAR_DEVICE_H
#define GARMIN_LDC_SIDECAR_DEVICE_H

#include "garmin_ldc_sidecar_adapter.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*garmin_ldc_dive_cb_t)(const uint8_t *data, size_t size, const uint8_t *fingerprint, size_t fingerprint_size, void *userdata);
typedef void (*garmin_ldc_progress_cb_t)(unsigned int current, unsigned int maximum, void *userdata);
typedef void (*garmin_ldc_directory_cb_t)(const garmin_directory_entry_t *entry, void *userdata);

typedef struct garmin_ldc_sidecar_callbacks_t {
	garmin_ldc_write_cb_t write;
	garmin_ldc_dive_cb_t dive;
	garmin_ldc_progress_cb_t progress;
	garmin_ldc_directory_cb_t directory;
	void *userdata;
} garmin_ldc_sidecar_callbacks_t;

typedef enum garmin_ldc_sidecar_state_t {
	GARMIN_LDC_STATE_CLOSED = 0,
	GARMIN_LDC_STATE_OPEN,
	GARMIN_LDC_STATE_STARTED,
	GARMIN_LDC_STATE_GFDI_READY,
	GARMIN_LDC_STATE_DIRECTORY_READY,
	GARMIN_LDC_STATE_DOWNLOADING,
	GARMIN_LDC_STATE_ERROR
} garmin_ldc_sidecar_state_t;

typedef struct garmin_ldc_sidecar_device_t {
	garmin_ldc_adapter_t adapter;
	garmin_ldc_sidecar_callbacks_t callbacks;
	garmin_ldc_sidecar_state_t state;
	garmin_directory_entry_t directory_entries[512];
	size_t directory_count;
	uint8_t *download_buffer;
	size_t download_capacity;
} garmin_ldc_sidecar_device_t;

garmin_status_t garmin_ldc_sidecar_open(garmin_ldc_sidecar_device_t *device, const garmin_ldc_sidecar_callbacks_t *callbacks, uint8_t *download_buffer, size_t download_capacity);
garmin_status_t garmin_ldc_sidecar_start(garmin_ldc_sidecar_device_t *device);
garmin_status_t garmin_ldc_sidecar_notify(garmin_ldc_sidecar_device_t *device, const uint8_t *data, size_t size);
garmin_status_t garmin_ldc_sidecar_foreach(garmin_ldc_sidecar_device_t *device);
garmin_status_t garmin_ldc_sidecar_request_file(garmin_ldc_sidecar_device_t *device, const garmin_directory_entry_t *entry);
void garmin_ldc_sidecar_close(garmin_ldc_sidecar_device_t *device);

#ifdef __cplusplus
}
#endif

#endif
