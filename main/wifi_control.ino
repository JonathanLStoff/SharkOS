// ─────────────────────────────────────────────────────────────────────────
// WiFi control transport
//
// Gives SharkOS a second control channel alongside BLE: a HIDDEN WiFi SoftAP
// plus a line-based TCP command server. A client (e.g. the Flipper's WiFi dev
// board) that already knows the SSID joins the (non-broadcast) AP and opens a
// socket to SHARK_CTRL_PORT. Every newline-terminated line it sends is fed
// into the exact same command queue as BLE writes (events_enqueue_command),
// so all `ble.scan.start` / `ble.emulate.start {json}` / etc. commands work
// identically over WiFi. Status/telemetry sent through notifyStatus() is
// mirrored to the socket.
//
// Transport exclusivity (per project requirement):
//   - Before anyone pairs, BOTH BLE and this WiFi AP accept connections.
//   - The first transport to *pair* wins: BLE pairing (PIN accepted) locks out
//     WiFi; a WiFi client connecting+handshaking locks out BLE. The loser is
//     shut down and stays down until the next reboot.
// ─────────────────────────────────────────────────────────────────────────

#include "globals.h"
#include "events.h"
#include <WiFi.h>
#include <BLEDevice.h>

// Hidden AP identity. The peer must know these; the SSID is never broadcast.
#define SHARK_AP_SSID  "SharkOS-LINK"
#define SHARK_AP_PASS  "sharktooth"   // >= 8 chars (WPA2 requirement)
#define SHARK_AP_CHAN  6
#define SHARK_CTRL_PORT 5555

// Active-transport lock (see globals.h for the enum + extern).
volatile int g_activeTransport = XPORT_NONE;
// Live connection state of the WiFi control client (drives the status LED).
volatile bool g_wifiClientConnected = false;

static WiFiServer  ctrlServer(SHARK_CTRL_PORT);
static WiFiClient  ctrlClient;
static bool        ctrlStarted   = false;   // AP + server currently up
static bool        ctrlLockedOut = false;   // permanently disabled (BLE won)
static String      ctrlLineBuf;

// Forward decl (defined below).
static void wifiCtrlStopServer();

// ── Setup: bring up the hidden AP and start listening ─────────────────────
void wifiCtrlSetup() {
  if (ctrlLockedOut) return;

  // AP_STA keeps the managed-WiFi/MQTT station capability available while we
  // also host the control AP.
  WiFi.mode(WIFI_AP_STA);

  // softAP(ssid, pass, channel, ssid_hidden=1, max_conn)
  bool ok = WiFi.softAP(SHARK_AP_SSID, SHARK_AP_PASS, SHARK_AP_CHAN, 1, 2);
  if (!ok) {
    Serial.println("[WiFiCtrl] softAP start FAILED");
    return;
  }

  ctrlServer.begin();
  ctrlServer.setNoDelay(true);
  ctrlStarted = true;
  Serial.printf("[WiFiCtrl] hidden AP '%s' up, TCP control on port %d\n",
                SHARK_AP_SSID, SHARK_CTRL_PORT);
}

// ── Lock exclusivity to one transport, tearing the other down ─────────────
// which = XPORT_BLE  → BLE won: shut the WiFi AP + server.
// which = XPORT_WIFI → WiFi won: stop BLE advertising so no new BLE clients.
void sharkLockTransport(int which) {
  if (g_activeTransport != XPORT_NONE) return; // already locked
  g_activeTransport = which;

  if (which == XPORT_BLE) {
    Serial.println("[WiFiCtrl] BLE paired first — disabling WiFi control until reboot");
    wifiCtrlStopServer();
    WiFi.softAPdisconnect(true);
    ctrlLockedOut = true;
  } else if (which == XPORT_WIFI) {
    Serial.println("[WiFiCtrl] WiFi client paired first — disabling BLE until reboot");
    pairingMode = false; // stop the pairing-mode blink; we're "connected"
    BLEAdvertising *adv = BLEDevice::getAdvertising();
    if (adv) adv->stop();
    // Drop any half-open BLE link that has not yet paired.
    if (pServer && anyConnected) {
      pServer->disconnect(pServer->getConnId());
    }
  }
}

static void wifiCtrlStopServer() {
  if (ctrlClient) ctrlClient.stop();
  ctrlServer.end();
  ctrlStarted = false;
}

// ── Send a status/telemetry line to the WiFi control client (if any) ──────
// Safe no-op when WiFi is not the active transport. Called by notifyStatus().
bool wifiCtrlSend(const char *s) {
  if (!ctrlStarted) return false;
  if (!ctrlClient || !ctrlClient.connected()) return false;
  ctrlClient.print(s);
  ctrlClient.print('\n');
  return true;
}

// ── Main-loop pump: accept a client, read command lines ───────────────────
void wifiCtrlLoop() {
  if (!ctrlStarted) return;

  // Accept a new client only if BLE has not already won.
  if (!ctrlClient || !ctrlClient.connected()) {
    if (g_wifiClientConnected) {
      g_wifiClientConnected = false; // previous client dropped
      Serial.println("[WiFiCtrl] control client disconnected");
    }
    WiFiClient incoming = ctrlServer.available();
    if (incoming) {
      if (g_activeTransport == XPORT_BLE) {
        // BLE already owns the device — refuse.
        incoming.stop();
      } else {
        ctrlClient = incoming;
        ctrlClient.setNoDelay(true);
        ctrlLineBuf = "";
        g_wifiClientConnected = true;
        // A WiFi client connecting to the hidden AP + control port counts as
        // the WiFi "pair" — lock out BLE now.
        sharkLockTransport(XPORT_WIFI);
        Serial.println("[WiFiCtrl] control client connected");
        ctrlClient.print("sharkos:wifi:ready\n");
      }
    }
    return;
  }

  // Connected — keep the LED/state reflecting an active WiFi client.
  g_wifiClientConnected = true;

  // Drain available bytes, dispatching on each newline.
  while (ctrlClient.available()) {
    char c = (char)ctrlClient.read();
    if (c == '\n') {
      ctrlLineBuf.trim();
      if (ctrlLineBuf.length() > 0) {
        Serial.printf("[WiFiCtrl] cmd: %s\n", ctrlLineBuf.c_str());
        events_enqueue_command(ctrlLineBuf);
      }
      ctrlLineBuf = "";
    } else if (c != '\r') {
      if (ctrlLineBuf.length() < 512) ctrlLineBuf += c;
    }
  }
}
