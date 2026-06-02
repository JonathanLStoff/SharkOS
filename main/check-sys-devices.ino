#include "globals.h"
#include <ArduinoJson.h>

extern SPIClass cc1101_spi2;
extern ELECHOUSE_CC1101 cc1101_driver_1;
extern ELECHOUSE_CC1101 cc1101_driver_2;
extern LLCC68 lora;

// --- Boot-time radio health flags (defaults = not OK) ---
bool radioOk_cc1101_1 = false;
bool radioOk_cc1101_2 = false;
bool radioOk_lora     = false;
bool radioOk_nfc      = false;
bool radioOk_nrf24    = false;

void checksysdevices() {
  Serial.println("checksysdevices: entering");
  Serial.println("checksysdevices: finished");
}

// Boot radio test — called once from setup() after deviceSetup().
// Tests all radios and sets the radioOk_* flags.
// If any radio fails, the LED turns red for 3 seconds.
void bootRadioTest() {
  Serial.println("[bootRadioTest] Testing all radios...");

  // ── CC1101 #1: read PARTNUM register (0x30 status), expect 0x00 for CC1101 ──
  {
    cc1101_driver_1.SpiStrobe(CC1101_SIDLE);
    byte partnum = cc1101_driver_1.SpiReadStatus(0x30); // PARTNUM
    byte version = cc1101_driver_1.SpiReadStatus(0x31); // VERSION
    if (version == 0x14 || version == 0x04 || version == 0x03) {
      radioOk_cc1101_1 = true;
      Serial.printf("[bootRadioTest] CC1101 #1 OK (PARTNUM=0x%02X VERSION=0x%02X)\n", partnum, version);
    } else {
      radioOk_cc1101_1 = false;
      Serial.printf("[bootRadioTest] CC1101 #1 FAIL (PARTNUM=0x%02X VERSION=0x%02X)\n", partnum, version);
    }
  }

  // ── CC1101 #2: same check on second radio ──
  {
    cc1101_driver_2.SpiStrobe(CC1101_SIDLE);
    byte partnum = cc1101_driver_2.SpiReadStatus(0x30);
    byte version = cc1101_driver_2.SpiReadStatus(0x31);
    if (version == 0x14 || version == 0x04 || version == 0x03) {
      radioOk_cc1101_2 = true;
      Serial.printf("[bootRadioTest] CC1101 #2 OK (PARTNUM=0x%02X VERSION=0x%02X)\n", partnum, version);
    } else {
      radioOk_cc1101_2 = false;
      Serial.printf("[bootRadioTest] CC1101 #2 FAIL (PARTNUM=0x%02X VERSION=0x%02X)\n", partnum, version);
    }
  }

  // ── LoRa (LLCC68): attempt init ──
  {
    // Idle CC1101 radios that share the HSPI bus so they don't interfere
    cc1101_driver_1.SpiStrobe(CC1101_SIDLE);
    cc1101_driver_2.SpiStrobe(CC1101_SIDLE);
    digitalWrite(CC1101_2_CS, HIGH);   // ensure CC1101 #2 CS is de-asserted
    delay(10);                          // allow SPI bus to settle

    // LLCC68 begin: freq, bw, sf, cr, syncWord, power, preamble, tcxoVoltage, useLDO
    // tcxoVoltage=0 → XTAL (LLCC68 default); useLDO=false → DC-DC
    int16_t state = lora.begin(915.0);
    if (state == RADIOLIB_ERR_NONE) {
      radioOk_lora = true;
      Serial.println("[bootRadioTest] LoRa (LLCC68) OK");
      lora.standby();
    } else {
      radioOk_lora = false;
      Serial.printf("[bootRadioTest] LoRa (LLCC68) FAIL (state=%d)\n", state);
    }
  }

  // ── NFC (PN532): check firmware version (already probed in deviceSetup) ──
  {
    uint32_t fwVer = nfc.getFirmwareVersion();
    if (fwVer) {
      radioOk_nfc = true;
      Serial.printf("[bootRadioTest] NFC (PN532) OK (FW=0x%08X)\n", fwVer);
    } else {
      radioOk_nfc = false;
      Serial.println("[bootRadioTest] NFC (PN532) FAIL — not detected");
    }
  }

  // ── nRF24: attempt begin and check chip present ──
  {
    bool nrfOk = radio1.begin();
    if (nrfOk && radio1.isChipConnected()) {
      radioOk_nrf24 = true;
      Serial.println("[bootRadioTest] nRF24 OK");
      radio1.powerDown(); // save power
    } else {
      radioOk_nrf24 = false;
      Serial.println("[bootRadioTest] nRF24 FAIL — not detected");
    }
  }

  // ── Summary ──
  // CC1101 radios are the primary/required hardware; others are optional
  // (LoRa, NFC, nRF24 have pin conflicts and may not be physically connected).
  bool requiredFailed = !radioOk_cc1101_1 || !radioOk_cc1101_2;

  Serial.printf("[bootRadioTest] Summary: CC1101_1=%s CC1101_2=%s LoRa=%s(opt) NFC=%s(opt) nRF24=%s(opt)\n",
    radioOk_cc1101_1 ? "OK" : "FAIL",
    radioOk_cc1101_2 ? "OK" : "FAIL",
    radioOk_lora     ? "OK" : "N/C",
    radioOk_nfc      ? "OK" : "N/C",
    radioOk_nrf24    ? "OK" : "N/C");

  if (requiredFailed) {
    Serial.println("[bootRadioTest] *** CC1101 FAILURE DETECTED — LED RED 3s ***");
    setColor(255, 0, 0);
    delay(3000);
    // Restore normal LED state — will be overridden by updateStatusLed() in loop()
    setColor(0, 0, 0);
  } else {
    Serial.println("[bootRadioTest] Required radios (CC1101 x2) passed.");
  }
}

// Return a JSON string with radio/device status for the status screen
String getRadioStatusJson() {
  JsonDocument doc;
  doc["device_status"] = true;
  JsonObject radios = doc["radios"].to<JsonObject>();
  radios["cc1101_1"] = radioOk_cc1101_1 ? "ok" : "fail";
  radios["cc1101_2"] = radioOk_cc1101_2 ? "ok" : "fail";
  radios["lora"]     = radioOk_lora     ? "ok" : "not_connected";
  radios["nfc"]      = radioOk_nfc      ? "ok" : "not_connected";
  radios["nrf24"]    = radioOk_nrf24    ? "ok" : "not_connected";
  doc["ble_paired"]  = paired;
  doc["ble_connected"] = anyConnected;
  doc["uptime_sec"] = (unsigned long)(millis() / 1000);
  String result;
  serializeJson(doc, result);
  return result;
}

// Minimal stubs for optional/disabled modules so the core firmware can
// link even when the full implementations live under `disabled_modules/`.
// These are lightweight fallbacks that simply report their feature is
// unavailable.

void i2cScan() {
  notifyStatus("i2c.scan:disabled");
}

void sdinfo_readStats() {
  notifyStatus("sd.info:disabled");
}




   





    

   



