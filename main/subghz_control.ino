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
extern LLCC68 lora;
extern Adafruit_PN532 nfc;

extern Preferences prefs;
extern bool paired;
extern bool pairingMode;

// ── Sniffer runtime state ──
float sniffer_top_mhz = 433.0f;
float sniffer_bot_mhz = 400.0f;
int   sniffer_rssi_threshold = -80;
String sniffer_modulation = "OOK";
bool  sniffer_use_lora = false;

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
  JsonDocument doc;
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

  // Arm RX — give it extra time to settle the PLL and sync detector
  cc1101_driver_2.SetRx();
  delay(50);

  // Timer-based send (bypasses GDO0)
  cc1101_driver_1.SendData((byte*)msg, len, dwell_ms);
  delay(dwell_ms + 50); // extra settle

  // Safe FIFO read — do NOT use ReceiveData() because the length byte
  // in the FIFO can be garbage (esp. in FSK with noise), causing a
  // stack buffer overflow if it exceeds sizeof(buf).
  byte rxBytes = cc1101_driver_2.SpiReadStatus(CC1101_RXBYTES) & 0x7F;
  Serial.printf("[SubGhzTest] tryLoopback: rxBytes=%d (msg len=%d, dwell=%d)\n", rxBytes, len, dwell_ms);
  if (rxBytes >= 1) {
    byte pktLen = cc1101_driver_2.SpiReadReg(CC1101_RXFIFO); // length byte
    if (pktLen > 0 && pktLen <= sizeof(buf) && pktLen <= rxBytes) {
      cc1101_driver_2.SpiReadBurstReg(CC1101_RXFIFO, buf, pktLen);
      cc1101_driver_2.SpiStrobe(CC1101_SIDLE);
      cc1101_driver_2.SpiStrobe(CC1101_SFRX);
      if ((int)pktLen == len && memcmp(buf, msg, len) == 0) return true;
      Serial.printf("[SubGhzTest] Loopback: got %d bytes (expected %d), rxTotal=%d\n", pktLen, len, rxBytes);
    } else {
      // Garbage length byte — flush and move on
      Serial.printf("[SubGhzTest] Loopback: bad pktLen=%d rxBytes=%d — flushing\n", pktLen, rxBytes);
      cc1101_driver_2.SpiStrobe(CC1101_SIDLE);
      cc1101_driver_2.SpiStrobe(CC1101_SFRX);
    }
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
    JsonDocument doc;
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
  // Hard-reset both CC1101s to POR defaults so no stale register config
  // bleeds in (m4RxBw, sync words, pktlen, etc.)
  cc1101_driver_1.SpiStrobe(CC1101_SRES);
  cc1101_driver_2.SpiStrobe(CC1101_SRES);
  delay(10);  // SRES needs ~1ms, give extra margin
  // Full reset to known state for FSK
  cc1101_driver_1.setCCMode(true);
  cc1101_driver_2.setCCMode(true);
  // CRITICAL: setCCMode leaves PKTLEN=0 (RegConfigSettings default).
  // In variable-length mode (LENGTH_CONFIG=01), PKTLEN is the MAX allowed
  // packet length.  With PKTLEN=0 the RX silently drops every packet.
  cc1101_driver_1.setPacketLength(61);
  cc1101_driver_2.setPacketLength(61);
  cc1101_driver_1.setGDO0(CC1101_1_GDO0);
  cc1101_driver_2.setGDO0(CC1101_2_GDO0);
  cc1101_driver_1.setModulation(0);  // 2-FSK
  cc1101_driver_2.setModulation(0);
  cc1101_driver_1.setDRate(9.6);
  cc1101_driver_2.setDRate(9.6);
  cc1101_driver_1.setDeviation(25.390625);  // explicit deviation for FSK
  cc1101_driver_2.setDeviation(25.390625);
  cc1101_driver_1.setRxBW(101.562500);      // RX bandwidth wide enough for deviation+baud
  cc1101_driver_2.setRxBW(101.562500);
  cc1101_driver_1.setPRE(4);  // 8 preamble bytes
  cc1101_driver_2.setPRE(4);
  cc1101_driver_1.setSyncMode(2);  // 16/16 sync word bits
  cc1101_driver_2.setSyncMode(2);
  cc1101_driver_1.setSyncWord(0xD3, 0x91);  // explicit sync word (POR default)
  cc1101_driver_2.setSyncWord(0xD3, 0x91);
  cc1101_driver_1.setMHZ(433.0);
  cc1101_driver_2.setMHZ(433.0);
  delay(10); // PLL settle

  // Dump registers for debugging
  {
    CC1101RegDump d1 = readCC1101Regs(cc1101_driver_1, CC1101_1_GDO0);
    CC1101RegDump d2 = readCC1101Regs(cc1101_driver_2, CC1101_2_GDO0);
    printCC1101Regs("Test1 TX (#1)", d1);
    printCC1101Regs("Test1 RX (#2)", d2);
    compareCC1101Regs(d1, d2);
  }

  // Try up to 3 times (timing-sensitive)
  bool test1 = false;
  for (int attempt = 0; attempt < 3 && !test1; attempt++) {
    test1 = tryLoopback("PING", 150);
    if (!test1) delay(20);
  }
  Serial.printf("[SubGhzTest] Test1 (2-FSK 9.6k): %s\n", test1 ? "PASS" : "FAIL");

  // ---- Test 2: 2-FSK at 100 kbaud (default setCCMode rate) ----
  Serial.println("[SubGhzTest] --- Test2: 2-FSK 100kbaud default ---");
  cc1101_driver_1.SpiStrobe(CC1101_SRES);
  cc1101_driver_2.SpiStrobe(CC1101_SRES);
  delay(10);
  cc1101_driver_1.setCCMode(true);  // resets to ~100 kbaud
  cc1101_driver_2.setCCMode(true);
  cc1101_driver_1.setPacketLength(61);
  cc1101_driver_2.setPacketLength(61);
  cc1101_driver_1.setGDO0(CC1101_1_GDO0);
  cc1101_driver_2.setGDO0(CC1101_2_GDO0);
  cc1101_driver_1.setModulation(0);
  cc1101_driver_2.setModulation(0);
  cc1101_driver_1.setDeviation(47.607422);  // default CC1101 deviation
  cc1101_driver_2.setDeviation(47.607422);
  cc1101_driver_1.setRxBW(203.125000);
  cc1101_driver_2.setRxBW(203.125000);
  cc1101_driver_1.setPRE(4);  // 8 preamble bytes for reliable sync
  cc1101_driver_2.setPRE(4);
  cc1101_driver_1.setSyncMode(2);  // 16/16 sync word bits
  cc1101_driver_2.setSyncMode(2);
  cc1101_driver_1.setSyncWord(0xD3, 0x91);
  cc1101_driver_2.setSyncWord(0xD3, 0x91);
  cc1101_driver_1.setMHZ(433.0);
  cc1101_driver_2.setMHZ(433.0);
  delay(10);

  bool test2 = false;
  for (int attempt = 0; attempt < 3 && !test2; attempt++) {
    test2 = tryLoopback("PING", 50);
    if (!test2) delay(20);
  }
  Serial.printf("[SubGhzTest] Test2 (2-FSK 100k): %s\n", test2 ? "PASS" : "FAIL");

  // ---- Test 3: ASK at 9.6 kbaud ----
  Serial.println("[SubGhzTest] --- Test3: ASK 9.6kbaud ---");
  cc1101_driver_1.SpiStrobe(CC1101_SRES);
  cc1101_driver_2.SpiStrobe(CC1101_SRES);
  delay(10);
  cc1101_driver_1.setCCMode(true);
  cc1101_driver_2.setCCMode(true);
  cc1101_driver_1.setPacketLength(61);
  cc1101_driver_2.setPacketLength(61);
  cc1101_driver_1.setGDO0(CC1101_1_GDO0);
  cc1101_driver_2.setGDO0(CC1101_2_GDO0);
  cc1101_driver_1.setModulation(2);  // ASK
  cc1101_driver_2.setModulation(2);
  cc1101_driver_1.setDRate(9.6);
  cc1101_driver_2.setDRate(9.6);
  cc1101_driver_1.setPRE(4);
  cc1101_driver_2.setPRE(4);
  cc1101_driver_1.setSyncMode(2);
  cc1101_driver_2.setSyncMode(2);
  cc1101_driver_1.setSyncWord(0xD3, 0x91);
  cc1101_driver_2.setSyncWord(0xD3, 0x91);
  cc1101_driver_1.setMHZ(433.0);
  cc1101_driver_2.setMHZ(433.0);

  bool test3 = tryLoopback("PING", 100);
  Serial.printf("[SubGhzTest] Test3 (ASK 9.6k):   %s\n", test3 ? "PASS" : "FAIL");

  // ---- Test 4: LoRa TX verification (single module, no loopback) ----
  Serial.println("[SubGhzTest] --- Test4: LoRa TX verify ---");
  bool test_lora = false;
  {
    // Put CC1101 radios to IDLE so they don't interfere with shared SPI lines
    cc1101_driver_1.SpiStrobe(CC1101_SIDLE);
    cc1101_driver_2.SpiStrobe(CC1101_SIDLE);
    digitalWrite(CC1101_2_CS, HIGH);  // ensure CC1101 #2 CS is de-asserted
    delay(10);

    int16_t loraState = lora.begin(915.0);
    if (loraState == RADIOLIB_ERR_NONE) {
      Serial.println("[SubGhzTest] LoRa begin(915.0) OK");
      // Attempt to transmit a test packet — transmit() blocks until TX_DONE
      // or timeout. Return code RADIOLIB_ERR_NONE means the LLCC68 reported
      // DIO0 TX_DONE, confirming the radio accepted and transmitted the packet.
      byte testPayload[] = { 'L','O','R','A','_','T','E','S','T' };
      int16_t txState = lora.transmit(testPayload, sizeof(testPayload));
      if (txState == RADIOLIB_ERR_NONE) {
        Serial.println("[SubGhzTest] LoRa transmit OK (TX_DONE confirmed)");
        test_lora = true;
      } else {
        Serial.printf("[SubGhzTest] LoRa transmit FAIL (state=%d)\n", txState);
      }
      lora.standby();
    } else {
      Serial.printf("[SubGhzTest] LoRa begin FAIL (state=%d)\n", loraState);
    }
  }
  Serial.printf("[SubGhzTest] Test4 (LoRa TX):    %s\n", test_lora ? "PASS" : "FAIL");

  // ---- Summary ----
  bool overall = test1 || test2 || test3;
  Serial.println("[SubGhzTest] ============ SUMMARY ============");
  Serial.printf("[SubGhzTest]   RSSI:             %s (delta %d dB)\n", rssi_ok ? "PASS" : "FAIL", rssi_delta);
  Serial.printf("[SubGhzTest]   2-FSK  9.6kbaud:  %s\n", test1 ? "PASS" : "FAIL");
  Serial.printf("[SubGhzTest]   2-FSK 100kbaud:   %s\n", test2 ? "PASS" : "FAIL");
  Serial.printf("[SubGhzTest]   ASK    9.6kbaud:  %s\n", test3 ? "PASS" : "FAIL");
  Serial.printf("[SubGhzTest]   LoRa TX:          %s\n", test_lora ? "PASS" : "FAIL");
  Serial.printf("[SubGhzTest] Final: %s\n", overall ? "PASS" : "FAIL");
  Serial.println("[SubGhzTest] ---- Radio loopback test END ----");

  // Restore default ASK modulation for normal operation
  cc1101_driver_1.setCCMode(true);
  cc1101_driver_2.setCCMode(true);
  cc1101_driver_1.setPacketLength(61);
  cc1101_driver_2.setPacketLength(61);
  cc1101_driver_1.setGDO0(CC1101_1_GDO0);
  cc1101_driver_2.setGDO0(CC1101_2_GDO0);
  cc1101_driver_1.setModulation(2);
  cc1101_driver_2.setModulation(2);
  cc1101_driver_1.setMHZ(433.92);
  cc1101_driver_2.setMHZ(433.92);

  // Build JSON BLE response (starts with '{' so Kotlin forwards as JSON)
  JsonDocument doc;
  doc["subghz_test"] = true;
  doc["overall"] = overall ? "pass" : "fail";
  doc["rssi"] = rssi_ok ? "pass" : "fail";
  doc["rssi_delta"] = rssi_delta;
  doc["fsk_slow"] = test1 ? "pass" : "fail";
  doc["fsk_fast"] = test2 ? "pass" : "fail";
  doc["ask_slow"] = test3 ? "pass" : "fail";
  doc["lora"] = test_lora ? "pass" : "fail";
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
  if (!loraTx) {
    notifyStatus("lora:read:no-transceiver");
    return;
  }
  loraTx->scan_range();
}

// ────────────────────────────────────────────────────────────────
// cc1101SnifferRead — Packet sniffer for CC1101 radios
// Sweeps freq range, captures raw FIFO data at frequencies where
// RSSI exceeds the user-set threshold. Sends JSON `sniffer_packet`
// objects over BLE for each detection.
// ────────────────────────────────────────────────────────────────
void cc1101SnifferRead() {
  extern bool scanningRadio;
  float low  = sniffer_bot_mhz;
  float high = sniffer_top_mhz;
  if (high < low) { float t = low; low = high; high = t; }
  const float stepMHz = 0.25f; // finer step for sniffer

  // Configure CC1101 for variable-length packet mode, no address filtering
  cc1101_driver_1.setCCMode(true);
  cc1101_driver_1.setGDO0(CC1101_1_GDO0);
  // Set modulation
  if (sniffer_modulation == "2-FSK" || sniffer_modulation == "FSK") {
    cc1101_driver_1.setModulation(0);
  } else if (sniffer_modulation == "GFSK") {
    cc1101_driver_1.setModulation(1);
  } else { // OOK/ASK
    cc1101_driver_1.setModulation(2);
  }

  for (float f = low; f <= high && scanningRadio; f += stepMHz) {
    cc1101_driver_1.setMHZ(f);
    cc1101_driver_1.SpiStrobe(CC1101_SIDLE);
    cc1101_driver_1.SpiStrobe(CC1101_SFRX);
    cc1101_driver_1.SetRx();
    delayMicroseconds(600);

    int rssi = cc1101_driver_1.getRssi();

    if (rssi >= sniffer_rssi_threshold) {
      // Signal above threshold — dwell longer and try to capture FIFO data
      delay(8); // allow packet to arrive

      byte rxBytes = cc1101_driver_1.SpiReadStatus(CC1101_RXBYTES) & 0x7F;
      uint8_t packet[64];
      int pktLen = 0;

      if (rxBytes >= 2) {
        byte len = cc1101_driver_1.SpiReadReg(CC1101_RXFIFO);
        if (len > 0 && len <= 64 && len <= (rxBytes - 1)) {
          cc1101_driver_1.SpiReadBurstReg(CC1101_RXFIFO, packet, len);
          pktLen = len;
        }
      } else if (rxBytes == 1) {
        // Single byte in FIFO — read it
        packet[0] = cc1101_driver_1.SpiReadReg(CC1101_RXFIFO);
        pktLen = 1;
      }

      // Flush RX FIFO after read attempt
      cc1101_driver_1.SpiStrobe(CC1101_SIDLE);
      cc1101_driver_1.SpiStrobe(CC1101_SFRX);

      // Build JSON sniffer_packet
      JsonDocument doc;
      JsonObject sp = doc["sniffer_packet"].to<JsonObject>();
      sp["freq"] = f;
      sp["rssi"] = rssi;
      sp["mod"] = sniffer_modulation;
      sp["module"] = (int)CC1101_1;
      sp["ts"] = millis();
      sp["len"] = pktLen;

      if (pktLen > 0) {
        char hex[129];
        for (int i = 0; i < pktLen && i < 64; i++) {
          sprintf(hex + i*2, "%02X", packet[i]);
        }
        hex[pktLen * 2] = 0;
        sp["data"] = hex;
      } else {
        sp["data"] = "";
      }

      String s;
      serializeJson(doc, s);
      notifyStatus(s.c_str());

      Serial.printf("[Sniffer] %.2f MHz RSSI=%d len=%d\n", f, rssi, pktLen);
    }

    cc1101_driver_1.SpiStrobe(CC1101_SIDLE);
  }
}

// ────────────────────────────────────────────────────────────────
// loraSnifferRead — Packet sniffer for LLCC68 LoRa module
// Sweeps the LoRa freq range, captures packets above RSSI threshold
// ────────────────────────────────────────────────────────────────
void loraSnifferRead() {
  extern bool scanningRadio;
  float low  = sniffer_bot_mhz;
  float high = sniffer_top_mhz;
  if (high < low) { float t = low; low = high; high = t; }
  if (low  < 150.0f) low  = 150.0f;
  if (high > 960.0f) high = 960.0f;
  const float stepMHz = 0.5f;

  lora.startReceive();
  delay(2);

  for (float f = low; f <= high && scanningRadio; f += stepMHz) {
    lora.setFrequency(f);
    lora.startReceive();
    delay(10); // longer dwell for LoRa packets

    float rssiF = lora.getRSSI(false); // instantaneous RSSI (not packet RSSI)
    int rssi = (int)rssiF;

    if (rssi >= sniffer_rssi_threshold) {
      // Try to read available data
      uint8_t buf[128];
      size_t rxLen = 0;
      int16_t state = lora.readData(buf, sizeof(buf));
      if (state == RADIOLIB_ERR_NONE) {
        rxLen = lora.getPacketLength();
        if (rxLen > sizeof(buf)) rxLen = sizeof(buf);
      }

      JsonDocument doc;
      JsonObject sp = doc["sniffer_packet"].to<JsonObject>();
      sp["freq"] = f;
      sp["rssi"] = rssi;
      sp["mod"] = "LoRa";
      sp["module"] = (int)LORA;
      sp["ts"] = millis();
      sp["len"] = (int)rxLen;

      if (rxLen > 0) {
        char hex[257];
        for (size_t i = 0; i < rxLen && i < 128; i++) {
          sprintf(hex + i*2, "%02X", buf[i]);
        }
        hex[rxLen * 2] = 0;
        sp["data"] = hex;
      } else {
        sp["data"] = "";
      }

      String s;
      serializeJson(doc, s);
      notifyStatus(s.c_str());
      Serial.printf("[LoRaSniffer] %.2f MHz RSSI=%d len=%d\n", f, rssi, (int)rxLen);

      // Restart receive after readData
      lora.startReceive();
    }
  }
  lora.standby();
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
      JsonDocument doc;
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
