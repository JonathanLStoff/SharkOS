/*
   Based on Neil Kolban example for IDF: https://github.com/nkolban/esp32-snippets/blob/master/cpp_utils/tests/BLE%20Tests/SampleScan.cpp
   Ported to Arduino ESP32 by Evandro Copercini
*/

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

int scanTime = 5; //In seconds
BLEScan* pBLEScan;
BLEUtils utils;

char convertCharToHex(char ch)
{
  char returnType;
  switch(ch)
  {
    case '0':
    returnType = 0;
    break;
    case  '1' :
    returnType = 1;
    break;
    case  '2':
    returnType = 2;
    break;
    case  '3':
    returnType = 3;
    break;
    case  '4' :
    returnType = 4;
    break;
    case  '5':
    returnType = 5;
    break;
    case  '6':
    returnType = 6;
    break;
    case  '7':
    returnType = 7;
    break;
    case  '8':
    returnType = 8;
    break;
    case  '9':
    returnType = 9;
    break;
    case  'a':
    returnType = 10;
    break;
    case  'b':
    returnType = 11;
    break;
    case  'c':
    returnType = 12;
    break;
    case  'd':
    returnType = 13;
    break;
    case  'e':
    returnType = 14;
    break;
    case  'f' :
    returnType = 15;
    break;
    default:
    returnType = 0;
    break;
  }
  return returnType;
}

class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    String addr = advertisedDevice.getAddress().toString().c_str();
    String mfdata = advertisedDevice.getManufacturerData().c_str();
    uint8_t* payload = advertisedDevice.getPayload();
    uint8_t payloadlen = advertisedDevice.getPayloadLength();
    char *payloadhex = utils.buildHexData(nullptr, payload, payloadlen);
    if (addr.startsWith("ac:15:85")) {  // my sensors MAC start with ac:15:85
      Serial.print("Payload: "); Serial.print(payloadhex);

// convert hex-payload to array
      char *pPL = utils.buildHexData(nullptr, (uint8_t*)advertisedDevice.getPayload(), advertisedDevice.getPayloadLength());
      String sPL = (String) pPL;
      byte plByte[16];
      byte plNib[31];
      sPL.getBytes(plNib,31);
      for (int i=0; i<30; i=i+2) {
        plByte[i/2] = convertCharToHex(plNib[i])*16 + convertCharToHex(plNib[i+1]);
      }
        
      Serial.print("  ADDR: "); Serial.print(addr.substring(12));
      char *pHex = utils.buildHexData(nullptr, (uint8_t*)advertisedDevice.getManufacturerData().data(), advertisedDevice.getManufacturerData().length());
      Serial.print("  MFG DATA: "); Serial.print(pHex);
      String sHex = (String) pHex;
      byte nib[16];
      sHex.getBytes(nib,15);
      for (int i=0; i<14; i++) {
        nib[i] = convertCharToHex(nib[i]);
      }
      float Press = (float)((nib[7]*256+nib[8]*16+nib[9])-145)/10.0;
      String sPress = (String)Press;
      Serial.print("  p: "); Serial.print(sPress.substring(0,sPress.length()-1));
      int Temp = nib[4]*16+nib[5];
      Serial.print("  T: "); Serial.print(Temp);
      float Batt = (float)(nib[2]*16+nib[3])/10.0;
      String sBatt = (String)Batt;
      Serial.print("  b: "); Serial.print(sBatt.substring(0,sBatt.length()-1));
      Serial.print("  BIN: ");
      for (int i=0; i<2; i++) {prtnib(nib[i]);}
      Serial.print(".");
      for (int i=2; i<4; i++) {prtnib(nib[i]);}
      Serial.print(".");
      for (int i=4; i<6; i++) {prtnib(nib[i]);}
      Serial.print(".");
      for (int i=6; i<10; i++) {prtnib(nib[i]);}
      Serial.print(".");
      for (int i=10; i<14; i++) {prtnib(nib[i]);}

      bool nl = false;
      if (nib[0]==8) {Serial.print("   ALARM"); nl=true;}
      if (nib[0]==4) {Serial.print("   ROTAT"); nl=true;}
      if (nib[0]==2) {Serial.print("   STILL"); nl=true;}
      if (nib[0]==1) {Serial.print("   BGROT"); nl=true;}
      if (nib[1]==8) {Serial.println("   DECR2"); nl=false;}
      if (nib[1]==4) {Serial.println("   RISIN"); nl=false;}
      if (nib[1]==2) {Serial.println("   DECR1"); nl=false;}
      if ((nib[0]*16+nib[1])==0xff) {Serial.println("   LBATT");}
      if (nl) {Serial.println();}
    }
  }
};

void prtnib(int n) {
  if (n>=8) {Serial.print("1"); n-=8;} else {Serial.print("0");}
  if (n>=4) {Serial.print("1"); n-=4;} else {Serial.print("0");}
  if (n>=2) {Serial.print("1"); n-=2;} else {Serial.print("0");}
  if (n>=1) {Serial.print("1");} else {Serial.print("0");}
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("Scanning...");

  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan(); //create new scan
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true); //active scan uses more power, but get results faster
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);  // less or equal setInterval value
}

void loop() {
  BLEScanResults foundDevices = pBLEScan->start(scanTime, false);
  pBLEScan->clearResults();   // delete results fromBLEScan buffer to release memory
  delay(2000);
}
void setup() {
  Serial.begin(9600);
  delay(500);
  Serial.println(); Serial.println();
  delay(500);

  // initialize CC1101 with default settings
  Serial.print(F("[CC1101] Initializing ... "));
  int state = radio.begin();
  if (state == RADIOLIB_ERR_NONE) {
    Serial.println(F("success!"));
  } else {
    Serial.print(F("failed, code "));
    Serial.println(state);
    while (true);
  }
    
  if (state == RADIOLIB_ERR_NONE) {
    Serial.println(F("success!"));
    Serial.print(F("[CC1101] Partnumber ")); Serial.println(radio.SPIgetRegValue(0x30), HEX);
    Serial.print(F("[CC1101] Version ")); Serial.println(radio.getChipVersion(), HEX);
    Serial.println();

    float frequency = 433.92;
    if (radio.setFrequency(frequency) == RADIOLIB_ERR_INVALID_FREQUENCY) {
      Serial.println(F("[CC1101] Selected frequency is invalid for this module!"));
      while (true);
    } else {
      Serial.print(F("[CC1101] setFrequency [MHz] ")); Serial.println(frequency);
    }
    float bitrate = 19.2;  // 19200 baud
    state = radio.setBitRate(bitrate);
    if (state == RADIOLIB_ERR_INVALID_BIT_RATE) {
      Serial.println(F("[CC1101] Selected bit rate is invalid for this module!"));
      while (true);
    } else if (state == RADIOLIB_ERR_INVALID_BIT_RATE_BW_RATIO) {
      Serial.println(F("[CC1101] Selected bit rate to bandwidth ratio is invalid!"));
      Serial.println(F("[CC1101] Increase receiver bandwidth to set this bit rate."));
      while (true);
    } else {
      Serial.print(F("[CC1101] setBitRate [kbps] ")); Serial.println(bitrate);
    }
    float bandwidth = 135.0;
    if (radio.setRxBandwidth(bandwidth) == RADIOLIB_ERR_INVALID_RX_BANDWIDTH) {
      Serial.println(F("[CC1101] Selected receiver bandwidth is invalid for this module!"));
      while (true);
    } else {
      Serial.print(F("[CC1101] setBandwidth [kHz] ")); Serial.println(bandwidth);
    }
    radio.fixedPacketLengthMode(9);
  
    byte syncHigh = 0x00; byte syncLow = 0x1a;
    radio.SPIwriteRegister(0x04, syncHigh); Serial.print(F("[CC1101] Reg0x04 SetSyncHigh ")); Serial.println(syncHigh);
    radio.SPIwriteRegister(0x05, syncLow); Serial.println(F("[CC1101] Reg0x05 SetSyncLow ")); Serial.println(syncLow);

    radio.setEncoding(RADIOLIB_ENCODING_MANCHESTER);

  } else {
    Serial.print(F("failed, code "));
    Serial.println(state);
    while (true);
  }

  // set the function that will be called
  // when new packet is received
  radio.setGdo0Action(setFlag);

  // start listening for packets
  Serial.print(F("[CC1101] Starting to listen ... "));
  state = radio.startReceive();
  if (state == RADIOLIB_ERR_NONE) {
    Serial.println(F("success!"));
  } else {
    Serial.print(F("failed, code "));
    Serial.println(state);
    while (true);
  }
  Serial.println();
}

// flag to indicate that a packet was received
volatile bool receivedFlag = false;

// disable interrupt when it's not needed
volatile bool enableInterrupt = true;

// this function is called when a complete packet
// is received by the module
// IMPORTANT: this function MUST be 'void' type
//            and MUST NOT have any arguments!
#if defined(ESP8266) || defined(ESP32)
  ICACHE_RAM_ATTR
#endif
void setFlag(void) {
  // check if the interrupt is enabled
  if(!enableInterrupt) {
    return;
  }

  // we got a packet, set the flag
  receivedFlag = true;
}

void printhex(int h) {
  if (h<16) {
    Serial.print("0");
  }
  Serial.print(h,HEX);
}

void loop() {
  // check if the flag is set
  if(receivedFlag) {
    // disable the interrupt service routine while
    // processing the data
    enableInterrupt = false;

    // reset flag
    receivedFlag = false;

    // you can read received data as an Arduino String

    // you can also read received data as byte array
    int len = radio.getPacketLength();
    byte byteArr[len+1];
    int state = radio.readData(byteArr, len);

    Serial.print(len); Serial.print("; ");
    Serial.print(radio.getRSSI());
    Serial.print("; ");
    Serial.print(radio.getLQI());
    Serial.print("; ");

    // print data of the packet
    for (int i=0; i<len; i++) {
      printhex(byteArr[i]);
    }

    int chksum = (byteArr[0]^byteArr[1]^byteArr[2]^byteArr[3]^byteArr[4]^byteArr[5]^byteArr[6]^byteArr[7]);
    if (chksum == byteArr[8]) {
      Serial.print("  wheel: "); Serial.print(byteArr[4],DEC);
      Serial.print("  press: "); Serial.print((byteArr[5]&0xf)*256+byteArr[6]);
      Serial.print("  temp: "); Serial.print(byteArr[7]);
    } else { Serial.print("  wrong checksum");
    }
    
    Serial.println();

    // put module back to listen mode
    radio.startReceive();

    // we're ready to receive more packets,
    // enable interrupt service routine
    enableInterrupt = true;
  }
}