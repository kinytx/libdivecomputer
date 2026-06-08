# Garmin WSS Protocol / Garmin WSS 协议

## Overview / 概览

EN: The client uses the same WSS envelope as traditional LDC sessions. The
server selects the implementation from `driver` and `channel`.

中文：客户端统一使用同一套 WSS envelope。服务器通过 `driver` 和 `channel` 区分走
Garmin sidecar 还是传统 LDC。

- `driver=garmin-sidecar`, `channel=ble-bridge`
  - EN: client owns BLE, backend owns Garmin ML/MLR/GFDI state.
  - 中文：客户端负责 BLE，后端负责 Garmin ML/MLR/GFDI 状态机。
- `driver=ldc`, `channel=serial/usb/mtp/file`
  - EN: backend wraps a normal libdivecomputer-style session.
  - 中文：后端包装普通 libdivecomputer session。

## Session Open / 打开会话

Garmin BLE bridge / Garmin BLE 桥：

```json
{"type":"session.open","sessionId":"watch-1","driver":"garmin-sidecar","channel":"ble-bridge","mtu":20}
```

Traditional LDC / 传统 LDC：

```json
{"type":"session.open","sessionId":"dc-1","driver":"ldc","channel":"serial","transport":{"path":"COM5"}}
```

EN: Legacy Garmin messages are normalized by `device_session_router.js`.

中文：迁移期旧 Garmin 消息会由 `device_session_router.js` 归一化。

```json
{"type":"ble.ready","mtu":20}
{"type":"ble.notify","data":"base64..."}
{"type":"garmin.requestDirectory"}
{"type":"garmin.requestFile","fileIndex":4660,"fileSize":123456}
```

## Client To Backend / 客户端到后端

```json
{"type":"transport.notify","sessionId":"watch-1","data":"base64..."}
{"type":"device.foreach","sessionId":"watch-1"}
{"type":"device.requestFile","sessionId":"watch-1","fileIndex":4660,"fileSize":123456,"dataType":128,"subType":4}
{"type":"session.close","sessionId":"watch-1"}
```

EN: For Garmin, `transport.notify` is a BLE notification from the watch.

中文：对 Garmin 来说，`transport.notify` 就是手表发出的 BLE notify 字节。

## Backend To Client / 后端到客户端

```json
{"type":"session.opened","sessionId":"watch-1","driver":"garmin-sidecar","channel":"ble-bridge"}
{"type":"transport.write","sessionId":"watch-1","channel":"ble-bridge","data":"base64..."}
{"type":"device.event","sessionId":"watch-1","event":{"type":1,"service":0,"messageId":0}}
{"type":"device.progress","sessionId":"watch-1","current":1024,"maximum":4096}
{"type":"device.directory","sessionId":"watch-1","format":"garmin-directory","data":"base64..."}
{"type":"device.dive","sessionId":"watch-1","format":"fit","data":"base64..."}
{"type":"device.error","sessionId":"watch-1","message":"..."}
```

EN: The client should only special-case local transport actions.

中文：客户端只需要特殊处理本地传输动作：

- `transport.write`
  - EN: write `data` to the selected Garmin BLE write characteristic.
  - 中文：把 `data` 写入已选 Garmin BLE write characteristic。
- `device.dive`
  - EN: pass completed FIT bytes into the import staging flow.
  - 中文：把完整 FIT bytes 交给导入暂存流程，而不是直接写正式日志。

## Router Integration / Router 接入

```js
import { DeviceSessionRouter } from './device_session_router.js';

const router = new DeviceSessionRouter(socket, {
  createGarminCore: () => createGarminBleCore(Module),
  createLdcSession: (packet) => createLdcSession(packet),
});

socket.on('message', (message) => router.onMessage(message));
```

EN: `wss/garmin_ble_wss_relay.js` is kept as old narrow Garmin-only glue. New
work should use `DeviceSessionRouter`.

中文：`wss/garmin_ble_wss_relay.js` 只是旧的 Garmin-only glue。新工作应使用
`DeviceSessionRouter`。
