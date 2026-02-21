/*

    ███████╗██╗  ██╗ █████╗ ██████╗ ██╗  ██╗ ██████╗ ███████╗
    ██╔════╝██║  ██║██╔══██╗██╔══██╗██║ ██╔╝██╔═══██╗██╔════╝
    ███████╗███████║███████║██████╔╝█████╔╝ ██║   ██║███████╗
    ╚════██║██╔══██║██╔══██║██╔══██╗██╔═██╗ ██║   ██║╚════██║
    ███████║██║  ██║██║  ██║██║  ██║██║  ██╗╚██████╔╝███████║
    ╚══════╝╚═╝  ╚═╝╚═╝  ╚═╝╚═╝  ╚═╝╚═╝  ╚═╝ ╚═════╝ ╚══════╝


              — CREATED BY Jonathan Stoff —

   - SHARKOS IS A FLIPPER ZERO REPLICA 'OPEN SOURCE' -
              
             ......... DEVELOPERS ........
                    {Jonathan Stoff} 
              
              """ MULTI TOOL DEVICE """

              <FOR>

              [*PENTESTERS & EMBED ENGINEERS 
              HOBBYIST , CYBER SECURITY EXPERTS*]

               #-FEATURES:

               1-WIFI ATTACKS
               2-BLE ATTACKS
               3-BAD USB
               4-NFC
               5-INFRARED
               6-SUB-GHZ
               7-GPIO
               8-APPS
               9-SETTINGS
               10-FILES   

    Process =>  - Bluetooth wait for connection and pairing (with timeout to auto-enable pairing mode)
                - Event queue for incoming BLE commands (with validation and pairing checks)
                - Execute commands from the queue, including starting/stopping background tasks (scans) and dispatching to handlers
                  - Wait for data from the commands that gather data and send it back to connected device
                  - Handle ongoing tasks (non-blocking)
                  - Wait for next BLE command for commands that require multiple steps (e.g. pairing, or multi-part commands)
                - Ensure main loop remains responsive and non-blocking, even when handling complex commands or background tasks
                - Max of one active background task (e.g. scan) at a time, with new tasks stopping previous ones

*/

#include "Arduino.h"
#include "globals.h"

#if defined(ARDUINO_ARCH_ESP32)
#include <esp_system.h>
#endif



extern bool paired;
extern bool pairingMode;
extern volatile bool anyConnected;
extern BLECharacteristic *pStatusChar;

unsigned long bootMillis;

static bool crashDetected = false;
static bool pairingBlinkOn = false;
static unsigned long pairingBlinkMillis = 0;

// Command/LED transient state (persistent `firstCommandReceived` becomes
// true after the first *valid* BLE command is received).
bool firstCommandReceived = false;          // visible via globals.h
static uint8_t cmdLedState = 0;             // 0 = none, 1 = yellow, 2 = red
static unsigned long cmdLedUntilMs = 0;     // transient override expiry (ms)

#include "events.h"

static void setStatusLed(uint8_t r, uint8_t g, uint8_t b) {
#if defined(ARDUINO_ARCH_ESP32) && defined(RGB_BUILTIN)
  neopixelWrite(RGB_BUILTIN, r, g, b);
#else
  (void)r;
  (void)g;
  (void)b;
#endif
}

// Simple public helper used elsewhere in codebase
void setColor(uint8_t r, uint8_t g, uint8_t b) { setStatusLed(r, g, b); }

static bool wasCrashReset() {
#if defined(ARDUINO_ARCH_ESP32)
  esp_reset_reason_t reason = esp_reset_reason();
  return reason == ESP_RST_PANIC || reason == ESP_RST_WDT || reason == ESP_RST_TASK_WDT || reason == ESP_RST_INT_WDT;
#else
  return false;
#endif
}

// Called by events code to indicate a successful/known command was received.
void indicate_command_success() {
  firstCommandReceived = true;           // persistently show green from now on
  cmdLedState = 1;                       // yellow flash
  cmdLedUntilMs = millis() + 500;       // 500 ms transient
}

// Called by events code to indicate an unknown/invalid command was received.
void indicate_command_failure() {
  // treat failure as a transient red flash; still mark that a command was seen
  firstCommandReceived = true;
  cmdLedState = 2;                       // red flash
  cmdLedUntilMs = millis() + 500;       // 500 ms transient
}

static void updateStatusLed() {
  unsigned long now = millis();

  // Crash always highest priority
  if (crashDetected) {
    setStatusLed(255, 0, 0);
    return;
  }

  // Transient command LED (flashes) override other states while active
  if (cmdLedUntilMs != 0 && now <= cmdLedUntilMs) {
    if (cmdLedState == 1) {         // yellow
      setStatusLed(255, 200, 0);
      return;
    }
    if (cmdLedState == 2) {         // red
      setStatusLed(255, 0, 0);
      return;
    }
  }

  // After first valid command, show steady green to indicate 'active' state
  if (firstCommandReceived) {
    setStatusLed(0, 128, 0);
    return;
  }

  // Pairing mode (only shown before firstCommandReceived)
  if (pairingMode) {
    if ((now - pairingBlinkMillis) >= 400) {
      pairingBlinkMillis = now;
      pairingBlinkOn = !pairingBlinkOn;
    }
    setStatusLed(0, 0, pairingBlinkOn ? 255 : 0);
    return;
  }

  // Fall back to previous behavior
  if (paired && anyConnected) {
    setStatusLed(0, 128, 0);
  } else {
    setStatusLed(0, 0, 255);
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("SharkOS starting (headless BLE mode)");
  delay(4500);  // Allow USB CDC to enumerate before any heavy init
  Serial.println("SharkOS passed the delay");

  crashDetected = wasCrashReset();
  if (crashDetected) {
    Serial.println("WARNING: Previous boot crashed (WDT/panic detected)");
  }

  // Initialize device (was previously setup) — deviceSetup starts BLE advertising
  deviceSetup();
  Serial.println("main.setup: deviceSetup() returned");

  // Ensure advertising is active so previously-paired clients can connect
  if (BLEDevice::getAdvertising()) {
    BLEDevice::getAdvertising()->start();
  }

  // Initialize BLE command/event subsystem
  events_init();

  // Boot time reference
  bootMillis = millis();

  // If already paired, pairingMode remains false and commands accepted
  if (paired) {
    Serial.println("Already paired (auth found)");
  }

  // Apply initial LED state
  updateStatusLed();
}

// Poll BLE characteristic (safe no-op if characteristic is null).
// This ensures commands are picked up even if a client writes without
// triggering a callback in some edge cases.
static void pollBluetoothCommands() {
  if (!pCmdChar) return;
  static String lastVal;
  String v = pCmdChar->getValue();
  if (v.length() > 0 && v != lastVal) {
    lastVal = v;
    bluetooth_receive_command(v);
  }
}

void loop() {
  // If connected + paired — exit pairing mode and accept commands.
  if (paired && anyConnected && pairingMode) {
    pairingMode = false;
    Serial.println("Connected + paired — exiting pairing mode");
  }

  // After 5 seconds: if device has NEVER been paired, or if it was paired
  // previously but *no client connected* within the timeout, enable pairing
  // mode so the app can authenticate (PIN-based pairing).
  if (!pairingMode && (millis() - bootMillis) > 5000) {
    if (!paired || (paired && !anyConnected)) {
      pairingMode = true;
      Serial.println("Pairing mode enabled (waiting for auth)");
      if (pStatusChar) {
        pStatusChar->setValue(String("pairing:ready"));
        pStatusChar->notify();
      }
    }
  }

  // If connected and paired — listen for BLE commands (both callback and polling)
  if (paired && anyConnected) {
    pollBluetoothCommands();
  }

  // Process one queued BLE event (non-blocking)
  events_process_one();

  // Handle ongoing tasks
  handleOngoingTasks();

  // Update onboard RGB LED status
  updateStatusLed();

  // Main loop idle
  delay(200);
}

