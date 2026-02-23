#include "globals.h"

void checksysdevices() {
  // String radio1_state = "error"; // removed to save heap
  Serial.println("checksysdevices: entering");

  // ----- Init radio1 -----
  Serial.println("checksysdevices: calling radio1.begin(&RADIO_SPI)");
  // if (radio1.begin(&RADIO_SPI)) {
  //   // radio1_state = "work";
  //   Serial.println("checksysdevices: radio1 OK");
  // } else {
  //   Serial.println("checksysdevices: radio1 FAIL");
  // }

  // radio2 disabled...
  Serial.println("checksysdevices: finished");
}

// Minimal stubs for optional/disabled modules so the core firmware can
// link even when the full implementations live under `disabled_modules/`.
// These are lightweight fallbacks that simply report their feature is
// unavailable.

void i2cScan() {
  notifyStatus("i2c.scan:disabled");
}

void sdinfo_readStats() {
  notifyStatus("sd.info:disabled");
}




   





    

   



