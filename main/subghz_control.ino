#include "globals.h"
// Sub-GHz and control stubs (safe defaults)

#include <Arduino.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include "transceivers.h"

extern BLECharacteristic *pStatusChar;
// extern CC1101 cc1101;
// extern CC1101 cc1101_2;
extern ELECHOUSE_CC1101 cc1101_driver_1;
extern ELECHOUSE_CC1101 cc1101_driver_2;
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
// Check primary / secondary modules
bool cc1101Connected() { 
    // Uses SmartRC Driver check
    return cc1101_driver_1.getCC1101(); 
}
bool cc1101_2Connected() { 
    return cc1101_driver_2.getCC1101(); 
}

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

// Helper: dump ALL critical CC1101 registers for one driver instance
// CC1101RegDump struct is defined in globals.h

static CC1101RegDump readCC1101Regs(ELECHOUSE_CC1101 &drv, int gdo0pin) {
  CC1101RegDump d;
  d.iocfg2   = drv.SpiReadReg(0x00);
  d.iocfg0   = drv.SpiReadReg(0x02);
  d.pktctrl1 = drv.SpiReadReg(0x07);
  d.pktctrl0 = drv.SpiReadReg(0x08);
  d.pktlen   = drv.SpiReadReg(0x06);
  d.sync1    = drv.SpiReadReg(0x04);
  d.sync0    = drv.SpiReadReg(0x05);
  d.addr     = drv.SpiReadReg(0x09);
  d.channr   = drv.SpiReadReg(0x0A);
  d.fsctrl1  = drv.SpiReadReg(0x0B);
  d.fsctrl0  = drv.SpiReadReg(0x0C);
  d.freq2    = drv.SpiReadReg(0x0D);
  d.freq1    = drv.SpiReadReg(0x0E);
  d.freq0    = drv.SpiReadReg(0x0F);
  d.mdmcfg4  = drv.SpiReadReg(0x10);
  d.mdmcfg3  = drv.SpiReadReg(0x11);
  d.mdmcfg2  = drv.SpiReadReg(0x12);
  d.mdmcfg1  = drv.SpiReadReg(0x13);
  d.mdmcfg0  = drv.SpiReadReg(0x14);
  d.deviatn  = drv.SpiReadReg(0x15);
  d.mcsm1    = drv.SpiReadReg(0x17);
  d.mcsm0    = drv.SpiReadReg(0x18);
  d.frend1   = drv.SpiReadReg(0x21);
  d.frend0   = drv.SpiReadReg(0x22);
  d.fscal3   = drv.SpiReadReg(0x23);
  d.fscal2   = drv.SpiReadReg(0x24);
  d.fscal1   = drv.SpiReadReg(0x25);
  d.fscal0   = drv.SpiReadReg(0x26);
  d.agcctrl2 = drv.SpiReadReg(0x1B);
  d.agcctrl1 = drv.SpiReadReg(0x1C);
  d.agcctrl0 = drv.SpiReadReg(0x1D);
  d.marcstate= drv.SpiReadStatus(CC1101_MARCSTATE);
  d.txbytes  = drv.SpiReadStatus(CC1101_TXBYTES);
  d.rxbytes  = drv.SpiReadStatus(CC1101_RXBYTES);
  d.gdo0val  = digitalRead(gdo0pin);
  return d;
}

static void printCC1101Regs(const char *label, const CC1101RegDump &d) {
  Serial.printf("[Diag] %s register dump:\n", label);
  Serial.printf("[Diag]   IOCFG0=0x%02X IOCFG2=0x%02X PKTCTRL0=0x%02X PKTCTRL1=0x%02X PKTLEN=%d\n",
                d.iocfg0, d.iocfg2, d.pktctrl0, d.pktctrl1, d.pktlen);
  Serial.printf("[Diag]   FREQ2=0x%02X FREQ1=0x%02X FREQ0=0x%02X  CHANNR=%d\n",
                d.freq2, d.freq1, d.freq0, d.channr);
  Serial.printf("[Diag]   SYNC1=0x%02X SYNC0=0x%02X  ADDR=0x%02X\n", d.sync1, d.sync0, d.addr);
  Serial.printf("[Diag]   MDMCFG4=0x%02X MDMCFG3=0x%02X MDMCFG2=0x%02X MDMCFG1=0x%02X MDMCFG0=0x%02X\n",
                d.mdmcfg4, d.mdmcfg3, d.mdmcfg2, d.mdmcfg1, d.mdmcfg0);
  Serial.printf("[Diag]   DEVIATN=0x%02X FREND0=0x%02X FREND1=0x%02X FSCTRL1=0x%02X\n",
                d.deviatn, d.frend0, d.frend1, d.fsctrl1);
  Serial.printf("[Diag]   MCSM1=0x%02X MCSM0=0x%02X  AGC2=0x%02X AGC1=0x%02X AGC0=0x%02X\n",
                d.mcsm1, d.mcsm0, d.agcctrl2, d.agcctrl1, d.agcctrl0);
  Serial.printf("[Diag]   MARCSTATE=0x%02X TXBYTES=%d RXBYTES=%d GDO0=%d\n",
                d.marcstate, d.txbytes, d.rxbytes, d.gdo0val);
}

static void compareCC1101Regs(const CC1101RegDump &a, const CC1101RegDump &b) {
  bool match = true;
  #define CMP(field, name) if (a.field != b.field) { \
    Serial.printf("[Diag]   ** MISMATCH %s: #1=0x%02X  #2=0x%02X\n", name, a.field, b.field); \
    match = false; }
  CMP(iocfg0,   "IOCFG0");   CMP(iocfg2,   "IOCFG2");
  CMP(pktctrl0, "PKTCTRL0"); CMP(pktctrl1, "PKTCTRL1"); CMP(pktlen, "PKTLEN");
  CMP(sync1,    "SYNC1");    CMP(sync0,    "SYNC0");
  CMP(addr,     "ADDR");     CMP(channr,   "CHANNR");
  CMP(fsctrl1,  "FSCTRL1");
  CMP(freq2,    "FREQ2");    CMP(freq1,    "FREQ1");    CMP(freq0,  "FREQ0");
  CMP(mdmcfg4,  "MDMCFG4");  CMP(mdmcfg3, "MDMCFG3");  CMP(mdmcfg2,"MDMCFG2");
  CMP(mdmcfg1,  "MDMCFG1");  CMP(mdmcfg0, "MDMCFG0");
  CMP(deviatn,  "DEVIATN");
  CMP(frend0,   "FREND0");   CMP(frend1,   "FREND1");
  CMP(mcsm1,    "MCSM1");    CMP(mcsm0,    "MCSM0");
  CMP(agcctrl2, "AGCCTRL2"); CMP(agcctrl1, "AGCCTRL1"); CMP(agcctrl0,"AGCCTRL0");
  #undef CMP
  if (match) Serial.println("[Diag]   All config registers MATCH between #1 and #2.");
}

// Perform a single TX/RX attempt with the current radio settings.
// Returns true if CC1101 #2 receives the expected message.
static bool tryLoopback(const char *msg, int dwell_ms) {
  byte buf[64];
  int len = strlen(msg);

  // Flush both FIFOs
  cc1101_driver_1.SpiStrobe(CC1101_SIDLE);
  cc1101_driver_1.SpiStrobe(CC1101_SFTX);
  cc1101_driver_2.SpiStrobe(CC1101_SIDLE);
  cc1101_driver_2.SpiStrobe(CC1101_SFRX);

  // Arm RX
  cc1101_driver_2.SetRx();
  delay(30);

  // Timer-based send
  cc1101_driver_1.SendData((byte*)msg, len, dwell_ms);
  delay(dwell_ms + 30); // extra settle

  byte rxBytes = cc1101_driver_2.SpiReadStatus(CC1101_RXBYTES);
  if (rxBytes > 0) {
    int rxLen = cc1101_driver_2.ReceiveData(buf);
    if (rxLen == len && memcmp(buf, msg, len) == 0) return true;
  }
  return false;
}

// Perform radio loopback test with multiple configurations.
// Returns a JSON string for BLE response and sets *overall to pass/fail.
String performCc1101TestDetailed() {
  Serial.println("[SubGhzTest] ---- Radio loopback test START ----");

  bool spi1_ok = cc1101_driver_1.getCC1101();
  bool spi2_ok = cc1101_driver_2.getCC1101();
  Serial.printf("[SubGhzTest] CC1101 #1 SPI: %s  #2 SPI: %s\n",
                spi1_ok ? "OK" : "FAIL", spi2_ok ? "OK" : "FAIL");

  if (!spi1_ok || !spi2_ok) {
    Serial.println("[SubGhzTest] SPI FAIL — aborting");
    DynamicJsonDocument doc(128);
    doc["subghz_test"] = true;
    doc["overall"] = "fail";
    doc["spi1"] = spi1_ok ? "pass" : "fail";
    doc["spi2"] = spi2_ok ? "pass" : "fail";
    String r;
    serializeJson(doc, r);
    return r;
  }

  // ---- RSSI carrier-sense ----
  Serial.println("[SubGhzTest] --- RSSI carrier-sense ---");
  cc1101_driver_1.setCCMode(true);
  cc1101_driver_2.setCCMode(true);
  cc1101_driver_1.setGDO0(CC1101_1_GDO0);
  cc1101_driver_2.setGDO0(CC1101_2_GDO0);
  cc1101_driver_1.setMHZ(433.0);
  cc1101_driver_2.setMHZ(433.0);

  cc1101_driver_2.SpiStrobe(CC1101_SIDLE);
  cc1101_driver_2.SpiStrobe(CC1101_SFRX);
  cc1101_driver_2.SetRx();
  delay(30);

  byte rssi_base = cc1101_driver_2.SpiReadStatus(0x34);
  int  base_dbm = (rssi_base >= 128) ? ((int)rssi_base - 256)/2 - 74 : (int)rssi_base/2 - 74;

  // Transmit a carrier burst
  cc1101_driver_1.SpiStrobe(CC1101_SIDLE);
  cc1101_driver_1.SpiStrobe(CC1101_SFTX);
  byte fillBuf[60];
  memset(fillBuf, 0xAA, 60);
  cc1101_driver_1.SpiWriteReg(CC1101_TXFIFO, 60);
  cc1101_driver_1.SpiWriteBurstReg(CC1101_TXFIFO, fillBuf, 60);
  cc1101_driver_1.SpiStrobe(CC1101_STX);

  int max_dbm = -200;
  for (int i = 0; i < 80; i++) {
    byte r = cc1101_driver_2.SpiReadStatus(0x34);
    int d = (r >= 128) ? ((int)r - 256)/2 - 74 : (int)r/2 - 74;
    if (d > max_dbm) max_dbm = d;
    delayMicroseconds(100);
  }
  cc1101_driver_1.SpiStrobe(CC1101_SIDLE);
  cc1101_driver_1.SpiStrobe(CC1101_SFTX);

  int rssi_delta = max_dbm - base_dbm;
  bool rssi_ok = rssi_delta >= 6;
  Serial.printf("[SubGhzTest] RSSI: base=%d dBm  peak=%d dBm  delta=%d dB => %s\n",
                base_dbm, max_dbm, rssi_delta, rssi_ok ? "PASS" : "FAIL");

  // ---- Test 1: 2-FSK at 9.6 kbaud, 8 preamble bytes (ultra-conservative) ----
  Serial.println("[SubGhzTest] --- Test1: 2-FSK 9.6kbaud 8-byte preamble ---");
  cc1101_driver_1.setCCMode(true);
  cc1101_driver_2.setCCMode(true);
  cc1101_driver_1.setGDO0(CC1101_1_GDO0);
  cc1101_driver_2.setGDO0(CC1101_2_GDO0);
  cc1101_driver_1.setModulation(0);  // 2-FSK
  cc1101_driver_2.setModulation(0);
  cc1101_driver_1.setDRate(9.6);
  cc1101_driver_2.setDRate(9.6);
  cc1101_driver_1.setPRE(4);  // 8 preamble bytes
  cc1101_driver_2.setPRE(4);
  cc1101_driver_1.setMHZ(433.0);
  cc1101_driver_2.setMHZ(433.0);

  // At 9.6 kbaud, 30 bytes ≈ 25 ms. Use 100ms dwell.
  bool test1 = tryLoopback("PING", 100);
  Serial.printf("[SubGhzTest] Test1 (2-FSK 9.6k): %s\n", test1 ? "PASS" : "FAIL");

  // ---- Test 2: 2-FSK at 100 kbaud (default setCCMode rate) ----
  Serial.println("[SubGhzTest] --- Test2: 2-FSK 100kbaud default ---");
  cc1101_driver_1.setCCMode(true);  // resets to 100 kbaud
  cc1101_driver_2.setCCMode(true);
  cc1101_driver_1.setGDO0(CC1101_1_GDO0);
  cc1101_driver_2.setGDO0(CC1101_2_GDO0);
  cc1101_driver_1.setModulation(0);
  cc1101_driver_2.setModulation(0);
  cc1101_driver_1.setMHZ(433.0);
  cc1101_driver_2.setMHZ(433.0);

  bool test2 = tryLoopback("PING", 50);
  Serial.printf("[SubGhzTest] Test2 (2-FSK 100k): %s\n", test2 ? "PASS" : "FAIL");

  // ---- Test 3: ASK at 9.6 kbaud ----
  Serial.println("[SubGhzTest] --- Test3: ASK 9.6kbaud ---");
  cc1101_driver_1.setCCMode(true);
  cc1101_driver_2.setCCMode(true);
  cc1101_driver_1.setGDO0(CC1101_1_GDO0);
  cc1101_driver_2.setGDO0(CC1101_2_GDO0);
  cc1101_driver_1.setModulation(2);  // ASK
  cc1101_driver_2.setModulation(2);
  cc1101_driver_1.setDRate(9.6);
  cc1101_driver_2.setDRate(9.6);
  cc1101_driver_1.setPRE(4);
  cc1101_driver_2.setPRE(4);
  cc1101_driver_1.setMHZ(433.0);
  cc1101_driver_2.setMHZ(433.0);

  bool test3 = tryLoopback("PING", 100);
  Serial.printf("[SubGhzTest] Test3 (ASK 9.6k):   %s\n", test3 ? "PASS" : "FAIL");

  // ---- Summary ----
  bool overall = test1 || test2 || test3;
  Serial.println("[SubGhzTest] ============ SUMMARY ============");
  Serial.printf("[SubGhzTest]   RSSI:             %s (delta %d dB)\n", rssi_ok ? "PASS" : "FAIL", rssi_delta);
  Serial.printf("[SubGhzTest]   2-FSK  9.6kbaud:  %s\n", test1 ? "PASS" : "FAIL");
  Serial.printf("[SubGhzTest]   2-FSK 100kbaud:   %s\n", test2 ? "PASS" : "FAIL");
  Serial.printf("[SubGhzTest]   ASK    9.6kbaud:  %s\n", test3 ? "PASS" : "FAIL");
  Serial.printf("[SubGhzTest] Final: %s\n", overall ? "PASS" : "FAIL");
  Serial.println("[SubGhzTest] ---- Radio loopback test END ----");

  // Restore default ASK modulation for normal operation
  cc1101_driver_1.setCCMode(true);
  cc1101_driver_2.setCCMode(true);
  cc1101_driver_1.setGDO0(CC1101_1_GDO0);
  cc1101_driver_2.setGDO0(CC1101_2_GDO0);
  cc1101_driver_1.setModulation(2);
  cc1101_driver_2.setModulation(2);
  cc1101_driver_1.setMHZ(433.92);
  cc1101_driver_2.setMHZ(433.92);

  // Build JSON BLE response (starts with '{' so Kotlin forwards as JSON)
  DynamicJsonDocument doc(256);
  doc["subghz_test"] = true;
  doc["overall"] = overall ? "pass" : "fail";
  doc["rssi"] = rssi_ok ? "pass" : "fail";
  doc["rssi_delta"] = rssi_delta;
  doc["fsk_slow"] = test1 ? "pass" : "fail";
  doc["fsk_fast"] = test2 ? "pass" : "fail";
  doc["ask_slow"] = test3 ? "pass" : "fail";
  String r;
  serializeJson(doc, r);
  return r;
}

// Legacy wrapper — returns bool for backward compat
bool performCc1101Test() {
  String result = performCc1101TestDetailed();
  return result.startsWith("subghz.test:ok");
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
