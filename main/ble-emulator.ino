// ─────────────────────────────────────────────────────────────────────────
// BLE emulator
//
// Makes the ESP32 advertise ("emulate") a target BLE device so a nearby
// central believes it has found that device. Triggered over the SharkOS BLE
// control link by the `ble.emulate.start` / `ble.emulate.stop` commands, so
// the Flipper (or the Android SharkTooth app) can drive it exactly like every
// other command.
//
// What it does:
//   - Rewrites the outgoing advertisement to carry the target's local name,
//     (optionally) its 16/128-bit service UUID and its manufacturer-specific
//     data. Any central scanning nearby will now list the emulated device.
//   - Optionally (spoof_mac:true) changes the address the ESP32 advertises
//     from to the target MAC, via the NimBLE random-static-address API
//     (BLEDevice::setOwnAddr / setOwnAddrType). BLE only permits *static
//     random* addresses to be set at runtime, whose top two bits must be 1, so
//     if the target MAC's first octet is below 0xC0 it is OR'd up to 0xC0 and
//     the effective address is reported back in the response. A reboot (or
//     ble.emulate.stop) restores the factory public address.
//
// The existing "SharkOS" GATT server keeps running underneath, so the control
// connection that issued the command is never dropped. `ble.emulate.stop`
// restores the normal SharkOS advertisement and public address.
// ─────────────────────────────────────────────────────────────────────────

#include "globals.h"
#include <BLEDevice.h>
#include <BLEAdvertising.h>

// SharkOS primary service UUID (kept in sync with BLE_SERVICE_UUID in
// hardware-utils.ino). Declared locally so this file does not depend on the
// concatenation order of the sketch's #defines.
static const char *kSharkosServiceUuid = "4fafc201-1fbd-459e-8fcc-c5c9c331914b";

// Runtime emulator state (visible via globals.h extern).
bool   g_bleEmulating = false;
String g_bleEmulateName = String();

// Convert a hex string like "4C0002..." into a raw byte String suitable for
// BLEAdvertisementData::setManufacturerData(). Odd trailing nibble is ignored.
static String ble_emulate_hex_to_bytes(const String &hex) {
  String out;
  for (size_t i = 0; i + 1 < hex.length(); i += 2) {
    char h[3] = { hex.charAt(i), hex.charAt(i + 1), 0 };
    out += (char)strtol(h, nullptr, 16);
  }
  return out;
}

// Parse "AA:BB:CC:DD:EE:FF" (or "aabbccddeeff") into a NimBLE address array.
// NimBLE stores addresses little-endian (out[0] = least-significant octet, i.e.
// the last one written by a human), so the parsed octets are reversed.
// Returns true when exactly six octets were found.
static bool ble_emulate_parse_mac(const String &macStr, uint8_t out[6]) {
  uint8_t tmp[6];
  int idx = 0;
  int nibble = -1;
  for (size_t i = 0; i < macStr.length() && idx < 6; ++i) {
    char c = macStr.charAt(i);
    int v;
    if (c >= '0' && c <= '9') v = c - '0';
    else if (c >= 'a' && c <= 'f') v = 10 + (c - 'a');
    else if (c >= 'A' && c <= 'F') v = 10 + (c - 'A');
    else continue; // skip separators (':', '-', spaces)
    if (nibble < 0) {
      nibble = v;
    } else {
      tmp[idx++] = (uint8_t)((nibble << 4) | v);
      nibble = -1;
    }
  }
  if (idx != 6) return false;
  for (int i = 0; i < 6; ++i) out[i] = tmp[5 - i]; // reverse to little-endian
  return true;
}

// Format a NimBLE little-endian address array back to "AA:BB:CC:DD:EE:FF".
static String ble_emulate_format_mac(const uint8_t addr[6]) {
  char buf[18];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
           addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
  return String(buf);
}

// Start emulating. Returns a JSON status string for the caller to send back.
String ble_emulate_start(const String &name,
                         const String &serviceUuid,
                         const String &manufacturerHex,
                         const String &macStr,
                         bool spoofMac) {
  BLEAdvertising *adv = BLEDevice::getAdvertising();
  if (!adv) {
    return String("{\"ble_emulate\":{\"active\":false,\"error\":\"no_advertising\"}}");
  }

  adv->stop();

  String advName = name.length() ? name : String("BLE-Device");

  // Build the impersonated advertisement payload.
  BLEAdvertisementData advData;
  advData.setFlags(0x06); // LE General Discoverable | BR/EDR not supported
  advData.setName(advName);
  if (serviceUuid.length()) {
    advData.setCompleteServices(BLEUUID(serviceUuid.c_str()));
  }
  if (manufacturerHex.length() >= 2) {
    advData.setManufacturerData(ble_emulate_hex_to_bytes(manufacturerHex));
  }
  adv->setAdvertisementData(advData);
  adv->setName(advName);

  // Optional MAC spoofing via NimBLE static-random address.
  // Advertising is already stopped (adv->stop() above), which is required
  // before the controller will accept a new random address.
  bool macSpoofed = false;
  String effectiveMac;
  if (spoofMac && macStr.length()) {
    uint8_t addr[6];
    if (ble_emulate_parse_mac(macStr, addr)) {
      // A static random address must have its two most-significant bits set.
      // addr[5] is the most-significant octet in NimBLE's little-endian array.
      addr[5] |= 0xC0;
      // Set the random address first, then switch own-address type to random
      // (setOwnAddrType verifies a random address is already present).
      if (BLEDevice::setOwnAddr(addr) &&
          BLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM)) {
        macSpoofed = true;
        effectiveMac = ble_emulate_format_mac(addr);
      } else {
        Serial.println("[BLEEmulate] setOwnAddr/setOwnAddrType failed — keeping factory address");
      }
    } else {
      Serial.println("[BLEEmulate] could not parse mac param");
    }
  }

  adv->start();

  g_bleEmulating = true;
  g_bleEmulateName = advName;
  Serial.printf("[BLEEmulate] advertising as '%s' (svc=%s, manuf=%s, mac_spoofed=%s%s%s)\n",
                advName.c_str(),
                serviceUuid.length() ? serviceUuid.c_str() : "-",
                manufacturerHex.length() ? manufacturerHex.c_str() : "-",
                macSpoofed ? "yes " : "no",
                macSpoofed ? "as " : "",
                macSpoofed ? effectiveMac.c_str() : "");

  String resp = "{\"ble_emulate\":{\"active\":true,\"name\":\"";
  resp += advName;
  resp += "\",\"mac_spoofed\":";
  resp += macSpoofed ? "true" : "false";
  if (macSpoofed) {
    resp += ",\"mac\":\"";
    resp += effectiveMac;
    resp += "\"";
  }
  resp += "}}";
  return resp;
}

// Stop emulating and restore the normal SharkOS advertisement.
String ble_emulate_stop() {
  BLEAdvertising *adv = BLEDevice::getAdvertising();
  if (adv) {
    adv->stop();

    // Restore the factory public address if we had spoofed one.
    if (!BLEDevice::setOwnAddrType(BLE_OWN_ADDR_PUBLIC)) {
      Serial.println("[BLEEmulate] could not restore public address type");
    }

    BLEAdvertisementData restore;
    restore.setFlags(0x06);
    restore.setName("SharkOS");
    restore.setCompleteServices(BLEUUID(kSharkosServiceUuid));
    adv->setAdvertisementData(restore);
    adv->setName("SharkOS");
    adv->addServiceUUID(kSharkosServiceUuid);

    adv->start();
    Serial.println("[BLEEmulate] stopped — restored SharkOS advertisement");
  }

  g_bleEmulating = false;
  g_bleEmulateName = String();
  return String("{\"ble_emulate\":{\"active\":false}}");
}
