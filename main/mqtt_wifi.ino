#include "globals.h"
#include "events.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// Persisted config
static String cfg_ssid;
static String cfg_wpwd;
static String cfg_mhost;
static int    cfg_mport = 1883;
static String cfg_muser;
static String cfg_mpwd;

static WiFiClient   wifiClient;
static PubSubClient mqttClient(wifiClient);

static bool wifi_enabled   = false;
static bool wifi_suspended = false;

static unsigned long last_wifi_attempt_ms = 0;
static unsigned long last_mqtt_attempt_ms = 0;
static const unsigned long WIFI_RETRY_MS = 15000;
static const unsigned long MQTT_RETRY_MS = 10000;

// Receives MQTT messages and feeds them through the same events queue as BLE
static void onMqttMessage(char *topic, byte *payload, unsigned int length) {
  String msg;
  msg.reserve(length);
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  Serial.printf("[MQTT] cmd on %s: %s\n", topic, msg.c_str());
  events_enqueue_command(msg);
}

static void applyMqttServer() {
  if (cfg_mhost.isEmpty()) return;
  mqttClient.setServer(cfg_mhost.c_str(), cfg_mport);
  mqttClient.setCallback(onMqttMessage);
  mqttClient.setKeepAlive(30);
  mqttClient.setSocketTimeout(5);
}

static bool doMqttConnect() {
  if (cfg_mhost.isEmpty()) return false;
  String cid = "sharkos-" + String((uint32_t)(ESP.getEfuseMac() & 0xFFFFFFFF), HEX);
  bool ok = cfg_muser.isEmpty()
    ? mqttClient.connect(cid.c_str())
    : mqttClient.connect(cid.c_str(), cfg_muser.c_str(), cfg_mpwd.c_str());
  if (ok) {
    mqttClient.subscribe("sharkos/cmd");
    Serial.printf("[MQTT] connected → %s:%d (sub: sharkos/cmd)\n",
                  cfg_mhost.c_str(), cfg_mport);
  } else {
    Serial.printf("[MQTT] connect failed rc=%d, retry in %lus\n",
                  mqttClient.state(), MQTT_RETRY_MS / 1000);
  }
  return ok;
}

// ── Public API ───────────────────────────────────────────────────────────────

void wifiMqttSetup() {
  Preferences p;
  p.begin("wmcfg", true);
  cfg_ssid  = p.getString("ssid",  "");
  cfg_wpwd  = p.getString("wpwd",  "");
  cfg_mhost = p.getString("mhost", "");
  cfg_mport = (int)p.getInt("mport", 1883);
  cfg_muser = p.getString("muser", "");
  cfg_mpwd  = p.getString("mpwd",  "");
  p.end();

  if (cfg_ssid.isEmpty()) {
    Serial.println("[WiFi] no SSID configured — managed WiFi disabled");
    return;
  }
  wifi_enabled = true;
  applyMqttServer();

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);
  WiFi.begin(cfg_ssid.c_str(), cfg_wpwd.c_str());
  last_wifi_attempt_ms = millis();
  Serial.printf("[WiFi] connecting to '%s' (MQTT → %s:%d)\n",
                cfg_ssid.c_str(), cfg_mhost.c_str(), cfg_mport);
}

void wifiMqttLoop() {
  if (!wifi_enabled || wifi_suspended) return;

  unsigned long now = millis();

  // ── WiFi reconnect ───────────────────────────────────────────────────────
  if (WiFi.status() != WL_CONNECTED) {
    if (now - last_wifi_attempt_ms >= WIFI_RETRY_MS) {
      last_wifi_attempt_ms = now;
      Serial.printf("[WiFi] reconnecting to '%s'...\n", cfg_ssid.c_str());
      WiFi.begin(cfg_ssid.c_str(), cfg_wpwd.c_str());
    }
    return; // no WiFi → no MQTT
  }

  // ── MQTT reconnect / loop ────────────────────────────────────────────────
  if (!mqttClient.connected()) {
    if (now - last_mqtt_attempt_ms >= MQTT_RETRY_MS) {
      last_mqtt_attempt_ms = now;
      doMqttConnect();
    }
  } else {
    mqttClient.loop();
  }
}

void wifiMqttConfig(const String &ssid, const String &wpwd,
                    const String &mhost, int mport,
                    const String &muser, const String &mpwd) {
  cfg_ssid  = ssid;
  cfg_wpwd  = wpwd;
  cfg_mhost = mhost;
  cfg_mport = mport;
  cfg_muser = muser;
  cfg_mpwd  = mpwd;

  Preferences p;
  p.begin("wmcfg", false);
  p.putString("ssid",  ssid);
  p.putString("wpwd",  wpwd);
  p.putString("mhost", mhost);
  p.putInt   ("mport", mport);
  p.putString("muser", muser);
  p.putString("mpwd",  mpwd);
  p.end();

  if (mqttClient.connected()) mqttClient.disconnect();
  applyMqttServer();

  if (!ssid.isEmpty()) {
    wifi_enabled = true;
    WiFi.disconnect(false);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false);
    WiFi.begin(ssid.c_str(), wpwd.c_str());
    last_wifi_attempt_ms = millis();
    last_mqtt_attempt_ms = 0;
    Serial.printf("[WiFi] config updated: SSID='%s' MQTT=%s:%d\n",
                  ssid.c_str(), mhost.c_str(), mport);
  }
}

void wifiMqttSuspend() {
  if (!wifi_enabled || wifi_suspended) return;
  wifi_suspended = true;
  if (mqttClient.connected()) mqttClient.disconnect();
  WiFi.disconnect(false);
  Serial.println("[WiFi] suspended for radio action");
}

void wifiMqttResume() {
  if (!wifi_enabled || !wifi_suspended) return;
  wifi_suspended = false;
  last_wifi_attempt_ms = millis() - WIFI_RETRY_MS; // trigger immediate reconnect
  last_mqtt_attempt_ms = 0;
  Serial.println("[WiFi] resuming after radio action");
}

bool wifiMqttIsConnected() {
  return WiFi.status() == WL_CONNECTED && mqttClient.connected();
}
