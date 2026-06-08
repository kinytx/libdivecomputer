function bytesToBase64(bytes) {
  return Buffer.from(bytes).toString('base64');
}

function base64ToBytes(text) {
  return new Uint8Array(Buffer.from(text || '', 'base64'));
}

function sendJson(socket, payload) {
  socket.send(JSON.stringify(payload));
}

function normalizeDriver(driver) {
  if (driver === 'garmin' || driver === 'garmin-sidecar') {
    return 'garmin-sidecar';
  }
  if (driver === 'ldc' || driver === 'libdivecomputer') {
    return 'ldc';
  }
  return driver || '';
}

function normalizeMessage(message) {
  const packet = typeof message === 'string' ? JSON.parse(message) : message;

  switch (packet.type) {
    case 'ble.ready':
      return {
        ...packet,
        type: 'session.open',
        driver: packet.driver || 'garmin-sidecar',
        channel: packet.channel || 'ble-bridge',
      };
    case 'ble.notify':
      return { ...packet, type: 'transport.notify' };
    case 'garmin.requestDirectory':
      return { ...packet, type: 'device.foreach' };
    case 'garmin.requestFile':
      return { ...packet, type: 'device.requestFile' };
    default:
      return packet;
  }
}

function eventToJson(event) {
  return {
    type: event.type,
    service: event.service,
    messageId: event.messageId,
    status: event.status,
    offset: event.offset,
    totalSize: event.totalSize,
    file: event.file,
    payload: event.payload && event.payload.length ? bytesToBase64(event.payload) : undefined,
  };
}

const GarminEvent = {
  DOWNLOAD_PROGRESS: 5,
  FILE_COMPLETE: 6,
  DIRECTORY_ENTRY: 7,
};

class GarminSidecarSession {
  constructor(core, socket, options = {}) {
    this.core = core;
    this.socket = socket;
    this.id = options.sessionId || null;
    this.driver = 'garmin-sidecar';
    this.channel = 'ble-bridge';
    this.maxPacketSize = options.maxPacketSize || options.mtu || 20;
    this.reliable = options.reliable !== false;
    this.currentFileIndex = null;
  }

  open() {
    this.core.init({
      reliable: this.reliable,
      maxPacketSize: this.maxPacketSize,
    });
    this.sendWrites(this.core.start());
    this.send({
      type: 'session.opened',
      sessionId: this.id,
      driver: this.driver,
      channel: this.channel,
    });
  }

  onMessage(packet) {
    switch (packet.type) {
      case 'transport.notify':
        this.onNotify(base64ToBytes(packet.data));
        break;
      case 'device.foreach':
      case 'device.requestDirectory':
        this.requestDirectory();
        break;
      case 'device.requestFile':
        this.currentFileIndex = packet.fileIndex;
        this.requestFile(packet);
        break;
      case 'session.close':
        this.send({ type: 'session.closed', sessionId: this.id });
        break;
      default:
        this.error(`unsupported garmin-sidecar message: ${packet.type}`);
        break;
    }
  }

  onNotify(bytes) {
    const result = this.core.onNotification(bytes);
    this.sendWrites(result.writes);
    if (result.event && result.event.type) {
      this.handleEvent(result.event);
      this.send({ type: 'device.event', sessionId: this.id, event: eventToJson(result.event) });
    }
  }

  requestDirectory() {
    if (!this.core.requestDirectory) {
      this.error('core does not expose requestDirectory');
      return;
    }
    this.sendWrites(this.core.requestDirectory());
  }

  requestFile(file) {
    if (!this.core.requestFile) {
      this.error('core does not expose requestFile');
      return;
    }
    this.currentFileIndex = file.fileIndex;
    this.sendWrites(this.core.requestFile(file));
  }

  handleEvent(event) {
    if (event.type === GarminEvent.DOWNLOAD_PROGRESS) {
      this.send({
        type: 'device.progress',
        sessionId: this.id,
        current: event.offset || 0,
        maximum: event.totalSize || 0,
      });
    }

    if (event.type === GarminEvent.DIRECTORY_ENTRY && event.payload && event.payload.length) {
      this.send({
        type: 'device.directory',
        sessionId: this.id,
        format: 'garmin-directory',
        data: bytesToBase64(event.payload),
        firstEntry: event.file,
      });
    }

    if (event.type === GarminEvent.FILE_COMPLETE && event.payload && event.payload.length) {
      this.send({
        type: 'device.dive',
        sessionId: this.id,
        format: 'fit',
        fileIndex: this.currentFileIndex,
        data: bytesToBase64(event.payload),
      });
    }
  }

  sendWrites(writes) {
    for (const packet of writes || []) {
      this.send({
        type: 'transport.write',
        sessionId: this.id,
        channel: this.channel,
        data: bytesToBase64(packet),
      });
    }
  }

  error(message) {
    this.send({ type: 'device.error', sessionId: this.id, message });
  }

  send(payload) {
    sendJson(this.socket, payload);
  }
}

class LdcSessionProxy {
  constructor(session, socket, options = {}) {
    this.session = session;
    this.socket = socket;
    this.id = options.sessionId || null;
  }

  open(packet) {
    if (this.session.open) {
      this.session.open(packet);
    }
    this.send({ type: 'session.opened', sessionId: this.id, driver: 'ldc', channel: packet.channel });
  }

  onMessage(packet) {
    if (!this.session.onMessage) {
      this.send({ type: 'device.error', sessionId: this.id, message: 'ldc session does not expose onMessage' });
      return;
    }
    this.session.onMessage(packet);
  }

  send(payload) {
    sendJson(this.socket, payload);
  }
}

export class DeviceSessionRouter {
  constructor(socket, factories = {}) {
    this.socket = socket;
    this.factories = factories;
    this.sessions = new Map();
    this.defaultSessionId = null;
  }

  onMessage(message) {
    const packet = normalizeMessage(message);
    if (packet.type === 'session.open') {
      this.openSession(packet);
      return;
    }

    const session = this.findSession(packet.sessionId);
    if (!session) {
      sendJson(this.socket, {
        type: 'device.error',
        sessionId: packet.sessionId,
        message: 'no active session; send session.open first',
      });
      return;
    }

    session.onMessage(packet);
  }

  openSession(packet) {
    const driver = normalizeDriver(packet.driver);
    const sessionId = packet.sessionId || packet.deviceId || `${driver}:${Date.now()}`;

    if (driver === 'garmin-sidecar') {
      if (!this.factories.createGarminCore) {
        sendJson(this.socket, { type: 'device.error', sessionId, message: 'createGarminCore factory is missing' });
        return;
      }
      const session = new GarminSidecarSession(this.factories.createGarminCore(packet), this.socket, {
        ...packet,
        sessionId,
      });
      this.sessions.set(sessionId, session);
      this.defaultSessionId = sessionId;
      session.open();
      return;
    }

    if (driver === 'ldc') {
      if (!this.factories.createLdcSession) {
        sendJson(this.socket, { type: 'device.error', sessionId, message: 'createLdcSession factory is missing' });
        return;
      }
      const session = new LdcSessionProxy(this.factories.createLdcSession(packet), this.socket, {
        sessionId,
      });
      this.sessions.set(sessionId, session);
      this.defaultSessionId = sessionId;
      session.open(packet);
      return;
    }

    sendJson(this.socket, { type: 'device.error', sessionId, message: `unknown driver: ${packet.driver}` });
  }

  findSession(sessionId) {
    if (sessionId && this.sessions.has(sessionId)) {
      return this.sessions.get(sessionId);
    }
    if (this.defaultSessionId) {
      return this.sessions.get(this.defaultSessionId);
    }
    return null;
  }
}

export {
  GarminSidecarSession,
  LdcSessionProxy,
  base64ToBytes,
  bytesToBase64,
  normalizeMessage,
};
