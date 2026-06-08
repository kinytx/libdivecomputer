#include "garmin_ble_core.h"

#include <string.h>

#define GARMIN_REQ_REGISTER_ML 0U
#define GARMIN_RESP_REGISTER_ML 1U
#define GARMIN_REQ_CLOSE_ALL 5U
#define GARMIN_RESP_CLOSE_ALL 6U

static const uint16_t crc_nibbles[16] = {
	0x0000, 0xCC01, 0xD801, 0x1400, 0xF001, 0x3C00, 0x2800, 0xE401,
	0xA001, 0x6C00, 0x7800, 0xB401, 0x5000, 0x9C01, 0x8801, 0x4400
};

static void put_u16le(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t) (v & 0xFF);
	p[1] = (uint8_t) ((v >> 8) & 0xFF);
}

static void put_u64le(uint8_t *p, uint64_t v)
{
	for (unsigned int i = 0; i < 8; i++)
		p[i] = (uint8_t) ((v >> (i * 8)) & 0xFF);
}

static uint16_t get_u16le(const uint8_t *p)
{
	return (uint16_t) p[0] | ((uint16_t) p[1] << 8);
}

static uint64_t get_u64le(const uint8_t *p)
{
	uint64_t v = 0;
	for (unsigned int i = 0; i < 8; i++)
		v |= ((uint64_t) p[i]) << (i * 8);
	return v;
}

static uint32_t get_u32le(const uint8_t *p)
{
	return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
}

static void put_u32le(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t) (v & 0xFF);
	p[1] = (uint8_t) ((v >> 8) & 0xFF);
	p[2] = (uint8_t) ((v >> 16) & 0xFF);
	p[3] = (uint8_t) ((v >> 24) & 0xFF);
}

static garmin_status_t queue_write(garmin_ble_core_t *core, const uint8_t *data, size_t size)
{
	if (core == 0 || data == 0 || size > GARMIN_PACKET_MAX)
		return GARMIN_ERR_ARG;

	size_t next = (core->write_tail + 1) % GARMIN_WRITE_QUEUE_MAX;
	if (next == core->write_head)
		return GARMIN_ERR_NO_SPACE;

	memcpy(core->writes[core->write_tail].data, data, size);
	core->writes[core->write_tail].size = size;
	core->write_tail = next;
	return GARMIN_OK;
}

uint16_t garmin_crc16(const uint8_t *data, size_t size, uint16_t initial)
{
	uint16_t crc = initial;
	for (size_t i = 0; i < size; i++) {
		uint8_t b = data[i];
		crc = (uint16_t) ((((crc >> 4) & 0x0FFF) ^ crc_nibbles[crc & 0x0F]) ^ crc_nibbles[b & 0x0F]);
		crc = (uint16_t) ((((crc >> 4) & 0x0FFF) ^ crc_nibbles[crc & 0x0F]) ^ crc_nibbles[(b >> 4) & 0x0F]);
	}
	return crc;
}

garmin_status_t garmin_cobs_encode(const uint8_t *input, size_t input_size, uint8_t *output, size_t output_capacity, size_t *output_size)
{
	if ((input_size && input == 0) || output == 0 || output_size == 0)
		return GARMIN_ERR_ARG;

	size_t out = 0;
	if (out >= output_capacity)
		return GARMIN_ERR_NO_SPACE;
	output[out++] = 0;

	size_t pos = 0;
	int last_was_zero = 0;
	while (pos < input_size) {
		size_t start = pos;
		while (pos < input_size && input[pos] != 0)
			pos++;
		last_was_zero = pos < input_size;
		size_t payload_size = pos - start;

		while (payload_size >= 0xFE) {
			if (out + 1 + 0xFE > output_capacity)
				return GARMIN_ERR_NO_SPACE;
			output[out++] = 0xFF;
			memcpy(output + out, input + start, 0xFE);
			out += 0xFE;
			start += 0xFE;
			payload_size -= 0xFE;
		}

		if (out + 1 + payload_size > output_capacity)
			return GARMIN_ERR_NO_SPACE;
		output[out++] = (uint8_t) (payload_size + 1);
		memcpy(output + out, input + start, payload_size);
		out += payload_size;

		if (pos < input_size && input[pos] == 0)
			pos++;
	}

	if (last_was_zero) {
		if (out >= output_capacity)
			return GARMIN_ERR_NO_SPACE;
		output[out++] = 1;
	}

	if (out >= output_capacity)
		return GARMIN_ERR_NO_SPACE;
	output[out++] = 0;
	*output_size = out;
	return GARMIN_OK;
}

garmin_status_t garmin_cobs_decode_frame(const uint8_t *frame, size_t frame_size, uint8_t *output, size_t output_capacity, size_t *output_size)
{
	if ((frame_size && frame == 0) || output == 0 || output_size == 0)
		return GARMIN_ERR_ARG;

	size_t pos = 0, out = 0;
	while (pos < frame_size) {
		uint8_t code = frame[pos++];
		if (code == 0)
			break;
		size_t payload_size = (size_t) code - 1;
		if (pos + payload_size > frame_size || out + payload_size > output_capacity)
			return GARMIN_ERR_BAD_PACKET;
		memcpy(output + out, frame + pos, payload_size);
		out += payload_size;
		pos += payload_size;
		if (code != 0xFF && pos < frame_size) {
			if (out >= output_capacity)
				return GARMIN_ERR_NO_SPACE;
			output[out++] = 0;
		}
	}

	*output_size = out;
	return GARMIN_OK;
}

garmin_status_t garmin_gfdi_build(uint16_t message_id, const uint8_t *payload, size_t payload_size, uint8_t *output, size_t output_capacity, size_t *output_size)
{
	if ((payload_size && payload == 0) || output == 0 || output_size == 0)
		return GARMIN_ERR_ARG;
	if (payload_size + 6 > output_capacity || payload_size + 6 > 0xFFFF)
		return GARMIN_ERR_NO_SPACE;

	size_t size = payload_size + 6;
	put_u16le(output, (uint16_t) size);
	put_u16le(output + 2, message_id);
	if (payload_size)
		memcpy(output + 4, payload, payload_size);
	put_u16le(output + size - 2, garmin_crc16(output, size - 2, 0));
	*output_size = size;
	return GARMIN_OK;
}

garmin_status_t garmin_gfdi_build_download_request(uint16_t file_index, uint32_t data_size, uint8_t request_type, uint16_t crc_seed, uint32_t data_offset, uint8_t *output, size_t output_capacity, size_t *output_size)
{
	uint8_t payload[13];
	put_u16le(payload, file_index);
	put_u32le(payload + 2, data_offset);
	payload[6] = request_type;
	put_u16le(payload + 7, crc_seed);
	put_u32le(payload + 9, data_size);
	return garmin_gfdi_build(GARMIN_GFDI_DOWNLOAD_REQUEST, payload, sizeof(payload), output, output_capacity, output_size);
}

garmin_status_t garmin_gfdi_build_filter(uint8_t filter_type, uint8_t *output, size_t output_capacity, size_t *output_size)
{
	return garmin_gfdi_build(GARMIN_GFDI_FILTER, &filter_type, 1, output, output_capacity, output_size);
}

garmin_status_t garmin_gfdi_build_file_transfer_ack(uint32_t data_offset, uint8_t *output, size_t output_capacity, size_t *output_size)
{
	uint8_t payload[8];
	put_u16le(payload, GARMIN_GFDI_FILE_TRANSFER_DATA);
	payload[2] = 0; /* ACK */
	payload[3] = 0; /* transfer OK */
	put_u32le(payload + 4, data_offset);
	return garmin_gfdi_build(GARMIN_GFDI_RESPONSE, payload, sizeof(payload), output, output_capacity, output_size);
}

garmin_status_t garmin_gfdi_parse(const uint8_t *frame, size_t frame_size, uint16_t *message_id, const uint8_t **payload, size_t *payload_size)
{
	if (frame == 0 || message_id == 0 || payload == 0 || payload_size == 0)
		return GARMIN_ERR_ARG;
	if (frame_size < 6 || get_u16le(frame) != frame_size)
		return GARMIN_ERR_BAD_PACKET;
	if (get_u16le(frame + frame_size - 2) != garmin_crc16(frame, frame_size - 2, 0))
		return GARMIN_ERR_CRC;

	uint16_t id = get_u16le(frame + 2);
	if (id & 0x8000)
		id = (uint16_t) ((id & 0x00FF) + 5000);
	*message_id = id;
	*payload = frame + 4;
	*payload_size = frame_size - 6;
	return GARMIN_OK;
}

garmin_status_t garmin_directory_entry_parse(const uint8_t data[16], garmin_directory_entry_t *entry)
{
	if (data == 0 || entry == 0)
		return GARMIN_ERR_ARG;
	entry->file_index = get_u16le(data);
	entry->data_type = data[2];
	entry->sub_type = data[3];
	entry->file_number = get_u16le(data + 4);
	entry->specific_flags = data[6];
	entry->file_flags = data[7];
	entry->file_size = get_u32le(data + 8);
	entry->garmin_time = get_u32le(data + 12);
	return GARMIN_OK;
}

size_t garmin_ml_close_all(uint64_t client_id, uint8_t out[12])
{
	out[0] = 0;
	out[1] = GARMIN_REQ_CLOSE_ALL;
	put_u64le(out + 2, client_id);
	put_u16le(out + 10, 0);
	return 12;
}

size_t garmin_ml_register_service(uint64_t client_id, uint16_t service, int reliable, uint8_t out[13])
{
	out[0] = 0;
	out[1] = GARMIN_REQ_REGISTER_ML;
	put_u64le(out + 2, client_id);
	put_u16le(out + 10, service);
	out[12] = reliable ? 2 : 0;
	return 13;
}

void garmin_ble_core_init(garmin_ble_core_t *core, int prefer_reliable, size_t max_packet_size)
{
	if (core == 0)
		return;
	memset(core, 0, sizeof(*core));
	core->client_id = GARMIN_CLIENT_ID;
	core->max_packet_size = max_packet_size ? max_packet_size : 20;
	core->prefer_reliable = prefer_reliable ? 1 : 0;
}

void garmin_ble_core_set_download_buffer(garmin_ble_core_t *core, uint8_t *buffer, size_t capacity)
{
	if (core == 0)
		return;
	core->download_buffer = buffer;
	core->download_capacity = capacity;
}

garmin_status_t garmin_ble_core_start(garmin_ble_core_t *core)
{
	if (core == 0)
		return GARMIN_ERR_ARG;
	uint8_t packet[12];
	return queue_write(core, packet, garmin_ml_close_all(core->client_id, packet));
}

int garmin_ble_core_take_write(garmin_ble_core_t *core, garmin_packet_t *packet)
{
	if (core == 0 || packet == 0 || core->write_head == core->write_tail)
		return 0;
	*packet = core->writes[core->write_head];
	core->write_head = (core->write_head + 1) % GARMIN_WRITE_QUEUE_MAX;
	return 1;
}

static garmin_status_t handle_management(garmin_ble_core_t *core, const uint8_t *data, size_t size, garmin_event_t *event)
{
	if (size < 10)
		return GARMIN_ERR_BAD_PACKET;
	if (get_u64le(data + 2) != core->client_id)
		return GARMIN_OK;

	if (data[1] == GARMIN_RESP_CLOSE_ALL) {
		uint8_t packet[13];
		core->gfdi_open = 0;
		core->cobs_size = 0;
		event->type = GARMIN_EVENT_CLOSED_ALL;
		return queue_write(core, packet, garmin_ml_register_service(core->client_id, GARMIN_SERVICE_GFDI, core->prefer_reliable, packet));
	}

	if (data[1] == GARMIN_RESP_REGISTER_ML) {
		if (size < 15)
			return GARMIN_ERR_BAD_PACKET;
		uint16_t service = get_u16le(data + 10);
		uint8_t status = data[12];
		uint8_t handle = data[13];
		uint8_t reliable = data[14] != 0;
		event->type = GARMIN_EVENT_SERVICE_REGISTERED;
		event->service = service;
		event->handle = handle;
		event->reliable = reliable;
		event->status = (garmin_status_t) status;
		if (service == GARMIN_SERVICE_GFDI && status == 0) {
			core->gfdi_handle = handle;
			core->gfdi_open = 1;
			core->gfdi_reliable = reliable;
		}
	}

	return GARMIN_OK;
}

static garmin_status_t feed_gfdi_cobs(garmin_ble_core_t *core, const uint8_t *data, size_t size, garmin_event_t *event)
{
	if (size > GARMIN_PACKET_MAX - core->cobs_size)
		return GARMIN_ERR_NO_SPACE;
	memcpy(core->cobs_buffer + core->cobs_size, data, size);
	core->cobs_size += size;

	size_t start = 0;
	while (start < core->cobs_size && core->cobs_buffer[start] != 0)
		start++;
	if (start >= core->cobs_size)
		return GARMIN_OK;

	size_t end = start + 1;
	while (end < core->cobs_size && core->cobs_buffer[end] != 0)
		end++;
	if (end >= core->cobs_size)
		return GARMIN_OK;

	uint8_t decoded[GARMIN_PACKET_MAX];
	size_t decoded_size = 0;
	garmin_status_t rc = garmin_cobs_decode_frame(core->cobs_buffer + start + 1, end - start - 1, decoded, sizeof(decoded), &decoded_size);
	memmove(core->cobs_buffer, core->cobs_buffer + end + 1, core->cobs_size - end - 1);
	core->cobs_size -= end + 1;
	if (rc != GARMIN_OK)
		return rc;

	uint16_t message_id = 0;
	const uint8_t *payload = 0;
	size_t payload_size = 0;
	rc = garmin_gfdi_parse(decoded, decoded_size, &message_id, &payload, &payload_size);
	if (rc != GARMIN_OK)
		return rc;

	event->type = GARMIN_EVENT_GFDI;
	event->service = GARMIN_SERVICE_GFDI;
	event->handle = core->gfdi_handle;
	event->message_id = message_id;
	if (payload_size > sizeof(core->event_payload))
		return GARMIN_ERR_NO_SPACE;
	memcpy(core->event_payload, payload, payload_size);
	core->event_payload_size = payload_size;
	event->payload = core->event_payload;
	event->payload_size = core->event_payload_size;

	if (message_id == GARMIN_GFDI_RESPONSE && payload_size >= 3) {
		uint16_t original = get_u16le(payload);
		uint8_t status = payload[2];
		event->message_id = original;
		event->status = (garmin_status_t) status;
		if (original == GARMIN_GFDI_DOWNLOAD_REQUEST && payload_size >= 8) {
			uint8_t download_status = payload[3];
			uint32_t max_file_size = get_u32le(payload + 4);
			event->status = (garmin_status_t) download_status;
			event->total_size = max_file_size;
			if (status == 0 && download_status == 0 && max_file_size <= core->download_capacity) {
				core->download_size = max_file_size;
				core->download_received = 0;
				event->type = GARMIN_EVENT_DOWNLOAD_READY;
			}
		}
		return GARMIN_OK;
	}

	if (message_id == GARMIN_GFDI_FILE_TRANSFER_DATA && payload_size >= 7) {
		/* byte 0: flags, bytes 1..2: running crc, bytes 3..6: data offset */
		uint32_t offset = get_u32le(payload + 3);
		const uint8_t *chunk = payload + 7;
		size_t chunk_size = payload_size - 7;
		if (core->download_buffer == 0 || offset + chunk_size > core->download_capacity)
			return GARMIN_ERR_NO_SPACE;
		if (core->download_size && offset + chunk_size > core->download_size)
			return GARMIN_ERR_BAD_PACKET;
		memcpy(core->download_buffer + offset, chunk, chunk_size);
		core->download_received = offset + chunk_size;

		uint8_t ack_frame[GARMIN_PACKET_MAX];
		size_t ack_frame_size = 0;
		garmin_status_t rc = garmin_gfdi_build_file_transfer_ack((uint32_t) core->download_received, ack_frame, sizeof(ack_frame), &ack_frame_size);
		if (rc != GARMIN_OK)
			return rc;
		uint8_t ack_encoded[GARMIN_PACKET_MAX];
		size_t ack_encoded_size = 0;
		rc = garmin_cobs_encode(ack_frame, ack_frame_size, ack_encoded, sizeof(ack_encoded), &ack_encoded_size);
		if (rc != GARMIN_OK)
			return rc;
		if (core->gfdi_reliable) {
			size_t max_payload = core->max_packet_size - 2;
			for (size_t pos = 0; pos < ack_encoded_size; pos += max_payload) {
				size_t chunk_len = ack_encoded_size - pos < max_payload ? ack_encoded_size - pos : max_payload;
				uint8_t packet[GARMIN_PACKET_MAX];
				packet[0] = (uint8_t) (GARMIN_MLR_FLAG | ((core->gfdi_handle & 0x07) << 4) | ((core->mlr_next_recv_seq >> 2) & 0x0F));
				packet[1] = (uint8_t) (((core->mlr_next_recv_seq & 0x03) << 6) | (core->mlr_next_send_seq & 0x3F));
				memcpy(packet + 2, ack_encoded + pos, chunk_len);
				rc = queue_write(core, packet, chunk_len + 2);
				if (rc != GARMIN_OK)
					return rc;
				core->mlr_next_send_seq = (uint8_t) ((core->mlr_next_send_seq + 1) & 0x3F);
			}
		} else {
			size_t max_payload = core->max_packet_size - 1;
			for (size_t pos = 0; pos < ack_encoded_size; pos += max_payload) {
				size_t chunk_len = ack_encoded_size - pos < max_payload ? ack_encoded_size - pos : max_payload;
				uint8_t packet[GARMIN_PACKET_MAX];
				packet[0] = core->gfdi_handle;
				memcpy(packet + 1, ack_encoded + pos, chunk_len);
				rc = queue_write(core, packet, chunk_len + 1);
				if (rc != GARMIN_OK)
					return rc;
			}
		}

		event->type = GARMIN_EVENT_DOWNLOAD_PROGRESS;
		event->offset = (uint32_t) core->download_received;
		event->total_size = (uint32_t) core->download_size;
		if (chunk_size > sizeof(core->event_payload))
			return GARMIN_ERR_NO_SPACE;
		memcpy(core->event_payload, chunk, chunk_size);
		core->event_payload_size = chunk_size;
		event->payload = core->event_payload;
		event->payload_size = chunk_size;

		if (core->download_size && core->download_received >= core->download_size) {
			event->type = GARMIN_EVENT_FILE_COMPLETE;
			event->payload = core->download_buffer;
			event->payload_size = core->download_size;
			if (core->download_target == GARMIN_DOWNLOAD_DIRECTORY && (core->download_size % 16) == 0 && core->download_size >= 16) {
				event->type = GARMIN_EVENT_DIRECTORY_ENTRY;
				garmin_directory_entry_parse(core->download_buffer, &event->directory_entry);
			}
			core->download_target = GARMIN_DOWNLOAD_NONE;
		}
		return GARMIN_OK;
	}

	return GARMIN_OK;
}

static garmin_status_t handle_mlr(garmin_ble_core_t *core, const uint8_t *data, size_t size, garmin_event_t *event)
{
	if (size < 2)
		return GARMIN_ERR_BAD_PACKET;
	uint8_t handle = (data[0] & 0x70) >> 4;
	if (!core->gfdi_open || handle != (core->gfdi_handle & 0x07))
		return GARMIN_ERR_STATE;

	uint8_t seq = data[1] & 0x3F;
	const uint8_t *payload = data + 2;
	size_t payload_size = size - 2;
	if (payload_size && seq == core->mlr_next_recv_seq) {
		uint8_t ack[2];
		core->mlr_next_recv_seq = (uint8_t) ((core->mlr_next_recv_seq + 1) & 0x3F);
		ack[0] = (uint8_t) (GARMIN_MLR_FLAG | ((core->gfdi_handle & 0x07) << 4) | ((core->mlr_next_recv_seq >> 2) & 0x0F));
		ack[1] = (uint8_t) ((core->mlr_next_recv_seq & 0x03) << 6);
		queue_write(core, ack, sizeof(ack));
		return feed_gfdi_cobs(core, payload, payload_size, event);
	}
	return GARMIN_OK;
}

garmin_status_t garmin_ble_core_on_notification(garmin_ble_core_t *core, const uint8_t *data, size_t size, garmin_event_t *event)
{
	if (event)
		memset(event, 0, sizeof(*event));
	if (core == 0 || data == 0 || size == 0 || event == 0)
		return GARMIN_ERR_ARG;

	if (data[0] & GARMIN_MLR_FLAG)
		return handle_mlr(core, data, size, event);
	if (data[0] == 0)
		return handle_management(core, data, size, event);
	if (core->gfdi_open && data[0] == core->gfdi_handle)
		return feed_gfdi_cobs(core, data + 1, size - 1, event);

	event->type = GARMIN_EVENT_SERVICE_DATA;
	event->handle = data[0];
	event->payload = data + 1;
	event->payload_size = size - 1;
	return GARMIN_OK;
}

garmin_status_t garmin_ble_core_send_gfdi(garmin_ble_core_t *core, uint16_t message_id, const uint8_t *payload, size_t payload_size)
{
	if (core == 0 || !core->gfdi_open)
		return GARMIN_ERR_STATE;

	uint8_t frame[GARMIN_PACKET_MAX];
	size_t frame_size = 0;
	garmin_status_t rc = garmin_gfdi_build(message_id, payload, payload_size, frame, sizeof(frame), &frame_size);
	if (rc != GARMIN_OK)
		return rc;

	uint8_t encoded[GARMIN_PACKET_MAX];
	size_t encoded_size = 0;
	rc = garmin_cobs_encode(frame, frame_size, encoded, sizeof(encoded), &encoded_size);
	if (rc != GARMIN_OK)
		return rc;

	if (core->gfdi_reliable) {
		size_t max_payload = core->max_packet_size - 2;
		for (size_t pos = 0; pos < encoded_size; pos += max_payload) {
			size_t chunk = encoded_size - pos < max_payload ? encoded_size - pos : max_payload;
			uint8_t packet[GARMIN_PACKET_MAX];
			packet[0] = (uint8_t) (GARMIN_MLR_FLAG | ((core->gfdi_handle & 0x07) << 4) | ((core->mlr_next_recv_seq >> 2) & 0x0F));
			packet[1] = (uint8_t) (((core->mlr_next_recv_seq & 0x03) << 6) | (core->mlr_next_send_seq & 0x3F));
			memcpy(packet + 2, encoded + pos, chunk);
			rc = queue_write(core, packet, chunk + 2);
			if (rc != GARMIN_OK)
				return rc;
			core->mlr_next_send_seq = (uint8_t) ((core->mlr_next_send_seq + 1) & 0x3F);
		}
	} else {
		size_t max_payload = core->max_packet_size - 1;
		for (size_t pos = 0; pos < encoded_size; pos += max_payload) {
			size_t chunk = encoded_size - pos < max_payload ? encoded_size - pos : max_payload;
			uint8_t packet[GARMIN_PACKET_MAX];
			packet[0] = core->gfdi_handle;
			memcpy(packet + 1, encoded + pos, chunk);
			rc = queue_write(core, packet, chunk + 1);
			if (rc != GARMIN_OK)
				return rc;
		}
	}

	return GARMIN_OK;
}

garmin_status_t garmin_ble_core_request_filter(garmin_ble_core_t *core)
{
	if (core == 0 || !core->gfdi_open)
		return GARMIN_ERR_STATE;
	uint8_t frame[GARMIN_PACKET_MAX];
	size_t frame_size = 0;
	garmin_status_t rc = garmin_gfdi_build_filter(3, frame, sizeof(frame), &frame_size);
	if (rc != GARMIN_OK)
		return rc;
	uint16_t message_id = 0;
	const uint8_t *payload = 0;
	size_t payload_size = 0;
	rc = garmin_gfdi_parse(frame, frame_size, &message_id, &payload, &payload_size);
	if (rc != GARMIN_OK)
		return rc;
	return garmin_ble_core_send_gfdi(core, message_id, payload, payload_size);
}

garmin_status_t garmin_ble_core_request_directory(garmin_ble_core_t *core)
{
	if (core == 0 || !core->gfdi_open)
		return GARMIN_ERR_STATE;
	core->download_target = GARMIN_DOWNLOAD_DIRECTORY;
	core->download_file_index = 0;
	core->download_size = 0;
	core->download_received = 0;
	uint8_t frame[GARMIN_PACKET_MAX];
	size_t frame_size = 0;
	garmin_status_t rc = garmin_gfdi_build_download_request(0, 0, 1, 0, 0, frame, sizeof(frame), &frame_size);
	if (rc != GARMIN_OK)
		return rc;
	uint16_t message_id = 0;
	const uint8_t *payload = 0;
	size_t payload_size = 0;
	rc = garmin_gfdi_parse(frame, frame_size, &message_id, &payload, &payload_size);
	if (rc != GARMIN_OK)
		return rc;
	return garmin_ble_core_send_gfdi(core, message_id, payload, payload_size);
}

garmin_status_t garmin_ble_core_request_file(garmin_ble_core_t *core, const garmin_directory_entry_t *entry)
{
	if (core == 0 || entry == 0 || !core->gfdi_open)
		return GARMIN_ERR_ARG;
	if (entry->file_size > GARMIN_DOWNLOAD_MAX)
		return GARMIN_ERR_NO_SPACE;
	core->download_target = GARMIN_DOWNLOAD_FILE;
	core->download_file_index = entry->file_index;
	core->download_size = 0;
	core->download_received = 0;
	uint8_t frame[GARMIN_PACKET_MAX];
	size_t frame_size = 0;
	garmin_status_t rc = garmin_gfdi_build_download_request(entry->file_index, 0, 1, 0, 0, frame, sizeof(frame), &frame_size);
	if (rc != GARMIN_OK)
		return rc;
	uint16_t message_id = 0;
	const uint8_t *payload = 0;
	size_t payload_size = 0;
	rc = garmin_gfdi_parse(frame, frame_size, &message_id, &payload, &payload_size);
	if (rc != GARMIN_OK)
		return rc;
	return garmin_ble_core_send_gfdi(core, message_id, payload, payload_size);
}
