function bytesToBase64(bytes) {
  return Buffer.from(bytes).toString('base64');
}

function base64ToBytes(text) {
  return new Uint8Array(Buffer.from(text, 'base64'));
}

function eventToJson(event) {
  return {
    type: event.type,
    service: event.service,
    messageId: event.messageId,
    payload: event.payload && event.payload.length ? bytesToBase64(event.payload) : undefined,
  };
}

const GarminEvent = {
  FILE_COMPLETE: 6,
  DIRECTORY_ENTRY: 7,
};

export class GarminBleWssRelay {
  constructor(core, socket, options = {}) {
    this.core = core;
    this.socket = socket;
    this.options = {
      reliable: true,
      maxPacketSize: 20,
      ...options,
    };
    this.fileChunks = [];
    this.currentFileIndex = null;
  }

  start() {
    this.core.init({
      reliable: this.options.reliable,
      maxPacketSize: this.options.maxPacketSize,
    });
    this.sendWrites(this.core.start());
  }

  onMessage(message) {
    const packet = typeof message === 'string' ? JSON.parse(message) : message;
    switch (packet.type) {
      case 'ble.ready':
        this.options.maxPacketSize = packet.maxPacketSize || packet.mtu || this.options.maxPacketSize;
        this.start();
        break;
      case 'ble.notify':
        this.onBleNotify(base64ToBytes(packet.data));
        break;
      case 'garmin.requestDirectory':
        this.requestDirectory();
        break;
      case 'garmin.requestFile':
        this.currentFileIndex = packet.fileIndex;
        this.requestFile(packet);
        break;
      default:
        this.send({ type: 'error', message: `unknown message type: ${packet.type}` });
        break;
    }
  }

  onBleNotify(bytes) {
    const result = this.core.onNotification(bytes);
    this.sendWrites(result.writes);
    if (result.event && result.event.type) {
      this.handleEvent(result.event);
      this.send({ type: 'garmin.event', event: eventToJson(result.event) });
    }
  }

  requestDirectory() {
    if (!this.core.requestDirectory) {
      this.send({ type: 'error', message: 'core does not expose requestDirectory' });
      return;
    }
    this.sendWrites(this.core.requestDirectory());
  }

  requestFile(file) {
    if (!this.core.requestFile) {
      this.send({ type: 'error', message: 'core does not expose requestFile' });
      return;
    }
    this.currentFileIndex = file.fileIndex;
    this.sendWrites(this.core.requestFile(file));
  }

  handleEvent(event) {
    if (event.type === GarminEvent.DIRECTORY_ENTRY && event.payload && event.payload.length) {
      this.send({
        type: 'garmin.directory',
        data: bytesToBase64(event.payload),
        firstEntry: event.file,
      });
    }

    if (event.type === GarminEvent.FILE_COMPLETE && event.payload && event.payload.length) {
      this.send({
        type: 'garmin.file',
        fileIndex: this.currentFileIndex,
        data: bytesToBase64(event.payload),
      });
    }
  }

  sendWrites(writes) {
    for (const packet of writes || []) {
      this.send({ type: 'ble.write', data: bytesToBase64(packet) });
    }
  }

  send(payload) {
    this.socket.send(JSON.stringify(payload));
  }
}
