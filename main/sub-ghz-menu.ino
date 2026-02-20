#include "globals.h"
void handlesubghzmenu() {
  const char* menuItems[] = {"READ", "READ RAW", "FREQUENCY ANALYZER", "JAMMER", "SAVED SIGNALS", "CC1101 READ", "CC1101 JAM", "LORA READ", "LORA JAM"};
  const int menuLength = sizeof(menuItems) / sizeof(menuItems[0]);
  const int visibleItems = 3;

  static int selectedItem = 0;
  static int scrollOffset = 0;

  static unsigned long lastInputTime = 0;

  if (millis() - lastInputTime > 150) {

  if (digitalRead(BTN_UP) == LOW) {
    selectedItem--;
    if (selectedItem < 0) selectedItem = menuLength - 1;
    scrollOffset = constrain(selectedItem - visibleItems + 1, 0, menuLength - visibleItems);
    lastInputTime = millis(); 
  }

  if (digitalRead(BTN_DOWN) == LOW) {
    selectedItem++;
    if (selectedItem >= menuLength) selectedItem = 0;
    scrollOffset = constrain(selectedItem - visibleItems + 1, 0, menuLength - visibleItems);
    lastInputTime = millis(); 
  }

  if (digitalRead(BTN_SELECT) == LOW) {
    switch (selectedItem) {
      case 0:
        cc1101Read();
        break;
      case 1:
        cc1101Read(); // READ RAW (placeholder)
        break;
      case 2:
        notifyStatus("frequency.analyzer:not_implemented");
        break;
      case 3:
        cc1101Jam();
        break;
      case 4:
        notifyStatus("subghz.saved:not_implemented");
        break;
      case 5:
       // CC1101 READ
       break;
      case 6:
       // CC1101 JAM
       break;
      case 7:
       // LORA READ
       break;
      case 8:
       // LORA JAM
       break;
      
    }
    lastInputTime = millis(); 
  }
  }
 

 
  
}


