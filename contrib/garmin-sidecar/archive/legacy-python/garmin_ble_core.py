#!/usr/bin/env python3
"""
Transport-neutral Garmin BLE protocol core.

This module does not scan, connect, or call any platform BLE API. A host app
such as a mini-program should feed notification bytes into `on_notification`
and write every packet returned by `take_writes` to the Garmin ML write
characteristic.
"""

from __future__ import annotations

import struct
from collections import deque
from dataclasses import dataclass
from typing import Deque, Iterable


CLIENT_ID = 2

SERVICE_GFDI = 1
SERVICE_REGISTRATION = 4

REQ_REGISTER_ML = 0
RESP_REGISTER_ML = 1
REQ_CLOSE_HANDLE = 2
RESP_CLOSE_HANDLE = 3
REQ_CLOSE_ALL = 5
RESP_CLOSE_ALL = 6

MLR_FLAG = 0x80
MLR_HANDLE_MASK = 0x70
MLR_REQ_MASK = 0x0F
MLR_SEQ_MASK = 0x3F
MLR_MAX_SEQ = 0x3F

CRC_NIBBLES = (
    0x0000, 0xCC01, 0xD801, 0x1400, 0xF001, 0x3C00, 0x2800, 0xE401,
    0xA001, 0x6C00, 0x7800, 0xB401, 0x5000, 0x9C01, 0x8801, 0x4400,
)


@dataclass
class GarminBleEvent:
    kind: str
    payload: bytes = b""
    service: int | None = None
    handle: int | None = None
    reliable: bool | None = None
    status: int | None = None
    message_id: int | None = None
    detail: str | None = None


def crc16_garmin(data: bytes, initial: int = 0) -> int:
    crc = initial
    for value in data:
        crc = (((crc >> 4) & 0x0FFF) ^ CRC_NIBBLES[crc & 0x0F]) ^ CRC_NIBBLES[value & 0x0F]
        crc = (((crc >> 4) & 0x0FFF) ^ CRC_NIBBLES[crc & 0x0F]) ^ CRC_NIBBLES[(value >> 4) & 0x0F]
    return crc & 0xFFFF


def cobs_encode(data: bytes) -> bytes:
    out = bytearray(b"\x00")
    pos = 0
    last_was_zero = False
    while pos < len(data):
        start = pos
        while pos < len(data) and data[pos] != 0:
            pos += 1
        last_was_zero = pos < len(data)
        size = pos - start
        while size >= 0xFE:
            out.append(0xFF)
            out.extend(data[start:start + 0xFE])
            start += 0xFE
            size -= 0xFE
        out.append(size + 1)
        out.extend(data[start:start + size])
        if pos < len(data) and data[pos] == 0:
            pos += 1
    if last_was_zero:
        out.append(0x01)
    out.append(0)
    return bytes(out)


class CobsStream:
    def __init__(self) -> None:
        self._buffer = bytearray()

    def feed(self, data: bytes) -> list[bytes]:
        self._buffer.extend(data)
        messages: list[bytes] = []
        while True:
            try:
                start = self._buffer.index(0)
                end = self._buffer.index(0, start + 1)
            except ValueError:
                if len(self._buffer) > 10000:
                    self._buffer.clear()
                return messages
            frame = bytes(self._buffer[start + 1:end])
            del self._buffer[:end + 1]
            if frame:
                messages.append(_cobs_decode_inner(frame))


def _cobs_decode_inner(frame: bytes) -> bytes:
    out = bytearray()
    pos = 0
    while pos < len(frame):
        code = frame[pos]
        pos += 1
        if code == 0:
            break
        size = code - 1
        out.extend(frame[pos:pos + size])
        pos += size
        if code != 0xFF and pos < len(frame):
            out.append(0)
    return bytes(out)


def gfdi_build(message_id: int, payload: bytes = b"", sequence: int | None = None) -> bytes:
    if sequence is not None:
        wire_id = 0x8000 | ((sequence & 0x7F) << 8) | ((message_id - 5000) & 0xFF)
    else:
        wire_id = message_id
    body = bytearray(b"\x00\x00")
    body.extend(struct.pack("<H", wire_id))
    body.extend(payload)
    struct.pack_into("<H", body, 0, len(body) + 2)
    body.extend(struct.pack("<H", crc16_garmin(bytes(body))))
    return bytes(body)


def gfdi_parse(frame: bytes) -> tuple[int, bytes]:
    if len(frame) < 6:
        raise ValueError("GFDI frame too short")
    size = struct.unpack_from("<H", frame, 0)[0]
    if size != len(frame):
        raise ValueError(f"GFDI length mismatch: {size} != {len(frame)}")
    expected = struct.unpack_from("<H", frame, len(frame) - 2)[0]
    actual = crc16_garmin(frame[:-2])
    if expected != actual:
        raise ValueError(f"GFDI CRC mismatch: {expected:04x} != {actual:04x}")
    message_id = struct.unpack_from("<H", frame, 2)[0]
    if message_id & 0x8000:
        message_id = (message_id & 0xFF) + 5000
    return message_id, frame[4:-2]


def ml_close_all(client_id: int = CLIENT_ID) -> bytes:
    return b"\x00" + bytes([REQ_CLOSE_ALL]) + struct.pack("<QH", client_id, 0)


def ml_register_service(service: int, reliable: bool = True, client_id: int = CLIENT_ID) -> bytes:
    return b"\x00" + bytes([REQ_REGISTER_ML]) + struct.pack("<QH", client_id, service) + bytes([2 if reliable else 0])


def ml_close_service(service: int, handle: int, client_id: int = CLIENT_ID) -> bytes:
    return b"\x00" + bytes([REQ_CLOSE_HANDLE]) + struct.pack("<QH", client_id, service) + bytes([handle & 0xFF])


class MlrChannel:
    def __init__(self, handle: int, max_packet_size: int = 20) -> None:
        self.handle = handle
        self.max_packet_size = max_packet_size
        self.next_send_seq = 0
        self.next_recv_seq = 0
        self.last_recv_ack = 0
        self.last_send_ack = 0
        self.pending: Deque[bytes] = deque()

    def queue_message(self, payload: bytes) -> list[bytes]:
        max_payload = self.max_packet_size - 2
        for pos in range(0, len(payload), max_payload):
            self.pending.append(payload[pos:pos + max_payload])
        return self.drain()

    def drain(self) -> list[bytes]:
        packets: list[bytes] = []
        while self.pending:
            data = self.pending.popleft()
            packets.append(self._packet(self.next_recv_seq, self.next_send_seq, data))
            self.next_send_seq = (self.next_send_seq + 1) % (MLR_MAX_SEQ + 1)
        return packets

    def on_packet(self, packet: bytes) -> tuple[list[bytes], bytes | None]:
        if len(packet) < 2:
            raise ValueError("MLR packet too short")
        byte0, byte1 = packet[0], packet[1]
        if not byte0 & MLR_FLAG:
            raise ValueError("not an MLR packet")
        handle = (byte0 & MLR_HANDLE_MASK) >> 4
        if handle != (self.handle & 0x07):
            raise ValueError(f"MLR handle mismatch: {handle} != {self.handle & 0x07}")
        req_num = ((byte0 & MLR_REQ_MASK) << 2) | ((byte1 >> 6) & 0x03)
        seq_num = byte1 & MLR_SEQ_MASK
        if req_num != self.last_recv_ack:
            self.last_recv_ack = req_num
        data = packet[2:]
        writes: list[bytes] = []
        if data and seq_num == self.next_recv_seq:
            self.next_recv_seq = (self.next_recv_seq + 1) % (MLR_MAX_SEQ + 1)
            writes.append(self.ack_packet())
            return writes, data
        if data:
            writes.append(self.ack_packet())
        return writes, None

    def ack_packet(self) -> bytes:
        self.last_send_ack = self.next_recv_seq
        return self._packet(self.next_recv_seq, 0, b"")

    def _packet(self, req_num: int, seq_num: int, data: bytes) -> bytes:
        return bytes([
            MLR_FLAG | ((self.handle & 0x07) << 4) | ((req_num >> 2) & 0x0F),
            ((req_num & 0x03) << 6) | (seq_num & MLR_SEQ_MASK),
        ]) + data


class GarminBleCore:
    def __init__(self, reliable: bool = True, max_packet_size: int = 20, client_id: int = CLIENT_ID) -> None:
        self.reliable = reliable
        self.max_packet_size = max_packet_size
        self.client_id = client_id
        self.handles: dict[int, int] = {}
        self.services: dict[int, int] = {}
        self.mlr: dict[int, MlrChannel] = {}
        self.cobs: dict[int, CobsStream] = {}
        self._writes: Deque[bytes] = deque()

    def start(self) -> None:
        self._writes.append(ml_close_all(self.client_id))

    def open_gfdi(self) -> None:
        self._writes.append(ml_register_service(SERVICE_GFDI, self.reliable, self.client_id))

    def take_writes(self) -> list[bytes]:
        writes = list(self._writes)
        self._writes.clear()
        return writes

    def send_gfdi(self, message_id: int, payload: bytes = b"", sequence: int | None = None) -> None:
        handle = self.handles.get(SERVICE_GFDI)
        if handle is None:
            raise RuntimeError("GFDI service is not open")
        encoded = cobs_encode(gfdi_build(message_id, payload, sequence))
        channel = self.mlr.get(handle)
        if channel is not None:
            self._writes.extend(channel.queue_message(encoded))
        else:
            max_payload = self.max_packet_size - 1
            for pos in range(0, len(encoded), max_payload):
                self._writes.append(bytes([handle & 0xFF]) + encoded[pos:pos + max_payload])

    def on_notification(self, packet: bytes) -> list[GarminBleEvent]:
        if not packet:
            return []
        if packet[0] & MLR_FLAG:
            handle = ((packet[0] & MLR_HANDLE_MASK) >> 4) | MLR_FLAG
            channel = self.mlr.get(handle) or self.mlr.get(handle & 0x7F)
            if channel is None:
                return [GarminBleEvent("unknown_mlr_handle", payload=packet, handle=handle)]
            writes, data = channel.on_packet(packet)
            self._writes.extend(writes)
            if data is None:
                return []
            return self._feed_service(channel.handle, data)
        handle = packet[0]
        if handle == 0:
            return self._handle_management(packet)
        return self._feed_service(handle, packet[1:])

    def _handle_management(self, packet: bytes) -> list[GarminBleEvent]:
        if len(packet) < 10:
            return [GarminBleEvent("bad_management_packet", payload=packet)]
        msg_type = packet[1]
        incoming_client = struct.unpack_from("<Q", packet, 2)[0]
        if incoming_client != self.client_id:
            return [GarminBleEvent("foreign_client", payload=packet)]
        if msg_type == RESP_CLOSE_ALL:
            self.handles.clear()
            self.services.clear()
            self.mlr.clear()
            self.cobs.clear()
            self.open_gfdi()
            return [GarminBleEvent("closed_all")]
        if msg_type == RESP_REGISTER_ML:
            if len(packet) < 15:
                return [GarminBleEvent("bad_register_response", payload=packet)]
            service = struct.unpack_from("<H", packet, 10)[0]
            status = packet[12]
            handle = packet[13]
            reliable = packet[14] != 0
            if status == 0:
                key_handle = handle | (MLR_FLAG if reliable else 0)
                self.handles[service] = key_handle
                self.services[key_handle] = service
                self.cobs[key_handle] = CobsStream()
                if reliable:
                    self.mlr[key_handle] = MlrChannel(key_handle, self.max_packet_size)
            return [GarminBleEvent("service_registered", service=service, handle=handle, reliable=reliable, status=status)]
        if msg_type == RESP_CLOSE_HANDLE:
            service = struct.unpack_from("<H", packet, 10)[0] if len(packet) >= 12 else None
            handle = packet[12] if len(packet) >= 13 else None
            if service in self.handles:
                old_handle = self.handles.pop(service)
                self.services.pop(old_handle, None)
                self.mlr.pop(old_handle, None)
                self.cobs.pop(old_handle, None)
            return [GarminBleEvent("service_closed", service=service, handle=handle)]
        return [GarminBleEvent("management", payload=packet, detail=f"type={msg_type}")]

    def _feed_service(self, handle: int, payload: bytes) -> list[GarminBleEvent]:
        service = self.services.get(handle)
        if service != SERVICE_GFDI:
            return [GarminBleEvent("service_data", payload=payload, service=service, handle=handle)]
        stream = self.cobs.setdefault(handle, CobsStream())
        events = []
        for frame in stream.feed(payload):
            try:
                message_id, body = gfdi_parse(frame)
                events.append(GarminBleEvent("gfdi", payload=body, service=service, handle=handle, message_id=message_id))
            except ValueError as exc:
                events.append(GarminBleEvent("bad_gfdi", payload=frame, service=service, handle=handle, detail=str(exc)))
        return events


def hex_packets(packets: Iterable[bytes]) -> list[str]:
    return [packet.hex(" ") for packet in packets]
