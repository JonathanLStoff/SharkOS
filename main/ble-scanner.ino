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

void blescanner_scan() {
  blescanner_devices.clear();
  BLEDevice::init("");
  blescanner_pBLEScan = BLEDevice::getScan();
  blescanner_pBLEScan->setAdvertisedDeviceCallbacks(new blescanner_AdvertisedDeviceCallbacks(), false);
  blescanner_pBLEScan->setActiveScan(true);
  blescanner_pBLEScan->start(5, false); // scan for 5 seconds
}

void blescanner() {
  static unsigned long lastPress = 0;

  if (millis() - lastPress > 200) {
    if (blescanner_isPressed(BTN_DOWN) && blescanner_selectedIndex < (int)blescanner_devices.size() - 1) {
      blescanner_selectedIndex++;
      blescanner_drawMenu();
      lastPress = millis();
    } else if (blescanner_isPressed(BTN_UP) && blescanner_selectedIndex > 0) {
      blescanner_selectedIndex--;
      blescanner_drawMenu();
      lastPress = millis();
    } else if (blescanner_isPressed(BTN_SELECT) && !blescanner_devices.empty()) {
      blescanner_drawDeviceDetails(blescanner_devices[blescanner_selectedIndex]);
      delay(3000);
      blescanner_drawMenu();
    } else if (blescanner_isPressed(BTN_BACK)) {
      u8g2.clearBuffer();
      u8g2.drawStr(0, 20, "Rescanning...");
      u8g2.sendBuffer();
      blescanner_scan();
      blescanner_selectedIndex = 0;
      blescanner_drawMenu();
      lastPress = millis();
    }
  }
}




