# Garmin Sidecar Architecture / Garmin 旁路架构

## Goal / 目标

EN: Garmin Descent support is implemented as a sidecar while the BLE path is
being tested with real watches. It intentionally does not change the
libdivecomputer public ABI yet.

中文：Garmin Descent 支持目前以 sidecar/旁路形式实现，用于在真实手表上验证 BLE
路线。当前不修改 libdivecomputer 的公开 ABI。

EN: The design keeps one Garmin protocol implementation and multiple thin
wrappers.

中文：设计原则是 Garmin 协议只实现一套，其他语言、WASM、LDC-style、WSS 都只是薄封装。

```text
client / mini-program / app
        |
        | unified WSS messages
        v
backend DeviceSessionRouter
        |
        +-- driver=garmin-sidecar, channel=ble-bridge -> Garmin C/WASM core
        |
        +-- driver=ldc, channel=serial/usb/mtp/file -> LDC session wrapper
```

## Active Layout / 当前目录

EN:

- `c/`: transport-neutral Garmin BLE/GFDI C core.
- `cpp/`: small C++ RAII wrapper.
- `ldc/`: LDC-style adapter and sidecar device facade.
- `wasm/`: C/WASM bridge plus JavaScript helper.
- `wss/`: unified WSS session router using `driver` and `channel`.
- `archive/`: older prototypes kept for reference only.

中文：

- `c/`：传输无关的 Garmin BLE/GFDI C 核心。
- `cpp/`：C++ RAII 薄封装。
- `ldc/`：LDC-style adapter 和 sidecar device facade。
- `wasm/`：C/WASM bridge 与 JS helper。
- `wss/`：通过 `driver/channel` 路由的统一 WSS session router。
- `archive/`：旧原型，仅作参考。

## LDC-Style Facade / LDC 风格封装

EN: Use `ldc/garmin_ldc_sidecar_device.h` when callers want an
application-facing shape close to the normal LDC rhythm.

中文：如果调用方希望接近 LDC 的 `open / foreach / close` 使用节奏，可以使用
`ldc/garmin_ldc_sidecar_device.h`。

```c
garmin_ldc_sidecar_device_t device;
garmin_ldc_sidecar_open(&device, &callbacks, download_buffer, buffer_size);
garmin_ldc_sidecar_start(&device);

/* feed BLE notifications from the client bridge */
garmin_ldc_sidecar_notify(&device, data, size);

/* request directory / files through the same session */
garmin_ldc_sidecar_foreach(&device);
garmin_ldc_sidecar_request_file(&device, &entry);

garmin_ldc_sidecar_close(&device);
```

EN: This is still a sidecar, not an upstream `dc_device_t` backend.

中文：这仍然是旁路层，不是正式并入上游的 `dc_device_t` backend。

## Native Tests / 本地测试

EN: Run all native smoke tests:

中文：运行全部 native smoke test：

```powershell
.\test.ps1
```

or:

```sh
make test
```

## WASM

EN: The `wasm/` folder contains a C bridge plus a JavaScript helper.

中文：`wasm/` 目录包含 C bridge 和 JavaScript helper。

```sh
emcc -O2 c/garmin_ble_core.c wasm/garmin_ble_wasm.c \
  -sMODULARIZE=1 \
  -sEXPORT_ES6=1 \
  -sEXPORTED_FUNCTIONS='["_malloc","_free","_garmin_wasm_init","_garmin_wasm_start","_garmin_wasm_on_notification","_garmin_wasm_send_gfdi","_garmin_wasm_request_filter","_garmin_wasm_request_directory","_garmin_wasm_request_file","_garmin_wasm_take_write","_garmin_wasm_write_ptr","_garmin_wasm_write_size","_garmin_wasm_event_type","_garmin_wasm_event_service","_garmin_wasm_event_message_id","_garmin_wasm_event_payload_ptr","_garmin_wasm_event_payload_size","_garmin_wasm_event_offset","_garmin_wasm_event_total_size","_garmin_wasm_event_file_index","_garmin_wasm_event_file_data_type","_garmin_wasm_event_file_sub_type","_garmin_wasm_event_file_size"]' \
  -o wasm/garmin_ble_core.mjs
```

## Archived Work / 已归档内容

EN: The older Python FIT/BLE command-line prototype has been moved to
`archive/legacy-python`. Keep it as temporary reference only.

中文：旧的 Python FIT/BLE 命令行原型已移动到 `archive/legacy-python`，仅作临时参考。

## License Note / 许可证注意

EN: Use Gadgetbridge as behavior reference and test oracle only. Do not copy
AGPL Gadgetbridge code into this sidecar or libdivecomputer.

中文：Gadgetbridge 只能作为行为参考和测试 oracle。不要把 AGPL 的 Gadgetbridge 代码复制进
这个 sidecar 或 libdivecomputer。
