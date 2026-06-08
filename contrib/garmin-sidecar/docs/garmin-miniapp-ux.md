# Garmin Mini-Program UX Notes / Garmin 小程序 UX 说明

## Role Split / 职责划分

EN: The mini-program should not implement Garmin ML/MLR/GFDI. It only bridges
BLE bytes to the backend.

中文：小程序不应实现 Garmin ML/MLR/GFDI，只负责把 BLE 字节桥接到后端。

```text
Garmin watch <BLE> mini-program <WSS> backend Garmin core
```

Mini-program responsibilities / 小程序职责：

- EN: scan/connect Garmin watch.
  中文：扫描并连接 Garmin 手表。
- EN: discover Garmin ML service.
  中文：发现 Garmin ML service。
- EN: select receive/write characteristic pair.
  中文：选择 receive/write characteristic pair。
- EN: enable notify.
  中文：开启 notify。
- EN: forward notify bytes to WSS.
  中文：把 notify 字节转发到 WSS。
- EN: execute `transport.write` bytes from WSS.
  中文：执行 WSS 下发的 `transport.write`。
- EN: display directory, progress and staged FIT results.
  中文：展示目录、进度和暂存 FIT 结果。

Backend responsibilities / 后端职责：

- Garmin ML close/register.
- MLR ACK/framing.
- COBS/GFDI parsing.
- Directory request.
- FIT file request.
- Import staging / 导入暂存。

## BLE Service / BLE 服务

Garmin ML service:

```text
6A4E2800-667B-11E3-949A-0800200C9A66
```

Characteristic pairs / 特征值配对：

```text
receive: 2810..2814
write:   2820..2824
```

EN: The client should pick a matched receive/write pair and keep it in
connection state.

中文：客户端应选择一组匹配的 receive/write pair，并保存在连接状态里。

## UX Flow / UX 流程

1. EN: connect watch.  
   中文：连接手表。
2. EN: open WSS session with `driver=garmin-sidecar`, `channel=ble-bridge`.  
   中文：打开 WSS 会话，声明 `driver=garmin-sidecar`、`channel=ble-bridge`。
3. EN: show "registering Garmin service" until GFDI ready.  
   中文：显示“正在注册 Garmin 服务”，直到 GFDI ready。
4. EN: request directory.  
   中文：请求目录。
5. EN: show activity/FIT candidates.  
   中文：展示 activity/FIT 候选文件。
6. EN: request selected files.  
   中文：请求选中文件。
7. EN: show download progress.  
   中文：展示下载进度。
8. EN: when a FIT file completes, send it into the same import staging UX used
   by normal LDC imports.  
   中文：FIT 文件完成后，进入与普通 LDC 导入相同的暂存确认 UX。

## Client Events / 客户端事件

- `transport.write`
  - EN: BLE write required.
  - 中文：需要小程序执行 BLE 写入。
- `device.progress`
  - EN: current/max bytes.
  - 中文：下载进度。
- `device.directory`
  - EN: Garmin directory payload.
  - 中文：Garmin 目录 payload。
- `device.dive`
  - EN: completed FIT bytes.
  - 中文：完整 FIT bytes。
- `device.error`
  - EN: recoverable or fatal error.
  - 中文：可恢复或致命错误。

EN: `device.dive` for Garmin should not directly create a formal log. Feed it
into the import staging API first, then let the user confirm.

中文：Garmin 的 `device.dive` 不应直接创建正式日志。应先进入导入暂存 API，再让用户确认。

## Error Hints / 错误提示

Common recoverable states / 常见可恢复状态：

- EN: watch disconnected. 中文：手表断开。
- EN: notify not enabled. 中文：notify 未开启。
- EN: wrong characteristic pair. 中文：characteristic pair 选错。
- EN: backend session opened but no `transport.write`. 中文：后端会话已开但没有下发 `transport.write`。
- EN: directory returned no ACTIVITY FIT files. 中文：目录里没有 ACTIVITY FIT 文件。
- EN: FIT downloaded but parser/staging failed. 中文：FIT 已下载但解析或暂存失败。

Diagnostics / 诊断信息建议包含：

- service UUID
- receive/write characteristic UUID
- MTU
- last WSS message type
- last BLE notify length
- last backend error
