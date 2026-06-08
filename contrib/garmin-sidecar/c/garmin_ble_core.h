#ifndef GARMIN_BLE_CORE_H
#define GARMIN_BLE_CORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GARMIN_CLIENT_ID 2ULL
#define GARMIN_SERVICE_GFDI 1U
#define GARMIN_SERVICE_REGISTRATION 4U

#define GARMIN_MLR_FLAG 0x80U
#define GARMIN_PACKET_MAX 1024U
#define GARMIN_WRITE_QUEUE_MAX 16U
#define GARMIN_DOWNLOAD_MAX 10485760U

#define GARMIN_GFDI_RESPONSE 5000U
#define GARMIN_GFDI_DOWNLOAD_REQUEST 5002U
#define GARMIN_GFDI_FILE_TRANSFER_DATA 5004U
#define GARMIN_GFDI_FILTER 5007U

typedef enum garmin_status_t {
	GARMIN_OK = 0,
	GARMIN_ERR_ARG = -1,
	GARMIN_ERR_NO_SPACE = -2,
	GARMIN_ERR_BAD_PACKET = -3,
	GARMIN_ERR_CRC = -4,
	GARMIN_ERR_STATE = -5
} garmin_status_t;

typedef enum garmin_event_type_t {
	GARMIN_EVENT_NONE = 0,
	GARMIN_EVENT_CLOSED_ALL,
	GARMIN_EVENT_SERVICE_REGISTERED,
	GARMIN_EVENT_GFDI,
	GARMIN_EVENT_DOWNLOAD_READY,
	GARMIN_EVENT_DOWNLOAD_PROGRESS,
	GARMIN_EVENT_FILE_COMPLETE,
	GARMIN_EVENT_DIRECTORY_ENTRY,
	GARMIN_EVENT_SERVICE_DATA,
	GARMIN_EVENT_ERROR
} garmin_event_type_t;

typedef enum garmin_download_target_t {
	GARMIN_DOWNLOAD_NONE = 0,
	GARMIN_DOWNLOAD_DIRECTORY,
	GARMIN_DOWNLOAD_FILE
} garmin_download_target_t;

typedef struct garmin_directory_entry_t {
	uint16_t file_index;
	uint8_t data_type;
	uint8_t sub_type;
	uint16_t file_number;
	uint8_t specific_flags;
	uint8_t file_flags;
	uint32_t file_size;
	uint32_t garmin_time;
} garmin_directory_entry_t;

typedef struct garmin_packet_t {
	uint8_t data[GARMIN_PACKET_MAX];
	size_t size;
} garmin_packet_t;

typedef struct garmin_event_t {
	garmin_event_type_t type;
	uint16_t service;
	uint8_t handle;
	uint8_t reliable;
	uint16_t message_id;
	const uint8_t *payload;
	size_t payload_size;
	uint32_t offset;
	uint32_t total_size;
	garmin_directory_entry_t directory_entry;
	garmin_status_t status;
} garmin_event_t;

typedef struct garmin_ble_core_t {
	uint64_t client_id;
	size_t max_packet_size;
	int prefer_reliable;

	uint8_t gfdi_handle;
	int gfdi_open;
	int gfdi_reliable;

	uint8_t mlr_next_send_seq;
	uint8_t mlr_next_recv_seq;

	uint8_t cobs_buffer[GARMIN_PACKET_MAX];
	size_t cobs_size;
	uint8_t event_payload[GARMIN_PACKET_MAX];
	size_t event_payload_size;

	garmin_download_target_t download_target;
	uint16_t download_file_index;
	uint8_t *download_buffer;
	size_t download_capacity;
	size_t download_size;
	size_t download_received;

	garmin_packet_t writes[GARMIN_WRITE_QUEUE_MAX];
	size_t write_head;
	size_t write_tail;
} garmin_ble_core_t;

void garmin_ble_core_init(garmin_ble_core_t *core, int prefer_reliable, size_t max_packet_size);
void garmin_ble_core_set_download_buffer(garmin_ble_core_t *core, uint8_t *buffer, size_t capacity);
garmin_status_t garmin_ble_core_start(garmin_ble_core_t *core);
int garmin_ble_core_take_write(garmin_ble_core_t *core, garmin_packet_t *packet);
garmin_status_t garmin_ble_core_on_notification(garmin_ble_core_t *core, const uint8_t *data, size_t size, garmin_event_t *event);
garmin_status_t garmin_ble_core_send_gfdi(garmin_ble_core_t *core, uint16_t message_id, const uint8_t *payload, size_t payload_size);
garmin_status_t garmin_ble_core_request_filter(garmin_ble_core_t *core);
garmin_status_t garmin_ble_core_request_directory(garmin_ble_core_t *core);
garmin_status_t garmin_ble_core_request_file(garmin_ble_core_t *core, const garmin_directory_entry_t *entry);

uint16_t garmin_crc16(const uint8_t *data, size_t size, uint16_t initial);
garmin_status_t garmin_cobs_encode(const uint8_t *input, size_t input_size, uint8_t *output, size_t output_capacity, size_t *output_size);
garmin_status_t garmin_cobs_decode_frame(const uint8_t *frame, size_t frame_size, uint8_t *output, size_t output_capacity, size_t *output_size);
garmin_status_t garmin_gfdi_build(uint16_t message_id, const uint8_t *payload, size_t payload_size, uint8_t *output, size_t output_capacity, size_t *output_size);
garmin_status_t garmin_gfdi_parse(const uint8_t *frame, size_t frame_size, uint16_t *message_id, const uint8_t **payload, size_t *payload_size);

size_t garmin_ml_close_all(uint64_t client_id, uint8_t out[12]);
size_t garmin_ml_register_service(uint64_t client_id, uint16_t service, int reliable, uint8_t out[13]);
garmin_status_t garmin_gfdi_build_download_request(uint16_t file_index, uint32_t data_size, uint8_t request_type, uint16_t crc_seed, uint32_t data_offset, uint8_t *output, size_t output_capacity, size_t *output_size);
garmin_status_t garmin_gfdi_build_filter(uint8_t filter_type, uint8_t *output, size_t output_capacity, size_t *output_size);
garmin_status_t garmin_gfdi_build_file_transfer_ack(uint32_t data_offset, uint8_t *output, size_t output_capacity, size_t *output_size);
garmin_status_t garmin_directory_entry_parse(const uint8_t data[16], garmin_directory_entry_t *entry);

#ifdef __cplusplus
}
#endif

#endif
