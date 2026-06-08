#include "garmin_ble_core.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void print_packet(const garmin_packet_t *packet)
{
	for (size_t i = 0; i < packet->size; i++)
		printf("%s%02x", i ? " " : "", packet->data[i]);
	printf("\n");
	fflush(stdout);
}

static void notify_gfdi_ml(garmin_ble_core_t *core, const uint8_t *frame, size_t frame_size, garmin_event_t *event)
{
	uint8_t encoded[256];
	size_t encoded_size = 0;
	assert(garmin_cobs_encode(frame, frame_size, encoded, sizeof(encoded), &encoded_size) == GARMIN_OK);
	uint8_t packet[300];
	packet[0] = core->gfdi_handle;
	memcpy(packet + 1, encoded, encoded_size);
	assert(garmin_ble_core_on_notification(core, packet, encoded_size + 1, event) == GARMIN_OK);
}

int main(void)
{
	const uint8_t payload[] = {0x02, 0x00, 0x88, 0x13, 'a', 'b', 'c', 0x00, 'd'};
	uint8_t encoded[128], decoded[128], frame[128];
	size_t encoded_size = 0, decoded_size = 0, frame_size = 0;

	assert(garmin_cobs_encode(payload, sizeof(payload), encoded, sizeof(encoded), &encoded_size) == GARMIN_OK);
	assert(garmin_cobs_decode_frame(encoded + 1, encoded_size - 2, decoded, sizeof(decoded), &decoded_size) == GARMIN_OK);
	assert(decoded_size == sizeof(payload));
	assert(memcmp(decoded, payload, sizeof(payload)) == 0);

	assert(garmin_gfdi_build(5002, (const uint8_t *) "hello", 5, frame, sizeof(frame), &frame_size) == GARMIN_OK);
	uint16_t message_id = 0;
	const uint8_t *body = 0;
	size_t body_size = 0;
	assert(garmin_gfdi_parse(frame, frame_size, &message_id, &body, &body_size) == GARMIN_OK);
	assert(message_id == 5002);
	assert(body_size == 5);
	assert(memcmp(body, "hello", 5) == 0);

	garmin_ble_core_t core;
	static uint8_t download_buffer[4096];
	garmin_ble_core_init(&core, 1, 20);
	garmin_ble_core_set_download_buffer(&core, download_buffer, sizeof(download_buffer));
	assert(garmin_ble_core_start(&core) == GARMIN_OK);
	garmin_packet_t packet;
	assert(garmin_ble_core_take_write(&core, &packet));
	print_packet(&packet);
	assert(packet.size == 12);
	assert(packet.data[0] == 0 && packet.data[1] == 5);

	uint8_t close_resp[12];
	garmin_ml_close_all(GARMIN_CLIENT_ID, close_resp);
	close_resp[1] = 6;
	garmin_event_t event;
	assert(garmin_ble_core_on_notification(&core, close_resp, sizeof(close_resp), &event) == GARMIN_OK);
	assert(event.type == GARMIN_EVENT_CLOSED_ALL);
	assert(garmin_ble_core_take_write(&core, &packet));
	print_packet(&packet);
	assert(packet.size == 13);

	uint8_t reg_resp[15] = {0};
	reg_resp[1] = 1;
	for (unsigned int i = 0; i < 8; i++)
		reg_resp[2 + i] = (uint8_t) ((GARMIN_CLIENT_ID >> (i * 8)) & 0xFF);
	reg_resp[10] = GARMIN_SERVICE_GFDI;
	reg_resp[12] = 0;
	reg_resp[13] = 2;
	reg_resp[14] = 0;
	assert(garmin_ble_core_on_notification(&core, reg_resp, sizeof(reg_resp), &event) == GARMIN_OK);
	assert(event.type == GARMIN_EVENT_SERVICE_REGISTERED);
	assert(core.gfdi_open);

	assert(garmin_ble_core_request_directory(&core) == GARMIN_OK);
	assert(garmin_ble_core_take_write(&core, &packet));
	print_packet(&packet);
	assert(packet.data[0] == 2);

	uint8_t ready_payload[8];
	ready_payload[0] = (uint8_t) (GARMIN_GFDI_DOWNLOAD_REQUEST & 0xFF);
	ready_payload[1] = (uint8_t) (GARMIN_GFDI_DOWNLOAD_REQUEST >> 8);
	ready_payload[2] = 0; /* ACK */
	ready_payload[3] = 0; /* download OK */
	ready_payload[4] = 16;
	ready_payload[5] = 0;
	ready_payload[6] = 0;
	ready_payload[7] = 0;
	assert(garmin_gfdi_build(GARMIN_GFDI_RESPONSE, ready_payload, sizeof(ready_payload), frame, sizeof(frame), &frame_size) == GARMIN_OK);
	notify_gfdi_ml(&core, frame, frame_size, &event);
	assert(event.type == GARMIN_EVENT_DOWNLOAD_READY);
	assert(event.total_size == 16);

	uint8_t dir_entry[16] = {
		0x34, 0x12, 128, 4,
		0x78, 0x56, 0x00, 0x00,
		0x04, 0x00, 0x00, 0x00,
		0x11, 0x22, 0x33, 0x44
	};
	uint8_t transfer_payload[23];
	transfer_payload[0] = 0;
	transfer_payload[1] = 0;
	transfer_payload[2] = 0;
	transfer_payload[3] = 0;
	transfer_payload[4] = 0;
	transfer_payload[5] = 0;
	transfer_payload[6] = 0;
	memcpy(transfer_payload + 7, dir_entry, sizeof(dir_entry));
	assert(garmin_gfdi_build(GARMIN_GFDI_FILE_TRANSFER_DATA, transfer_payload, sizeof(transfer_payload), frame, sizeof(frame), &frame_size) == GARMIN_OK);
	notify_gfdi_ml(&core, frame, frame_size, &event);
	assert(event.type == GARMIN_EVENT_DIRECTORY_ENTRY);
	assert(event.directory_entry.file_index == 0x1234);
	assert(event.directory_entry.data_type == 128);
	assert(event.directory_entry.sub_type == 4);
	assert(garmin_ble_core_take_write(&core, &packet));
	print_packet(&packet);

	assert(garmin_ble_core_send_gfdi(&core, 5002, (const uint8_t *) "hello", 5) == GARMIN_OK);
	assert(garmin_ble_core_take_write(&core, &packet));
	print_packet(&packet);
	assert(packet.data[0] == 2);

	return 0;
}
