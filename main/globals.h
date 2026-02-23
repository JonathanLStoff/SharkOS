#ifndef SHARKOS_H
#define SHARKOS_H

// Central shared header for SharkOS (temporary - refactor to .cpp/.h later)
// - Provides common externs, forward declarations and a NO_OLED headless stub
// - Included from all .ino files to avoid duplicate-symbol / ordering issues

#include <Arduino.h>
#include <Preferences.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
// U8g2 library removed — DummyU8g2 provides headless no-op stubs.
// Stub out macros and font symbols that the codebase still references.
#ifndef U8X8_PROGMEM
#define U8X8_PROGMEM
#endif
#define u8g2_font_6x10_tf   nullptr
#define u8g2_font_6x10_tr   nullptr
#define u8g2_font_6x12_tf   nullptr
#define u8g2_font_6x12_tr   nullptr
#define u8g2_font_6x13_tr   nullptr
#define u8g2_font_7x14_tf   nullptr
#define u8g2_font_7x14B_tf  nullptr
#define u8g2_font_8x13B_tf  nullptr
#define u8g2_font_4x6_tr    nullptr
#define u8g2_font_haxrcorp4089_tr nullptr
#define u8g2_font_profont12_tr    nullptr
#define u8g2_font_profont15_tr    nullptr
#include <WiFi.h>
#include "USBHIDKeyboard.h"
#include <esp_system.h>
#include <esp_chip_info.h>

// NOTE: do NOT force NO_OLED here — let individual build configs
// or the user's choice determine whether the OLED/UI is compiled.


class TwoWire; // forward

// Lightweight headless OLED stub used when NO_OLED is defined so UI calls
// compile safely even if the display isn't present.
class DummyU8g2 {
public:
  void setWire(TwoWire*) {}
  void begin() {}
  void clearBuffer() {}
  void setCursor(int, int) {}
  void print(const String&) {}
  void print(const char*) {}
  void print(char) {}
  void print(int) {}
  void print(unsigned int, int) {}
  void print(unsigned long, int) {}
  void print(uint8_t, int) {}
  void printf(const char*, ...) {}
  void setFont(const uint8_t*) {}
  void setFontMode(int) {}
  void setBitmapMode(int) {}
  void setDrawColor(int) {}
  void drawStr(int, int, const char*) {}
  void drawXBMP(int, int, int, int, const uint8_t*) {}
  void drawRBox(int, int, int, int, int) {}
  void drawBox(int, int, int, int) {}
  void drawPixel(int, int) {}
  void drawFrame(int, int, int, int, int) {}
  void drawFrame(int, int, int, int) {}
  void drawRFrame(int, int, int, int, int) {}
  void drawLine(int, int, int, int) {}
  void drawHLine(int, int, int) {}
  void sendBuffer() {}
};

extern DummyU8g2 u8g2;

struct blescanner_Device {
  String name;
  String address;
  int rssi;
  String manufacturer;
  String deviceType;
};
struct wifi_Network {
  String ssid;
  int rssi;
  String bssid;
};
struct rf_signal {
  String type;
  String data;
  int frequency;
  int rssi;
};
// SD Card via HSPI

// Button GPIO pin assignments (defaults — adjust for your board if needed)
#define BTN_UP     8
#define BTN_DOWN   8
#define BTN_LEFT   8
#define BTN_RIGHT  8
#define BTN_SELECT 8
#define BTN_BACK   8

// Hardware pin mapping (Corrected for ESP32-S3 N16R8)
#define ANALOG_PIN 3
#define WAVE_OUT_PIN 47 // Moved from 48 (hardwired to onboard RGB LED) [1, 2]

#define I2C_SDA 8
#define I2C_SCL 7

// ==== IR Pins ====
#define irsenderpin  1 // Relocated to avoid SPI/UART overlap
#define irrecivepin  2 

#define PN532_IRQ   6
#define PN532_RESET 21

// Shared Hardware SPI Bus (FSPI/SPI2) for all radio modules [3, 4]
#define SPI_SCK    12 // Hardware FSPICLK [3]
#define SPI_MISO   13 // Hardware FSPIQ [3]
#define SPI_MOSI   11 // Hardware FSPID [3]

// nRF24 Module (Shared SPI)
#define NRF_SCK    12
#define NRF_MISO   13
#define NRF_MOSI   11
#define CE1_PIN    9 
#define CSN1_PIN   10 // Hardware FSPICS0 [3]

// CC1101_1 Module (Shared SPI)
#define CC1101_1_SCK   12 // phys 5
#define CC1101_1_MISO  13 // phys 7
#define CC1101_1_MOSI  11 // phys 6
#define CC1101_1_CS    14 // phys 4
#define CC1101_1_GDO0  15 // phys 3
#define CC1101_1_GDO2  16 // phys 8

// CC1101_2 Module (Shared SPI)
#define CC1101_2_SCK   12
#define CC1101_2_MISO  13
#define CC1101_2_MOSI  11
#define CC1101_2_CS    17
#define CC1101_2_GDO0  18
#define CC1101_2_GDO2  4

// LoRa Module (Shared SPI)
#define LORA_SCK   12
#define LORA_MISO  13
#define LORA_MOSI  11
#define LORA_NSS   38 // Moved from 35-37 (Restricted Octal SPI PSRAM pins) [2, 5]
#define LORA_RESET 39
#define LORA_DIO0  40
#define LORA_DIO1  41
#define LORA_DIO2  42

#define DISRUPT_DURATION 500

// Radio batching configuration: buffer size (bytes) and idle timeout (ms)
#ifndef RADIO_SIGNAL_BUFFER_SIZE
#define RADIO_SIGNAL_BUFFER_SIZE 256
#endif
#ifndef RADIO_SIGNAL_IDLE_TIMEOUT_MS
#define RADIO_SIGNAL_IDLE_TIMEOUT_MS 500
#endif

// --- protobuf data types shared across modules ---

enum RadioModule { CC1101_1 = 0, CC1101_2 = 1, LORA = 2, NFC = 3, WIFI = 4, BLUETOOTH = 5, IR = 6 };

struct RadioSignal {
  uint64_t timestamp_ms;
  RadioModule module;
  float frequency_mhz;
  int32_t rssi;
  std::vector<uint8_t> payload;
  String extra;
  // helper defined in hardware-utils.ino
  String toJson() const;
};

// forward declarations for utilities implemented elsewhere
extern String base64_encode(const uint8_t *data, size_t len);

// --- Shared globals (defined in one .ino only, declared extern here) ---
#include <SPI.h>
#include <Wire.h>
//#include <SD.h>
#include <RadioLib.h>
#include <RF24.h>
#include <IRremote.h>
#include <Adafruit_PN532.h>
#include <BLEScan.h>
//#include <BleMouse.h>

extern Preferences prefs;           // NVS storage
extern BLECharacteristic *pStatusChar;
extern BLECharacteristic *pMenuChar;
extern BLECharacteristic *pCmdChar;
extern bool paired;
extern bool pairingMode;
extern volatile bool anyConnected;
extern int batteryPercent;

// Shared globals from hardware-utils.ino (make them visible to other modules)
//extern USBHIDKeyboard Keyboard;
//extern BleMouse mouse_ble;
extern BLEServer *pServer;
extern bool scanningRadio;
extern int wifi_scan_channel; // 0 = all
extern bool wifi_scan_5ghz;   // false = 2.4 GHz only
extern float scanFrequency;
// modulation enumeration used by CC1101 transceivers and UI commands
// LoRa modulation is handled by the separate LoRaTransceiver class.
enum ModulationType {
    MOD_OOK,
    MOD_2FSK,
    MOD_ASK,
    MOD_GFSK,
    MOD_MSK,
    MOD_UNKNOWN
};

extern std::vector<String> scanModulation;

// helpers for converting between string and enum (implemented in hardware-utils)
ModulationType modulationFromString(const String &s);
String modulationToString(ModulationType m);
extern bool readingNfc;
extern Adafruit_PN532 nfc;
extern bool sendingIr;

extern std::vector<blescanner_Device> blescanner_devices;
extern int blescanner_selectedIndex;
extern BLEScan* blescanner_pBLEScan;

extern int wifi_selectedIndex;
extern int wifi_networkCount;
extern bool wifi_showInfo;

//extern SPIClass RADIO_SPI;
// extern SPIClass CC1101_SPI; // Removed
// LORA_SPI removed — LoRa now shares RADIO_SPI (both FSPI)

extern RF24 radio1;
// radio2 REMOVED — single nRF24 module only

// extern Module cc1101_module;
// extern CC1101 cc1101;
// extern Module cc1101_module2;
// extern CC1101 cc1101_2;

#include <ELECHOUSE_CC1101_SRC_DRV.h>
extern ELECHOUSE_CC1101 cc1101_driver_1;
extern ELECHOUSE_CC1101 cc1101_driver_2;
extern Module lora_module;
extern SX1276 lora;

extern IRrecv irrecv;
extern IRsend irsend;
extern decode_results results;

extern bool sdOK;
extern int autoImageIndex;
extern int manualImageIndex;
extern bool autoMode;
extern unsigned long lastImageChangeTime;
extern unsigned long lastButtonPressTime;

// --- Common function prototypes (prevent "not declared" errors) ---
void deviceSetup();
void handleOngoingTasks();
void notifyStatus(const char *s);
void displayImage(const unsigned char* image);
void checksysdevices();

// Headless / UI stubs (implemented in hardware-utils.ino)
void showRunningScreen(const String &taskName, uint8_t duration = 5);
void drawMenu();
void blescanner_drawMenu();
void blescanner_drawDeviceDetails(const struct blescanner_Device &d);
bool blescanner_isPressed(int pin);
void setColor(uint8_t r, uint8_t g, uint8_t b);
void hidShowDebug(const String &s);
void drawnosdcard();

// Forward-declare types used by UI prototypes
struct SnifferGraph;

// Simple global placeholders used by menu code (headless defaults)
extern int selectedItem;
extern int scrollOffset;
extern int visibleItems;
extern int menuLength;
extern unsigned long lastInputTime;

// UI helper prototypes (headless no-op implementations in hardware-utils.ino)
void drawBoxMessage(const char *line1, const char *line2, const char *line3);
void formaterDrawProgress(int progress);
void drawGraph(const SnifferGraph &sniffer, uint16_t pktCount);
void drawHIDKeyboard();

// Screen defaults (used by various UI modules)
#ifndef SCREEN_WIDTH
#define SCREEN_WIDTH 128
#endif
#ifndef SCREEN_HEIGHT
#define SCREEN_HEIGHT 64
#endif
#ifndef GRAPH_HEIGHT
#define GRAPH_HEIGHT 48
#endif

// If the board doesn't provide RGB_BUILTIN via core headers, default to
// the common pin used on YD-ESP32-S3 / similar dev modules.
#ifndef RGB_BUILTIN
#define RGB_BUILTIN 48
#endif

// Misc UI helpers
void drawFrameRounded(int x, int y, int w, int h, int r);

// Common app/ui functions (prototypes moved here to fix cross-file calls)
void runLoop(void (*func)());
void readSDfiles();
void flasherloop();
void flashBinary(String path);
void loading();
void displaymainanim(const uint8_t* image, int batteryPercent, bool sdOK);
void handlemainmenu();
void about();

// Calculator
void claculaterloop();
void moveCursor(int dr, int dc);
void handleSelect();
float evalExpression(String expr);

// Snake / games
void snakeSetup();
void snakeLoop();
void spacegame();

// Pomodoro / misc
void pomdorotimerloop();
void updateTimer();
bool buttonPressed(int pin);
void handlePasswordMaker();

// HID
void hidkeyboard();
void hidInit();
void hidscriptmenu();

// Misc helpers
void runCommand(const char *cmd);

// BLE / scanners
void blescanner_scan();
void blemouse();

// WiFi helpers
void scanningwifi();
String wifi_encryptionType(wifi_auth_mode_t encryption);

// nRF / NRF scanner helpers
void scanAll();

// Oscilloscope
void analogread();
void wavecreator();

// Restart helpers
void restartDevice();

// Extern image/frame arrays used across files
extern const uint8_t* nosdFrames[];
// BLE / scanners
void blescanner_scan();
void blemouse();

// I2C
void i2cScan();
void i2cscanner();
void i2cShowDeviceInfo(uint8_t address);
const char* identifyDevice(uint8_t address);

// Infrared
void recvIR();
void irSend(const String &payload);
void listIRFiles();

// SD / FS
void sdinfo_readStats();

// Events helpers
void events_init();
bool events_enqueue_command(const String &cmd);
void events_process_one();
bool events_validate_topic(const String &payload);
void handleBLECommand(const String &jsonCmd);
void bluetooth_send_response(const String &payload, const String &inReplyTo = "");

// LED / command indicators
// - `firstCommandReceived` becomes true after the first *valid* BLE command
// - `indicate_command_success()` / `indicate_command_failure()` trigger
//   a 500ms transient LED flash (yellow for success, red for failure)
extern bool firstCommandReceived;
void indicate_command_success();
void indicate_command_failure();

// CC1101 / LoRa functions
void cc1101Read();
void cc1101Jam();
void loraRead();
void loraJam();

// nRF24 functions
void nrfscanner();

// Restart logic
void drawCancelledMessage();

void runWifiBleScanTasks();
void hw_send_status_protobuf(bool is_scanning,
                             int battery_percent,
                             bool cc1101_1_connected,
                             bool cc1101_2_connected,
                             bool lora_connected,
                             bool nfc_connected,
                             bool wifi_connected,
                             bool bluetooth_connected,
                             bool ir_connected,
                             bool serial_connected);

#endif // SHARKOS_H
