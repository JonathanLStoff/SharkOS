// Sub-GHz and control stubs (safe defaults)

#include <Arduino.h>
#include <Preferences.h>
#include <ArduinoJson.h>

extern BLECharacteristic *pStatusChar;
extern CC1101 cc1101;
extern SX1276 lora;
extern Adafruit_PN532 nfc;

extern Preferences prefs;
extern bool paired;
extern bool pairingMode;

void notifyStatus(const char *s) {
  if (pStatusChar) {
    pStatusChar->setValue(std::string(s));
    pStatusChar->notify();
  }
}

void cc1101Read() {
  // Placeholder: non-destructive passive read not implemented; return example data
  Serial.println("CC1101 read requested (placeholder)");
  notifyStatus("cc1101:read:placeholder:OK");
}

void loraRead() {
  Serial.println("LoRa read requested (placeholder)");
  notifyStatus("lora:read:placeholder:OK");
}

void cc1101Jam() {
  // Jamming is disabled for safety and legality
  Serial.println("CC1101 jamming command received but jamming is disabled for safety.");
  notifyStatus("ERROR: jamming_disabled");
}

void loraJam() {
  // Jamming is disabled for safety and legality
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
    DynamicJsonDocument doc(512);
    doc["Response"]["RadioScanResults"][0]["frequency"] = scanFrequency;
    doc["Response"]["RadioScanResults"][0]["rssi"] = random(-100, -50);
    doc["Response"]["RadioScanResults"][0]["modulation"] = scanModulation;
    // Add more simulated results
    doc["Response"]["RadioScanResults"][1]["frequency"] = scanFrequency + 0.1;
    doc["Response"]["RadioScanResults"][1]["rssi"] = random(-100, -50);
    doc["Response"]["RadioScanResults"][1]["modulation"] = scanModulation;

    String json;
    serializeJson(doc, json);
    if (pStatusChar) {
      pStatusChar->setValue(std::string(json.c_str()));
      pStatusChar->notify();
    }
    Serial.println("Sent scan results: " + json);
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
        pStatusChar->setValue(std::string(json.c_str()));
        pStatusChar->notify();
      }
      Serial.println("Sent NFC data: " + json);
      readingNfc = false; // Stop after one read
    }
  }

  // Add other ongoing tasks if needed
}

void handleBLECommand(String jsonCmd) {
  Serial.print("Handling BLE command: "); Serial.println(jsonCmd);

  // Parse JSON
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, jsonCmd);
  if (error) {
    Serial.print("JSON parse error: ");
    Serial.println(error.c_str());
    notifyStatus("ERROR:json_parse");
    return;
  }

  // Check if it's a BleMessage
  if (!doc.containsKey("Command") && !doc.containsKey("Response")) {
    notifyStatus("ERROR:invalid_message");
    return;
  }

  // For now, assume incoming are Commands (from Android)
  if (doc.containsKey("Command")) {
    JsonObject cmdObj = doc["Command"];
    String cmdType = cmdObj.keys().next();

    if (cmdType == "StartRadioScan") {
      JsonObject params = cmdObj["StartRadioScan"];
      if (params.containsKey("frequency")) {
        scanFrequency = params["frequency"];
      }
      if (params.containsKey("modulation")) {
        scanModulation = params["modulation"];
      }
      scanningRadio = true;
      Serial.println("Starting radio scan");
      notifyStatus("radio_scan:started");
    } else if (cmdType == "StopRadioScan") {
      scanningRadio = false;
      Serial.println("Stopping radio scan");
      notifyStatus("radio_scan:stopped");
    } else if (cmdType == "ReadNfc") {
      readingNfc = true;
      Serial.println("Starting NFC read");
      notifyStatus("nfc_read:started");
    } else if (cmdType == "WriteNfc") {
      // Not implemented yet
      notifyStatus("ERROR:nfc_write_not_implemented");
    } else if (cmdType == "SendIr") {
      JsonObject irObj = cmdObj["SendIr"];
      // Extract IrData
      // For now, placeholder
      Serial.println("IR send requested");
      notifyStatus("ir_send:ok");
    } else if (cmdType == "GetStatus") {
      notifyStatus("status:ok");
    } else {
      notifyStatus("ERROR:unknown_command");
    }
  } else {
    // Incoming Response? Not expected
    notifyStatus("ERROR:unexpected_response");
  }
}
