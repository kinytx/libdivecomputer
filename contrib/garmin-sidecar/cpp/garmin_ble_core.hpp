#ifndef GARMIN_BLE_CORE_HPP
#define GARMIN_BLE_CORE_HPP

#include <cstdint>
#include <stdexcept>
#include <vector>

extern "C" {
#include "../c/garmin_ble_core.h"
}

namespace garmin {

class Error : public std::runtime_error {
public:
	explicit Error(garmin_status_t status)
		: std::runtime_error("Garmin BLE core error"), status_(status)
	{
	}

	garmin_status_t status() const noexcept
	{
		return status_;
	}

private:
	garmin_status_t status_;
};

struct Event {
	garmin_event_type_t type = GARMIN_EVENT_NONE;
	std::uint16_t service = 0;
	std::uint8_t handle = 0;
	bool reliable = false;
	std::uint16_t message_id = 0;
	std::vector<std::uint8_t> payload;
	garmin_status_t status = GARMIN_OK;
};

class BleCore {
public:
	explicit BleCore(bool preferReliable = true, std::size_t maxPacketSize = 20)
		: downloadBuffer_(1024 * 1024)
	{
		garmin_ble_core_init(&core_, preferReliable ? 1 : 0, maxPacketSize);
		garmin_ble_core_set_download_buffer(&core_, downloadBuffer_.data(), downloadBuffer_.size());
	}

	void start()
	{
		check(garmin_ble_core_start(&core_));
	}

	std::vector<std::vector<std::uint8_t>> takeWrites()
	{
		std::vector<std::vector<std::uint8_t>> writes;
		garmin_packet_t packet;
		while (garmin_ble_core_take_write(&core_, &packet)) {
			writes.emplace_back(packet.data, packet.data + packet.size);
		}
		return writes;
	}

	Event onNotification(const std::vector<std::uint8_t>& bytes)
	{
		return onNotification(bytes.data(), bytes.size());
	}

	Event onNotification(const std::uint8_t *bytes, std::size_t size)
	{
		garmin_event_t raw;
		check(garmin_ble_core_on_notification(&core_, bytes, size, &raw));

		Event event;
		event.type = raw.type;
		event.service = raw.service;
		event.handle = raw.handle;
		event.reliable = raw.reliable != 0;
		event.message_id = raw.message_id;
		event.status = raw.status;
		if (raw.payload != nullptr && raw.payload_size > 0) {
			event.payload.assign(raw.payload, raw.payload + raw.payload_size);
		}
		return event;
	}

	void sendGfdi(std::uint16_t messageId, const std::vector<std::uint8_t>& payload = {})
	{
		check(garmin_ble_core_send_gfdi(&core_, messageId, payload.data(), payload.size()));
	}

	void requestFilter()
	{
		check(garmin_ble_core_request_filter(&core_));
	}

	void requestDirectory()
	{
		check(garmin_ble_core_request_directory(&core_));
	}

	void requestFile(const garmin_directory_entry_t& entry)
	{
		check(garmin_ble_core_request_file(&core_, &entry));
	}

	garmin_ble_core_t *native() noexcept
	{
		return &core_;
	}

	const garmin_ble_core_t *native() const noexcept
	{
		return &core_;
	}

private:
	static void check(garmin_status_t status)
	{
		if (status != GARMIN_OK) {
			throw Error(status);
		}
	}

	garmin_ble_core_t core_{};
	std::vector<std::uint8_t> downloadBuffer_;
};

} // namespace garmin

#endif
