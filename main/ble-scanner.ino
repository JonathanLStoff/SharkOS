#include "globals.h"
class blescanner_AdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) override {
    blescanner_Device dev;
    
    dev.name = advertisedDevice.getName().c_str();
    if (dev.name == "") dev.name = "(no name)";
    dev.address = advertisedDevice.getAddress().toString().c_str();
    dev.rssi = advertisedDevice.getRSSI();

    if (advertisedDevice.haveManufacturerData()) {
      String mData = advertisedDevice.getManufacturerData();

      if (mData.length() >= 2) {
        char buffer[10];
        sprintf(buffer, "0x%02X%02X", (uint8_t)mData[1], (uint8_t)mData[0]);
        dev.manufacturer = String(buffer);
      } else {
        dev.manufacturer = "unknown";
      }
    } else {
      dev.manufacturer = "unknown";
    }

    if (advertisedDevice.haveServiceUUID()) {
      dev.deviceType = advertisedDevice.getServiceUUID().toString().c_str();
    } else {
      dev.deviceType = "unknown";
    }

    blescanner_devices.push_back(dev);
  }
};

// void blescanner_scan() {
//   blescanner_devices.clear();
//   // CRITICAL: Do NOT call BLEDevice::init("") here!
//   // It re-initializes the entire BLE stack, destroying the GATT server
//   // ("SharkOS") and killing any active BLE connections. The BLE stack
//   // is already initialized in deviceSetup().
//   blescanner_pBLEScan = BLEDevice::getScan();
//   blescanner_pBLEScan->setAdvertisedDeviceCallbacks(new blescanner_AdvertisedDeviceCallbacks(), false);
//   blescanner_pBLEScan->setActiveScan(false); // passive scan to reduce interference with GATT
//   blescanner_pBLEScan->start(3, false); // reduced from 5s to 3s to minimize GATT disruption
// }

// void blescanner() {
//   static unsigned long lastPress = 0;

//   if (millis() - lastPress > 200) {
//     if (blescanner_isPressed(BTN_DOWN) && blescanner_selectedIndex < (int)blescanner_devices.size() - 1) {
//       blescanner_selectedIndex++;
//       blescanner_drawMenu();
//       lastPress = millis();
//     } else if (blescanner_isPressed(BTN_UP) && blescanner_selectedIndex > 0) {
//       blescanner_selectedIndex--;
//       blescanner_drawMenu();
//       lastPress = millis();
//     } else if (blescanner_isPressed(BTN_SELECT) && !blescanner_devices.empty()) {
//       blescanner_drawDeviceDetails(blescanner_devices[blescanner_selectedIndex]);
//       delay(3000);
//       blescanner_drawMenu();
//     } else if (blescanner_isPressed(BTN_BACK)) {
//       u8g2.clearBuffer();
//       u8g2.drawStr(0, 20, "Rescanning...");
//       u8g2.sendBuffer();
//       blescanner_scan();
//       blescanner_selectedIndex = 0;
//       blescanner_drawMenu();
//       lastPress = millis();
//     }
//   }
// }

// Active BLE scan.
//
// This kicks off a short, non-blocking BLE observer scan. Results are
// delivered asynchronously through blescanner_AdvertisedDeviceCallbacks::onResult
// (which appends to the global `blescanner_devices` vector). The caller
// (events.ino, CMD_BLE_SCAN_START) clears `blescanner_devices` first, calls
// this, then reads back the collected devices ~5.5s later.
//
// IMPORTANT: We intentionally do NOT call BLEDevice::init() here — the BLE
// stack is already initialised in deviceSetup(). Re-initialising it would
// destroy the "SharkOS" GATT server and drop the control connection (e.g. the
// Flipper / Android app that issued the scan command). A passive scan is used
// so the ESP32's own advertising / active GATT link is disturbed as little as
// possible.
void blescanner_scan() {
  if (!blescanner_pBLEScan) {
    blescanner_pBLEScan = BLEDevice::getScan();
    blescanner_pBLEScan->setAdvertisedDeviceCallbacks(
        new blescanner_AdvertisedDeviceCallbacks(), /*wantDuplicates=*/false);
  }

  // Passive scan: listen only, never send scan-request packets. This keeps
  // the radio mostly in RX and coexists better with the active GATT link.
  blescanner_pBLEScan->setActiveScan(false);
  blescanner_pBLEScan->setInterval(100);
  blescanner_pBLEScan->setWindow(80);

  Serial.println("blescanner_scan: starting 5s passive scan");
  // Async form: returns immediately, fires the completion callback after 5s.
  // onResult populates blescanner_devices as adverts arrive.
  bool ok = blescanner_pBLEScan->start(
      5,
      [](BLEScanResults results) {
        Serial.printf("blescanner_scan: complete, %d device(s)\n", results.getCount());
        // Free the BLEScan-internal result list (our findings are already
        // stored in blescanner_devices via onResult).
        if (blescanner_pBLEScan) blescanner_pBLEScan->clearResults();
      },
      false);
  if (!ok) {
    Serial.println("blescanner_scan: start() failed");
  }
}


