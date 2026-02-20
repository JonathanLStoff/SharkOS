
#include "globals.h"

void scanningwifi() {


  if (digitalRead(BTN_DOWN) == LOW) {
    wifi_selectedIndex++;
    if (wifi_selectedIndex >= wifi_networkCount) wifi_selectedIndex = 0;
    delay(50);
  }

  if (digitalRead(BTN_UP) == LOW) {
    wifi_selectedIndex--;
    if (wifi_selectedIndex < 0) wifi_selectedIndex = wifi_networkCount - 1;
    delay(50);
  }

  if (digitalRead(BTN_SELECT) == LOW) {
    wifi_showInfo = true;
    delay(50);
  }

  if (digitalRead(BTN_BACK) == LOW) {
    wifi_showInfo = false;
    delay(50);
  }

  u8g2.clearBuffer();


}

String wifi_encryptionType(wifi_auth_mode_t encryption) {
  switch (encryption) {
    case WIFI_AUTH_OPEN: return "Open";
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA";
    case WIFI_AUTH_WPA2_PSK: return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2";
    case WIFI_AUTH_WPA3_PSK: return "WPA3";
    default: return "Unknown";
  }
}

