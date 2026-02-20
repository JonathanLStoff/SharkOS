


String mouse_currentStatus = "Waiting...";
String mouse_lastPressed = "None";




void blemouse() {


  String mouse_currentStatus = "";
  String mouse_lastPressed = "None";

  // Update connection status
  bool isConnected = mouse_ble.isConnected();
  mouse_currentStatus = isConnected ? "Connected" : "Disconnected";

  if (isConnected) {
    if (digitalRead(BTN_UP) == LOW) {
      mouse_ble.move(0, -40);
      mouse_lastPressed = "UP";
    } else if (digitalRead(BTN_DOWN) == LOW) {
      mouse_ble.move(0, 50);
      mouse_lastPressed = "DOWN";
    } else if (digitalRead(BTN_LEFT) == LOW) {
      mouse_ble.move(-50, 0);
      mouse_lastPressed = "LEFT";
    } else if (digitalRead(BTN_RIGHT) == LOW) {
      mouse_ble.move(50, 0);
      mouse_lastPressed = "RIGHT";
    } else if (digitalRead(BTN_SELECT) == LOW) {
      mouse_ble.click(MOUSE_LEFT);
      mouse_lastPressed = "SELECT";
    } else if (digitalRead(BTN_BACK) == LOW) {
      mouse_ble.click(MOUSE_RIGHT);
      mouse_lastPressed = "BACK";
    }
  }
  
  delay(50);
}


