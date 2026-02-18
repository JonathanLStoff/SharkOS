
  /*

   ██╗  ██╗██╗███████╗███╗   ███╗ ██████╗ ███████╗
   ██║  ██║██║     ██╝████╗ ████║██╔═══██╗██╔════╝
   ███████║██║  ███╗  ██╔████╔██║██║   ██║███████╗
   ██╔══██║██║██╔══╝  ██║╚██╔╝██║██║   ██║╚════██║
   ██║  ██║██║███████╗██║ ╚═╝ ██║╚██████╔╝███████║
   ╚═╝  ╚═╝╚═╝╚══════╝╚═╝     ╚═╝ ╚═════╝ ╚══════╝

              — CREATED BY HIKTRON —

   - HIZMOS IS A FLIPPER ZERO REPLICA 'OPEN SOURCE' -
              
             ......... DEVELOPERS ........
            {YOUSSEF I HIKAL && OMAR K KHEDR} 
              
              """ MULTI TOOL DEVICE """

              <FOR>

              [*PENTESTERS & EMBED ENGINEERS 
              HOBBYIST , CYBER SECURITY EXPERTS*]

               #-FEATURES:

               1-WIFI ATTACKS
               2-BLE ATTACKS
               3-BAD USB
               4-NFC
               5-INFRARED
               6-SUB-GHZ
               7-GPIO
               8-APPS
               9-SETTINGS
               10-FILES   

*/

#include "animations.h"
#include "mainmenu.h"
#include "dolphinreactions.h"
#include <stdint.h>
#include <Arduino.h>
#include <U8g2lib.h>
#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <Adafruit_NeoPixel.h>
#include <SD.h>
#include <Update.h>
#include <vector>
#include "USB.h"
#include "USBHIDKeyboard.h"
#include <BleMouse.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <WiFi.h>
#include <IRremote.h>
#include <Wire.h>
#include <Adafruit_PN532.h>
#include <RadioLib.h>

#include <esp_wifi.h>
#include <ArduinoJson.h>


// HID
USBHIDKeyboard Keyboard;

BleMouse mouse_ble("hizmos", "hizmos", 100);

BLEServer *pServer;

// BLE UUIDs and characteristics
#define BLE_SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define BLE_MENU_CHAR_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define BLE_CMD_CHAR_UUID "2a73f2c0-1fb5-459e-8fcc-c5c9c331914c"
#define BLE_STATUS_CHAR_UUID "3a2743a1-1fb5-459e-8fcc-c5c9c331914d"

#include <Preferences.h>

BLECharacteristic *pMenuChar;
BLECharacteristic *pCmdChar;
BLECharacteristic *pStatusChar;

Preferences prefs; // NVS storage
bool paired = false;           // persistent auth flag
bool pairingMode = false;      // enabled after timeout if not paired
volatile bool anyConnected = false; // updated in server callbacks

// State variables for commands
bool scanningRadio = false;
float scanFrequency = 433.0;
String scanModulation = "OOK";
bool readingNfc = false;
bool sendingIr = false;

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    anyConnected = true;
    Serial.println("BLE client connected");
  }
  void onDisconnect(BLEServer* pServer) {
    anyConnected = false;
    Serial.println("BLE client disconnected");
  }
};

class CommandCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *p) {
    std::string rx = p->getValue();
    if (rx.length() > 0) {
      String s = String(rx.c_str());
      Serial.print("BLE Command received: ");
      Serial.println(s);
      handleBLECommand(s);
    }
  }
};

void handleBLECommand(String cmd); // forward
void handleOngoingTasks(); // forward

///////////////////////////////////////////////////


// هيكل الجهاز
struct blescanner_Device {
  String name;
  String address;
  int rssi;
  String manufacturer;
  String deviceType;
};

std::vector<blescanner_Device> blescanner_devices;
int blescanner_selectedIndex = 0;
BLEScan* blescanner_pBLEScan;








// Variables
int wifi_selectedIndex = 0;
int wifi_networkCount = 0;
bool wifi_showInfo = false;






/////////NEO PIXEL LED PIN DEFENTION AND VARIABLES/////////////////

#define LED_PIN    48   // Built-in NeoPixel pin on ESP32-S3 DevKit
#define LED_COUNT  1    // Only one built-in LED

Adafruit_NeoPixel pixel(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800); ////// CREATE OBJECT FOR NEO PIXEL LED

//  function to set color FOR NEO PIXEL LED (R,G,B) 
void setColor(uint8_t r, uint8_t g, uint8_t b) {
  pixel.setPixelColor(0, pixel.Color(r, g, b));
  pixel.show();
}
////////////////////////////////////////////////////////////////////


U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(
  
 U8G2_R0,
  U8X8_PIN_NONE // Reset pin not in action
 );






////////U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);


/////////////////////////////////////////////////////////////////////




///gpio pins////
#define ANALOG_PIN 3
#define WAVE_OUT_PIN 48




// Pins
#define I2C_SDA 8
#define I2C_SCL 7

// ==== IR Pins ====
#define irsenderpin  17
#define irrecivepin  18

#define PN532_IRQ   6
#define PN532_RESET 21

TwoWire myWire(0);
Adafruit_PN532 nfc(PN532_IRQ, PN532_RESET, &myWire);

// nRF24 via FSPI
#define NRF_SCK   18
#define NRF_MISO  16
#define NRF_MOSI  17

// SD Card via HSPI
#define SD_SCK    14
#define SD_MISO   39
#define SD_MOSI   38
#define SD_CS     37

// RF24 Modules
#define CE1_PIN   10
#define CSN1_PIN  11

#define CE2_PIN   12
#define CSN2_PIN  13

// CC1101_1 via custom SPI (HSPI-like)
#define CC1101_1_SCK   12
#define CC1101_1_MISO  13
#define CC1101_1_MOSI  11
#define CC1101_1_CS    10
#define CC1101_1_GDO0  9
#define CC1101_1_GDO2  14

// LoRa via custom SPI (FSPI-like)
#define LORA_SCK   36
#define LORA_MISO  37
#define LORA_MOSI  35
#define LORA_NSS   38
#define LORA_RESET 39
#define LORA_DIO0  40
#define LORA_DIO1  41
#define LORA_DIO2  42

// CC1101_2 placeholder
#define CC1101_2_SCK   69
#define CC1101_2_MISO  69
#define CC1101_2_MOSI  69
#define CC1101_2_CS    69
#define CC1101_2_GDO0  69
#define CC1101_2_GDO2  69

// RF24 objects using fspi
SPIClass RADIO_SPI(FSPI);
SPIClass SD_SPI(HSPI);
SPIClass CC1101_SPI(HSPI); // Custom for CC1101
SPIClass LORA_SPI(FSPI);   // Custom for LoRa


RF24 radio1(CE1_PIN, CSN1_PIN);
RF24 radio2(CE2_PIN, CSN2_PIN);

// CC1101 module
Module cc1101_module(CC1101_1_CS, CC1101_1_GDO0, RADIOLIB_NC, CC1101_1_GDO2, CC1101_SPI);
CC1101 cc1101(&cc1101_module);

// LoRa module
Module lora_module(LORA_NSS, LORA_DIO0, LORA_RESET, LORA_DIO1, LORA_SPI);
SX1276 lora(&lora_module);



// ==== IR ====
IRrecv irrecv(irrecivepin);
IRsend irsend(irsenderpin);
decode_results results;





// Pins for buttons
#define BTN_UP     4
#define BTN_DOWN   5
#define BTN_SELECT 6
#define BTN_BACK   7
#define BTN_LEFT   1
#define BTN_RIGHT  2

// Jamming time per channel (ms)
#define JAM_DURATION 500


const int totalAutoImages = 406;
const int totalManualImages = 11;


int batteryPercent = 87;    // أو من قراءاتك
bool sdOK = true;           // يعتمد على if (SD.begin(...))











///////////////////////////////////////
///////////////////////////////////////
bool inhizmosmenu =0;

int autoImageIndex = 0;
int manualImageIndex = 0;

bool autoMode = true;
unsigned long lastImageChangeTime = 0;
unsigned long lastButtonPressTime = 0;
const unsigned long autoModeTimeout = 120000; //
///////////////////////////////////////////////////////////////
/*






void runLoop(void (*func)()) {
  while (digitalRead(BTN_BACK) == HIGH) {
    func();

   
    if (digitalRead(BTN_SELECT) == LOW) {
      waitForRelease(BTN_SELECT);
    }
  }

  waitForRelease(BTN_BACK);  
}

void waitForRelease(uint8_t pin) {
  while (digitalRead(pin) == LOW);  
}

*/

////////////////////////////////////////////////////////////////////

// ====== Device Control Functions ======
void deactivateSD() {
  digitalWrite(SD_CS, HIGH);
}

void deactivateNRF1() {
  digitalWrite(CSN1_PIN, HIGH);
  digitalWrite(CE1_PIN, LOW);
}

void deactivateNRF2() {
  digitalWrite(CSN2_PIN, HIGH);
  digitalWrite(CE2_PIN, LOW);
}

void deactivateCC1101() {
  digitalWrite(CC1101_1_CS, HIGH);
}

void deactivateLoRa() {
  digitalWrite(LORA_NSS, HIGH);
}


// ====== Device Control Functions ======
void activateSD() {
  digitalWrite(SD_CS, LOW);
}





// دالة تتحقق من الضغط على SELECT وتنتظر رفع اليد
bool selectPressed() {
  if (digitalRead(BTN_SELECT) == LOW) {
    while (digitalRead(BTN_SELECT) == LOW);  // انتظر لحد ما يرفع صباعه
    return true;
  }
  return false;
}

void runLoop(void (*func)()) {
  unsigned long lastRun = millis();

  while (true) {
    // هدي السرعة، شغل func كل 30ms مثلاً
    if (millis() - lastRun > 130) {
      func();                 // شغّل القائمة
      lastRun = millis();    // سجل آخر وقت
    }

    // الخروج بـ BACK
    if (digitalRead(BTN_BACK) == LOW) {
      while (digitalRead(BTN_BACK) == LOW);  // استنى المستخدم يرفع صباعه
      break;
    }
  }
}












/////////////////////////////////////////////////////////////////////////

void handleButtons() {
  static unsigned long lastInputTime = 0; // ✅ لتتبع آخر وقت تم فيه الضغط
  bool buttonPressed = false;

  // ✅ السماح بالضغط فقط كل 150 ملي ثانية
  if (millis() - lastInputTime > 150) {

    // ⬆️ زر UP: ينتقل للصورة السابقة
    if (digitalRead(BTN_UP) == LOW) {
      manualImageIndex = (manualImageIndex - 1 + totalManualImages) % totalManualImages;
      autoMode = false;         // ❌ إيقاف الوضع التلقائي
      buttonPressed = true;     // ✅ تسجيل إن زر تم الضغط عليه
    }

    // ⬇️ زر DOWN: ينتقل للصورة التالية
    if (digitalRead(BTN_DOWN) == LOW) {
      manualImageIndex = (manualImageIndex + 1) % totalManualImages;
      autoMode = false;
      buttonPressed = true;
    }

    // ⬅️ زر LEFT: نفس الفكرة، ممكن تستخدمه لوظيفه إضافية
    if (digitalRead(BTN_LEFT) == LOW) {
      autoMode = false;
      buttonPressed = true;
    }

    // ➡️ زر RIGHT: ممكن تستخدمه لوظيفة أخرى
    if (digitalRead(BTN_RIGHT) == LOW) {
      autoMode = false;
      buttonPressed = true;
    }

    // 🔙 زر BACK: يُرجع للوضع التلقائي
    if (digitalRead(BTN_BACK) == LOW) {
      autoMode = true;          // ✅ تفعيل الوضع التلقائي
      buttonPressed = false;    // ❌ ما فيش حاجة يدوي حصلت
    }

    if (buttonPressed) {
      lastButtonPressTime = millis();  // ✅ تحديث وقت آخر ضغط فعلي
    }

    lastInputTime = millis(); // ✅ تحديث وقت آخر قراءة أزرار
  }
}


void autoModeCheck() {
  if (!autoMode && millis() - lastButtonPressTime > autoModeTimeout) {
    autoMode = true;
  }
}

void displayImage(const uint8_t* image) {
  u8g2.clearBuffer();
  
  u8g2.drawXBMP(0, 0, 128, 64, image);
  u8g2.sendBuffer();
}

void displaymainanim(const uint8_t* image, int batteryPercent, bool sdOK) {


  static const unsigned char image_Background_bits[] U8X8_PROGMEM = {0xfe,0x01,0x00,0x00,0x00,0x00,0x00,0xe0,0xff,0xff,0xff,0x0f,0x00,0x00,0x00,0x00,0x01,0x03,0x00,0x00,0x00,0x00,0x00,0x30,0x00,0x00,0x00,0x18,0x00,0x00,0x00,0x00,0x7d,0x06,0x00,0x00,0x00,0x00,0x00,0x18,0xff,0xb7,0x55,0x31,0x00,0x00,0x00,0x00,0x81,0xfc,0xff,0xff,0xff,0xff,0xff,0x8f,0x00,0x00,0x00,0xe2,0xff,0xff,0xff,0x7f,0x3d,0x01,0x00,0x00,0x00,0x00,0x00,0x40,0xb6,0xea,0xff,0x04,0x00,0x00,0x00,0x80,0x41,0xfe,0xff,0xff,0xaa,0xfe,0xff,0x3f,0x01,0x00,0x00,0xf9,0xff,0xff,0xff,0xab,0x9f,0x00,0x00,0x00,0x00,0x00,0x00,0x80,0xf8,0xff,0x7f,0x02,0x00,0x00,0x00,0x80,0x3e,0xff,0xff,0xff,0xff,0x55,0xfd,0x7f,0xfc,0xff,0xff,0x6c,0xff,0xff,0xff,0xb5,0x60,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x06,0x00,0x80,0x01,0x00,0x00,0x00,0x80,0xc0,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0x03,0x00,0x00,0xff,0xff,0xff,0xff,0xff,0x80,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0x01,0x00,0x00,0xfe,0xff,0xff,0xff,0x7f};
static const unsigned char image_Battery_bits[] U8X8_PROGMEM = {0xfe,0xff,0x7f,0x00,0x01,0x00,0x80,0x00,0x01,0x00,0x80,0x03,0x01,0x00,0x80,0x02,0x01,0x00,0x80,0x02,0x01,0x00,0x80,0x03,0x01,0x00,0x80,0x00,0xfe,0xff,0x7f,0x00};
static const unsigned char image_SDcardMounted_bits[] U8X8_PROGMEM = {0xff,0x07,0xff,0x04,0xff,0x07,0xff,0x04,0xff,0x07,0xff,0x04,0xff,0x07,0x67,0x00};




  u8g2.clearBuffer();

  // الخلفية (الأنيميشن)
  u8g2.setDrawColor(1);  // أبيض
  u8g2.drawXBMP(0, 0, 128, 64, image);

  // نمط XOR لعكس الألوان
  u8g2.setDrawColor(2);  // XOR mode

  

  // عرض كل شيء
  u8g2.sendBuffer();
}





void deviceSetup() {

  pinMode(BTN_UP, INPUT_PULLUP);
   pinMode(BTN_DOWN, INPUT_PULLUP);
   pinMode(BTN_SELECT, INPUT_PULLUP);
   pinMode(BTN_BACK, INPUT_PULLUP);
   pinMode(BTN_LEFT, INPUT_PULLUP);
   pinMode(BTN_RIGHT, INPUT_PULLUP);
   pinMode(WAVE_OUT_PIN, OUTPUT);
  // Set pin modes
   pinMode(SD_CS, OUTPUT);
   pinMode(CSN1_PIN, OUTPUT);
   pinMode(CSN2_PIN, OUTPUT);
   pinMode(CE1_PIN, OUTPUT);
   pinMode(CE2_PIN, OUTPUT);
   pinMode(CC1101_1_CS, OUTPUT);
   pinMode(LORA_NSS, OUTPUT);

   
  IrReceiver.begin(irrecivepin);
  IrSender.begin(irsenderpin);

  // I2C for both OLED and NFC
  myWire.begin(I2C_SDA, I2C_SCL);
  u8g2.setWire(&myWire);
  u8g2.begin();
  u8g2.setFont(u8g2_font_6x10_tf);

  nfc.begin();

  nfc.SAMConfig();

  // Init CC1101
  int state = cc1101.begin(433.0);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.print("CC1101 init failed: ");
    Serial.println(state);
  }

  // Init LoRa
  state = lora.begin(915.0);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.print("LoRa init failed: ");
    Serial.println(state);
  }




   deactivateSD();
   deactivateNRF1();
   deactivateNRF2();
   deactivateCC1101();
   deactivateLoRa();


   // Start SPI buses
   RADIO_SPI.begin(NRF_SCK, NRF_MISO, NRF_MOSI); // FSPI for NRF
   SD_SPI.begin(SD_SCK, SD_MISO, SD_MOSI);    // HSPI for SD
   CC1101_SPI.begin(CC1101_1_SCK, CC1101_1_MISO, CC1101_1_MOSI); // Custom for CC1101
   LORA_SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI); // Custom for LoRa





     Serial.begin(9600);

     pixel.begin();                                         // INITIALIZE NEOPIXEL LED  
     pixel.setBrightness(80);                              // Optional: reduce brightness
     pixel.show();                                         // Initialize all pixels to 'off'
     USB.begin();
     Keyboard.begin();

     // BLE Server for app control
    BLEDevice::init("SharkOS");
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());
    BLEService *pService = pServer->createService(BLE_SERVICE_UUID);

    // Menu (read), Command (write), Status (notify)
    pMenuChar = pService->createCharacteristic(BLE_MENU_CHAR_UUID, BLECharacteristic::PROPERTY_READ);
    pCmdChar = pService->createCharacteristic(BLE_CMD_CHAR_UUID, BLECharacteristic::PROPERTY_WRITE);
    pStatusChar = pService->createCharacteristic(BLE_STATUS_CHAR_UUID, BLECharacteristic::PROPERTY_NOTIFY);

    // Initial menu value (JSON)
    std::string menuJson = "{\"menu\":[\"CC1101 Read\",\"CC1101 Jam (disabled)\",\"LoRa Read\",\"LoRa Jam (disabled)\",\"IR Send\",\"NFC Read\"]}";
    pMenuChar->setValue(menuJson);

    pCmdChar->setCallbacks(new CommandCallbacks());

    // Load persistent pairing state
    prefs.begin("sharkos", false);
    paired = prefs.getBool("paired", false);
    pairingMode = false;
    if (paired) {
      Serial.println("Device previously paired (auth found)");
      // notify status
      if (pStatusChar) { std::string st("paired:yes"); pStatusChar->setValue(st); pStatusChar->notify(); }
    }

    pService->start();
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(BLE_SERVICE_UUID);
    pAdvertising->start();
    Serial.println("BLE advertising started");

   

     //////////////////nessesarry to activate modules or activate manually
  
  
  drawstartinfo();
  delay(1000);
  u8g2.clearBuffer();
  displayImage(config2);
  u8g2.sendBuffer();
  delay(1000);
  checksysdevices();
  delay(1000);
  activateSD();
  
}

void loop() {
  handleButtons();
  autoModeCheck();

  setColor(0, 0, 0);
 

  if (autoMode) {
    if (millis() - lastImageChangeTime > 300) {
      autoImageIndex = (autoImageIndex + 1) % totalAutoImages;
      lastImageChangeTime = millis();
    }
     
    displaymainanim(autoImages[autoImageIndex], batteryPercent, sdOK);
  
     
   
  } else {
   handlemainmenu(); 
  }


}
