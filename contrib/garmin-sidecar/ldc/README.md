# Garmin LDC Sidecar Adapter

This folder is the current "LDC bypass" integration point.

It does not modify libdivecomputer's public ABI. Instead, it wraps the Garmin
BLE/GFDI C core in a callback adapter that feels like a small device backend:

- `garmin_ldc_adapter_start()` queues initial BLE writes.
- `garmin_ldc_adapter_notify()` accepts BLE notification bytes.
- `garmin_ldc_adapter_request_directory()` starts Garmin directory download.
- `garmin_ldc_adapter_request_file()` starts a file download.
- write callback sends bytes to the actual BLE transport.
- event callback receives parsed Garmin protocol events.

## Higher-Level Sidecar Device

`garmin_ldc_sidecar_device.h` adds a small facade above the raw adapter. This is
the preferred entry point for callers that want something close to the normal
LDC shape without registering a new upstream `dc_device_t` backend yet:

- `garmin_ldc_sidecar_open()` creates the session.
- `garmin_ldc_sidecar_start()` queues the initial Garmin ML handshake writes.
- `garmin_ldc_sidecar_notify()` feeds BLE notification bytes into the session.
- `garmin_ldc_sidecar_foreach()` requests the Garmin directory and delivers
  completed FIT file bytes through a dive callback.
- `garmin_ldc_sidecar_close()` releases the session.

The facade keeps the transport outside the core. A Mini Program, Node WSS relay,
desktop helper, or future LDC backend can all provide the same BLE write and
notification plumbing while sharing the same Garmin protocol code.

This adapter can be used by:

- a future libdivecomputer device backend
- a backend WSS session
- a desktop helper
- tests that replay captured BLE packets

## Minimal Flow

```c
garmin_ldc_adapter_t adapter;
garmin_ldc_adapter_init(&adapter, write_cb, event_cb, userdata);
garmin_ldc_adapter_set_download_buffer(&adapter, buffer, buffer_size);

garmin_ldc_adapter_start(&adapter);

/* for each BLE notification */
garmin_ldc_adapter_notify(&adapter, data, size);

/* once GFDI is registered */
garmin_ldc_adapter_request_directory(&adapter);
```

The event callback will receive directory/file completion events. The file
payload points to the caller-provided download buffer.

## LDC-Style Flow

```c
garmin_ldc_sidecar_device_t *device = NULL;

garmin_ldc_sidecar_open(&device, write_cb, progress_cb, directory_cb,
                        userdata, buffer, buffer_size);
garmin_ldc_sidecar_start(device);

/* for each BLE notification */
garmin_ldc_sidecar_notify(device, data, size);

/* once the session reaches GFDI_READY */
garmin_ldc_sidecar_foreach(device, dive_cb, userdata);

garmin_ldc_sidecar_close(device);
```

This is still a sidecar, not a committed LDC public ABI change. The goal is to
let Garmin follow the same application-level rhythm as existing LDC devices
while we keep the Garmin BLE/WSS transport separate during real watch testing.
