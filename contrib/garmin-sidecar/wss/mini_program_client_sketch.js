// Pseudocode for the mini-program side.
// Adapt names to the actual platform APIs.

function arrayBufferToBase64(buffer) {
  // WeChat-style environments often expose wx.arrayBufferToBase64.
  if (typeof wx !== 'undefined' && wx.arrayBufferToBase64) {
    return wx.arrayBufferToBase64(buffer);
  }
  let binary = '';
  const bytes = new Uint8Array(buffer);
  for (const byte of bytes) binary += String.fromCharCode(byte);
  return btoa(binary);
}

function base64ToArrayBuffer(text) {
  if (typeof wx !== 'undefined' && wx.base64ToArrayBuffer) {
    return wx.base64ToArrayBuffer(text);
  }
  const binary = atob(text);
  const bytes = new Uint8Array(binary.length);
  for (let i = 0; i < binary.length; i++) bytes[i] = binary.charCodeAt(i);
  return bytes.buffer;
}

export function bindGarminBleRelay({ socket, deviceId, serviceId, writeCharacteristicId }) {
  socket.send(JSON.stringify({ type: 'ble.ready', mtu: 20 }));

  wx.onBLECharacteristicValueChange((res) => {
    socket.send(JSON.stringify({
      type: 'ble.notify',
      data: arrayBufferToBase64(res.value),
    }));
  });

  socket.onMessage((event) => {
    const message = JSON.parse(event.data);
    if (message.type === 'ble.write') {
      wx.writeBLECharacteristicValue({
        deviceId,
        serviceId,
        characteristicId: writeCharacteristicId,
        value: base64ToArrayBuffer(message.data),
      });
    }
  });
}
