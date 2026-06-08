# Garmin LDC Sidecar

Garmin Descent support lives here as a sidecar while the BLE path is tested with
real watches. The protocol implementation is the C99 core under `c/`; C++,
LDC-style, WASM and WSS layers are thin wrappers around the same core.

Docs:

- `docs/garmin-sidecar-architecture.md`
- `docs/garmin-wss-protocol.md`
- `docs/garmin-miniapp-ux.md`

Run native smoke tests:

```powershell
.\test.ps1
```

or:

```sh
make test
```
