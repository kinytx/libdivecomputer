# LDC + Garmin WASM Unified Plan

This plan keeps libdivecomputer and Garmin BLE work aligned while avoiding an
ABI change in the main library during exploration.

## Target Architecture

```text
Garmin watch <BLE> mini-program <WSS> backend JS
                                      |
                                      +-- Garmin BLE/GFDI WASM core
                                      |
                                      +-- parser-only LDC/FIT WASM layer
```

The mini-program owns platform BLE and does not implement Garmin protocol
logic. The backend owns Garmin protocol state and file assembly.

## Module Split

### Garmin BLE/GFDI Core

Location:

- `contrib/garmin-sidecar/c`
- `contrib/garmin-sidecar/wasm`
- `contrib/garmin-sidecar/wss`

Responsibilities:

- Garmin ML/MLR framing.
- Garmin COBS and GFDI CRC.
- GFDI filter, directory download, file download.
- Directory entry parsing.
- File completion event with FIT bytes.

### LDC Sidecar Adapter

Location:

- `contrib/garmin-sidecar/ldc`

Responsibilities:

- Provide a libdivecomputer-like callback bridge.
- Let a future LDC device implementation supply BLE writes and receive Garmin events.
- Keep Garmin code outside the main LDC ABI for now.

### Parser-Only WASM

Future target:

- `ldc-wasm-parser`

Responsibilities:

- Parse already-downloaded dive bytes.
- Avoid serial/USB/HID/Bluetooth platform code.
- Expose fields/samples as JSON or compact binary events.

This should not include LDC transport backends.

## Current Garmin Sidecar Status

Implemented:

- C99 Garmin protocol core.
- C++ RAII wrapper.
- LDC-style callback adapter.
- WASM C bridge and JS helper.
- WSS relay sketch for mini-program/backend forwarding.
- FIT file parser sidecar for local `.FIT` files.
- Basic Subsurface XML export from local `.FIT` files.

Verified locally:

- C core smoke test.
- C++ wrapper smoke test.
- LDC adapter smoke test.
- WASM C bridge compile check with native C compiler.

Not verified yet:

- Real Garmin BLE handshake.
- Real directory download.
- Real FIT download over WSS.
- Emscripten build, because `emcc` is not installed in this environment.

## Mini-Program Test Flow

1. Mini-program scans and connects to the Garmin watch.
2. Mini-program finds Garmin ML service `6A4E2800-667B-11E3-949A-0800200C9A66`.
3. Mini-program chooses receive/write characteristic pair such as `2810/2820`.
4. Mini-program enables notify on receive characteristic.
5. Mini-program opens WSS session to backend and sends `ble.ready`.
6. Backend starts Garmin core and sends `ble.write` packets.
7. Mini-program writes every `ble.write` packet to Garmin.
8. Mini-program forwards every notify packet as `ble.notify`.
9. Backend requests directory, selects FIT entries, requests files.
10. Backend emits `garmin.file` with FIT bytes.

## Next Implementation Steps

1. Build the WASM artifact with Emscripten.
2. Add a real Node WSS server harness around `GarminBleWssRelay`.
3. Add per-session file assembly and persistence.
4. Add directory entry filtering for `ACTIVITY` FIT files first.
5. Feed downloaded FIT bytes into the existing Python FIT sidecar, then replace with parser-only WASM.
6. Add packet logs from the first real mini-program test and compare with Gadgetbridge behavior.
