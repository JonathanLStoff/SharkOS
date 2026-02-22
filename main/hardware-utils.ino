//#include <stdint.h>
#include <Arduino.h>
#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
//#include <SD.h>
#include <Update.h>
#include <vector>
//#include "USB.h"
#include "USBHIDKeyboard.h"
//#include <BleMouse.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <WiFi.h>
#include <IRremote.h>
#include <Wire.h>
#include <Adafruit_PN532.h>
#include <RadioLib.h>

#include <esp_wifi.h>
#include <ArduinoJson.h>


// HID
//USBHIDKeyboard Keyboard;

//BleMouse mouse_ble("sharkos", "sharkos", 100);

BLEServer *pServer;

// Headless UI stubs / placeholders (provide a single-instance dummy u8g2 and
// simple menu/button placeholders so the firmware builds without a display.)

int batteryPercent = 0;  // battery level (0-100), updated elsewhere

int selectedItem = 0;
int scrollOffset = 0;
int visibleItems = 3;
int menuLength = 1;
unsigned long lastInputTime = 0;

// No-op UI helper implementations (used when running headless)
void drawMenu() {}
void blescanner_drawMenu() {}
void blescanner_drawDeviceDetails(const struct blescanner_Device &d) { (void)d; }
bool blescanner_isPressed(int /*pin*/) { return false; }
void hidShowDebug(const String &s) { (void)s; }
void drawnosdcard() {}

// Additional UI stubs (added to eliminate "not declared" build errors)
void drawBoxMessage(const char *line1, const char *line2, const char *line3) { (void)line1; (void)line2; (void)line3; }
void formaterDrawProgress(int progress) { (void)progress; }
void drawHIDKeyboard() { }
// Forward-declare SnifferGraph to keep this file independent of packet-analyzer.h
struct SnifferGraph;

// BLE UUIDs and characteristics
#define BLE_SERVICE_UUID "4fafc201-1fbd-459e-8fcc-c5c9c331914b"
#define BLE_MENU_CHAR_UUID "beb5483e-36ed-4688-b7f5-ea07361b26a8"
#define BLE_CMD_CHAR_UUID "2a73f2c0-1fbd-459e-8fcc-c5c9c331914c"
#define BLE_STATUS_CHAR_UUID "3a2743a1-1fbd-459e-8fcc-c5c9c331914d"

#include <Preferences.h>

BLECharacteristic *pMenuChar;
BLECharacteristic *pCmdChar;
BLECharacteristic *pStatusChar;

Preferences prefs; // NVS storage
bool paired = false;           // persistent auth flag
bool pairingMode = false;      // enabled after timeout if not paired
volatile bool anyConnected = false; // updated in server callbacks

// Modulation enum matching UI options (CC1101 only)
// LoRa is handled by the separate LoRaTransceiver class, so it is *not*
// represented here.
// Enum is defined in globals.h

ModulationType modulationFromString(const String &s) {
  if (s == "OOK") return MOD_OOK;
  if (s == "2-FSK") return MOD_2FSK;
  if (s == "ASK") return MOD_ASK;
  if (s == "GFSK") return MOD_GFSK;
  if (s == "MSK") return MOD_MSK;
  return MOD_UNKNOWN;
}
String modulationToString(ModulationType m) {
  switch (m) {
    case MOD_OOK: return "OOK";
    case MOD_2FSK: return "2-FSK";
    case MOD_ASK: return "ASK";
    case MOD_GFSK: return "GFSK";
    case MOD_MSK: return "MSK";
    default: return "";
  }
}

// State variables for commands
bool scanningRadio = false;
int wifi_scan_channel = 0; // 0 = all
bool wifi_scan_5ghz = false; // standard is 2.4 only
float scanFrequency = 433.0;
std::vector<String> scanModulation = {"OOK"};
bool readingNfc = false;
bool sendingIr = false;

static bool scanModulationContains(const char *value) {
  for (const auto &item : scanModulation) {
    if (item == String(value)) return true;
  }
  return false;
}


// Simple base64 encoder (sufficient for small payloads)
static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
String base64_encode(const uint8_t *data, size_t len) {
  String out;
  out.reserve(4 * ((len + 2) / 3));
  size_t i = 0;
  while (i < len) {
    uint32_t a = i < len ? data[i++] : 0;
    uint32_t b = i < len ? data[i++] : 0;
    uint32_t c = i < len ? data[i++] : 0;
    uint32_t triple = (a << 16) | (b << 8) | c;
    out += b64_table[(triple >> 18) & 0x3F];
    out += b64_table[(triple >> 12) & 0x3F];
    out += (i - 2 <= len) ? b64_table[(triple >> 6) & 0x3F] : '=';
    out += (i - 1 <= len) ? b64_table[triple & 0x3F] : '=';
  }
  return out;
}

// Send RadioSignal over BLE (notify) following JSON schema. Falls back to Serial
void sendRadioSignalOverBle(const RadioSignal &rs) {
  // Legacy JSON notify (kept for compatibility)
  String j = rs.toJson();
  if (pStatusChar && anyConnected) {
    pStatusChar->setValue((uint8_t*)j.c_str(), j.length());
    pStatusChar->notify();
  } else {
    Serial.print("radio-signal-json: "); Serial.println(j);
  }
}

// --- Simple protobuf encoder (hand-rolled) + framing ---
// NOTE: This is a lightweight manual protobuf encoder tailored to the
// `RadioSignal` message in .proto. For production use replace with
// nanopb-generated encoder. This implementation writes fields:
// 1: timestamp_ms (varint)
// 2: module (varint)
// 3: frequency_mhz (32-bit fixed)
// 4: rssi (varint)
// 5: payload (length-delimited)
// 6: extra (length-delimited string)

static void write_varint(std::vector<uint8_t> &out, uint64_t v) {
  while (v >= 0x80) {
    out.push_back((uint8_t)((v & 0x7F) | 0x80));
    v >>= 7;
  }
  out.push_back((uint8_t)v);
}

static void write_tag(std::vector<uint8_t> &out, uint32_t field, uint8_t wire_type) {
  write_varint(out, ((uint64_t)field << 3) | (wire_type & 0x7));
}

static void write_fixed32(std::vector<uint8_t> &out, float f) {
  union { float f; uint32_t u; } u; u.f = f;
  out.push_back((uint8_t)(u.u & 0xFF));
  out.push_back((uint8_t)((u.u >> 8) & 0xFF));
  out.push_back((uint8_t)((u.u >> 16) & 0xFF));
  out.push_back((uint8_t)((u.u >> 24) & 0xFF));
}

static void write_bytes_len_prefixed(std::vector<uint8_t> &out, const uint8_t *buf, size_t len) {
  write_varint(out, (uint64_t)len);
  for (size_t i = 0; i < len; ++i) out.push_back(buf[i]);
}

// Encodes RadioSignal into a protobuf-like byte vector. Returns true on success.
bool encode_radio_signal_pb(const RadioSignal &rs, std::vector<uint8_t> &out) {
  out.clear();
  // field 1: timestamp_ms (varint)
  write_tag(out, 1, 0);
  write_varint(out, rs.timestamp_ms);
  // field 2: module (varint)
  write_tag(out, 2, 0);
  write_varint(out, (uint64_t)rs.module);
  // field 3: frequency_mhz (32-bit fixed)
  write_tag(out, 3, 5);
  write_fixed32(out, rs.frequency_mhz);
  // field 4: rssi (varint)
  write_tag(out, 4, 0);
  // ZigZag-encode signed RSSI so negative dBm values are encoded compactly
  {
    int64_t rssi_signed = (int64_t)rs.rssi;
    uint64_t zz = ((uint64_t)(rssi_signed << 1)) ^ (uint64_t)(rssi_signed >> 63);
    write_varint(out, zz);
  }
  // field 5: payload (length-delimited)
  if (!rs.payload.empty()) {
    write_tag(out, 5, 2);
    write_bytes_len_prefixed(out, rs.payload.data(), rs.payload.size());
  }
  // field 6: extra (length-delimited string)
  if (rs.extra.length() > 0) {
    write_tag(out, 6, 2);
    const char *s = rs.extra.c_str();
    write_bytes_len_prefixed(out, (const uint8_t*)s, strlen(s));
  }
  return true;
}

// Encodes the `.proto` `status` message fields into protobuf bytes.
// Field mapping:
// 1: is_scanning (varint bool)
// 2: battery_percent (varint int32)
// 3: cc1101_1_connected (varint bool)
// 4: cc1101_2_connected (varint bool)
// 5: lora_connected (varint bool)
// 6: nfc_connected (varint bool)
// 7: wifi_connected (varint bool)
// 8: bluetooth_connected (varint bool)
// 9: ir_connected (varint bool)
// 10: serial_connected (varint bool)
static bool encode_status_pb(bool is_scanning,
                             int battery_percent,
                             bool cc1101_1_connected,
                             bool cc1101_2_connected,
                             bool lora_connected,
                             bool nfc_connected,
                             bool wifi_connected,
                             bool bluetooth_connected,
                             bool ir_connected,
                             bool serial_connected,
                             std::vector<uint8_t> &out) {
  out.clear();

  int bp = battery_percent;
  if (bp < 0) bp = 0;
  if (bp > 100) bp = 100;

  write_tag(out, 1, 0); write_varint(out, is_scanning ? 1 : 0);
  write_tag(out, 2, 0); write_varint(out, (uint64_t)bp);
  write_tag(out, 3, 0); write_varint(out, cc1101_1_connected ? 1 : 0);
  write_tag(out, 4, 0); write_varint(out, cc1101_2_connected ? 1 : 0);
  write_tag(out, 5, 0); write_varint(out, lora_connected ? 1 : 0);
  write_tag(out, 6, 0); write_varint(out, nfc_connected ? 1 : 0);
  write_tag(out, 7, 0); write_varint(out, wifi_connected ? 1 : 0);
  write_tag(out, 8, 0); write_varint(out, bluetooth_connected ? 1 : 0);
  write_tag(out, 9, 0); write_varint(out, ir_connected ? 1 : 0);
  write_tag(out, 10, 0); write_varint(out, serial_connected ? 1 : 0);

  return true;
}

// Framing: [0xAA 0x55][u16 le length][payload bytes]
void send_protobuf_framed(const std::vector<uint8_t> &payload) {
  uint16_t len = (uint16_t)payload.size();
  uint8_t header[4];
  header[0] = 0xAA; header[1] = 0x55;
  header[2] = (uint8_t)(len & 0xFF);
  header[3] = (uint8_t)((len >> 8) & 0xFF);
  // send over BLE if available (binary)
  if (pStatusChar && anyConnected) {
    // build contiguous buffer
    std::vector<uint8_t> buf;
    buf.insert(buf.end(), header, header+4);
    buf.insert(buf.end(), payload.begin(), payload.end());
    pStatusChar->setValue((uint8_t*)buf.data(), buf.size());
    pStatusChar->notify();
  }
  // Always emit base64-framed string over Serial for Rust listener fallback
  // Format: PROTO:<base64_of_framed>
  // Build framed bytes
  std::vector<uint8_t> framed;
  framed.insert(framed.end(), header, header+4);
  framed.insert(framed.end(), payload.begin(), payload.end());
  String b64 = base64_encode(framed.data(), framed.size());
  Serial.print("PROTO:"); Serial.println(b64);
}

// Convenience helper used by events/task code to send a single RadioSignal
// as a protobuf-framed BLE notify (and Serial PROTO: line). Exposed for
// callers in other files.
void hw_send_radio_signal_protobuf(int module, float frequency_mhz, int32_t rssi, const uint8_t* data, size_t len, const char* extra) {
  RadioSignal rs;
  rs.timestamp_ms = (uint64_t)millis();
  rs.module = (RadioModule)module;
  rs.frequency_mhz = frequency_mhz;
  rs.rssi = rssi;
  rs.payload.clear();
  if (data && len > 0) rs.payload.assign(data, data + len);

  rs.extra = extra ? String(extra) : String();
  Serial.print("hw_send_radio_signal_protobuf: module="); Serial.print(module);
  Serial.print(", frequency_mhz="); Serial.print(frequency_mhz);
  Serial.print(", rssi="); Serial.print(rssi);
  Serial.print(", extra="); Serial.println(rs.extra);
  std::vector<uint8_t> pb;
  if (encode_radio_signal_pb(rs, pb)) {
    send_protobuf_framed(pb);
  }
}

void hw_send_status_protobuf(bool is_scanning,
                             int battery_percent,
                             bool cc1101_1_connected,
                             bool cc1101_2_connected,
                             bool lora_connected,
                             bool nfc_connected,
                             bool wifi_connected,
                             bool bluetooth_connected,
                             bool ir_connected,
                             bool serial_connected) {
  std::vector<uint8_t> pb;
  if (encode_status_pb(is_scanning,
                       battery_percent,
                       cc1101_1_connected,
                       cc1101_2_connected,
                       lora_connected,
                       nfc_connected,
                       wifi_connected,
                       bluetooth_connected,
                       ir_connected,
                       serial_connected,
                       pb)) {
    send_protobuf_framed(pb);
  }
}

// Forward declaration of event enqueue function implemented in events.ino
extern void events_enqueue_radio_bytes(int module, const uint8_t* data, size_t len, float frequency_mhz, int32_t rssi);

#include "transceivers.h"

// Global transceiver pointers — will be initialized after hardware objects exist
CC1101_1Transceiver *cc1101Tx = nullptr;
CC1101_2Transceiver *cc1101Tx2 = nullptr;
LoRaTransceiver *loraTx = nullptr;
NRF24Transceiver *nrf1Tx = nullptr;
NRF24Transceiver *nrf2Tx = nullptr;

void initTransceivers() {
  // underlying hardware objects (cc1101, cc1101_2, lora, radio1/2) are
  // defined later in this file; call this after they are constructed.
  if (!cc1101Tx) cc1101Tx = new CC1101_1Transceiver(&cc1101);
  if (!cc1101Tx2) cc1101Tx2 = new CC1101_2Transceiver(&cc1101_2);
  if (!loraTx) loraTx = new LoRaTransceiver(&lora);
  if (!nrf1Tx) nrf1Tx = new NRF24Transceiver(&radio1);
  // nrf2Tx disabled (radio2 removed)
}

// Run poll tasks for all transceivers (called from events or main loop)
// IMPORTANT: This must only be called from the main loop context, never from
// an RTOS task, to avoid concurrent SPI bus access.
void runTransceiverPollTasks() {
  if (cc1101Tx) {
    cc1101Tx->poll();
  }
  if (cc1101Tx2) {
    cc1101Tx2->poll();
  }
  if (loraTx) loraTx->poll();
  if (nrf1Tx) nrf1Tx->poll();
  // NOTE: runWifiBleScanTasks() is NO LONGER called here automatically.
  // WiFi/BLE scanning is only triggered on-demand via BLE commands.
  // This prevents WiFi radio from activating on every loop and conflicting
  // with SPI radios on ADC2 pins (GPIO 11-18).
}

// WiFi / BLE periodic scanner (run approx every 2000ms)
static unsigned long last_scan_ms = 0;
static const unsigned long SCAN_INTERVAL_MS = 2000;

void runWifiBleScanTasks() {
  unsigned long now = millis();
  if (now - last_scan_ms < SCAN_INTERVAL_MS) return;
  last_scan_ms = now;

  // Only run WiFi scan when explicitly requested via BLE command.
  // WiFi radio activation conflicts with ADC2 pins (GPIO 11-18) used by
  // nRF24 and CC1101 SPI buses. Never run this automatically.
  if (scanningRadio && scanModulationContains("WIFI")) {
      // If 5GHz mode is requested
      // if (wifi_scan_5ghz) {
      //     // Standard ESP32 is 2.4GHz only.
      //     // SIMULATION: Generate fake 5GHz traffic so the UI can be verified.
      //     // This maps 2.4GHz real networks to 5GHz channels for visualization testing.
          
      //     int n = WiFi.scanNetworks(false, true, false, 300, wifi_scan_channel > 0 ? (wifi_scan_channel % 13) + 1 : 0);
      //     if (n >= 0) {
      //         for (int i = 0; i < n; ++i) {
      //             RadioSignal rs;
      //             rs.timestamp_ms = (uint64_t)millis();
      //             rs.module = WIFI;
                  
      //             // FAKE FREQUENCY CALCULATION FOR 5GHz
      //             // If a specific channel was requested (e.g. 36), use it. 
      //             // Otherwise map the 2.4GHz index to a random 5GHz channel.
      //             int fakeChannel = wifi_scan_channel;
      //             if (fakeChannel <= 0) {
      //                  // Map i to a valid 5GHz channel: 36, 40, 44, 48...
      //                  fakeChannel = 36 + (i * 4); 
      //                  if (fakeChannel > 165) fakeChannel = 36;
      //             }
                  
      //             // Base frequency for 5GHz channel N = 5000 + (N * 5)
      //             rs.frequency_mhz = 5000.0 + (fakeChannel * 5.0);
                  
      //             rs.rssi = WiFi.RSSI(i);
      //             String ssid = WiFi.SSID(i) + " (5G)";
      //             rs.extra = ssid;
      //             rs.payload.clear();
                  
      //             std::vector<uint8_t> pb;
      //             if (encode_radio_signal_pb(rs, pb)) send_protobuf_framed(pb);
      //             events_enqueue_radio_bytes((int)rs.module, nullptr, 0, rs.frequency_mhz, rs.rssi);
      //         }
      //     }
      // } else {
      // 2.4GHz Mode (Standard)
      // Use the channel-specific overload if a channel is selected
      // int16_t scanNetworks(bool async = false, bool show_hidden = false, bool passive = false, uint32_t max_ms_per_chan = 300, uint8_t channel = 0, const char* ssid=nullptr, const uint8_t* bssid=nullptr);
      // If we only have the old signature available, we can't filter by channel easily. 
      // Assuming standard ESP32 core > 2.0:
      int n = WiFi.scanNetworks(false, true, false, 300, wifi_scan_channel);

      if (n >= 0) {
        for (int i = 0; i < n; ++i) {
          RadioSignal rs;
          rs.timestamp_ms = (uint64_t)millis();
          rs.module = WIFI;
          // estimate frequency from channel: WiFi.channel(i) not available here,
          // fallback to 2412 + 5*(chan-1) if we get channel via WiFi.channel(i)
          int channel = WiFi.channel(i);
          if (channel <= 0) rs.frequency_mhz = 2412.0;
          else rs.frequency_mhz = 2412.0 + (channel - 1) * 5.0;
          rs.rssi = WiFi.RSSI(i);
          Serial.print("Found WiFi network: SSID="); Serial.print(WiFi.SSID(i)); Serial.print(" RSSI="); Serial.println(rs.rssi);
          String ssid = WiFi.SSID(i);
          String enc = wifi_encryptionType(WiFi.encryptionType(i));
          rs.extra = ssid;
          rs.payload.clear();
          // Append encryption type string as bytes into the payload
          for (size_t k = 0; k < enc.length(); ++k) {
            rs.payload.push_back((uint8_t)enc.charAt(k));
          }
          // Encode protobuf and send framed
          std::vector<uint8_t> pb;
          if (encode_radio_signal_pb(rs, pb)) send_protobuf_framed(pb);
          // Enqueue minimal event for existing batching system (empty payload).
          events_enqueue_radio_bytes((int)rs.module, nullptr, 0, rs.frequency_mhz, rs.rssi);
      }
    }
    //} // END if 5GHz else 2.4GHz
 } // END if scanningRadio && WIFI


  // --- BLE scan (short passive scan) ---
  /* BLE SCANNING DISABLED TO PREVENT CRASH/CONNECTION ISSUES
  if (!blescanner_pBLEScan) {
    blescanner_pBLEScan = BLEDevice::getScan();
  }
  if (blescanner_pBLEScan) {
    BLEScanResults *found = blescanner_pBLEScan->start(1, false);
    if (found) {
      int cnt = found->getCount();
      for (int i = 0; i < cnt; ++i) {
        BLEAdvertisedDevice d = found->getDevice(i);
      RadioSignal rs;
      rs.timestamp_ms = (uint64_t)millis();
      rs.module = BLUETOOTH;
      rs.frequency_mhz = 0.0; // BLE adv channel unknown in this API
      rs.rssi = d.getRSSI();
      rs.extra = d.getName().c_str();
      rs.payload.clear();
      std::vector<uint8_t> pb;
      if (encode_radio_signal_pb(rs, pb)) send_protobuf_framed(pb);
      events_enqueue_radio_bytes((int)rs.module, nullptr, 0, rs.frequency_mhz, rs.rssi);
    }
      blescanner_pBLEScan->clearResults();
    }
  }
  */

}


class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    anyConnected = true;
    Serial.println("BLE client connected");
    // If already paired, exit pairing mode immediately so LED updates.
    if (paired) pairingMode = false;
    // print remote address if available (helps debugging mobile/OS bonding issues)
    if (pServer && pServer->getConnId() >= 0) {
      Serial.print("Connection id: "); Serial.println(pServer->getConnId());
    }
  }
  void onDisconnect(BLEServer* pServer) {
    anyConnected = false;
    Serial.println("BLE client disconnected");
    // Restart advertising so new clients can discover and connect
    BLEDevice::startAdvertising();
    Serial.println("BLE advertising restarted");
  }
};

#include "events.h"

class CommandCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *p) {
    String s = p->getValue();
    if (s.length() > 0) {
      Serial.print("BLE Command received: ");
      Serial.println(s);
      // Forward to the central BLE->events wrapper (permitting pairing checks)
      bluetooth_receive_command(s);
    }
  }
};

void handleBLECommand(const String &cmd); // forward (match prototype in events.h)
void handleOngoingTasks(); // forward

///////////////////////////////////////////////////


std::vector<blescanner_Device> blescanner_devices;
int blescanner_selectedIndex = 0;
BLEScan* blescanner_pBLEScan;








// Variables
int wifi_selectedIndex = 0;
int wifi_networkCount = 0;
bool wifi_showInfo = false;



///gpio pins////
// pin definitions moved to `globals.h`




// Pins (moved to globals.h)
// I2C / IR / PN532 pin macros are defined in globals.h

TwoWire myWire(0);
Adafruit_PN532 nfc(PN532_IRQ, PN532_RESET, &myWire);

// Peripheral pin macros moved to globals.h (so all modules include them)
// nRF24 / SD / RF24 / CC1101 / LoRa pin macros are defined in globals.h now.

// SPI bus instances — ONE per hardware peripheral, never two on the same bus.
// FSPI: nRF24 + LoRa (shared, different CS lines)
// HSPI: REMOVED - CC1101 now shares RADIO_SPI
SPIClass RADIO_SPI(FSPI);
// SPIClass CC1101_SPI(HSPI); // Removed duplicate bus on same pins
// LORA_SPI removed — LoRa now shares RADIO_SPI (both FSPI)


RF24 radio1(CE1_PIN, CSN1_PIN);
// radio2 REMOVED — only one nRF24 module exists

// CC1101 module
Module cc1101_module(CC1101_1_CS, CC1101_1_GDO0, RADIOLIB_NC, CC1101_1_GDO2, RADIO_SPI);
CC1101 cc1101(&cc1101_module);
// 2nd CC1101 module
Module cc1101_module2(CC1101_2_CS, CC1101_2_GDO0, RADIOLIB_NC, CC1101_2_GDO2, RADIO_SPI);
CC1101 cc1101_2(&cc1101_module2);

// LoRa module — shares RADIO_SPI (FSPI) with nRF24, different CS pin (LORA_NSS)
Module lora_module(LORA_NSS, LORA_DIO0, LORA_RESET, LORA_DIO1, RADIO_SPI);
SX1276 lora(&lora_module);



// ==== IR ====
IRrecv irrecv(irrecivepin);
IRsend irsend(irsenderpin);
decode_results results;

// Disrupting time per channel (ms)
#define DISRUPT_DURATION 500

bool sdOK = true;           

int autoImageIndex = 0;
int manualImageIndex = 0;

bool autoMode = true;
unsigned long lastImageChangeTime = 0;
unsigned long lastButtonPressTime = 0;
const unsigned long autoModeTimeout = 120000; //

// ====== Device Control Functions ======
void deactivateSD() {
}

void deactivateNRF1() {
  digitalWrite(CSN1_PIN, HIGH);
  digitalWrite(CE1_PIN, LOW);
}

// void deactivateNRF2() {
//   digitalWrite(CSN2_PIN, HIGH);
//   digitalWrite(CE2_PIN, LOW);
// }

void deactivateCC1101() {
  digitalWrite(CC1101_1_CS, HIGH);
}

void deactivateLoRa() {
  digitalWrite(LORA_NSS, HIGH);
}


// ====== Device Control Functions ======
void activateSD() {
}


void autoModeCheck() {
  if (!autoMode && millis() - lastButtonPressTime > autoModeTimeout) {
    autoMode = true;
  }
}




void deviceSetup() {

  // Feed the watchdog between init stages to prevent Task WDT reset.
  // Total setup previously exceeded the 5s WDT timeout → crash loop.
  Serial.println("deviceSetup: pin modes");

  // Button GPIOs are intentionally *not* configured here for headless/test builds
  // (commented out to avoid hardware conflicts during debugging).
  // pinMode(BTN_UP, INPUT_PULLUP);
  // pinMode(BTN_DOWN, INPUT_PULLUP);
  // pinMode(BTN_SELECT, INPUT_PULLUP);
  // pinMode(BTN_BACK, INPUT_PULLUP);
  // pinMode(BTN_LEFT, INPUT_PULLUP);
  // pinMode(BTN_RIGHT, INPUT_PULLUP);

  pinMode(WAVE_OUT_PIN, OUTPUT);
  // Set pin modes for peripherals (SD is intentionally left uninitialized)
  // pinMode(SD_CS, OUTPUT);
  pinMode(CSN1_PIN, OUTPUT);
  // second nRF24 module disabled, no CSN2/CE2 pins
  pinMode(CE1_PIN, OUTPUT);
  pinMode(CC1101_1_CS, OUTPUT);
  pinMode(LORA_NSS, OUTPUT);

  delay(10); // yield to WDT

  IrReceiver.begin(irrecivepin);
  IrSender.begin(irsenderpin);

  // I2C for PN532 NFC
  myWire.begin(I2C_SDA, I2C_SCL);
  delay(10); // yield to WDT

  // NFC init — guarded. If PN532 is not connected, begin()/SAMConfig()
  // can block for several seconds on I2C, contributing to WDT timeout.
  Serial.println("deviceSetup: NFC init (guarded)");
  nfc.begin();  // sets up I2C transport (fast, just NACKs if absent)
  delay(10); // yield to WDT
  uint32_t nfcVersion = nfc.getFirmwareVersion();
  if (nfcVersion) {
    Serial.print("PN532 found, FW: "); Serial.println(nfcVersion, HEX);
    nfc.SAMConfig();
  } else {
    Serial.println("PN532 not found — skipping NFC init");
  }
  delay(10); // yield to WDT

  // Start SPI buses BEFORE initializing radio modules
  Serial.println("deviceSetup: starting SPI buses");
  RADIO_SPI.begin(NRF_SCK, NRF_MISO, NRF_MOSI); // FSPI for NRF + LoRa + CC1101
  // CC1101_SPI.begin(CC1101_1_SCK, CC1101_1_MISO, CC1101_1_MOSI); // Removed duplicate init
  delay(10); // yield to WDT

  // Init CC1101
  Serial.println("deviceSetup: initializing CC1101");
  int state = cc1101.begin(433.0);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.print("CC1101 init failed: ");
    Serial.println(state);
  }

  // Init LoRa (disabled by default for S3 dev boards because the
  // LORA SPI pins overlap the board's flash/PSRAM lines; leave as a
  // no-op here to avoid corrupting flash/PSRAM.)
  Serial.println("deviceSetup: LoRa init skipped (pins overlap PSRAM/flash)");
  // state = lora.begin(915.0);
  // if (state != RADIOLIB_ERR_NONE) {
  //   Serial.print("LoRa init failed: ");
  //   Serial.println(state);
  // }




  deactivateSD();
  deactivateNRF1();
  //deactivateNRF2(); // nRF2 disabled
  deactivateCC1101();
  deactivateLoRa();

  Serial.println("deviceSetup: radios initialized");
  delay(10); // yield to WDT

  // Initialize transceiver wrappers now that hardware objects exist
  // We do this BEFORE BLE init to ensure no resource conflicts
  initTransceivers();

  Serial.println("deviceSetup: checking peripherals");
  delay(100); // brief settle
  checksysdevices();
  delay(100); 
  Serial.println("deviceSetup: peripherals checked");

  
  Serial.println("deviceSetup: serial already initialized");

    // USB.begin(); // disabled for headless / compile-only builds
    //Keyboard.begin();

    // BLE Server for app control
    Serial.println("deviceSetup: BLE init");
    delay(50); // yield to WDT
    BLEDevice::init("SharkOS");
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());
    BLEService *pService = pServer->createService(BLE_SERVICE_UUID);

    // Menu (read), Command (write), Status (notify)
    pMenuChar = pService->createCharacteristic(BLE_MENU_CHAR_UUID, BLECharacteristic::PROPERTY_READ);
    // accept both WRITE (with response) and WRITE_NO_RESPONSE from clients
    pCmdChar = pService->createCharacteristic(BLE_CMD_CHAR_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
    pStatusChar = pService->createCharacteristic(BLE_STATUS_CHAR_UUID, BLECharacteristic::PROPERTY_NOTIFY);

    // attach BLE2902 descriptor so clients can enable notifications on the status char
    pStatusChar->addDescriptor(new BLE2902());

    // Initial menu value (JSON)
    String menuJson = "{\"menu\":[\"CC1101 Read\",\"CC1101 Jam (disabled)\",\"LoRa Read\",\"LoRa Jam (disabled)\",\"IR Send\",\"NFC Read\"]}";
    pMenuChar->setValue(menuJson);

    pCmdChar->setCallbacks(new CommandCallbacks());

    // Load persistent pairing state
    prefs.begin("sharkos", false);
    paired = prefs.getBool("paired", false);
    pairingMode = false;
    if (paired) {
      Serial.println("Device previously paired (auth found)");
      // notify status
      if (pStatusChar) { pStatusChar->setValue(String("paired:yes")); pStatusChar->notify(); }
    }

    pService->start();
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(BLE_SERVICE_UUID);
    pAdvertising->start();
    Serial.println("BLE advertising started");

   

     //////////////////nessesarry to activate modules or activate manually
  
  
  
  Serial.println("deviceSetup: COMPLETE");
  //activateSD();
  
}

// Example helper: send a small test RadioSignal via first CC1101
void sendTestSignalViaCC1101() {
  if (!cc1101Tx) return;
  std::vector<uint8_t> payload = { 0x01, 0x02, 0x03, 0x04 };
  cc1101Tx->sendPacket(payload, 433.0, -42, "test");
}

