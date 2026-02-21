#include "globals.h"
// #include <SD.h>  // SD library DISABLED — saves ~30KB flash, prevents SPI conflicts
#include <Update.h>
//////////sd flasher tool variables///////////////
//////////////////////////////////////////////////
// Files kept minimal — SD functions are stubs until SD hardware is connected
int fileCount = 0;
int topIndex = 0;

bool confirmFlash = false;


void flasherloop(){
  // SD flasher disabled (no SD card module)
}







void readSDfiles() {
  // SD disabled
  fileCount = 0;
}


void flashBinary(String path) {
  // SD disabled — cannot flash from SD card
  Serial.println("flashBinary: SD disabled");
}
