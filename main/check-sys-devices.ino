#include "globals.h"

void checksysdevices() {


  String radio1_state = "error";
  String radio2_state = "error";
  String sd_state     = "error";

  // ----- Init radio1 -----
  if (radio1.begin(&RADIO_SPI)) {
    radio1_state = "work";
    
    
  }

  // ----- Init radio2 -----
  if (radio2.begin(&RADIO_SPI)) {
    radio2_state = "work";
   
    
  }

  // ----- Init SD Card -----
  // if (SD.begin(SD_CS, SD_SPI)) {
  //   sd_state = "work";
  //   sdOK = true;
  //   setColor(0,255,0);
  // } else {
  //   sd_state = "error";
  //   sdOK = false;
  // }

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




   





    

   



