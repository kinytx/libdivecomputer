#include "garmin_ldc_sidecar_adapter.h"

static garmin_status_t flush_writes(garmin_ldc_adapter_t *adapter)
{
	if (adapter == 0 || adapter->write_cb == 0)
		return GARMIN_ERR_ARG;

	garmin_packet_t packet;
	while (garmin_ble_core_take_write(&adapter->core, &packet)) {
		if (adapter->write_cb(packet.data, packet.size, adapter->userdata) != 0)
			return GARMIN_ERR_STATE;
	}
	return GARMIN_OK;
}

void garmin_ldc_adapter_init(garmin_ldc_adapter_t *adapter, garmin_ldc_write_cb_t write_cb, garmin_ldc_event_cb_t event_cb, void *userdata)
{
	if (adapter == 0)
		return;
	garmin_ble_core_init(&adapter->core, 1, 20);
	adapter->download_buffer = 0;
	adapter->download_capacity = 0;
	adapter->write_cb = write_cb;
	adapter->event_cb = event_cb;
	adapter->userdata = userdata;
}

void garmin_ldc_adapter_set_download_buffer(garmin_ldc_adapter_t *adapter, uint8_t *buffer, size_t capacity)
{
	if (adapter == 0)
		return;
	adapter->download_buffer = buffer;
	adapter->download_capacity = capacity;
	garmin_ble_core_set_download_buffer(&adapter->core, buffer, capacity);
}

garmin_status_t garmin_ldc_adapter_start(garmin_ldc_adapter_t *adapter)
{
	if (adapter == 0)
		return GARMIN_ERR_ARG;
	garmin_status_t rc = garmin_ble_core_start(&adapter->core);
	if (rc != GARMIN_OK)
		return rc;
	return flush_writes(adapter);
}

garmin_status_t garmin_ldc_adapter_notify(garmin_ldc_adapter_t *adapter, const uint8_t *data, size_t size)
{
	if (adapter == 0)
		return GARMIN_ERR_ARG;

	garmin_event_t event;
	garmin_status_t rc = garmin_ble_core_on_notification(&adapter->core, data, size, &event);
	if (rc != GARMIN_OK)
		return rc;

	rc = flush_writes(adapter);
	if (rc != GARMIN_OK)
		return rc;

	if (adapter->event_cb != 0 && event.type != GARMIN_EVENT_NONE) {
		if (adapter->event_cb(&event, adapter->userdata) != 0)
			return GARMIN_ERR_STATE;
	}

	return GARMIN_OK;
}

garmin_status_t garmin_ldc_adapter_send_gfdi(garmin_ldc_adapter_t *adapter, uint16_t message_id, const uint8_t *payload, size_t payload_size)
{
	if (adapter == 0)
		return GARMIN_ERR_ARG;

	garmin_status_t rc = garmin_ble_core_send_gfdi(&adapter->core, message_id, payload, payload_size);
	if (rc != GARMIN_OK)
		return rc;
	return flush_writes(adapter);
}

garmin_status_t garmin_ldc_adapter_request_filter(garmin_ldc_adapter_t *adapter)
{
	if (adapter == 0)
		return GARMIN_ERR_ARG;
	garmin_status_t rc = garmin_ble_core_request_filter(&adapter->core);
	if (rc != GARMIN_OK)
		return rc;
	return flush_writes(adapter);
}

garmin_status_t garmin_ldc_adapter_request_directory(garmin_ldc_adapter_t *adapter)
{
	if (adapter == 0)
		return GARMIN_ERR_ARG;
	garmin_status_t rc = garmin_ble_core_request_directory(&adapter->core);
	if (rc != GARMIN_OK)
		return rc;
	return flush_writes(adapter);
}

garmin_status_t garmin_ldc_adapter_request_file(garmin_ldc_adapter_t *adapter, const garmin_directory_entry_t *entry)
{
	if (adapter == 0)
		return GARMIN_ERR_ARG;
	garmin_status_t rc = garmin_ble_core_request_file(&adapter->core, entry);
	if (rc != GARMIN_OK)
		return rc;
	return flush_writes(adapter);
}
