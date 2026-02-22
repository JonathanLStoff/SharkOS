const raw = "PROTO:qlUvAAix+wUQBB0AQBlFIEsqBE9wZW4yGkFzQVN0YXJQZXJmb3JtaW5nQXJ0c0d1ZXN0";
const b64 = raw.slice(6);
const bin = atob(b64);
const bytes = new Uint8Array(bin.length);
for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);

let cursor = 0;
if (bytes.length >= 4 && bytes[0] === 0xAA && bytes[1] === 0x55) {
  cursor = 4;
}
const obj = {};
let i = cursor;
function readVarint() {
  let val = 0, shift = 0;
  while (i < bytes.length) {
    const b = bytes[i++];
    val |= (b & 0x7F) << shift;
    if ((b & 0x80) === 0) break;
    shift += 7;
  }
  return val >>> 0;
}
let statusMsg = false;
while (i < bytes.length) {
  const tag = readVarint();
  const field = tag >>> 3;
  const wire = tag & 0x7;
  if (wire === 0) {
    const val = readVarint();
    if (field === 1 && (val === 0 || val === 1)) {
      obj.is_scanning = Boolean(val);
      statusMsg = true;
    } else if (field === 2) {
      if (statusMsg) obj.battery_percent = val;
      else obj.module = val;
    } else if (field === 3) {
      obj.cc1101_1_connected = Boolean(val);
      statusMsg = true;
    } else if (field === 4) {
      if (statusMsg) obj.cc1101_2_connected = Boolean(val);
      else obj.rssi = ((val >>> 1) ^ -(val & 1));
    } else if (field === 5) { obj.lora_connected = Boolean(val); statusMsg = true; }
    else if (field === 6) { obj.nfc_connected = Boolean(val); statusMsg = true; }
    else if (field === 7) { obj.wifi_connected = Boolean(val); statusMsg = true; }
    else if (field === 8) { obj.bluetooth_connected = Boolean(val); statusMsg = true; }
    else if (field === 9) { obj.ir_connected = Boolean(val); statusMsg = true; }
    else if (field === 10) { obj.serial_connected = Boolean(val); statusMsg = true; }
    else obj[`f${field}`] = val;
  } else if (wire === 5) {
    if (i + 4 <= bytes.length) {
      const dv = new DataView(bytes.buffer, bytes.byteOffset + i, 4);
      const f = dv.getFloat32(0, true);
      i += 4;
      if (field === 3 && !statusMsg) obj.frequency_mhz = f;
      else obj[`f${field}_32`] = f;
    } else break;
  } else if (wire === 2) {
    const len = readVarint();
    if (i + len <= bytes.length) {
      const slice = bytes.slice(i, i + len);
      i += len;
      if (field === 5 && !statusMsg) obj.payload = btoa(String.fromCharCode(...slice));
      else if (field === 6 && !statusMsg) obj.extra = new TextDecoder().decode(slice);
      else obj[`f${field}_str`] = new TextDecoder().decode(slice);
    } else break;
  } else break;
}
console.log(JSON.stringify(obj, null, 2));
