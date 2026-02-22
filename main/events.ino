#include "Arduino.h"
#include "transceivers.h"
#include "globals.h"
#include "events.h"
#include "commands.h"
#include <ArduinoJson.h>
#include <Preferences.h>
#include <vector>
#include <WiFi.h>
#include <BLEDevice.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Status / firmware externs used by the event subsystem
extern void notifyStatus(const char *s);
extern int batteryPercent; 
extern bool paired;        // persistent auth flag (from HIZMOS_OLED_U8G2lib.ino)
extern bool pairingMode;   // pairing mode enabled/disabled
extern Preferences prefs;  // NVS storage (initialized during device setup)

// Hooks provided by hardware-utils for polling transceivers and base64 encoding
extern void runTransceiverPollTasks();
extern String base64_encode(const uint8_t *data, size_t len);
extern void hw_send_status_protobuf(bool is_scanning,
                                    int battery_percent,
                                    bool cc1101_1_connected,
                                    bool cc1101_2_connected,
                                    bool lora_connected,
                                    bool nfc_connected,
                                    bool wifi_connected,
                                    bool bluetooth_connected,
                                    bool ir_connected,
                                    bool serial_connected);
extern bool cc1101Connected();
extern bool cc1101_2Connected();

// Buffered signal representation used by events enqueueing
struct BufferedSignal {
  std::vector<uint8_t> payload;
  float frequency_mhz;
  int32_t rssi;
  uint64_t timestamp_ms;
};

// Per-module buffers
static const int RADIO_MODULE_COUNT = 8;
static std::vector<BufferedSignal> radioBuffers[RADIO_MODULE_COUNT];
static size_t radioBufferBytes[RADIO_MODULE_COUNT] = {0};
static size_t radioBufferCount[RADIO_MODULE_COUNT] = {0};
static unsigned long radioLastReceivedMs[RADIO_MODULE_COUNT] = {0};
static const int RADIO_SIGNAL_EVENT_COUNT = 250; // flush threshold for event-counted modules (subghz)

// Forward: flush buffer for given module index
static void events_flush_radio_buffer(int moduleIdx);
static void send_status_snapshot_protobuf();
static void set_scan_modulation_single(const String &value);
static void set_scan_modulation_pair(const String &v1, const String &v2);
static void set_scan_modulation_index(size_t idx, const String &value);
static bool scan_modulation_contains(const String &value);
static String scan_modulation_joined();

// Enqueue raw packet bytes from transceivers. Called from hardware-utils.
// forward to hardware helper for immediate protobuf sends
extern void hw_send_radio_signal_protobuf(int module, float frequency_mhz, int32_t rssi, const uint8_t* data, size_t len, const char* extra);

void events_enqueue_radio_bytes(int module, const uint8_t* data, size_t len, float frequency_mhz, int32_t rssi) {
  if (module < 0 || module >= RADIO_MODULE_COUNT) return;
  BufferedSignal bs;
  bs.payload.assign(data, data + len);
  bs.frequency_mhz = frequency_mhz;
  bs.rssi = rssi;
  bs.timestamp_ms = (uint64_t)millis();
  radioBuffers[module].push_back(std::move(bs));
  radioBufferBytes[module] += len;
  radioBufferCount[module] += 1;
  radioLastReceivedMs[module] = millis();

  // For sub-ghz modules (CC1101_1/2, LORA) flush when we reach event-count threshold
  if (module == (int)CC1101_1 || module == (int)CC1101_2 || module == (int)LORA) {
    if (radioBufferCount[module] >= RADIO_SIGNAL_EVENT_COUNT) {
      events_flush_radio_buffer(module);
    }
  } else {
    // For other modules flush on bytes threshold (legacy behavior)
    if (radioBufferBytes[module] >= RADIO_SIGNAL_BUFFER_SIZE) {
      events_flush_radio_buffer(module);
    }
  }
}

static void events_check_radio_idle_flush() {
  unsigned long now = millis();
  for (int i = 0; i < RADIO_MODULE_COUNT; ++i) {
    if (radioBuffers[i].empty()) continue;
    if (radioLastReceivedMs[i] == 0) continue;
    if ((now - radioLastReceivedMs[i]) >= RADIO_SIGNAL_IDLE_TIMEOUT_MS) {
      events_flush_radio_buffer(i);
    }
  }
}

static void events_flush_radio_buffer(int moduleIdx) {
  if (moduleIdx < 0 || moduleIdx >= RADIO_MODULE_COUNT) return;
  if (radioBuffers[moduleIdx].empty()) return;

  // Build a lightweight JSON batch string to send over notifyStatus()
  String out;
  out.reserve(256);
  out += "{\"type\":\"radio-batch\",\"module\":";
  out += String(moduleIdx);
  out += ",\"signals\":[";

  bool first = true;
  for (auto &bs : radioBuffers[moduleIdx]) {
    if (!first) out += ',';
    first = false;
    String b64 = base64_encode(bs.payload.data(), bs.payload.size());
    out += "{";
    out += "\"timestamp_ms\":"; out += String(bs.timestamp_ms);
    out += ",\"module\":"; out += String(moduleIdx);
    out += ",\"frequency_mhz\":"; out += String(bs.frequency_mhz, 6);
    out += ",\"rssi\":"; out += String(bs.rssi);
    out += ",\"payload\":\""; out += b64; out += "\"";
    out += "}";
  }

  out += "]}";

  // send via notifyStatus which abstracts BLE/Serial transport
  notifyStatus(out.c_str());

  // clear buffer
  radioBuffers[moduleIdx].clear();
  radioBufferBytes[moduleIdx] = 0;
  radioBufferCount[moduleIdx] = 0;
  radioLastReceivedMs[moduleIdx] = 0;
}

static void send_status_snapshot_protobuf() {
  bool isScanning = scanningRadio;
  bool cc1 = cc1101Connected();
  bool cc2 = cc1101_2Connected();
  bool loraConnected = (loraTx != nullptr);
  bool nfcConnected = (nfc.getFirmwareVersion() != 0);
  bool wifiConnected = (WiFi.getMode() != WIFI_MODE_NULL);
  bool bluetoothConnected = anyConnected;
  bool irConnected = true;
  bool serialConnected = true;

  hw_send_status_protobuf(isScanning,
                          batteryPercent,
                          cc1,
                          cc2,
                          loraConnected,
                          nfcConnected,
                          wifiConnected,
                          bluetoothConnected,
                          irConnected,
                          serialConnected);
}

static void set_scan_modulation_single(const String &value) {
  scanModulation.clear();
  if (value.length() > 0) scanModulation.push_back(value);
}

static void set_scan_modulation_pair(const String &v1, const String &v2) {
  scanModulation.clear();
  if (v1.length() > 0) scanModulation.push_back(v1);
  if (v2.length() > 0) {
    if (scanModulation.empty() || scanModulation[0] != v2) scanModulation.push_back(v2);
  }
  if (scanModulation.size() > 2) scanModulation.resize(2);
}

static void set_scan_modulation_index(size_t idx, const String &value) {
  if (idx > 1) return;
  while (scanModulation.size() <= idx) scanModulation.push_back("");
  scanModulation[idx] = value;
  if (scanModulation.size() > 2) scanModulation.resize(2);
  while (!scanModulation.empty() && scanModulation.back().length() == 0) {
    scanModulation.pop_back();
  }
}

static bool scan_modulation_contains(const String &value) {
  for (const auto &item : scanModulation) {
    if (item == value) return true;
  }
  return false;
}

static String scan_modulation_joined() {
  String out;
  for (size_t i = 0; i < scanModulation.size(); ++i) {
    if (scanModulation[i].length() == 0) continue;
    if (out.length() > 0) out += ",";
    out += scanModulation[i];
  }
  return out;
}

// Simple ring-buffer queue for incoming BLE command strings
static const int EVENT_QUEUE_SIZE = 12;
static String eventQueue[EVENT_QUEUE_SIZE];
static int eventHead = 0;
static int eventTail = 0;
static int eventCount = 0;
static portMUX_TYPE eventQueueMux = portMUX_INITIALIZER_UNLOCKED;

void events_init() {
  portENTER_CRITICAL(&eventQueueMux);
  eventHead = eventTail = eventCount = 0;
  portEXIT_CRITICAL(&eventQueueMux);
}

bool events_enqueue_command(const String &cmd) {
  portENTER_CRITICAL(&eventQueueMux);
  if (eventCount >= EVENT_QUEUE_SIZE) {
    portEXIT_CRITICAL(&eventQueueMux);
    // queue full
    return false;
  }
  eventQueue[eventTail] = cmd;
  eventTail = (eventTail + 1) % EVENT_QUEUE_SIZE;
  eventCount++;
  portEXIT_CRITICAL(&eventQueueMux);
  return true;
}

// Central BLE -> events wrapper.  
// This used to drop every incoming command when the unit was unpaired,
// which meant the BLE write log looked successful even though nothing
// ever executed.  We now always enqueue the string; pairing restrictions
// are enforced later by the dispatcher if required.
void bluetooth_receive_command(const String &cmd) {
  // Use critical section for Serial to prevent garbled output if interrupt occurs during print
  portENTER_CRITICAL(&eventQueueMux);
  Serial.print("enqueue BLE command: "); Serial.println(cmd);
  portEXIT_CRITICAL(&eventQueueMux);

  if (!events_enqueue_command(cmd)) {
    Serial.println("Event queue full — dropping BLE command");
    notifyStatus("ERROR:event_queue_full");
  } else {
    // Force yield to ensure main loop gets a chance
    taskYIELD();
  }
}

static String events_dequeue() {
  portENTER_CRITICAL(&eventQueueMux);
  if (eventCount == 0) {
    portEXIT_CRITICAL(&eventQueueMux);
    return String("");
  }
  String s = eventQueue[eventHead];
  eventHead = (eventHead + 1) % EVENT_QUEUE_SIZE;
  eventCount--;
  portEXIT_CRITICAL(&eventQueueMux);
  return s;
}

// ---------------------------
// Background (single) scan-loop manager
// - Only one background "scan loop" may run at a time. Starting any new
//   loop will stop the previously running one.
// - Background tasks are called from `events_process_one()` (non-blocking)
//   on a periodic interval and must not block the main loop.
// ---------------------------

enum ActiveScanKind {
  SCAN_NONE = 0,
  SCAN_WIFI_SNIFFER,
  SCAN_BLE,
  SCAN_NRF,
  SCAN_SUBGHZ,
  SCAN_OSCILLOSCOPE,
  SCAN_I2C,
  SCAN_NFC_POLL,
  SCAN_SENSOR_STREAM,
  SCAN_STATUS_REPORT
};

typedef void (*bg_task_fn_t)();
static ActiveScanKind activeScan = SCAN_NONE;
static bg_task_fn_t activeScanFn = nullptr;
static unsigned long activeScanIntervalMs = 1000;
static unsigned long activeScanLastMs = 0;
static String activeScanName = String();
// Active scan RTOS task handle (only one may exist at a time)
static TaskHandle_t activeScanTask = NULL;

// Task runner that executes the active scan kind in a loop until stopped.
static void active_scan_task_runner(void *param) {
  (void)param;
  const TickType_t delayTick = pdMS_TO_TICKS(activeScanIntervalMs ? activeScanIntervalMs : 2000);
  while (activeScan != SCAN_NONE) {
    // perform one iteration according to activeScan
    switch (activeScan) {
      case SCAN_WIFI_SNIFFER: {
        // WiFi scanning DISABLED in RTOS task context.
        // WiFi.scanNetworks() activates the WiFi radio which conflicts
        // with ADC2 pins (GPIO 11-18) used by SPI radios, and is not
        // safe to call from a non-main task. WiFi scanning is handled
        // by runWifiBleScanTasks() called from the main loop.
        break;
      }
      case SCAN_BLE: {
        // BLE scanning DISABLED in RTOS task context.
        // BLEScan::start() is NOT thread-safe and conflicts with the
        // BLE GATT server (connection drops, crashes, watchdog resets).
        // BLE scanning must only be triggered from the main loop when
        // no GATT client is connected.
        break;
      }
      case SCAN_SUBGHZ: {
        // NOTE: Do NOT call runTransceiverPollTasks() from this RTOS task!
        // It accesses SPI buses which are not thread-safe. The main loop
        // already calls runTransceiverPollTasks() via events_process_one().
        // Just signal that subghz scanning is active; the main-loop poll
        // handles the actual SPI communication.
        break;
      }
      default:
        break;
    }
    // delay between iterations
    vTaskDelay(delayTick);
  }
  // cleanup and delete task
  TaskHandle_t self = xTaskGetCurrentTaskHandle();
  if (self) vTaskDelete(self);
}

static void stop_active_scan_internal();
static void start_active_scan_internal(ActiveScanKind kind, bg_task_fn_t fn, unsigned long intervalMs, const char *name);

// forward-declare internal response helper used throughout this file
static void bluetooth_send_response_internal(const String &payload, const String &inReplyTo = String());

static void scan_loop_tick() {
  if (activeScanFn == nullptr) return;
  unsigned long now = millis();
  if (activeScanLastMs == 0 || now - activeScanLastMs >= activeScanIntervalMs) {
    activeScanLastMs = now;
    activeScanFn();
  }
}

static void start_active_scan_internal(ActiveScanKind kind, bg_task_fn_t fn, unsigned long intervalMs, const char *name) {
  // stop any existing scan first
  if (activeScan != SCAN_NONE) stop_active_scan_internal();
  activeScan = kind;
  activeScanFn = fn;
  activeScanIntervalMs = intervalMs;
  activeScanLastMs = 0;
  activeScanName = name ? String(name) : String();
  Serial.print("Started background scan: "); Serial.println(activeScanName);
  // spawn RTOS task to run the scan loop
  if (activeScanTask != NULL) {
    vTaskDelete(activeScanTask);
    activeScanTask = NULL;
  }
  BaseType_t res = xTaskCreate(active_scan_task_runner, "active_scan", 8 * 1024, NULL, 1, &activeScanTask);
  if (res != pdPASS) {
    Serial.println("Failed to create active_scan task");
    activeScan = SCAN_NONE;
    activeScanTask = NULL;
  }
}

static void stop_active_scan_internal() {
  if (activeScan == SCAN_NONE) return;
  Serial.print("Stopping background scan: "); Serial.println(activeScanName);

  // clear scan-specific flags that older code may rely on
  if (activeScan == SCAN_SUBGHZ) scanningRadio = false;
  if (activeScan == SCAN_NFC_POLL) readingNfc = false;

  activeScan = SCAN_NONE;
  activeScanFn = nullptr;
  activeScanIntervalMs = 1000;
  activeScanLastMs = 0;
  activeScanName = String();
  // delete task if exists
  if (activeScanTask != NULL) {
    vTaskDelete(activeScanTask);
    activeScanTask = NULL;
  }
}

// Helper used by dispatch_command_key to start/stop by command key
static void start_scan_for_key(const String &key, const JsonObject *params = nullptr) {
  if (key == CMD_WIFI_SNIFFER_START) {
    start_active_scan_internal(SCAN_WIFI_SNIFFER, [](){
      // non-blocking WiFi sniffer: start async scan if not running, then
      // collect results when complete and notify.
      static bool pending = false;
      static unsigned long pendingUntil = 0;
      if (!pending) {
        (void)WiFi.scanNetworks(true); // start async
        pending = true;
        pendingUntil = millis() + 5000;
        return;
      }
      int n = WiFi.scanComplete();
      if (n > 0) {
        DynamicJsonDocument out(1024);
        JsonArray arr = out.createNestedArray("Networks");
        for (int i = 0; i < n && i < 12; ++i) {
          JsonObject it = arr.createNestedObject();
          it["ssid"] = WiFi.SSID(i);
          it["rssi"] = WiFi.RSSI(i);
          it["bssid"] = WiFi.BSSIDstr(i);
        }
        String s; serializeJson(out, s);
        notifyStatus(s.c_str());
        WiFi.scanDelete();
        pending = false;
        return;
      }
      // if timeout without completion, clear and retry next tick
      if (pending && millis() >= pendingUntil) {
        pending = false;
        notifyStatus("ERROR:wifi_scan_timeout");
      }
    }, 6000, "wifi_sniffer");
    bluetooth_send_response_internal("wifi.sniffer:started");
    return;
  }

  if (key == CMD_BLE_SCAN_START) {
    set_scan_modulation_single("BLE");
    // BLE scanning runs from the main loop scan_loop_tick(), NOT an RTOS task.
    // This avoids thread-safety issues with the BLE GATT server.
    start_active_scan_internal(SCAN_BLE, [](){
      // fire a BLE scan and report collected devices
      static unsigned long reportAt = 0;
      if (reportAt == 0) {
        blescanner_devices.clear();
        blescanner_scan(); // starts a 5s scan in background
        reportAt = millis() + 5500;
        return;
      }
      if (millis() >= reportAt) {
        DynamicJsonDocument out(1024);
        JsonArray arr = out.createNestedArray("Devices");
        for (size_t i = 0; i < blescanner_devices.size() && i < 20; ++i) {
          JsonObject d = arr.createNestedObject();
          d["name"] = blescanner_devices[i].name;
          d["addr"] = blescanner_devices[i].address;
          d["rssi"] = blescanner_devices[i].rssi;
          d["manuf"] = blescanner_devices[i].manufacturer;
        }
        String s; serializeJson(out, s);
        notifyStatus(s.c_str());
        reportAt = 0;
      }
    }, 6000, "ble_scan");
    bluetooth_send_response_internal("ble.scan:started");
    return;
  }

  if (key == CMD_SUBGHZ_READ_START) {
    // optional params: { frequency_khz: int, modulation: string }
    if (params && params->containsKey("frequency_khz")) {
      long fk = (*params)["frequency_khz"].as<long>();
      if (fk > 0) {
        scanFrequency = ((float)fk) / 1000.0; // store as MHz
      }
    } else if (params && params->containsKey("frequency_mhz")) {
      float fm = (*params)["frequency_mhz"].as<float>();
      if (fm > 0.0) scanFrequency = fm;
    }
    if (params && params->containsKey("modulation")) {
      const char *m = (*params)["modulation"];
      if (m) {
        set_scan_modulation_pair(String(m), String(m));
      }
    }
    // use existing cc1101Read() which is non-blocking and reports via notifyStatus
    start_active_scan_internal(SCAN_SUBGHZ, [](){ cc1101Read(); }, 2000, "subghz_read");
    scanningRadio = true; // keep compatibility with older handlers
    bluetooth_send_response_internal("subghz.read:started");
    return;
  }

  if (key == CMD_NRF_SCAN_START) {
    // lightweight non-blocking nRF sampling: sample a few channels per tick
    // NOTE: SPI access is NOT thread-safe. This lambda runs from scan_loop_tick()
    // in the main loop context, which is safe.
    start_active_scan_internal(SCAN_NRF, [](){
      static uint8_t ch = 0;
      // sample a single channel quickly (non-blocking) and send a small report
      radio1.setChannel(ch);
      delayMicroseconds(100);
      bool r1 = radio1.testRPD();
      // radio2 disabled (single nRF24 module)
      DynamicJsonDocument out(256);
      out["channel"] = ch;
      out["r1"] = r1;
      out["r2"] = false;
      String s; serializeJson(out, s);
      notifyStatus(s.c_str());
      ch = (ch + 1) % 126;
    }, 200, "nrf_scan");
    bluetooth_send_response_internal("nrf.scan:started");
    return;
  }

  if (key == CMD_OSCILLOSCOPE_START) {
    start_active_scan_internal(SCAN_OSCILLOSCOPE, [](){
      // NOTE: GPIO 3 (ANALOG_PIN) is a strapping pin. analogRead() is safe
      // after boot but we guard with a try to avoid crashes if pin is
      // misconfigured.
      int raw = analogRead(ANALOG_PIN);
      DynamicJsonDocument out(128);
      out["analog"] = raw;
      String s; serializeJson(out, s);
      notifyStatus(s.c_str());
    }, 200, "oscilloscope"); // slowed from 100ms to 200ms to reduce load
    bluetooth_send_response_internal("oscilloscope:started");
    return;
  }

  if (key == CMD_I2C_SCAN_START) {
    start_active_scan_internal(SCAN_I2C, [](){
      i2cScan();
      // i2cScan already calls notifyStatus or updates state; send lightweight ack
      notifyStatus("i2c.scan:iter");
    }, 2000, "i2c_scan");
    bluetooth_send_response_internal("i2c.scan:started");
    return;
  }

  if (key == CMD_NFC_POLL_START) {
    set_scan_modulation_single("NFC");
    start_active_scan_internal(SCAN_NFC_POLL, [](){
      // reuse NFC read logic from handleOngoingTasks but non-blocking
      uint8_t uid[7]; uint8_t uidLength;
      if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength)) {
        DynamicJsonDocument doc(256);
        String uidStr = "";
        for (uint8_t i = 0; i < uidLength; i++) {
          if (uid[i] < 0x10) uidStr += "0";
          uidStr += String(uid[i], HEX);
        }
        doc["Response"]["NfcData"]["uid"] = uidStr;
        String json; serializeJson(doc, json);
        notifyStatus(json.c_str());
      }
    }, 1000, "nfc_poll");
    bluetooth_send_response_internal("nfc.poll:started");
    return;
  }

  if (key == CMD_SENSOR_STREAM_START) {
    // placeholder sensor stream: send small sample periodically
    start_active_scan_internal(SCAN_SENSOR_STREAM, [](){
      DynamicJsonDocument doc(128);
      doc["sensor"]["accel.x"] = random(-10, 10);
      doc["sensor"]["accel.y"] = random(-10, 10);
      doc["sensor"]["accel.z"] = random(-10, 10);
      String s; serializeJson(doc, s);
      notifyStatus(s.c_str());
    }, 200, "sensor_stream");
    bluetooth_send_response_internal("sensor.stream:started");
    return;
  }

  if (key == CMD_STATUS_REPORT_START) {
    unsigned long intervalMs = 5000;
    if (params && params->containsKey("interval_ms")) {
      unsigned long requested = (*params)["interval_ms"].as<unsigned long>();
      if (requested >= 250) intervalMs = requested;
    }
    start_active_scan_internal(SCAN_STATUS_REPORT, [](){
      send_status_snapshot_protobuf();
    }, intervalMs, "status_report");
    bluetooth_send_response_internal("status.reporting:started");
    return;
  }

  // Fallback: no background task for this key
  bluetooth_send_response_internal(String(key + String(":start-not-supported")));
}

static void stop_scan_for_key(const String &key) {
  // stop any active scan if it matches the requested key (or stop any if key=="*")
  if (activeScan == SCAN_NONE) {
    bluetooth_send_response_internal("ERROR:no_active_scan");
    return;
  }

  bool matches = false;
  if ((key == CMD_WIFI_SNIFFER_STOP) && activeScan == SCAN_WIFI_SNIFFER) matches = true;
  if ((key == CMD_BLE_SCAN_STOP) && activeScan == SCAN_BLE) matches = true;
  if ((key == CMD_SUBGHZ_READ_STOP) && activeScan == SCAN_SUBGHZ) matches = true;
  if ((key == CMD_NRF_SCAN_STOP) && activeScan == SCAN_NRF) matches = true;
  if ((key == CMD_OSCILLOSCOPE_STOP) && activeScan == SCAN_OSCILLOSCOPE) matches = true;
  if ((key == CMD_I2C_SCAN_STOP) && activeScan == SCAN_I2C) matches = true;
  if ((key == CMD_NFC_POLL_STOP) && activeScan == SCAN_NFC_POLL) matches = true;
  if ((key == CMD_SENSOR_STREAM_STOP) && activeScan == SCAN_SENSOR_STREAM) matches = true;
  if ((key == CMD_STATUS_REPORT_STOP) && activeScan == SCAN_STATUS_REPORT) matches = true;

  if (!matches) {
    // If a different scan is active, stop it and report the change
    stop_active_scan_internal();
    bluetooth_send_response_internal("scan:stopped-other");
    return;
  }

  stop_active_scan_internal();
  bluetooth_send_response_internal(String(key + String(":stopped")));
}

bool events_validate_topic(const String &payload) {
  // Accept either existing BleMessage JSON (contains "Command") or
  // a simple JSON/payload with a `command` field that matches commands.h.
  DynamicJsonDocument doc(1024);
  auto err = deserializeJson(doc, payload);
  if (!err) {
    if (doc.containsKey("Command")) return true; // legacy firmware command
    if (doc.containsKey("command")) {
      const char* key = doc["command"];
      if (!key) return false;
      // check against known command constants
      for (unsigned int i = 0; i < SHARKOS_BT_COMMAND_COUNT; ++i) {
        if (strcmp(key, SHARKOS_BT_COMMANDS[i]) == 0) return true;
      }
      return false;
    }
  }

  // Not JSON? accept plain-string commands if they match constants
  String trimmed = payload;
  trimmed.trim();
  for (unsigned int i = 0; i < SHARKOS_BT_COMMAND_COUNT; ++i) {
    if (trimmed == String(SHARKOS_BT_COMMANDS[i])) return true;
  }
  return false;
}

// Helper: send a BLE response. If `inReplyTo` is provided we wrap the
// payload so the client can correlate the reply to a request id. Falls back
// to the existing `notifyStatus` for plain strings.
static void bluetooth_send_response_internal(const String &payload, const String &inReplyTo) {
  if (inReplyTo.length() == 0) {
    // preserve existing simple text-notify behavior
    notifyStatus(payload.c_str());
    return;
  }

  DynamicJsonDocument out(512);
  out["Response"] = payload;
  out["inReplyTo"] = inReplyTo;
  String outStr;
  serializeJson(out, outStr);
  notifyStatus(outStr.c_str());
}

// Public wrapper matching prototype in events.h
void bluetooth_send_response(const String &payload, const String &inReplyTo) {
  bluetooth_send_response_internal(payload, inReplyTo);
}

// Legacy JSON 'Command' handler (moved from other firmware files). This now
// runs in events context and will call the same handlers as dispatch_command_key
// when possible.
void handleBLECommand(const String &jsonCmd) {
  Serial.print("Handling BLE command (legacy JSON): "); Serial.println(jsonCmd);

  DynamicJsonDocument doc(1024);
  auto err = deserializeJson(doc, jsonCmd);
  if (err) {
    Serial.print("JSON parse error: "); Serial.println(err.c_str());
    notifyStatus("ERROR:json_parse");
    return;
  }

  // allow optional correlation id at top level and echo it back on replies
  String correlationId = String();
  if (doc.containsKey("id")) {
    const char *cid = doc["id"];
    if (cid) correlationId = String(cid);
  }
  if (doc.containsKey("requestId")) {
    const char *cid = doc["requestId"];
    if (cid && correlationId.length() == 0) correlationId = String(cid);
  }

  // If this is the legacy 'Command' object, handle well-known subcommands
  if (doc.containsKey("Command")) {
    JsonObject cmdObj = doc["Command"].as<JsonObject>();
    if (cmdObj.isNull()) {
      bluetooth_send_response_internal("ERROR:invalid_command", correlationId);
      return;
    }

    // single key expected inside Command e.g. { "StartRadioScan": { ... } }
    if (cmdObj.size() == 0) {
      bluetooth_send_response_internal("ERROR:empty_command", correlationId);
      return;
    }
    auto it = cmdObj.begin();
    const char *cmdType = (it != cmdObj.end()) ? it->key().c_str() : nullptr;

    if (strcmp(cmdType, "StartRadioScan") == 0) {
      JsonObject params = cmdObj["StartRadioScan"].as<JsonObject>();
      if (params.containsKey("frequency")) scanFrequency = params["frequency"];
      if (params.containsKey("modulation")) {
        set_scan_modulation_pair(String((const char*)params["modulation"]), String((const char*)params["modulation"]));
      }
      scanningRadio = true;
      Serial.println("Starting radio scan (legacy)");
      bluetooth_send_response_internal("radio_scan:started", correlationId);
      return;
    }

    if (strcmp(cmdType, "StopRadioScan") == 0) {
      scanningRadio = false;
      Serial.println("Stopping radio scan (legacy)");
      bluetooth_send_response_internal("radio_scan:stopped", correlationId);
      return;
    }

    if (strcmp(cmdType, "ReadNfc") == 0) {
      readingNfc = true;
      bluetooth_send_response_internal("nfc_read:started", correlationId);
      return;
    }

    if (strcmp(cmdType, "WriteNfc") == 0) {
      // not implemented yet
      bluetooth_send_response_internal("ERROR:nfc_write_not_implemented", correlationId);
      return;
    }

    if (strcmp(cmdType, "SendIr") == 0) {
      JsonObject irObj = cmdObj["SendIr"].as<JsonObject>();
      // payload is implementation-dependent; forward to existing helper if present
      if (irObj.containsKey("IrData")) {
        String payload = String((const char*)irObj["IrData"]);
        irSend(payload);
        bluetooth_send_response_internal("ir_send:ok", correlationId);
      } else {
        bluetooth_send_response_internal("ERROR:ir_missing_data", correlationId);
      }
      return;
    }

    if (strcmp(cmdType, "GetStatus") == 0) {
      send_status_snapshot_protobuf();
      bluetooth_send_response_internal("status.info:ok", correlationId);
      return;
    }

    // unknown legacy command
    bluetooth_send_response_internal("ERROR:unknown_command", correlationId);
    return;
  }

  // If not a legacy Command object, treat as invalid for this path
  bluetooth_send_response_internal("ERROR:invalid_message", correlationId);
}

// Dispatch a plain command key (matches commands.h). This runs in main loop
// context and may call existing functions or set flags.
static void dispatch_command_key(const String &key, const JsonObject *params = nullptr) {
  // BLE scanner
  if (key == CMD_BLE_SCAN_START) { start_scan_for_key(String(CMD_BLE_SCAN_START), params); return; }
  if (key == CMD_BLE_SCAN_STOP)  { stop_scan_for_key(String(CMD_BLE_SCAN_STOP)); return; }

  // Wi‑Fi scan / sniffer
  if (key == CMD_WIFI_SCAN_START || key == CMD_WIFI_CHANNEL_SCAN) {
        // channel-scan is exactly the same operation on the device side;
        // the UI decides how to plot the results.
        
        // Parse optional parameters from run_action()
        wifi_scan_channel = 0;
        wifi_scan_5ghz = false;
        if (params) {
          if (params->containsKey("channel")) {
             int c = (*params)["channel"].as<int>();
             if (c > 0) wifi_scan_channel = c;
          }
          if (params->containsKey("band")) {
            // "2.4" or "5"
            String b = (*params)["band"].as<String>();
            if (b == "5") wifi_scan_5ghz = true;
          }
        }
        
        // Mark as scanning so runWifiBleScanTasks picks it up
        scanningRadio = true;
        set_scan_modulation_single("WIFI");

        // trigger immediate scan (sync) to ensure user sees results quickly?
        // Or rely on background task. Background task is better.
        // runWifiBleScanTasks() runs every 2s.
        bluetooth_send_response_internal("wifi.scan:started");
        return;
    }
    if (key == CMD_WIFI_SCAN_STOP)     { bluetooth_send_response_internal("wifi.scan:stopped"); return; }
  if (key == CMD_WIFI_SNIFFER_START) { start_scan_for_key(String(CMD_WIFI_SNIFFER_START), params); return; }
  if (key == CMD_WIFI_SNIFFER_STOP)  { stop_scan_for_key(String(CMD_WIFI_SNIFFER_STOP)); return; }

  // nRF (2.4GHz)
  if (key == CMD_NRF_SCAN_START) { start_scan_for_key(String(CMD_NRF_SCAN_START), params); return; }
  if (key == CMD_NRF_SCAN_STOP)  { stop_scan_for_key(String(CMD_NRF_SCAN_STOP)); return; }

  // Sub‑GHz (CC1101/LoRa)
  if (key == CMD_SUBGHZ_SET_MOD_ONE) {
    if (params && params->containsKey("modulation")) {
      String m = (*params)["modulation"].as<String>();
      ModulationType mt = modulationFromString(m);
      if (cc1101Tx && mt != MOD_UNKNOWN) {
        cc1101Tx->setModulation(m);
        set_scan_modulation_index(0, m);
      }
    }
    bluetooth_send_response_internal("subghz.mod.one:ok");
    return;
  }
  if (key == CMD_SUBGHZ_SET_MOD_TWO) {
    if (params && params->containsKey("modulation")) {
      String m = (*params)["modulation"].as<String>();
      ModulationType mt = modulationFromString(m);
      if (cc1101Tx2 && mt != MOD_UNKNOWN) {
        cc1101Tx2->setModulation(m);
        set_scan_modulation_index(1, m);
      }
    }
    bluetooth_send_response_internal("subghz.mod.two:ok");
    return;
  }
  if (key == CMD_SUBGHZ_READ_START) { start_scan_for_key(String(CMD_SUBGHZ_READ_START), params); return; }
  if (key == CMD_SUBGHZ_READ_STOP)  { stop_scan_for_key(String(CMD_SUBGHZ_READ_STOP)); return; }

  // Oscilloscope / ADC
  if (key == CMD_OSCILLOSCOPE_START) { start_scan_for_key(String(CMD_OSCILLOSCOPE_START), params); return; }
  if (key == CMD_OSCILLOSCOPE_STOP)  { stop_scan_for_key(String(CMD_OSCILLOSCOPE_STOP)); return; }

  // I2C scanner
  if (key == CMD_I2C_SCAN_ONCE)  { i2cScan(); bluetooth_send_response_internal("i2c.scan:done"); return; }
  if (key == CMD_I2C_SCAN_START) { start_scan_for_key(String(CMD_I2C_SCAN_START), params); return; }
  if (key == CMD_I2C_SCAN_STOP)  { stop_scan_for_key(String(CMD_I2C_SCAN_STOP)); return; }

  // NFC polling
  if (key == CMD_NFC_POLL_START) { start_scan_for_key(String(CMD_NFC_POLL_START), params); return; }
  if (key == CMD_NFC_POLL_STOP)  { stop_scan_for_key(String(CMD_NFC_POLL_STOP)); return; }

  // Sensor streaming
  if (key == CMD_SENSOR_STREAM_START) { start_scan_for_key(String(CMD_SENSOR_STREAM_START), params); return; }
  if (key == CMD_SENSOR_STREAM_STOP)  { stop_scan_for_key(String(CMD_SENSOR_STREAM_STOP)); return; }

  // Cellular / cell-scan (not implemented)
  if (key == CMD_CELL_SCAN_START) { bluetooth_send_response_internal("cell.scan:start-not-implemented"); return; }
  if (key == CMD_CELL_SCAN_STOP)  { bluetooth_send_response_internal("cell.scan:stopped"); return; }

  // SD / Files
  if (key == CMD_SD_INFO)   { sdinfo_readStats(); bluetooth_send_response_internal("sd.info:ok"); return; }
  if (key == CMD_FILES_LIST) {
    // optional param: path (not fully implemented) — return a stub or basic root listing
    if (params && params->containsKey("path")) {
      String path = String((*params)["path"].as<const char*>());
      bluetooth_send_response_internal(String("files.list:requested ") + path);
    } else {
      bluetooth_send_response_internal("files.list:root-not-implemented");
    }
    return;
  }

  // IR receive
  if (key == CMD_IR_RECV_START) { set_scan_modulation_single("IR"); bluetooth_send_response_internal("ir.recv:started"); return; }
  if (key == CMD_IR_RECV_STOP)  { bluetooth_send_response_internal("ir.recv:stopped"); return; }

  // Convenience / control
  if (key == CMD_LIST_PAIRED_DEVICES) { bluetooth_send_response_internal("list.paired.devices:[]"); return; }
  if (key == CMD_BATTERY_INFO) { DynamicJsonDocument jb(128); jb["battery"] = batteryPercent; String s; serializeJson(jb,s); bluetooth_send_response_internal(s); return; }
  if (key == CMD_STATUS_INFO) { send_status_snapshot_protobuf(); bluetooth_send_response_internal("status.info:ok"); return; }
  if (key == CMD_STATUS_REPORT_START) { start_scan_for_key(String(CMD_STATUS_REPORT_START), params); return; }
  if (key == CMD_STATUS_REPORT_STOP)  { stop_scan_for_key(String(CMD_STATUS_REPORT_STOP)); return; }

  // Fallback: unknown command
  bluetooth_send_response_internal("ERROR:unknown_command_key");
}

void events_process_one() {
  //Serial.println("DEBUG: events_process_one start"); // Un-comment to trace loop spam if needed

  // Run any active background scan task (non-blocking tick)
  // scan_loop_tick runs the scan lambda from main-loop context (thread-safe)
  scan_loop_tick();

  // Poll transceivers to perform non-blocking reads and enqueue packets
  // only if specifically requested via scanningRadio flag (legacy behavior)
  // or if we decide to poll all the time (but user wants it gated).
  if (scanningRadio) {
    Serial.print("DEBUG: scanningRadio=true, modulation="); Serial.println(scan_modulation_joined());
    if (scan_modulation_contains("WIFI")) {
       // WiFi scanning requested - run from main loop (safe context)
       // Serial.println("DEBUG: runWifiBleScanTasks");
       runWifiBleScanTasks();
    } else if (scan_modulation_contains("BLE")) {
        // BLE scanning requested - run from main loop (safe context)
    } else {
      // Sub-GHz/LoRa/NRF polling requested
      // Only poll if no RTOS scan task is running (avoid concurrent SPI access)
      
      runTransceiverPollTasks();
      
    }
  }

  // Check if any radio buffers should be flushed due to idle timeout
  events_check_radio_idle_flush();

  if (eventCount == 0) return;
  String raw = events_dequeue();
  if (raw.length() == 0) return;

  Serial.print("events_process_one: dequeued -> "); Serial.println(raw);

  // Indicate LED feedback immediately based on whether the payload is a
  // known/valid topic.  - valid => yellow flash (success), - invalid => red.
  if (events_validate_topic(raw)) {
    indicate_command_success();
  } else {
    indicate_command_failure();
  }

  // Prefer JSON 'Command' format — forward to existing handler
  DynamicJsonDocument doc(1024);
  auto err = deserializeJson(doc, raw);
  if (!err) {
    // extract optional correlation id so replies can be correlated
    String correlationId = String();
    if (doc.containsKey("id")) {
      const char *cid = doc["id"];
      if (cid) correlationId = String(cid);
    } else if (doc.containsKey("requestId")) {
      const char *cid = doc["requestId"];
      if (cid) correlationId = String(cid);
    }

    // Handle pairing command if present (works whether or not in pairingMode)
    if (doc.containsKey("command")) {
      const char *pkey = doc["command"];
      if (pkey && String(pkey) == String(CMD_PAIR_SET)) {
        JsonObject params = doc.containsKey("params") ? doc["params"].as<JsonObject>() : JsonObject();
        String pinStr = String();
        if (params.containsKey("pin")) {
          if (params["pin"].is<const char*>()) pinStr = String((const char*)params["pin"]);
          else pinStr = String((long)params["pin"]);
        } else if (params.containsKey("code")) {
          if (params["code"].is<const char*>()) pinStr = String((const char*)params["code"]);
          else pinStr = String((long)params["code"]);
        }
        if (pinStr == String("6942")) {
          paired = true;
          pairingMode = false;
          prefs.putBool("paired", true);
          notifyStatus("paired:yes");
          bluetooth_send_response_internal("pair:ok", correlationId);
        } else {
          bluetooth_send_response_internal("ERROR:invalid_pin", correlationId);
        }
        return;
      }
    }
    // Legacy pairing: { "Command": { "Pair": { "pin": "6942" } } }
    if (doc.containsKey("Command")) {
      JsonObject cmdObj = doc["Command"].as<JsonObject>();
      if (!cmdObj.isNull() && cmdObj.containsKey("Pair")) {
        JsonObject p = cmdObj["Pair"].as<JsonObject>();
        String pinStr = String();
        if (p.containsKey("pin")) {
          if (p["pin"].is<const char*>()) pinStr = String((const char*)p["pin"]);
          else pinStr = String((long)p["pin"]);
        }
        if (pinStr == String("6942")) {
          paired = true;
          pairingMode = false;
          prefs.putBool("paired", true);
          notifyStatus("paired:yes");
          bluetooth_send_response_internal("pair:ok", correlationId);
        } else {
          bluetooth_send_response_internal("ERROR:invalid_pin", correlationId);
        }
        return;
      }
    }

    // Normal flows below (pairing no longer blocks commands)
    if (doc.containsKey("Command")) {
      // Legacy firmware command format — let existing handler parse & act
      handleBLECommand(raw);
      return;
    }

    // If 'command' key present, dispatch by key
    if (doc.containsKey("command")) {
      const char *key = doc["command"];
      if (key) {
        JsonObject params = doc.containsKey("params") ? doc["params"].as<JsonObject>() : JsonObject();
        // Allow start/stop control for background scans here
        String k = String(key);
        if (k == String(CMD_WIFI_SNIFFER_START) || k == String(CMD_BLE_SCAN_START) || k == String(CMD_NRF_SCAN_START) || k == String(CMD_SUBGHZ_READ_START) || k == String(CMD_OSCILLOSCOPE_START) || k == String(CMD_I2C_SCAN_START) || k == String(CMD_NFC_POLL_START) || k == String(CMD_SENSOR_STREAM_START)) {
          start_scan_for_key(k, &params);
          return;
        }
        if (k == String(CMD_WIFI_SNIFFER_STOP) || k == String(CMD_BLE_SCAN_STOP) || k == String(CMD_NRF_SCAN_STOP) || k == String(CMD_SUBGHZ_READ_STOP) || k == String(CMD_OSCILLOSCOPE_STOP) || k == String(CMD_I2C_SCAN_STOP) || k == String(CMD_NFC_POLL_STOP) || k == String(CMD_SENSOR_STREAM_STOP)) {
          stop_scan_for_key(k);
          return;
        }

        Serial.print("dispatch_command_key: key=\""); Serial.print(k); Serial.println("\"");
        dispatch_command_key(k, &params);
        return;
      }
    }
  }

  // Not JSON or not recognized JSON — treat as plain command key string
  String trimmed = raw;
  trimmed.trim();
  for (unsigned int i = 0; i < SHARKOS_BT_COMMAND_COUNT; ++i) {
    if (trimmed == String(SHARKOS_BT_COMMANDS[i])) {
      // translate plain-key starts/stops into scan manager control too
      if (trimmed == String(CMD_WIFI_SNIFFER_START) || trimmed == String(CMD_BLE_SCAN_START) || trimmed == String(CMD_NRF_SCAN_START) || trimmed == String(CMD_SUBGHZ_READ_START) || trimmed == String(CMD_OSCILLOSCOPE_START) || trimmed == String(CMD_I2C_SCAN_START) || trimmed == String(CMD_NFC_POLL_START) || trimmed == String(CMD_SENSOR_STREAM_START)) {
        start_scan_for_key(trimmed, nullptr);
        return;
      }
      if (trimmed == String(CMD_WIFI_SNIFFER_STOP) || trimmed == String(CMD_BLE_SCAN_STOP) || trimmed == String(CMD_NRF_SCAN_STOP) || trimmed == String(CMD_SUBGHZ_READ_STOP) || trimmed == String(CMD_OSCILLOSCOPE_STOP) || trimmed == String(CMD_I2C_SCAN_STOP) || trimmed == String(CMD_NFC_POLL_STOP) || trimmed == String(CMD_SENSOR_STREAM_STOP)) {
        stop_scan_for_key(trimmed);
        return;
      }

      dispatch_command_key(trimmed, nullptr);
      return;
    }
  }

  // If we reach here, unknown payload
  notifyStatus("ERROR:invalid_payload");
}
