export function createGarminBleCore(Module) {
  const u8 = () => Module.HEAPU8;

  function readBytes(ptr, size) {
    return new Uint8Array(u8().slice(ptr, ptr + size));
  }

  function withBytes(bytes, fn) {
    const input = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
    const ptr = Module._malloc(input.length || 1);
    try {
      u8().set(input, ptr);
      return fn(ptr, input.length);
    } finally {
      Module._free(ptr);
    }
  }

  return {
    init({ reliable = true, maxPacketSize = 20 } = {}) {
      Module._garmin_wasm_init(reliable ? 1 : 0, maxPacketSize);
    },

    start() {
      const rc = Module._garmin_wasm_start();
      if (rc !== 0) throw new Error(`Garmin start failed: ${rc}`);
      return this.takeWrites();
    },

    onNotification(bytes) {
      const rc = withBytes(bytes, (ptr, size) => Module._garmin_wasm_on_notification(ptr, size));
      if (rc !== 0) throw new Error(`Garmin notify failed: ${rc}`);
      return {
        event: this.lastEvent(),
        writes: this.takeWrites(),
      };
    },

    sendGfdi(messageId, payload = new Uint8Array()) {
      const rc = withBytes(payload, (ptr, size) => Module._garmin_wasm_send_gfdi(messageId, ptr, size));
      if (rc !== 0) throw new Error(`Garmin GFDI send failed: ${rc}`);
      return this.takeWrites();
    },

    requestFilter() {
      const rc = Module._garmin_wasm_request_filter();
      if (rc !== 0) throw new Error(`Garmin filter request failed: ${rc}`);
      return this.takeWrites();
    },

    requestDirectory() {
      const rc = Module._garmin_wasm_request_directory();
      if (rc !== 0) throw new Error(`Garmin directory request failed: ${rc}`);
      return this.takeWrites();
    },

    requestFile({ fileIndex, dataType = 128, subType = 4, fileSize = 0 }) {
      const rc = Module._garmin_wasm_request_file(fileIndex, dataType, subType, fileSize);
      if (rc !== 0) throw new Error(`Garmin file request failed: ${rc}`);
      return this.takeWrites();
    },

    takeWrites() {
      const writes = [];
      while (Module._garmin_wasm_take_write()) {
        writes.push(readBytes(Module._garmin_wasm_write_ptr(), Module._garmin_wasm_write_size()));
      }
      return writes;
    },

    lastEvent() {
      const payloadSize = Module._garmin_wasm_event_payload_size();
      const payloadPtr = Module._garmin_wasm_event_payload_ptr();
      return {
        type: Module._garmin_wasm_event_type(),
        service: Module._garmin_wasm_event_service(),
        messageId: Module._garmin_wasm_event_message_id(),
        offset: Module._garmin_wasm_event_offset(),
        totalSize: Module._garmin_wasm_event_total_size(),
        file: {
          fileIndex: Module._garmin_wasm_event_file_index(),
          dataType: Module._garmin_wasm_event_file_data_type(),
          subType: Module._garmin_wasm_event_file_sub_type(),
          fileSize: Module._garmin_wasm_event_file_size(),
        },
        payload: payloadPtr && payloadSize ? readBytes(payloadPtr, payloadSize) : new Uint8Array(),
      };
    },
  };
}
