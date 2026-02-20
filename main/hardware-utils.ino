//#include <stdint.h>
#include <Arduino.h>
#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
//#include <SD.h>
#include <Update.h>
#include <vector>
//#include "USB.h"
#include "USBHIDKeyboard.h"
//#include <BleMouse.h>
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
//USBHIDKeyboard Keyboard;

//BleMouse mouse_ble("sharkos", "sharkos", 100);

BLEServer *pServer;

// Headless UI stubs / placeholders (provide a single-instance dummy u8g2 and
// simple menu/button placeholders so the firmware builds without a display.)

int batteryPercent = 0;  // battery level (0-100), updated elsewhere

int selectedItem = 0;
int scrollOffset = 0;
int visibleItems = 3;
int menuLength = 1;
unsigned long lastInputTime = 0;

// No-op UI helper implementations (used when running headless)
void drawMenu() {}
void blescanner_drawMenu() {}
void blescanner_drawDeviceDetails(const struct blescanner_Device &d) { (void)d; }
bool blescanner_isPressed(int /*pin*/) { return false; }
void hidShowDebug(const String &s) { (void)s; }
void drawnosdcard() {}

// Additional UI stubs (added to eliminate "not declared" build errors)
void drawBoxMessage(const char *line1, const char *line2, const char *line3) { (void)line1; (void)line2; (void)line3; }
void formaterDrawProgress(int progress) { (void)progress; }
void drawHIDKeyboard() { }
// Forward-declare SnifferGraph to keep this file independent of packet-analyzer.h
struct SnifferGraph;

// BLE UUIDs and characteristics
#define BLE_SERVICE_UUID "4fafc201-1fbd-459e-8fcc-c5c9c331914b"
#define BLE_MENU_CHAR_UUID "beb5483e-36ed-4688-b7f5-ea07361b26a8"
#define BLE_CMD_CHAR_UUID "2a73f2c0-1fbd-459e-8fcc-c5c9c331914c"
#define BLE_STATUS_CHAR_UUID "3a2743a1-1fbd-459e-8fcc-c5c9c331914d"

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
    // If already paired, exit pairing mode immediately so LED updates.
    if (paired) pairingMode = false;
    // print remote address if available (helps debugging mobile/OS bonding issues)
    if (pServer && pServer->getConnId() >= 0) {
      Serial.print("Connection id: "); Serial.println(pServer->getConnId());
    }
  }
  void onDisconnect(BLEServer* pServer) {
    anyConnected = false;
    Serial.println("BLE client disconnected");
  }
};

#include "events.h"

class CommandCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *p) {
    String s = p->getValue();
    if (s.length() > 0) {
      Serial.print("BLE Command received: ");
      Serial.println(s);
      // Forward to the central BLE->events wrapper (permitting pairing checks)
      bluetooth_receive_command(s);
    }
  }
};

void handleBLECommand(const String &cmd); // forward (match prototype in events.h)
void handleOngoingTasks(); // forward

///////////////////////////////////////////////////


std::vector<blescanner_Device> blescanner_devices;
int blescanner_selectedIndex = 0;
BLEScan* blescanner_pBLEScan;








// Variables
int wifi_selectedIndex = 0;
int wifi_networkCount = 0;
bool wifi_showInfo = false;



///gpio pins////
// pin definitions moved to `globals.h`




// Pins (moved to globals.h)
// I2C / IR / PN532 pin macros are defined in globals.h

TwoWire myWire(0);
Adafruit_PN532 nfc(PN532_IRQ, PN532_RESET, &myWire);

// Peripheral pin macros moved to globals.h (so all modules include them)
// nRF24 / SD / RF24 / CC1101 / LoRa pin macros are defined in globals.h now.

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
// 2nd CC1101 module
Module cc1101_module2(CC1101_2_CS, CC1101_2_GDO0, RADIOLIB_NC, CC1101_2_GDO2, CC1101_SPI);
CC1101 cc1101_2(&cc1101_module2);

// LoRa module
Module lora_module(LORA_NSS, LORA_DIO0, LORA_RESET, LORA_DIO1, LORA_SPI);
SX1276 lora(&lora_module);



// ==== IR ====
IRrecv irrecv(irrecivepin);
IRsend irsend(irsenderpin);
decode_results results;

// Disrupting time per channel (ms)
#define DISRUPT_DURATION 500

bool sdOK = true;           

int autoImageIndex = 0;
int manualImageIndex = 0;

bool autoMode = true;
unsigned long lastImageChangeTime = 0;
unsigned long lastButtonPressTime = 0;
const unsigned long autoModeTimeout = 120000; //

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


void autoModeCheck() {
  if (!autoMode && millis() - lastButtonPressTime > autoModeTimeout) {
    autoMode = true;
  }
}




void deviceSetup() {

  // Button GPIOs are intentionally *not* configured here for headless/test builds
  // (commented out to avoid hardware conflicts during debugging).
  // pinMode(BTN_UP, INPUT_PULLUP);
  // pinMode(BTN_DOWN, INPUT_PULLUP);
  // pinMode(BTN_SELECT, INPUT_PULLUP);
  // pinMode(BTN_BACK, INPUT_PULLUP);
  // pinMode(BTN_LEFT, INPUT_PULLUP);
  // pinMode(BTN_RIGHT, INPUT_PULLUP);

  pinMode(WAVE_OUT_PIN, OUTPUT);
  // Set pin modes for peripherals (SD is intentionally left uninitialized)
  // pinMode(SD_CS, OUTPUT);
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

  nfc.begin();

  nfc.SAMConfig();

  // Start SPI buses BEFORE initializing radio modules (fixes crashes on some boards)
  Serial.println("deviceSetup: starting SPI buses");
  RADIO_SPI.begin(NRF_SCK, NRF_MISO, NRF_MOSI); // FSPI for NRF
  // SD_SPI.begin(SD_SCK, SD_MISO, SD_MOSI);    // SD disabled for headless/testing
  CC1101_SPI.begin(CC1101_1_SCK, CC1101_1_MISO, CC1101_1_MOSI); // Custom for CC1101
  // LORA_SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI); // LORA pins overlap PSRAM/flash on some S3 boards — disabled

  // Init CC1101
  Serial.println("deviceSetup: initializing CC1101");
  int state = cc1101.begin(433.0);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.print("CC1101 init failed: ");
    Serial.println(state);
  }

  // Init LoRa (disabled by default for S3 dev boards because the
  // LORA SPI pins overlap the board's flash/PSRAM lines; leave as a
  // no-op here to avoid corrupting flash/PSRAM.)
  Serial.println("deviceSetup: LoRa init skipped (pins overlap PSRAM/flash)");
  // state = lora.begin(915.0);
  // if (state != RADIOLIB_ERR_NONE) {
  //   Serial.print("LoRa init failed: ");
  //   Serial.println(state);
  // }




  deactivateSD();
  deactivateNRF1();
  deactivateNRF2();
  deactivateCC1101();
  deactivateLoRa();

  Serial.println("deviceSetup: radios initialized");


   // Start SPI buses (repeat removed where possible). Keep only the
   // SPI buses we actually use for headless/test builds.
   RADIO_SPI.begin(NRF_SCK, NRF_MISO, NRF_MOSI); // FSPI for NRF
   // SD_SPI.begin(SD_SCK, SD_MISO, SD_MOSI);    // SD disabled for headless/testing
   CC1101_SPI.begin(CC1101_1_SCK, CC1101_1_MISO, CC1101_1_MOSI); // Custom for CC1101
   // LORA_SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI); // disabled because pins overlap PSRAM/flash





     Serial.println("deviceSetup: serial already initialized");

    // USB.begin(); // disabled for headless / compile-only builds
    //Keyboard.begin();

    // BLE Server for app control
    BLEDevice::init("SharkOS");
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());
    BLEService *pService = pServer->createService(BLE_SERVICE_UUID);

    // Menu (read), Command (write), Status (notify)
    pMenuChar = pService->createCharacteristic(BLE_MENU_CHAR_UUID, BLECharacteristic::PROPERTY_READ);
    // accept both WRITE (with response) and WRITE_NO_RESPONSE from clients
    pCmdChar = pService->createCharacteristic(BLE_CMD_CHAR_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
    pStatusChar = pService->createCharacteristic(BLE_STATUS_CHAR_UUID, BLECharacteristic::PROPERTY_NOTIFY);

    // attach BLE2902 descriptor so clients can enable notifications on the status char
    pStatusChar->addDescriptor(new BLE2902());

    // Initial menu value (JSON)
    String menuJson = "{\"menu\":[\"CC1101 Read\",\"CC1101 Jam (disabled)\",\"LoRa Read\",\"LoRa Jam (disabled)\",\"IR Send\",\"NFC Read\"]}";
    pMenuChar->setValue(menuJson);

    pCmdChar->setCallbacks(new CommandCallbacks());

    // Load persistent pairing state
    prefs.begin("sharkos", false);
    paired = prefs.getBool("paired", false);
    pairingMode = false;
    if (paired) {
      Serial.println("Device previously paired (auth found)");
      // notify status
      if (pStatusChar) { pStatusChar->setValue(String("paired:yes")); pStatusChar->notify(); }
    }

    pService->start();
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(BLE_SERVICE_UUID);
    pAdvertising->start();
    Serial.println("BLE advertising started");

   

     //////////////////nessesarry to activate modules or activate manually
  
  
  delay(1000);
  checksysdevices();
  delay(1000);
  //activateSD();
  
}

