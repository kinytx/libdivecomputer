#!/usr/bin/env python3
"""
Experimental Garmin Descent sidecar for libdivecomputer-like workflows.

This tool is deliberately standalone: it reads Garmin FIT files and emits an
LDC-shaped JSON document. MTP and BLE channels are command-line placeholders so
callers can switch channel implementations later without changing consumers.
"""

from __future__ import annotations

import argparse
import datetime as _dt
import glob
import json
import math
import os
import struct
import sys
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path
from typing import Any, BinaryIO

from garmin_ble_core import GarminBleCore, hex_packets


GARMIN_EPOCH = _dt.datetime(1989, 12, 31, tzinfo=_dt.timezone.utc)


BASE_TYPES: dict[int, tuple[str, int, str, Any]] = {
    0x00: ("enum", 1, "B", 0xFF),
    0x01: ("sint8", 1, "b", 0x7F),
    0x02: ("uint8", 1, "B", 0xFF),
    0x83: ("sint16", 2, "h", 0x7FFF),
    0x84: ("uint16", 2, "H", 0xFFFF),
    0x85: ("sint32", 4, "i", 0x7FFFFFFF),
    0x86: ("uint32", 4, "I", 0xFFFFFFFF),
    0x07: ("string", 1, "s", None),
    0x88: ("float32", 4, "f", math.nan),
    0x89: ("float64", 8, "d", math.nan),
    0x0A: ("uint8z", 1, "B", 0x00),
    0x8B: ("uint16z", 2, "H", 0x0000),
    0x8C: ("uint32z", 4, "I", 0x00000000),
    0x0D: ("byte", 1, "B", None),
    0x8E: ("sint64", 8, "q", 0x7FFFFFFFFFFFFFFF),
    0x8F: ("uint64", 8, "Q", 0xFFFFFFFFFFFFFFFF),
    0x90: ("uint64z", 8, "Q", 0x0000000000000000),
}


FIELD_MAP: dict[int, dict[int, tuple[str, float, float, str | None]]] = {
    0: {
        1: ("manufacturer", 1, 0, None),
        2: ("product", 1, 0, None),
        3: ("serial_number", 1, 0, None),
        4: ("time_created", 1, 0, "timestamp"),
        5: ("number", 1, 0, None),
        7: ("product_name", 1, 0, None),
    },
    2: {
        1: ("utc_offset", 1, 0, None),
        2: ("time_offset", 1, 0, None),
        253: ("timestamp", 1, 0, "timestamp"),
    },
    18: {
        2: ("start_time", 1, 0, "timestamp"),
        3: ("start_latitude", 1, 0, "coordinate"),
        4: ("start_longitude", 1, 0, "coordinate"),
        5: ("sport", 1, 0, None),
        6: ("sub_sport", 1, 0, None),
        7: ("total_elapsed_time_ms", 1, 0, None),
        8: ("total_timer_time", 1000, 0, None),
        16: ("average_heart_rate", 1, 0, None),
        17: ("max_heart_rate", 1, 0, None),
        57: ("avg_temperature", 1, 0, None),
        58: ("max_temperature", 1, 0, None),
        253: ("timestamp", 1, 0, "timestamp"),
        254: ("message_index", 1, 0, None),
    },
    20: {
        0: ("latitude", 1, 0, "coordinate"),
        1: ("longitude", 1, 0, "coordinate"),
        3: ("heart_rate", 1, 0, None),
        13: ("temperature", 1, 0, None),
        91: ("absolute_pressure_pa", 1, 0, None),
        92: ("depth", 1000, 0, None),
        93: ("next_stop_depth", 1000, 0, None),
        94: ("next_stop_time", 1, 0, None),
        95: ("time_to_surface", 1, 0, None),
        96: ("ndl_time", 1, 0, None),
        97: ("cns_load", 1, 0, None),
        98: ("n2_load", 1, 0, None),
        123: ("air_time_remaining", 1, 0, None),
        124: ("pressure_sac", 100, 0, None),
        125: ("volume_sac", 100, 0, None),
        126: ("rmv", 100, 0, None),
        127: ("ascent_rate", 1000, 0, None),
        129: ("po2", 100, 0, None),
        136: ("wrist_heart_rate", 1, 0, None),
        253: ("timestamp", 1, 0, "timestamp"),
    },
    21: {
        0: ("event", 1, 0, None),
        1: ("event_type", 1, 0, None),
        3: ("data", 1, 0, None),
        4: ("event_group", 1, 0, None),
        253: ("timestamp", 1, 0, "timestamp"),
    },
    29: {
        0: ("name", 1, 0, None),
        1: ("latitude", 1, 0, "coordinate"),
        2: ("longitude", 1, 0, "coordinate"),
        3: ("symbol", 1, 0, None),
        6: ("description", 1, 0, None),
        19: ("type", 1, 0, None),
        20: ("color", 1, 0, None),
        21: ("display_mode", 1, 0, None),
        253: ("timestamp", 1, 0, "timestamp"),
        254: ("message_index", 1, 0, None),
    },
    258: {
        0: ("name", 1, 0, None),
        1: ("model", 1, 0, None),
        2: ("gf_low", 1, 0, None),
        3: ("gf_high", 1, 0, None),
        4: ("water_type", 1, 0, None),
        5: ("water_density", 1, 0, None),
        6: ("po2_warn", 100, 0, None),
        7: ("po2_critical", 100, 0, None),
        8: ("po2_deco", 100, 0, None),
        18: ("safety_stop_time", 1, 0, None),
        23: ("ccr_low_setpoint", 100, 0, None),
        24: ("ccr_low_setpoint_depth", 1000, 0, None),
        26: ("ccr_high_setpoint", 100, 0, None),
        27: ("ccr_high_setpoint_depth", 1000, 0, None),
        253: ("timestamp", 1, 0, "timestamp"),
        254: ("message_index", 1, 0, None),
    },
    259: {
        0: ("helium_content", 1, 0, None),
        1: ("oxygen_content", 1, 0, None),
        2: ("status", 1, 0, None),
        3: ("mode", 1, 0, None),
        254: ("message_index", 1, 0, None),
    },
    268: {
        0: ("reference_mesg", 1, 0, None),
        1: ("reference_index", 1, 0, None),
        2: ("avg_depth", 1000, 0, None),
        3: ("max_depth", 1000, 0, None),
        4: ("surface_interval", 1, 0, None),
        5: ("start_cns", 1, 0, None),
        6: ("end_cns", 1, 0, None),
        7: ("start_n2", 1, 0, None),
        8: ("end_n2", 1, 0, None),
        9: ("o2_toxicity", 1, 0, None),
        10: ("dive_number", 1, 0, None),
        11: ("bottom_time", 1000, 0, None),
        12: ("avg_pressure_sac", 100, 0, None),
        13: ("avg_volume_sac", 100, 0, None),
        14: ("avg_rmv", 100, 0, None),
        15: ("descent_time", 1000, 0, None),
        16: ("ascent_time", 1000, 0, None),
        17: ("avg_ascent_rate", 1000, 0, None),
        22: ("avg_descent_rate", 1000, 0, None),
        23: ("max_ascent_rate", 1000, 0, None),
        24: ("max_descent_rate", 1000, 0, None),
        25: ("hang_time", 1000, 0, None),
        253: ("timestamp", 1, 0, "timestamp"),
    },
    319: {
        0: ("sensor", 1, 0, None),
        1: ("pressure", 100, 0, None),
        253: ("timestamp", 1, 0, "timestamp"),
    },
    323: {
        0: ("sensor", 1, 0, None),
        1: ("start_pressure", 100, 0, None),
        2: ("end_pressure", 100, 0, None),
        3: ("volume_used", 100, 0, None),
        253: ("timestamp", 1, 0, "timestamp"),
    },
}

MESSAGE_NAMES = {
    0: "file_id",
    2: "device_settings",
    18: "session",
    20: "record",
    21: "event",
    23: "device_info",
    29: "location",
    49: "file_creator",
    258: "dive_settings",
    259: "dive_gas",
    262: "dive_alarm",
    268: "dive_summary",
    319: "tank_update",
    323: "tank_summary",
    393: "dive_apnea_alarm",
}

DIVING_SPORTS = {53, 54, 55, 56, 57}
DIVING_SUBSPORTS = {
    53: "single_gas_diving",
    54: "multi_gas_diving",
    55: "gauge_diving",
    56: "apnea_diving",
    57: "apnea_hunting",
}

GARMIN_PRODUCTS = {
    2604: "Fenix 5X",
    2859: "Descent Mk1",
    3143: "Descent T1",
    3258: "Descent Mk2",
    3542: "Descent Mk2S",
    4005: "Descent G1",
    4132: "Descent G1 Asia",
    4222: "Descent Mk3",
    4223: "Descent Mk3i",
    4442: "Descent T2",
    4518: "Descent X50i",
    4588: "Descent G2",
}

DIVING_MODE_NAMES = set(DIVING_SUBSPORTS.values())


@dataclass
class FieldDef:
    number: int
    size: int
    base_type: int


@dataclass
class RecordDef:
    global_mesg: int
    endian: str
    fields: list[FieldDef]
    dev_fields: list[tuple[int, int, int]]


def garmin_time(value: int | float | None) -> str | None:
    if value is None:
        return None
    return (GARMIN_EPOCH + _dt.timedelta(seconds=int(value))).isoformat().replace("+00:00", "Z")


def semicircles_to_degrees(value: int | float | None) -> float | None:
    if value is None:
        return None
    return float(value) * (180.0 / 2**31)


def is_invalid(raw: Any, invalid: Any) -> bool:
    if invalid is None:
        return False
    if isinstance(raw, float) and math.isnan(raw):
        return True
    return raw == invalid


def read_scalar(data: bytes, base_type: int, endian: str) -> Any:
    info = BASE_TYPES.get(base_type)
    if info is None:
        return data.hex()
    name, size, fmt, invalid = info
    if name == "string":
        return data.split(b"\x00", 1)[0].decode("utf-8", errors="replace")
    if name == "byte":
        return list(data)
    if len(data) != size:
        return data.hex()
    raw = struct.unpack(endian + fmt, data)[0]
    if is_invalid(raw, invalid):
        return None
    return raw


def decode_value(data: bytes, base_type: int, endian: str) -> Any:
    info = BASE_TYPES.get(base_type)
    if info is None:
        return data.hex()
    name, size, _fmt, _invalid = info
    if name == "string":
        return read_scalar(data, base_type, endian)
    if size <= 0 or len(data) <= size:
        return read_scalar(data, base_type, endian)
    values = []
    for offset in range(0, len(data), size):
        chunk = data[offset : offset + size]
        if len(chunk) == size:
            value = read_scalar(chunk, base_type, endian)
            if value is not None:
                values.append(value)
    return values


def apply_field(global_mesg: int, field_number: int, value: Any) -> tuple[str, Any]:
    spec = FIELD_MAP.get(global_mesg, {}).get(field_number)
    if spec is None:
        return f"field_{field_number}", value
    name, scale, offset, kind = spec
    if value is None:
        return name, None
    if kind == "timestamp":
        return name, garmin_time(value)
    if kind == "coordinate":
        return name, semicircles_to_degrees(value)
    if isinstance(value, list):
        converted = [((item / scale) - offset) if isinstance(item, (int, float)) else item for item in value]
        return name, converted
    if isinstance(value, (int, float)) and scale != 1:
        return name, (value / scale) - offset
    if isinstance(value, (int, float)) and offset != 0:
        return name, value - offset
    return name, value


class FitReader:
    def __init__(self, path: Path):
        self.path = path
        self.defs: dict[int, RecordDef] = {}
        self.last_timestamp: int | None = None

    def read(self) -> list[dict[str, Any]]:
        data = self.path.read_bytes()
        if len(data) < 14:
            raise ValueError("FIT file too short")
        header_size = data[0]
        if header_size not in (12, 14):
            raise ValueError(f"unsupported FIT header size {header_size}")
        data_size = struct.unpack_from("<I", data, 4)[0]
        if data[8:12] != b".FIT":
            raise ValueError("missing .FIT signature")
        pos = header_size
        end = header_size + data_size
        if end > len(data):
            raise ValueError("FIT data section exceeds file size")

        records: list[dict[str, Any]] = []
        while pos < end:
            header = data[pos]
            pos += 1
            if header & 0x80:
                local = (header >> 5) & 0x03
                compressed_offset = header & 0x1F
                pos = self._read_data_message(data, pos, local, records, compressed_offset)
            else:
                local = header & 0x0F
                if header & 0x40:
                    pos = self._read_definition(data, pos, local, bool(header & 0x20))
                else:
                    pos = self._read_data_message(data, pos, local, records, None)
        return records

    def _read_definition(self, data: bytes, pos: int, local: int, has_dev_fields: bool) -> int:
        _reserved = data[pos]
        arch = data[pos + 1]
        pos += 2
        endian = "<" if arch == 0 else ">"
        global_mesg = struct.unpack_from(endian + "H", data, pos)[0]
        pos += 2
        field_count = data[pos]
        pos += 1
        fields: list[FieldDef] = []
        for _ in range(field_count):
            fields.append(FieldDef(data[pos], data[pos + 1], data[pos + 2]))
            pos += 3
        dev_fields: list[tuple[int, int, int]] = []
        if has_dev_fields:
            dev_count = data[pos]
            pos += 1
            for _ in range(dev_count):
                dev_fields.append((data[pos], data[pos + 1], data[pos + 2]))
                pos += 3
        self.defs[local] = RecordDef(global_mesg, endian, fields, dev_fields)
        return pos

    def _read_data_message(
        self,
        data: bytes,
        pos: int,
        local: int,
        records: list[dict[str, Any]],
        compressed_offset: int | None,
    ) -> int:
        definition = self.defs.get(local)
        if definition is None:
            raise ValueError(f"data message references missing local definition {local}")
        values: dict[str, Any] = {}
        raw_values: dict[str, Any] = {}
        for field in definition.fields:
            chunk = data[pos : pos + field.size]
            pos += field.size
            decoded = decode_value(chunk, field.base_type, definition.endian)
            name, converted = apply_field(definition.global_mesg, field.number, decoded)
            values[name] = converted
            raw_values[str(field.number)] = decoded
        for _num, size, _idx in definition.dev_fields:
            pos += size

        if compressed_offset is not None and "timestamp" not in values and self.last_timestamp is not None:
            timestamp = (self.last_timestamp & ~0x1F) + compressed_offset
            if timestamp <= self.last_timestamp:
                timestamp += 0x20
            values["timestamp"] = garmin_time(timestamp)
            raw_values["compressed_timestamp"] = timestamp

        raw_ts = raw_values.get("253")
        if isinstance(raw_ts, int):
            self.last_timestamp = raw_ts

        records.append(
            {
                "message_number": definition.global_mesg,
                "message_name": MESSAGE_NAMES.get(definition.global_mesg, f"message_{definition.global_mesg}"),
                "values": values,
                "raw": raw_values,
            }
        )
        return pos


def iter_fit_sources(channel: str, sources: list[str]) -> list[Path]:
    if channel in ("mtp", "ble"):
        raise NotImplementedError(f"{channel} channel is reserved but not implemented yet")
    paths: list[Path] = []
    for source in sources:
        expanded = glob.glob(source)
        if not expanded:
            expanded = [source]
        for item in expanded:
            path = Path(item)
            if channel == "fit-dir" or path.is_dir():
                paths.extend(sorted(path.rglob("*.fit")))
            elif channel == "fit-file" or path.is_file():
                paths.append(path)
    return sorted(set(paths))


def parse_fit(path: Path, include_samples: bool = True) -> dict[str, Any]:
    records = FitReader(path).read()
    by_name: dict[str, list[dict[str, Any]]] = {}
    for record in records:
        by_name.setdefault(record["message_name"], []).append(record["values"])

    file_ids = by_name.get("file_id", [])
    sessions = by_name.get("session", [])
    locations = _locations(by_name.get("location", []))
    summaries = by_name.get("dive_summary", [])
    settings = by_name.get("dive_settings", [])
    gases = by_name.get("dive_gas", [])
    tank_summaries = by_name.get("tank_summary", [])
    tank_updates = by_name.get("tank_update", [])

    session = sessions[0] if sessions else {}
    summary = next((s for s in summaries if s.get("reference_mesg") in (18, "session")), summaries[0] if summaries else {})
    file_id = file_ids[0] if file_ids else {}

    product = file_id.get("product")
    product_name = GARMIN_PRODUCTS.get(product, file_id.get("product_name"))
    sport = session.get("sport")
    sub_sport = session.get("sub_sport")
    mode_name = DIVING_SUBSPORTS.get(sub_sport, str(sub_sport) if sub_sport is not None else None)

    gasmixes = []
    for idx, gas in enumerate(gases):
        oxygen = gas.get("oxygen_content")
        helium = gas.get("helium_content")
        if oxygen is None and helium is None:
            continue
        oxygen_f = (oxygen or 0) / 100.0
        helium_f = (helium or 0) / 100.0
        gasmixes.append(
            {
                "index": gas.get("message_index", idx),
                "oxygen": oxygen_f,
                "helium": helium_f,
                "nitrogen": max(0.0, 1.0 - oxygen_f - helium_f),
                "status": gas.get("status"),
                "mode": gas.get("mode"),
            }
        )

    tanks = [
        {
            "sensor": tank.get("sensor"),
            "beginpressure": tank.get("start_pressure"),
            "endpressure": tank.get("end_pressure"),
            "volume_used": tank.get("volume_used"),
            "timestamp": tank.get("timestamp"),
        }
        for tank in tank_summaries
    ]

    samples = []
    if include_samples:
        for record in by_name.get("record", []):
            sample: dict[str, Any] = {}
            for key in (
                "timestamp",
                "depth",
                "temperature",
                "heart_rate",
                "wrist_heart_rate",
                "ndl_time",
                "next_stop_depth",
                "next_stop_time",
                "time_to_surface",
                "air_time_remaining",
                "cns_load",
                "n2_load",
                "pressure_sac",
                "volume_sac",
                "rmv",
                "ascent_rate",
                "po2",
            ):
                if key in record:
                    sample[key] = record[key]
            if sample:
                samples.append(sample)

    return {
        "source": str(path),
        "datetime": session.get("start_time") or summary.get("timestamp"),
        "device": {
            "manufacturer": file_id.get("manufacturer"),
            "product": product,
            "name": product_name,
            "serial_number": file_id.get("serial_number"),
        },
        "fields": {
            "divemode": mode_name,
            "sport": sport,
            "sub_sport": sub_sport,
            "divetime": summary.get("bottom_time") or session.get("total_timer_time"),
            "maxdepth": summary.get("max_depth"),
            "avgdepth": summary.get("avg_depth"),
            "temperature_minimum": None,
            "temperature_maximum": session.get("max_temperature"),
            "decomodel": _deco_model(settings),
            "location": _location(session) or (locations[0] if locations else None),
            "dive_number": summary.get("dive_number"),
            "end_cns": summary.get("end_cns"),
            "o2_toxicity": summary.get("o2_toxicity"),
        },
        "gasmixes": gasmixes,
        "tanks": tanks,
        "samples": samples,
        "garmin": {
            "dive_settings": settings,
            "dive_summary": summaries,
            "locations": locations,
            "tank_updates": tank_updates,
            "message_counts": _message_counts(records),
            "record_count": len(samples),
        },
    }


def is_dive(payload: dict[str, Any]) -> bool:
    fields = payload.get("fields", {})
    mode = fields.get("divemode")
    return mode in DIVING_MODE_NAMES or fields.get("maxdepth") is not None


def _deco_model(settings: list[dict[str, Any]]) -> dict[str, Any] | None:
    if not settings:
        return None
    setting = settings[0]
    gf_low = setting.get("gf_low")
    gf_high = setting.get("gf_high")
    return {
        "type": "buhlmann",
        "gf_low": gf_low,
        "gf_high": gf_high,
        "model": setting.get("model"),
    }


def _location(session: dict[str, Any]) -> dict[str, Any] | None:
    lat = session.get("start_latitude")
    lon = session.get("start_longitude")
    if lat is None or lon is None:
        return None
    return {"latitude": lat, "longitude": lon, "source": "session"}


def _locations(records: list[dict[str, Any]]) -> list[dict[str, Any]]:
    locations = []
    for record in records:
        lat = record.get("latitude")
        lon = record.get("longitude")
        if lat is None or lon is None:
            continue
        locations.append(
            {
                "name": record.get("name"),
                "latitude": lat,
                "longitude": lon,
                "timestamp": record.get("timestamp"),
                "message_index": record.get("message_index"),
                "source": "fit_location",
            }
        )
    return locations


def _message_counts(records: list[dict[str, Any]]) -> dict[str, int]:
    counts: dict[str, int] = {}
    for record in records:
        key = f"{record['message_number']}:{record['message_name']}"
        counts[key] = counts.get(key, 0) + 1
    return dict(sorted(counts.items()))


def parse_sources(args: argparse.Namespace, include_samples: bool) -> list[dict[str, Any]]:
    paths = iter_fit_sources(args.channel, args.source)
    dives = [parse_fit(path, include_samples=include_samples) for path in paths]
    if getattr(args, "dives_only", False):
        dives = [dive for dive in dives if is_dive(dive)]
    return dives


def command_list(args: argparse.Namespace) -> int:
    paths = iter_fit_sources(args.channel, args.source)
    rows = []
    for path in paths:
        try:
            dive = parse_fit(path, include_samples=False)
            if args.dives_only and not is_dive(dive):
                continue
            rows.append(
                {
                    "file": str(path),
                    "datetime": dive["datetime"],
                    "device": dive["device"].get("name"),
                    "mode": dive["fields"].get("divemode"),
                    "divetime": dive["fields"].get("divetime"),
                    "maxdepth": dive["fields"].get("maxdepth"),
                    "gasmixes": len(dive["gasmixes"]),
                    "tanks": len(dive["tanks"]),
                }
            )
        except Exception as exc:
            rows.append({"file": str(path), "error": str(exc)})
    print(json.dumps(rows, indent=2, ensure_ascii=False))
    return 0


def command_parse(args: argparse.Namespace) -> int:
    dives = parse_sources(args, include_samples=not args.no_samples)
    payload: Any = dives[0] if len(dives) == 1 else dives
    text = json.dumps(payload, indent=2, ensure_ascii=False)
    if args.output:
        Path(args.output).write_text(text + "\n", encoding="utf-8")
    else:
        print(text)
    return 0


def fmt_duration(seconds: float | int | None) -> str | None:
    if seconds is None:
        return None
    total = max(0, int(round(float(seconds))))
    return f"{total // 60}:{total % 60:02d} min"


def iso_to_datetime(value: str | None) -> _dt.datetime | None:
    if not value:
        return None
    if value.endswith("Z"):
        value = value[:-1] + "+00:00"
    return _dt.datetime.fromisoformat(value)


def sample_offset(start: _dt.datetime | None, timestamp: str | None) -> str | None:
    if start is None or timestamp is None:
        return None
    current = iso_to_datetime(timestamp)
    if current is None:
        return None
    return fmt_duration((current - start).total_seconds())


def pct(value: float | int | None) -> str | None:
    if value is None:
        return None
    return f"{float(value) * 100:.1f}%"


def gas_description(index: int, oxygen: float | None, helium: float | None) -> str:
    o2 = int(round((oxygen or 0) * 100))
    he = int(round((helium or 0) * 100))
    if o2 >= 99:
        name = "Oxygen"
    elif he == 0 and o2 == 21:
        name = "Air"
    elif he == 0:
        name = f"EAN{o2}"
    else:
        name = f"{o2}/{he}"
    return f"{index}. {name}"


def add_attr(element: ET.Element, key: str, value: Any, suffix: str | None = None, precision: int | None = None) -> None:
    if value is None:
        return
    if isinstance(value, float) and precision is not None:
        text = f"{value:.{precision}f}"
    else:
        text = str(value)
    if suffix:
        text = f"{text} {suffix}"
    element.set(key, text)


def build_subsurface_xml(dives: list[dict[str, Any]]) -> ET.Element:
    root = ET.Element("divelog", {"program": "subsurface", "version": "3"})
    ET.SubElement(root, "settings")
    sites = ET.SubElement(root, "divesites")
    dives_el = ET.SubElement(root, "dives")

    for idx, dive in enumerate(dives, start=1):
        dt = iso_to_datetime(dive.get("datetime"))
        fields = dive.get("fields", {})

        dive_attrs = {"number": str(idx)}
        if dt is not None:
            dive_attrs["date"] = dt.date().isoformat()
            dive_attrs["time"] = dt.time().strftime("%H:%M:%S")

        location = fields.get("location")
        if location:
            site_id = f"garmin-{abs(hash((location.get('latitude'), location.get('longitude')))) & 0xFFFFFFFF:08x}"
            if sites.find(f"./site[@uuid='{site_id}']") is None:
                site = ET.SubElement(sites, "site")
                site.set("uuid", site_id)
                site.set("name", f"Garmin site {idx}")
                site.set("gps", f"{location.get('latitude'):.6f} {location.get('longitude'):.6f}")
            dive_attrs["divesiteid"] = site_id

        dive_el = ET.SubElement(dives_el, "dive", dive_attrs)
        for gas in dive.get("gasmixes", []):
            cyl = ET.SubElement(dive_el, "cylinder")
            gas_index = gas.get("index", len(dive_el.findall("cylinder")))
            cyl.set("description", gas_description(int(gas_index), gas.get("oxygen"), gas.get("helium")))
            o2 = pct(gas.get("oxygen"))
            he = pct(gas.get("helium"))
            if o2:
                cyl.set("o2", o2)
            if he:
                cyl.set("he", he)

        computer = ET.SubElement(dive_el, "divecomputer")
        computer.set("model", dive.get("device", {}).get("name") or "Garmin Descent")
        computer.set("deviceid", str(dive.get("device", {}).get("serial_number") or "garmin"))
        computer.set("diveid", f"garmin-{dive.get('datetime') or idx}")
        if fields.get("divemode") in ("apnea_diving", "apnea_hunting"):
            computer.set("dctype", "Freedive")

        depth_el = ET.SubElement(computer, "depth")
        add_attr(depth_el, "max", fields.get("maxdepth"), "m", 2)
        add_attr(depth_el, "mean", fields.get("avgdepth"), "m", 2)

        temp_max = fields.get("temperature_maximum")
        if temp_max is not None:
            temp_el = ET.SubElement(computer, "temperature")
            add_attr(temp_el, "water", temp_max, "C", 1 if isinstance(temp_max, float) else None)

        deco = fields.get("decomodel") or {}
        if deco:
            extra = ET.SubElement(computer, "extradata")
            extra.set("key", "Garmin deco")
            extra.set("value", json.dumps(deco, separators=(",", ":")))

        start = iso_to_datetime(dive.get("datetime"))
        previous: dict[str, Any] = {}
        for sample in dive.get("samples", []):
            sample_el = ET.SubElement(computer, "sample")
            offset = sample_offset(start, sample.get("timestamp"))
            if offset:
                sample_el.set("time", offset)
            add_attr(sample_el, "depth", sample.get("depth"), "m", 2)
            if "temperature" in sample and sample.get("temperature") != previous.get("temperature"):
                add_attr(sample_el, "temp", sample.get("temperature"), "C", 1 if isinstance(sample.get("temperature"), float) else None)
            hr = sample.get("heart_rate") or sample.get("wrist_heart_rate")
            if hr is not None and hr != previous.get("heart_rate"):
                sample_el.set("heartbeat", str(hr))
            if sample.get("ndl_time") is not None and sample.get("ndl_time") != previous.get("ndl_time"):
                sample_el.set("ndl", fmt_duration(sample.get("ndl_time")) or "")
            if sample.get("time_to_surface") is not None and sample.get("time_to_surface") != previous.get("time_to_surface"):
                sample_el.set("tts", fmt_duration(sample.get("time_to_surface")) or "")
            if sample.get("next_stop_depth") is not None and sample.get("next_stop_depth") != previous.get("next_stop_depth"):
                add_attr(sample_el, "stopdepth", sample.get("next_stop_depth"), "m", 1)
            if sample.get("next_stop_time") is not None and sample.get("next_stop_time") != previous.get("next_stop_time"):
                sample_el.set("stoptime", fmt_duration(sample.get("next_stop_time")) or "")
            if sample.get("cns_load") is not None and sample.get("cns_load") != previous.get("cns_load"):
                sample_el.set("cns", str(sample.get("cns_load")))
            previous = sample

    return root


def command_subsurface(args: argparse.Namespace) -> int:
    dives = parse_sources(args, include_samples=True)
    root = build_subsurface_xml(dives)
    ET.indent(root, space="  ")
    tree = ET.ElementTree(root)
    if args.output:
        tree.write(args.output, encoding="utf-8", xml_declaration=True)
    else:
        print(ET.tostring(root, encoding="unicode"))
    return 0


def command_dump(args: argparse.Namespace) -> int:
    paths = iter_fit_sources(args.channel, args.source)
    payload = []
    for path in paths:
        records = FitReader(path).read()
        payload.append({"source": str(path), "message_counts": _message_counts(records)})
    print(json.dumps(payload[0] if len(payload) == 1 else payload, indent=2, ensure_ascii=False))
    return 0


def command_ble_init(args: argparse.Namespace) -> int:
    core = GarminBleCore(reliable=not args.no_reliable, max_packet_size=args.max_packet_size)
    core.start()
    print(json.dumps({
        "writes": hex_packets(core.take_writes()),
        "note": "write these bytes to the selected Garmin ML write characteristic; feed notifications back into GarminBleCore.on_notification",
    }, indent=2))
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Garmin Descent sidecar for LDC-like import paths")
    subparsers = parser.add_subparsers(dest="command", required=True)
    for name, handler in (("list", command_list), ("parse", command_parse), ("dump", command_dump), ("subsurface", command_subsurface), ("ble-init", command_ble_init)):
        sub = subparsers.add_parser(name)
        if name != "ble-init":
            sub.add_argument("--channel", choices=("fit-file", "fit-dir", "mtp", "ble"), default="fit-file")
            sub.add_argument("--source", nargs="+", required=True)
            sub.add_argument("--dives-only", action="store_true")
        sub.set_defaults(handler=handler)
        if name == "parse":
            sub.add_argument("--output")
            sub.add_argument("--no-samples", action="store_true")
        if name == "subsurface":
            sub.add_argument("--output")
        if name == "ble-init":
            sub.add_argument("--max-packet-size", type=int, default=20)
            sub.add_argument("--no-reliable", action="store_true")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        return args.handler(args)
    except NotImplementedError as exc:
        print(str(exc), file=sys.stderr)
        return 2
    except Exception as exc:
        print(f"garmin sidecar failed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
