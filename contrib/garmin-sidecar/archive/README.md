# Garmin Sidecar Archive

This folder keeps earlier experiments that are useful for reference but are no
longer the primary integration path.

Current primary path:

- `c/`: one Garmin BLE/GFDI protocol core.
- `ldc/`: LDC-style sidecar adapter and device facade.
- `wasm/`: WASM bridge around the same C core.
- `wss/`: unified WSS session router with `driver` and `channel` routing.

Archived material should not be used as the production protocol source. It can
still be useful for comparing FIT parsing ideas or older command-line flows.
