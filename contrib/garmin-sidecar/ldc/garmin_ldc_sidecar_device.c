#include "garmin_ldc_sidecar_device.h"

#include <string.h>

static int sidecar_write_cb(const uint8_t *data, size_t size, void *userdata)
{
	garmin_ldc_sidecar_device_t *device = (garmin_ldc_sidecar_device_t *) userdata;
	if (device == 0 || device->callbacks.write == 0)
		return -1;
	return device->callbacks.write(data, size, device->callbacks.userdata);
}

static int sidecar_event_cb(const garmin_event_t *event, void *userdata)
{
	garmin_ldc_sidecar_device_t *device = (garmin_ldc_sidecar_device_t *) userdata;
	if (device == 0 || event == 0)
		return -1;

	switch (event->type) {
	case GARMIN_EVENT_SERVICE_REGISTERED:
		if (event->service == GARMIN_SERVICE_GFDI && event->status == GARMIN_OK)
			device->state = GARMIN_LDC_STATE_GFDI_READY;
		break;
	case GARMIN_EVENT_DOWNLOAD_READY:
		device->state = GARMIN_LDC_STATE_DOWNLOADING;
		if (device->callbacks.progress)
			device->callbacks.progress(0, event->total_size, device->callbacks.userdata);
		break;
	case GARMIN_EVENT_DOWNLOAD_PROGRESS:
		if (device->callbacks.progress)
			device->callbacks.progress(event->offset, event->total_size, device->callbacks.userdata);
		break;
	case GARMIN_EVENT_DIRECTORY_ENTRY:
		device->state = GARMIN_LDC_STATE_DIRECTORY_READY;
		device->directory_count = 0;
		if (event->payload != 0 && event->payload_size % 16 == 0) {
			for (size_t pos = 0; pos < event->payload_size && device->directory_count < 512; pos += 16) {
				garmin_directory_entry_t entry;
				if (garmin_directory_entry_parse(event->payload + pos, &entry) == GARMIN_OK) {
					device->directory_entries[device->directory_count++] = entry;
					if (device->callbacks.directory)
						device->callbacks.directory(&entry, device->callbacks.userdata);
				}
			}
		}
		break;
	case GARMIN_EVENT_FILE_COMPLETE:
		device->state = GARMIN_LDC_STATE_DIRECTORY_READY;
		if (device->callbacks.progress)
			device->callbacks.progress((unsigned int) event->payload_size, (unsigned int) event->payload_size, device->callbacks.userdata);
		if (device->callbacks.dive) {
			if (device->callbacks.dive(event->payload, event->payload_size, 0, 0, device->callbacks.userdata) != 0)
				return -1;
		}
		break;
	case GARMIN_EVENT_ERROR:
		device->state = GARMIN_LDC_STATE_ERROR;
		break;
	default:
		break;
	}

	return 0;
}

garmin_status_t garmin_ldc_sidecar_open(garmin_ldc_sidecar_device_t *device, const garmin_ldc_sidecar_callbacks_t *callbacks, uint8_t *download_buffer, size_t download_capacity)
{
	if (device == 0 || callbacks == 0 || callbacks->write == 0 || download_buffer == 0 || download_capacity == 0)
		return GARMIN_ERR_ARG;

	memset(device, 0, sizeof(*device));
	device->callbacks = *callbacks;
	device->download_buffer = download_buffer;
	device->download_capacity = download_capacity;
	device->state = GARMIN_LDC_STATE_OPEN;

	garmin_ldc_adapter_init(&device->adapter, sidecar_write_cb, sidecar_event_cb, device);
	garmin_ldc_adapter_set_download_buffer(&device->adapter, download_buffer, download_capacity);
	return GARMIN_OK;
}

garmin_status_t garmin_ldc_sidecar_start(garmin_ldc_sidecar_device_t *device)
{
	if (device == 0 || device->state == GARMIN_LDC_STATE_CLOSED)
		return GARMIN_ERR_ARG;
	garmin_status_t rc = garmin_ldc_adapter_start(&device->adapter);
	if (rc == GARMIN_OK)
		device->state = GARMIN_LDC_STATE_STARTED;
	return rc;
}

garmin_status_t garmin_ldc_sidecar_notify(garmin_ldc_sidecar_device_t *device, const uint8_t *data, size_t size)
{
	if (device == 0)
		return GARMIN_ERR_ARG;
	return garmin_ldc_adapter_notify(&device->adapter, data, size);
}

garmin_status_t garmin_ldc_sidecar_foreach(garmin_ldc_sidecar_device_t *device)
{
	if (device == 0)
		return GARMIN_ERR_ARG;
	if (device->state < GARMIN_LDC_STATE_GFDI_READY)
		return GARMIN_ERR_STATE;
	device->directory_count = 0;
	return garmin_ldc_adapter_request_directory(&device->adapter);
}

garmin_status_t garmin_ldc_sidecar_request_file(garmin_ldc_sidecar_device_t *device, const garmin_directory_entry_t *entry)
{
	if (device == 0 || entry == 0)
		return GARMIN_ERR_ARG;
	if (device->state < GARMIN_LDC_STATE_GFDI_READY)
		return GARMIN_ERR_STATE;
	return garmin_ldc_adapter_request_file(&device->adapter, entry);
}

void garmin_ldc_sidecar_close(garmin_ldc_sidecar_device_t *device)
{
	if (device == 0)
		return;
	memset(device, 0, sizeof(*device));
	device->state = GARMIN_LDC_STATE_CLOSED;
}
