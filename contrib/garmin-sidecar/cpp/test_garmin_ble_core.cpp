#include "garmin_ble_core.hpp"

#include <cassert>
#include <iomanip>
#include <iostream>

static void print_packet(const std::vector<std::uint8_t>& packet)
{
	for (std::size_t i = 0; i < packet.size(); ++i) {
		if (i) {
			std::cout << ' ';
		}
		std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned int>(packet[i]);
	}
	std::cout << std::dec << '\n';
}

static std::vector<std::uint8_t> close_all_response()
{
	std::vector<std::uint8_t> packet(12);
	garmin_ml_close_all(GARMIN_CLIENT_ID, packet.data());
	packet[1] = 6;
	return packet;
}

static std::vector<std::uint8_t> register_response()
{
	std::vector<std::uint8_t> packet(15);
	packet[1] = 1;
	for (unsigned int i = 0; i < 8; ++i) {
		packet[2 + i] = static_cast<std::uint8_t>((GARMIN_CLIENT_ID >> (i * 8)) & 0xff);
	}
	packet[10] = GARMIN_SERVICE_GFDI;
	packet[12] = 0;
	packet[13] = 2;
	packet[14] = 2;
	return packet;
}

int main()
{
	garmin::BleCore core;
	core.start();
	auto writes = core.takeWrites();
	assert(writes.size() == 1);
	print_packet(writes[0]);

	auto event = core.onNotification(close_all_response());
	assert(event.type == GARMIN_EVENT_CLOSED_ALL);
	writes = core.takeWrites();
	assert(writes.size() == 1);
	print_packet(writes[0]);

	event = core.onNotification(register_response());
	assert(event.type == GARMIN_EVENT_SERVICE_REGISTERED);
	assert(event.service == GARMIN_SERVICE_GFDI);

	core.sendGfdi(5002, {'h', 'e', 'l', 'l', 'o'});
	writes = core.takeWrites();
	assert(!writes.empty());
	assert(writes[0][0] & GARMIN_MLR_FLAG);
	print_packet(writes[0]);
}
