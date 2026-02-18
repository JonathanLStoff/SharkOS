#include "HIZMOS_OLED_U8G2lib.ino"
#include "mainmenu.ino"

extern bool paired;
extern bool pairingMode;
extern BLECharacteristic *pStatusChar;

unsigned long bootMillis;

void setup() {
  Serial.begin(115200);
  Serial.println("SharkOS starting (headless BLE mode)");

  // Initialize device (was previously setup)
  deviceSetup();

  // Boot time reference
  bootMillis = millis();

  // If already paired, pairingMode remains false and commands accepted
  if (paired) {
    Serial.println("Already paired (auth found)");
  }
}

void loop() {
  // After 5 seconds without a pairing, enable pairing mode (allow auth)
  if (!paired && !pairingMode && (millis() - bootMillis) > 5000) {
    pairingMode = true;
    Serial.println("Pairing mode enabled (waiting for auth)");
    if (pStatusChar) {
      std::string st("pairing:ready");
      pStatusChar->setValue(st);
      pStatusChar->notify();
    }
  }

  // Handle ongoing tasks
  handleOngoingTasks();

  // Main loop idle — BLE callbacks handle incoming commands
  delay(200);
}

