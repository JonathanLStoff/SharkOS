#include "globals.h"

// === Global Variables ===
float sdinfo_totalMB = 0, sdinfo_usedMB = 0, sdinfo_freeMB = 0;
int sdinfo_fileCount = 0;
uint64_t sdinfo_largestFile = 0, sdinfo_smallestFile = UINT64_MAX;
bool sdinfo_showDetails = false;

// === Init Display ===
void sdinfo_initDisplay() {
  u8g2.begin();
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(20, 30, "Initializing...");
  u8g2.sendBuffer();
}



// === Read SD Stats ===
void sdinfo_readStats() {
  File root = SD.open("/");
  sdinfo_usedMB = 0;
  sdinfo_fileCount = 0;
  sdinfo_largestFile = 0;
  sdinfo_smallestFile = UINT64_MAX;

  while (true) {
    File entry = root.openNextFile();
    if (!entry) break;
    if (!entry.isDirectory()) {
      uint64_t size = entry.size();
      sdinfo_usedMB += (float)size / (1024.0 * 1024.0);
      sdinfo_fileCount++;
      if (size > sdinfo_largestFile) sdinfo_largestFile = size;
      if (size < sdinfo_smallestFile) sdinfo_smallestFile = size;
    }
    entry.close();
  }
  root.close();

  sdinfo_totalMB = (float)SD.cardSize() / (1024.0 * 1024.0);
  sdinfo_freeMB = sdinfo_totalMB - sdinfo_usedMB;
}


// === Button Handling ===
void sdinfo_handleButtons() {
  static bool lastSelect = HIGH;
  bool selectPressed = digitalRead(BTN_SELECT) == LOW;

  if (selectPressed && lastSelect == HIGH) {
    sdinfo_showDetails = !sdinfo_showDetails;
    
  }

  lastSelect = !selectPressed;
}

void showsdinfo() {
  sdinfo_handleButtons();
  delay(100);  // زرار debounce
}


