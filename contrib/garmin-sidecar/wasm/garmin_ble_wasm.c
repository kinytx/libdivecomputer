#include "../c/garmin_ble_core.h"

#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define WASM_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define WASM_EXPORT
#endif

static garmin_ble_core_t g_core;
static uint8_t g_download_buffer[GARMIN_DOWNLOAD_MAX];
static garmin_packet_t g_last_write;
static garmin_event_t g_last_event;
static uint8_t g_last_payload[GARMIN_PACKET_MAX];

WASM_EXPORT void garmin_wasm_init(int prefer_reliable, unsigned int max_packet_size)
{
	garmin_ble_core_init(&g_core, prefer_reliable, max_packet_size);
	garmin_ble_core_set_download_buffer(&g_core, g_download_buffer, sizeof(g_download_buffer));
	memset(&g_last_write, 0, sizeof(g_last_write));
	memset(&g_last_event, 0, sizeof(g_last_event));
}

WASM_EXPORT int garmin_wasm_start(void)
{
	return garmin_ble_core_start(&g_core);
}

WASM_EXPORT int garmin_wasm_on_notification(const uint8_t *data, unsigned int size)
{
	garmin_event_t event;
	garmin_status_t rc = garmin_ble_core_on_notification(&g_core, data, size, &event);
	if (rc != GARMIN_OK) {
		memset(&g_last_event, 0, sizeof(g_last_event));
		g_last_event.type = GARMIN_EVENT_ERROR;
		g_last_event.status = rc;
		return rc;
	}

	g_last_event = event;
	if (event.payload != 0 && event.payload_size <= sizeof(g_last_payload)) {
		memcpy(g_last_payload, event.payload, event.payload_size);
		g_last_event.payload = g_last_payload;
	}
	return GARMIN_OK;
}

WASM_EXPORT int garmin_wasm_send_gfdi(unsigned int message_id, const uint8_t *payload, unsigned int payload_size)
{
	return garmin_ble_core_send_gfdi(&g_core, (uint16_t) message_id, payload, payload_size);
}

WASM_EXPORT int garmin_wasm_request_filter(void)
{
	return garmin_ble_core_request_filter(&g_core);
}

WASM_EXPORT int garmin_wasm_request_directory(void)
{
	return garmin_ble_core_request_directory(&g_core);
}

WASM_EXPORT int garmin_wasm_request_file(unsigned int file_index, unsigned int data_type, unsigned int sub_type, unsigned int file_size)
{
	garmin_directory_entry_t entry;
	memset(&entry, 0, sizeof(entry));
	entry.file_index = (uint16_t) file_index;
	entry.data_type = (uint8_t) data_type;
	entry.sub_type = (uint8_t) sub_type;
	entry.file_size = file_size;
	return garmin_ble_core_request_file(&g_core, &entry);
}

WASM_EXPORT int garmin_wasm_take_write(void)
{
	return garmin_ble_core_take_write(&g_core, &g_last_write);
}

WASM_EXPORT const uint8_t *garmin_wasm_write_ptr(void)
{
	return g_last_write.data;
}

WASM_EXPORT unsigned int garmin_wasm_write_size(void)
{
	return (unsigned int) g_last_write.size;
}

WASM_EXPORT int garmin_wasm_event_type(void)
{
	return g_last_event.type;
}

WASM_EXPORT unsigned int garmin_wasm_event_service(void)
{
	return g_last_event.service;
}

WASM_EXPORT unsigned int garmin_wasm_event_message_id(void)
{
	return g_last_event.message_id;
}

WASM_EXPORT const uint8_t *garmin_wasm_event_payload_ptr(void)
{
	return g_last_event.payload;
}

WASM_EXPORT unsigned int garmin_wasm_event_payload_size(void)
{
	return (unsigned int) g_last_event.payload_size;
}

WASM_EXPORT unsigned int garmin_wasm_event_offset(void)
{
	return g_last_event.offset;
}

WASM_EXPORT unsigned int garmin_wasm_event_total_size(void)
{
	return g_last_event.total_size;
}

WASM_EXPORT unsigned int garmin_wasm_event_file_index(void)
{
	return g_last_event.directory_entry.file_index;
}

WASM_EXPORT unsigned int garmin_wasm_event_file_data_type(void)
{
	return g_last_event.directory_entry.data_type;
}

WASM_EXPORT unsigned int garmin_wasm_event_file_sub_type(void)
{
	return g_last_event.directory_entry.sub_type;
}

WASM_EXPORT unsigned int garmin_wasm_event_file_size(void)
{
	return g_last_event.directory_entry.file_size;
}
