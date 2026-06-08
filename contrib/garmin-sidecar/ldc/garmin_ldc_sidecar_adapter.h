#ifndef GARMIN_LDC_SIDECAR_ADAPTER_H
#define GARMIN_LDC_SIDECAR_ADAPTER_H

#include "../c/garmin_ble_core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*garmin_ldc_write_cb_t)(const uint8_t *data, size_t size, void *userdata);
typedef int (*garmin_ldc_event_cb_t)(const garmin_event_t *event, void *userdata);

typedef struct garmin_ldc_adapter_t {
	garmin_ble_core_t core;
	uint8_t *download_buffer;
	size_t download_capacity;
	garmin_ldc_write_cb_t write_cb;
	garmin_ldc_event_cb_t event_cb;
	void *userdata;
} garmin_ldc_adapter_t;

void garmin_ldc_adapter_init(garmin_ldc_adapter_t *adapter, garmin_ldc_write_cb_t write_cb, garmin_ldc_event_cb_t event_cb, void *userdata);
void garmin_ldc_adapter_set_download_buffer(garmin_ldc_adapter_t *adapter, uint8_t *buffer, size_t capacity);
garmin_status_t garmin_ldc_adapter_start(garmin_ldc_adapter_t *adapter);
garmin_status_t garmin_ldc_adapter_notify(garmin_ldc_adapter_t *adapter, const uint8_t *data, size_t size);
garmin_status_t garmin_ldc_adapter_send_gfdi(garmin_ldc_adapter_t *adapter, uint16_t message_id, const uint8_t *payload, size_t payload_size);
garmin_status_t garmin_ldc_adapter_request_filter(garmin_ldc_adapter_t *adapter);
garmin_status_t garmin_ldc_adapter_request_directory(garmin_ldc_adapter_t *adapter);
garmin_status_t garmin_ldc_adapter_request_file(garmin_ldc_adapter_t *adapter, const garmin_directory_entry_t *entry);

#ifdef __cplusplus
}
#endif

#endif
