#include "globals.h"
// Sub-GHz and control stubs (safe defaults)

#include <Arduino.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include "transceivers.h"

extern BLECharacteristic *pStatusChar;
extern CC1101 cc1101;
extern CC1101 cc1101_2;
extern SX1276 lora;
extern Adafruit_PN532 nfc;

extern Preferences prefs;
extern bool paired;
extern bool pairingMode;

void notifyStatus(const char *s) {
  if (pStatusChar) {
    // Use std::string for BLECharacteristic::setValue overload
    pStatusChar->setValue(String(s));
    pStatusChar->notify();
  }
}

void cc1101Read() {
  bool didScan = false;
  if (cc1101Tx) {
    cc1101Tx->scan_range();
    didScan = true;
  }
  if (cc1101Tx2) {
    cc1101Tx2->scan_range();
    didScan = true;
  }
  if (!didScan) {
    notifyStatus("cc1101:read:no-transceiver");
  }
}

// --- CC1101 connectivity checks -------------------------------------------------
// Returns true if the RadioLib CC1101 instance responds to SPI register reads.
static inline bool cc1101IsPresent(CC1101 &mod) {
  int16_t ver = mod.getChipVersion();
  return (ver > 0); // positive version indicates a responding chip
}

// Check primary / secondary modules
bool cc1101Connected() { return cc1101IsPresent(cc1101); }
bool cc1101_2Connected() { return cc1101IsPresent(cc1101_2); }

// Convenience: true only if both modules are present
bool cc1101BothConnected() { return cc1101Connected() && cc1101_2Connected(); }

// Optionally notify current connection state over BLE status characteristic
void cc1101ReportConnectionStatus() {
  DynamicJsonDocument doc(128);
  doc["cc1101"]["primary"] = cc1101Connected() ? "connected" : "disconnected";
  doc["cc1101"]["secondary"] = cc1101_2Connected() ? "connected" : "disconnected";
  String json;
  serializeJson(doc, json);
  notifyStatus(json.c_str());
}

void loraRead() {
  Serial.println("LoRa read requested (placeholder)");
  notifyStatus("lora:read:placeholder:OK");
}

void cc1101Jam() {
  // Disrupting is disabled for safety and legality
  Serial.println("CC1101 jamming command received but jamming is disabled for safety.");
  notifyStatus("ERROR: jamming_disabled");
}

void loraJam() {
  // Disrupting is disabled for safety and legality
  Serial.println("LoRa jamming command received but jamming is disabled for safety.");
  notifyStatus("ERROR: jamming_disabled");
}

void irSend(const String &payload) {
  // IR send placeholder - use existing IR transmit functions if present
  Serial.print("IR send requested (payload): "); Serial.println(payload);
  notifyStatus("ir:send:ok");
}

void handleOngoingTasks() {
  static unsigned long lastScanTime = 0;

  if (scanningRadio && millis() - lastScanTime > 2000) { // Send data every 2 seconds
    lastScanTime = millis();
    // Simulate scan results
    // DynamicJsonDocument doc(512);
    // doc["Response"]["RadioScanResults"][0]["frequency"] = scanFrequency;
    // doc["Response"]["RadioScanResults"][0]["rssi"] = random(-100, -50);
    // doc["Response"]["RadioScanResults"][0]["modulation"] = scanModulation;
    // // Add more simulated results
    // doc["Response"]["RadioScanResults"][1]["frequency"] = scanFrequency + 0.1;
    // doc["Response"]["RadioScanResults"][1]["rssi"] = random(-100, -50);
    // doc["Response"]["RadioScanResults"][1]["modulation"] = scanModulation;

    // String json;
    // serializeJson(doc, json);
    // if (pStatusChar) {
    //   pStatusChar->setValue(json);
    //   pStatusChar->notify();
    // }
    // Serial.println("Sent scan results: " + json);
  }

  if (readingNfc) {
    uint8_t uid[7];
    uint8_t uidLength;
    if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength)) {
      DynamicJsonDocument doc(512);
      doc["Response"]["NfcData"]["uid"] = ""; // Convert uid to string
      String uidStr = "";
      for (uint8_t i = 0; i < uidLength; i++) {
        if (uid[i] < 0x10) uidStr += "0";
        uidStr += String(uid[i], HEX);
      }
      doc["Response"]["NfcData"]["uid"] = uidStr;
      doc["Response"]["NfcData"]["data"] = ""; // Placeholder
      doc["Response"]["NfcData"]["tag_type"] = "MIFARE";

      String json;
      serializeJson(doc, json);
      if (pStatusChar) {
        pStatusChar->setValue(json);
        pStatusChar->notify();
      }
      Serial.println("Sent NFC data: " + json);
      readingNfc = false; // Stop after one read
    }
  }

  // Add other ongoing tasks if needed
}

// handleBLECommand moved to `events.ino` (events subsystem now handles
// legacy JSON 'Command' messages and dispatches to `dispatch_command_key`)
// Original implementation preserved in `events.ino`.
