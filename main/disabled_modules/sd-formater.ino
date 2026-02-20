#include "globals.h"
// ==== Format State ====
enum FormaterState {
  FORMAT_ASK,
  FORMAT_CONFIRM,
  FORMAT_PROGRESS,
  FORMAT_DONE
};

FormaterState formaterState = FORMAT_ASK;
bool formaterLastSelectState = HIGH;


void formaterDeleteAllFromSD(File dir) {
  while (true) {
    File entry = dir.openNextFile();
    if (!entry) break;

    if (entry.isDirectory()) {
      formaterDeleteAllFromSD(entry);
      SD.rmdir(entry.name());
    } else {
      SD.remove(entry.name());
    }

    entry.close();
  }
  dir.close();
}

// ==== تنفيذ عملية الفورمات ====
void formaterStart() {
  for (int p = 0; p <= 108; p += 10) {
    formaterDrawProgress(p);
    delay(100);
  }

  File root = SD.open("/");
  formaterDeleteAllFromSD(root);
  delay(300);

  formaterState = FORMAT_DONE;
  drawBoxMessage("SD Card", "Formatted!", "Returning...");
  delay(2000);

  formaterState = FORMAT_ASK;
  drawBoxMessage("Do you want to", "format SD card?", "Press SELECT to continue");
}



void sdformater() {
  bool selectPressed = digitalRead(BTN_SELECT) == LOW;
  bool backPressed = digitalRead(BTN_BACK) == LOW;

  if (selectPressed && formaterLastSelectState == HIGH) {
    switch (formaterState) {
      case FORMAT_ASK:
        formaterState = FORMAT_CONFIRM;
        drawBoxMessage("Are you sure?", "Data will be erased!", "SELECT=Yes  BACK=No");
        break;

      case FORMAT_CONFIRM:
        formaterState = FORMAT_PROGRESS;
        formaterStart();
        break;

      default:
        break;
    }
  }

  if (backPressed && formaterState == FORMAT_CONFIRM) {
    formaterState = FORMAT_ASK;
    drawBoxMessage("Do you want to", "format SD card?", "Press SELECT to continue");
  }

  formaterLastSelectState = !selectPressed;
  delay(100);
}
