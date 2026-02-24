/*
  ELECHOUSE_CC1101.cpp - CC1101 module library
  Copyright (c) 2010 Michael.
    Author: Michael, <www.elechouse.com>
    Version: November 12, 2010

  This library is designed to use CC1101/CC1100 module on Arduino platform.
  CC1101/CC1100 module is an useful wireless module.Using the functions of the 
  library, you can easily send and receive data by the CC1101/CC1100 module. 
  Just have fun!
  For the details, please refer to the datasheet of CC1100/CC1101.
----------------------------------------------------------------------------------------------------------------
cc1101 Driver for RC Switch. Mod by Little Satan. With permission to modify and publish Wilson Shen (ELECHOUSE).
----------------------------------------------------------------------------------------------------------------
*/
#include <SPI.h>
#include "ELECHOUSE_CC1101_SRC_DRV.h"
#include <Arduino.h>

/****************************************************************/
#define   WRITE_BURST       0x40            //write burst
#define   READ_SINGLE       0x80            //read single
#define   READ_BURST        0xC0            //read burst
#define   BYTES_IN_RXFIFO   0x7F            //byte number in RXfifo
#define   max_modul 6

// Timeout (ms) for waiting on MISO to go LOW. Prevents infinite hang if
// CC1101 hardware is absent or not responding.
#define   CC1101_MISO_TIMEOUT_MS  100

// Helper: wait for MISO LOW with timeout. Returns true if MISO went LOW.
static bool waitMiso(byte pin) {
  unsigned long start = millis();
  while (digitalRead(pin)) {
    if (millis() - start > CC1101_MISO_TIMEOUT_MS) return false;
    yield();
  }
  return true;
}

// Read-only PA power tables shared across all instances (const, not per-instance)
// _PA_TABLE (the mutable working copy) is a per-instance member defined in the class.
//----- per-instance constructor + setSPIBus ----------------------------------------

ELECHOUSE_CC1101::ELECHOUSE_CC1101()
  : _spiBus(&SPI),
    _SCK_PIN(0), _MISO_PIN(0), _MOSI_PIN(0), _SS_PIN(0),
    _GDO0(0), _GDO2(0),
    _gdo_set(0),
    _spi(0), _ccmode(1),
    _MHz(433.92),
    _modulation(2), _frend0(0), _chan(0), _pa(12), _last_pa(0),
    _m4RxBw(0), _m4DaRa(0),
    _m2DCOFF(0), _m2MODFM(0), _m2MANCH(0), _m2SYNCM(0),
    _m1FEC(0), _m1PRE(0), _m1CHSP(0),
    _pc1PQT(0), _pc1CRC_AF(0), _pc1APP_ST(0), _pc1ADRCHK(0),
    _pc0WDATA(0), _pc0PktForm(0), _pc0CRC_EN(0), _pc0LenConf(0),
    _trxstate(0),
    _spi_initialized(false)
{
  _clb1[0]=24; _clb1[1]=28;
  _clb2[0]=31; _clb2[1]=38;
  _clb3[0]=65; _clb3[1]=76;
  _clb4[0]=77; _clb4[1]=79;
  _PA_TABLE[0]=0x00; _PA_TABLE[1]=0xC0;
  for (int i=2;i<8;i++) _PA_TABLE[i]=0x00;
  for (int i=0;i<max_modul;i++){
    _SCK_PIN_M[i]=0; _MISO_PIN_M[i]=0;
    _MOSI_PIN_M[i]=0; _SS_PIN_M[i]=0;
    _GDO0_M[i]=0; _GDO2_M[i]=0;
  }
}

void ELECHOUSE_CC1101::setSPIBus(SPIClass *bus) {
  _spiBus = bus;
}
//                       -30  -20  -15  -10   0    5    7    10
uint8_t PA_TABLE_315[8] {0x12,0x0D,0x1C,0x34,0x51,0x85,0xCB,0xC2,};             //300 - 348
uint8_t PA_TABLE_433[8] {0x12,0x0E,0x1D,0x34,0x60,0x84,0xC8,0xC0,};             //387 - 464
//                        -30  -20  -15  -10  -6    0    5    7    10   12
uint8_t PA_TABLE_868[10] {0x03,0x17,0x1D,0x26,0x37,0x50,0x86,0xCD,0xC5,0xC0,};  //779 - 899.99
//                        -30  -20  -15  -10  -6    0    5    7    10   11
uint8_t PA_TABLE_915[10] {0x03,0x0E,0x1E,0x27,0x38,0x8E,0x84,0xCC,0xC3,0xC0,};  //900 - 928
/****************************************************************
*FUNCTION NAME:SpiStart
*FUNCTION     :_spi communication start
*INPUT        :none
*OUTPUT       :none
****************************************************************/
void ELECHOUSE_CC1101::SpiStart(void)
{
  if (_spi_initialized) {
    // SPI already running — do NOT call pinMode() again as it detaches
    // the GPIO matrix routing that _spiBus->begin() set up, causing
    // _spiBus->transfer() to crash.
    return;
  }
  // initialize the SPI pins
  pinMode(_SCK_PIN, OUTPUT);
  pinMode(_MOSI_PIN, OUTPUT);
  pinMode(_MISO_PIN, INPUT);
  pinMode(_SS_PIN, OUTPUT);

  // enable SPI
  #ifdef ESP32
  if (!_spiBus->begin(_SCK_PIN, _MISO_PIN, _MOSI_PIN, _SS_PIN)) {
    return;
  }
  #else
  _spiBus->begin();
  #endif
  _spi_initialized = true;
}
/****************************************************************
*FUNCTION NAME:SpiEnd
*FUNCTION     :_spi communication disable
*INPUT        :none
*OUTPUT       :none
****************************************************************/
void ELECHOUSE_CC1101::SpiEnd(void)
{
  // No-op. Do NOT call _spiBus->endTransaction() or _spiBus->end() here.
  // The SPI bus is shared with other peripherals and must stay active.
  // Each _spiBus->transfer() is atomic on ESP32 and doesn't require
  // explicit transaction begin/end for basic register access.
}
/****************************************************************
*FUNCTION NAME: GDO_Set()
*FUNCTION     : set _GDO0,_GDO2 pin for serial pinmode.
*INPUT        : none
*OUTPUT       : none
****************************************************************/
void ELECHOUSE_CC1101::GDO_Set (void)
{
	pinMode(_GDO0, OUTPUT);
	pinMode(_GDO2, INPUT);
}
/****************************************************************
*FUNCTION NAME: GDO_Set()
*FUNCTION     : set _GDO0 for internal transmission mode.
*INPUT        : none
*OUTPUT       : none
****************************************************************/
void ELECHOUSE_CC1101::GDO0_Set (void)
{
  pinMode(_GDO0, INPUT);
}
/****************************************************************
*FUNCTION NAME:Reset
*FUNCTION     :CC1101 reset //details refer datasheet of CC1101/CC1100//
*INPUT        :none
*OUTPUT       :none
****************************************************************/
void ELECHOUSE_CC1101::Reset (void)
{
	digitalWrite(_SS_PIN, LOW);
	delay(1);
	digitalWrite(_SS_PIN, HIGH);
	delay(1);
	digitalWrite(_SS_PIN, LOW);
	if (!waitMiso(_MISO_PIN)) { return; }
  _spiBus->transfer(CC1101_SRES);
  if (!waitMiso(_MISO_PIN)) { return; }
	digitalWrite(_SS_PIN, HIGH);
}
/****************************************************************
*FUNCTION NAME:Init
*FUNCTION     :CC1101 initialization
*INPUT        :none
*OUTPUT       :none
****************************************************************/
void ELECHOUSE_CC1101::Init(void)
{
  setSpi();
  SpiStart();                   //_spi initialization
  digitalWrite(_SS_PIN, HIGH);
  digitalWrite(_SCK_PIN, HIGH);
  digitalWrite(_MOSI_PIN, LOW);
  Reset();                    //CC1101 reset
  RegConfigSettings();            //CC1101 register config
  SpiEnd();
}
/****************************************************************
*FUNCTION NAME:SpiWriteReg
*FUNCTION     :CC1101 write data to register
*INPUT        :addr: register address; value: register value
*OUTPUT       :none
****************************************************************/
void ELECHOUSE_CC1101::SpiWriteReg(byte addr, byte value)
{
  SpiStart();
  digitalWrite(_SS_PIN, LOW);
  if (!waitMiso(_MISO_PIN)) { digitalWrite(_SS_PIN, HIGH); SpiEnd(); return; }
  _spiBus->transfer(addr);
  _spiBus->transfer(value); 
  digitalWrite(_SS_PIN, HIGH);
  SpiEnd();
}
/****************************************************************
*FUNCTION NAME:SpiWriteBurstReg
*FUNCTION     :CC1101 write burst data to register
*INPUT        :addr: register address; buffer:register value array; num:number to write
*OUTPUT       :none
****************************************************************/
void ELECHOUSE_CC1101::SpiWriteBurstReg(byte addr, byte *buffer, byte num)
{
  byte i, temp;
  SpiStart();
  temp = addr | WRITE_BURST;
  digitalWrite(_SS_PIN, LOW);
  if (!waitMiso(_MISO_PIN)) { digitalWrite(_SS_PIN, HIGH); SpiEnd(); return; }
  _spiBus->transfer(temp);
  for (i = 0; i < num; i++)
  {
  _spiBus->transfer(buffer[i]);
  }
  digitalWrite(_SS_PIN, HIGH);
  SpiEnd();
}
/****************************************************************
*FUNCTION NAME:SpiStrobe
*FUNCTION     :CC1101 Strobe
*INPUT        :strobe: command; //refer define in CC1101.h//
*OUTPUT       :none
****************************************************************/
void ELECHOUSE_CC1101::SpiStrobe(byte strobe)
{
  SpiStart();
  digitalWrite(_SS_PIN, LOW);
  if (!waitMiso(_MISO_PIN)) { digitalWrite(_SS_PIN, HIGH); SpiEnd(); return; }
  _spiBus->transfer(strobe);
  digitalWrite(_SS_PIN, HIGH);
  SpiEnd();
}
/****************************************************************
*FUNCTION NAME:SpiReadReg
*FUNCTION     :CC1101 read data from register
*INPUT        :addr: register address
*OUTPUT       :register value
****************************************************************/
byte ELECHOUSE_CC1101::SpiReadReg(byte addr) 
{
  byte temp, value;
  SpiStart();
  temp = addr| READ_SINGLE;
  digitalWrite(_SS_PIN, LOW);
  if (!waitMiso(_MISO_PIN)) { digitalWrite(_SS_PIN, HIGH); SpiEnd(); return 0; }
  _spiBus->transfer(temp);
  value=_spiBus->transfer(0);
  digitalWrite(_SS_PIN, HIGH);
  SpiEnd();
  return value;
}

/****************************************************************
*FUNCTION NAME:SpiReadBurstReg
*FUNCTION     :CC1101 read burst data from register
*INPUT        :addr: register address; buffer:array to store register value; num: number to read
*OUTPUT       :none
****************************************************************/
void ELECHOUSE_CC1101::SpiReadBurstReg(byte addr, byte *buffer, byte num)
{
  byte i,temp;
  SpiStart();
  temp = addr | READ_BURST;
  digitalWrite(_SS_PIN, LOW);
  if (!waitMiso(_MISO_PIN)) { digitalWrite(_SS_PIN, HIGH); SpiEnd(); return; }
  _spiBus->transfer(temp);
  for(i=0;i<num;i++)
  {
  buffer[i]=_spiBus->transfer(0);
  }
  digitalWrite(_SS_PIN, HIGH);
  SpiEnd();
}

/****************************************************************
*FUNCTION NAME:SpiReadStatus
*FUNCTION     :CC1101 read status register
*INPUT        :addr: register address
*OUTPUT       :status value
****************************************************************/
byte ELECHOUSE_CC1101::SpiReadStatus(byte addr) 
{
  byte value,temp;
  SpiStart();
  temp = addr | READ_BURST;
  digitalWrite(_SS_PIN, LOW);
  if (!waitMiso(_MISO_PIN)) { digitalWrite(_SS_PIN, HIGH); SpiEnd(); return 0; }
  _spiBus->transfer(temp);
  value=_spiBus->transfer(0);
  digitalWrite(_SS_PIN, HIGH);
  SpiEnd();
  return value;
}
/****************************************************************
*FUNCTION NAME:SPI pin Settings
*FUNCTION     :Set Spi pins
*INPUT        :none
*OUTPUT       :none
****************************************************************/
void ELECHOUSE_CC1101::setSpi(void){
  if (_spi == 0){
  #if defined __AVR_ATmega168__ || defined __AVR_ATmega328P__
  _SCK_PIN = 13; _MISO_PIN = 12; _MOSI_PIN = 11; _SS_PIN = 10;
  #elif defined __AVR_ATmega1280__ || defined __AVR_ATmega2560__
  _SCK_PIN = 52; _MISO_PIN = 50; _MOSI_PIN = 51; _SS_PIN = 53;
  #elif ESP8266
  _SCK_PIN = 14; _MISO_PIN = 12; _MOSI_PIN = 13; _SS_PIN = 15;
  #elif ESP32
  _SCK_PIN = 18; _MISO_PIN = 19; _MOSI_PIN = 23; _SS_PIN = 5;
  #else
  _SCK_PIN = 13; _MISO_PIN = 12; _MOSI_PIN = 11; _SS_PIN = 10;
  #endif
}
}
/****************************************************************
*FUNCTION NAME:COSTUM SPI
*FUNCTION     :set costum _spi pins.
*INPUT        :none
*OUTPUT       :none
****************************************************************/
void ELECHOUSE_CC1101::setSpiPin(byte sck, byte miso, byte mosi, byte ss){
  _spi = 1;
  _SCK_PIN = sck;
  _MISO_PIN = miso;
  _MOSI_PIN = mosi;
  _SS_PIN = ss;
}
/****************************************************************
*FUNCTION NAME:COSTUM SPI
*FUNCTION     :set costum _spi pins.
*INPUT        :none
*OUTPUT       :none
****************************************************************/
void ELECHOUSE_CC1101::addSpiPin(byte sck, byte miso, byte mosi, byte ss, byte modul){
  _spi = 1;
  _SCK_PIN_M[modul] = sck;
  _MISO_PIN_M[modul] = miso;
  _MOSI_PIN_M[modul] = mosi;
  _SS_PIN_M[modul] = ss;
}
/****************************************************************
*FUNCTION NAME:GDO Pin settings
*FUNCTION     :set GDO Pins
*INPUT        :none
*OUTPUT       :none
****************************************************************/
void ELECHOUSE_CC1101::setGDO(byte gdo0, byte gdo2){
_GDO0 = gdo0;
_GDO2 = gdo2;  
GDO_Set();
}
/****************************************************************
*FUNCTION NAME:_GDO0 Pin setting
*FUNCTION     :set _GDO0 Pin
*INPUT        :none
*OUTPUT       :none
****************************************************************/
void ELECHOUSE_CC1101::setGDO0(byte gdo0){
_GDO0 = gdo0;
GDO0_Set();
}
/****************************************************************
*FUNCTION NAME:GDO Pin settings
*FUNCTION     :add GDO Pins
*INPUT        :none
*OUTPUT       :none
****************************************************************/
void ELECHOUSE_CC1101::addGDO(byte gdo0, byte gdo2, byte modul){
_GDO0_M[modul] = gdo0;
_GDO2_M[modul] = gdo2;  
_gdo_set=2;
GDO_Set();
}
/****************************************************************
*FUNCTION NAME:add _GDO0 Pin
*FUNCTION     :add _GDO0 Pin
*INPUT        :none
*OUTPUT       :none
****************************************************************/
void ELECHOUSE_CC1101::addGDO0(byte gdo0, byte modul){
_GDO0_M[modul] = gdo0;
_gdo_set=1;
GDO0_Set();
}
/****************************************************************
*FUNCTION NAME:set Modul
*FUNCTION     :change modul
*INPUT        :none
*OUTPUT       :none
****************************************************************/
void ELECHOUSE_CC1101::setModul(byte modul){
  _SCK_PIN = _SCK_PIN_M[modul];
  _MISO_PIN = _MISO_PIN_M[modul];
  _MOSI_PIN = _MOSI_PIN_M[modul];
  _SS_PIN = _SS_PIN_M[modul];
  if (_gdo_set==1){
  _GDO0 = _GDO0_M[modul];
  }
  else if (_gdo_set==2){
  _GDO0 = _GDO0_M[modul];
  _GDO2 = _GDO2_M[modul];
  }
}
/****************************************************************
*FUNCTION NAME:CCMode
*FUNCTION     :Format of RX and TX data
*INPUT        :none
*OUTPUT       :none
****************************************************************/
void ELECHOUSE_CC1101::setCCMode(bool s){
_ccmode = s;
if (_ccmode == 1){
SpiWriteReg(CC1101_IOCFG2,      0x0B);
SpiWriteReg(CC1101_IOCFG0,      0x06);
SpiWriteReg(CC1101_PKTCTRL0,    0x05);
SpiWriteReg(CC1101_MDMCFG3,     0xF8);
SpiWriteReg(CC1101_MDMCFG4,11+_m4RxBw);
}else{
SpiWriteReg(CC1101_IOCFG2,      0x0D);
SpiWriteReg(CC1101_IOCFG0,      0x0D);
SpiWriteReg(CC1101_PKTCTRL0,    0x32);
SpiWriteReg(CC1101_MDMCFG3,     0x93);
SpiWriteReg(CC1101_MDMCFG4, 7+_m4RxBw);
}
setModulation(_modulation);
}
/****************************************************************
*FUNCTION NAME:Modulation
*FUNCTION     :set CC1101 Modulation 
*INPUT        :none
*OUTPUT       :none
****************************************************************/
void ELECHOUSE_CC1101::setModulation(byte m){
if (m>4){m=4;}
_modulation = m;
Split_MDMCFG2();
switch (m)
{
case 0: _m2MODFM=0x00; _frend0=0x10; break; // 2-FSK
case 1: _m2MODFM=0x10; _frend0=0x10; break; // GFSK
case 2: _m2MODFM=0x30; _frend0=0x11; break; // ASK
case 3: _m2MODFM=0x40; _frend0=0x10; break; // 4-FSK
case 4: _m2MODFM=0x70; _frend0=0x10; break; // MSK
}
SpiWriteReg(CC1101_MDMCFG2, _m2DCOFF+_m2MODFM+_m2MANCH+_m2SYNCM);
SpiWriteReg(CC1101_FREND0,   _frend0);
setPA(_pa);
}
/****************************************************************
*FUNCTION NAME:PA Power
*FUNCTION     :set CC1101 PA Power 
*INPUT        :none
*OUTPUT       :none
****************************************************************/
void ELECHOUSE_CC1101::setPA(int p)
{
int a;
_pa = p;

if (_MHz >= 300 && _MHz <= 348){
if (_pa <= -30){a = PA_TABLE_315[0];}
else if (_pa > -30 && _pa <= -20){a = PA_TABLE_315[1];}
else if (_pa > -20 && _pa <= -15){a = PA_TABLE_315[2];}
else if (_pa > -15 && _pa <= -10){a = PA_TABLE_315[3];}
else if (_pa > -10 && _pa <= 0){a = PA_TABLE_315[4];}
else if (_pa > 0 && _pa <= 5){a = PA_TABLE_315[5];}
else if (_pa > 5 && _pa <= 7){a = PA_TABLE_315[6];}
else if (_pa > 7){a = PA_TABLE_315[7];}
_last_pa = 1;
}
else if (_MHz >= 378 && _MHz <= 464){
if (_pa <= -30){a = PA_TABLE_433[0];}
else if (_pa > -30 && _pa <= -20){a = PA_TABLE_433[1];}
else if (_pa > -20 && _pa <= -15){a = PA_TABLE_433[2];}
else if (_pa > -15 && _pa <= -10){a = PA_TABLE_433[3];}
else if (_pa > -10 && _pa <= 0){a = PA_TABLE_433[4];}
else if (_pa > 0 && _pa <= 5){a = PA_TABLE_433[5];}
else if (_pa > 5 && _pa <= 7){a = PA_TABLE_433[6];}
else if (_pa > 7){a = PA_TABLE_433[7];}
_last_pa = 2;
}
else if (_MHz >= 779 && _MHz <= 899.99){
if (_pa <= -30){a = PA_TABLE_868[0];}
else if (_pa > -30 && _pa <= -20){a = PA_TABLE_868[1];}
else if (_pa > -20 && _pa <= -15){a = PA_TABLE_868[2];}
else if (_pa > -15 && _pa <= -10){a = PA_TABLE_868[3];}
else if (_pa > -10 && _pa <= -6){a = PA_TABLE_868[4];}
else if (_pa > -6 && _pa <= 0){a = PA_TABLE_868[5];}
else if (_pa > 0 && _pa <= 5){a = PA_TABLE_868[6];}
else if (_pa > 5 && _pa <= 7){a = PA_TABLE_868[7];}
else if (_pa > 7 && _pa <= 10){a = PA_TABLE_868[8];}
else if (_pa > 10){a = PA_TABLE_868[9];}
_last_pa = 3;
}
else if (_MHz >= 900 && _MHz <= 928){
if (_pa <= -30){a = PA_TABLE_915[0];}
else if (_pa > -30 && _pa <= -20){a = PA_TABLE_915[1];}
else if (_pa > -20 && _pa <= -15){a = PA_TABLE_915[2];}
else if (_pa > -15 && _pa <= -10){a = PA_TABLE_915[3];}
else if (_pa > -10 && _pa <= -6){a = PA_TABLE_915[4];}
else if (_pa > -6 && _pa <= 0){a = PA_TABLE_915[5];}
else if (_pa > 0 && _pa <= 5){a = PA_TABLE_915[6];}
else if (_pa > 5 && _pa <= 7){a = PA_TABLE_915[7];}
else if (_pa > 7 && _pa <= 10){a = PA_TABLE_915[8];}
else if (_pa > 10){a = PA_TABLE_915[9];}
_last_pa = 4;
}
if (_modulation == 2){
_PA_TABLE[0] = 0;  
_PA_TABLE[1] = a;
}else{
_PA_TABLE[0] = a;  
_PA_TABLE[1] = 0; 
}
SpiWriteBurstReg(CC1101_PATABLE,_PA_TABLE,8);
}
/****************************************************************
*FUNCTION NAME:Frequency Calculator
*FUNCTION     :Calculate the basic frequency.
*INPUT        :none
*OUTPUT       :none
****************************************************************/
void ELECHOUSE_CC1101::setMHZ(float mhz){
byte freq2 = 0;
byte freq1 = 0;
byte freq0 = 0;

_MHz = mhz;

for (bool i = 0; i==0;){
if (mhz >= 26){
mhz-=26;
freq2+=1;
}
else if (mhz >= 0.1015625){
mhz-=0.1015625;
freq1+=1;
}
else if (mhz >= 0.00039675){
mhz-=0.00039675;
freq0+=1;
}
else{i=1;}
}
if (freq0 > 255){freq1+=1;freq0-=256;}

SpiWriteReg(CC1101_FREQ2, freq2);
SpiWriteReg(CC1101_FREQ1, freq1);
SpiWriteReg(CC1101_FREQ0, freq0);

Calibrate();
}
/****************************************************************
*FUNCTION NAME:Calibrate
*FUNCTION     :Calibrate frequency
*INPUT        :none
*OUTPUT       :none
****************************************************************/
void ELECHOUSE_CC1101::Calibrate(void){

if (_MHz >= 300 && _MHz <= 348){
SpiWriteReg(CC1101_FSCTRL0, map(_MHz, 300, 348, _clb1[0], _clb1[1]));
if (_MHz < 322.88){SpiWriteReg(CC1101_TEST0,0x0B);}
else{
SpiWriteReg(CC1101_TEST0,0x09);
int s = SpiReadStatus(CC1101_FSCAL2);
if (s<32){SpiWriteReg(CC1101_FSCAL2, s+32);}
if (_last_pa != 1){setPA(_pa);}
}
}
else if (_MHz >= 378 && _MHz <= 464){
SpiWriteReg(CC1101_FSCTRL0, map(_MHz, 378, 464, _clb2[0], _clb2[1]));
if (_MHz < 430.5){SpiWriteReg(CC1101_TEST0,0x0B);}
else{
SpiWriteReg(CC1101_TEST0,0x09);
int s = SpiReadStatus(CC1101_FSCAL2);
if (s<32){SpiWriteReg(CC1101_FSCAL2, s+32);}
if (_last_pa != 2){setPA(_pa);}
}
}
else if (_MHz >= 779 && _MHz <= 899.99){
SpiWriteReg(CC1101_FSCTRL0, map(_MHz, 779, 899, _clb3[0], _clb3[1]));
if (_MHz < 861){SpiWriteReg(CC1101_TEST0,0x0B);}
else{
SpiWriteReg(CC1101_TEST0,0x09);
int s = SpiReadStatus(CC1101_FSCAL2);
if (s<32){SpiWriteReg(CC1101_FSCAL2, s+32);}
if (_last_pa != 3){setPA(_pa);}
}
}
else if (_MHz >= 900 && _MHz <= 928){
SpiWriteReg(CC1101_FSCTRL0, map(_MHz, 900, 928, _clb4[0], _clb4[1]));
SpiWriteReg(CC1101_TEST0,0x09);
int s = SpiReadStatus(CC1101_FSCAL2);
if (s<32){SpiWriteReg(CC1101_FSCAL2, s+32);}
if (_last_pa != 4){setPA(_pa);}
}
}
/****************************************************************
*FUNCTION NAME:Calibration offset
*FUNCTION     :Set calibration offset
*INPUT        :none
*OUTPUT       :none
****************************************************************/
void ELECHOUSE_CC1101::setClb(byte b, byte s, byte e){
if (b == 1){
_clb1[0]=s;
_clb1[1]=e;  
}
else if (b == 2){
_clb2[0]=s;
_clb2[1]=e;  
}
else if (b == 3){
_clb3[0]=s;
_clb3[1]=e;  
}
else if (b == 4){
_clb4[0]=s;
_clb4[1]=e;  
}
}
/****************************************************************
*FUNCTION NAME:getCC1101
*FUNCTION     :Test Spi connection and return 1 when true.
*INPUT        :none
*OUTPUT       :none
****************************************************************/
bool ELECHOUSE_CC1101::getCC1101(void){
setSpi();
if (SpiReadStatus(0x31)>0){
return 1;
}else{
return 0;
}
}
/****************************************************************
*FUNCTION NAME:getMode
*FUNCTION     :Return the Mode. Sidle = 0, TX = 1, Rx = 2.
*INPUT        :none
*OUTPUT       :none
****************************************************************/
byte ELECHOUSE_CC1101::getMode(void){
return _trxstate;
}
/****************************************************************
*FUNCTION NAME:Set Sync_Word
*FUNCTION     :Sync Word
*INPUT        :none
*OUTPUT       :none
****************************************************************/
void ELECHOUSE_CC1101::setSyncWord(byte sh, byte sl){
SpiWriteReg(CC1101_SYNC1, sh);
SpiWriteReg(CC1101_SYNC0, sl);
}
/****************************************************************
*FUNCTION NAME:Set ADDR
*FUNCTION     :Address used for packet filtration. Optional broadcast addresses are 0 (0x00) and 255 (0xFF).
*INPUT        :none
*OUTPUT       :none
****************************************************************/
void ELECHOUSE_CC1101::setAddr(byte v){
SpiWriteReg(CC1101_ADDR, v);
}
/****************************************************************
*FUNCTION NAME:Set PQT
*FUNCTION     :Preamble quality estimator threshold
*INPUT        :none
*OUTPUT       :none
****************************************************************/
void ELECHOUSE_CC1101::setPQT(byte v){
Split_PKTCTRL1();
_pc1PQT = 0;
if (v>7){v=7;}
_pc1PQT = v*32;
SpiWriteReg(CC1101_PKTCTRL1, _pc1PQT+_pc1CRC_AF+_pc1APP_ST+_pc1ADRCHK);
}
/****************************************************************
*FUNCTION NAME:Set CRC_AUTOFLUSH
*FUNCTION     :Enable automatic flush of RX FIFO when CRC is not OK
*INPUT        :none
*OUTPUT       :none
****************************************************************/
void ELECHOUSE_CC1101::setCRC_AF(bool v){
Split_PKTCTRL1();
_pc1CRC_AF = 0;
if (v==1){_pc1CRC_AF=8;}
SpiWriteReg(CC1101_PKTCTRL1, _pc1PQT+_pc1CRC_AF+_pc1APP_ST+_pc1ADRCHK);
}
/****************************************************************
*FUNCTION NAME:Set APPEND_STATUS
*FUNCTION     :When enabled, two status bytes will be appended to the payload of the packet
*INPUT        :none
*OUTPUT       :none
****************************************************************/
void ELECHOUSE_CC1101::setAppendStatus(bool v){
Split_PKTCTRL1();
_pc1APP_ST = 0;
if (v==1){_pc1APP_ST=4;}
SpiWriteReg(CC1101_PKTCTRL1, _pc1PQT+_pc1CRC_AF+_pc1APP_ST+_pc1ADRCHK);
}
/****************************************************************
*FUNCTION NAME:Set ADR_CHK
*FUNCTION     :Controls address check configuration of received packages
*INPUT        :none
*OUTPUT       :none
****************************************************************/
void ELECHOUSE_CC1101::setAdrChk(byte v){
Split_PKTCTRL1();
_pc1ADRCHK = 0;
if (v>3){v=3;}
_pc1ADRCHK = v;
SpiWriteReg(CC1101_PKTCTRL1, _pc1PQT+_pc1CRC_AF+_pc1APP_ST+_pc1ADRCHK);
}
/****************************************************************
*FUNCTION NAME:Set WHITE_DATA
*FUNCTION     :Turn data whitening on / off.
*INPUT        :none
*OUTPUT       :none
****************************************************************/
void ELECHOUSE_CC1101::setWhiteData(bool v){
Split_PKTCTRL0();
_pc0WDATA = 0;
if (v == 1){_pc0WDATA=64;}
SpiWriteReg(CC1101_PKTCTRL0, _pc0WDATA+_pc0PktForm+_pc0CRC_EN+_pc0LenConf);
}
/****************************************************************
*FUNCTION NAME:Set PKT_FORMAT
*FUNCTION     :Format of RX and TX data
*INPUT        :none
*OUTPUT       :none
****************************************************************/
void ELECHOUSE_CC1101::setPktFormat(byte v){
Split_PKTCTRL0();
_pc0PktForm = 0;
if (v>3){v=3;}
_pc0PktForm = v*16;
SpiWriteReg(CC1101_PKTCTRL0, _pc0WDATA+_pc0PktForm+_pc0CRC_EN+_pc0LenConf);
}
/****************************************************************
*FUNCTION NAME:Set CRC
*FUNCTION     :CRC calculation in TX and CRC check in RX
*INPUT        :none
*OUTPUT       :none
****************************************************************/
void ELECHOUSE_CC1101::setCrc(bool v){
Split_PKTCTRL0();
_pc0CRC_EN = 0;
if (v==1){_pc0CRC_EN=4;}
SpiWriteReg(CC1101_PKTCTRL0, _pc0WDATA+_pc0PktForm+_pc0CRC_EN+_pc0LenConf);
}
/****************************************************************
*FUNCTION NAME:Set LENGTH_CONFIG
*FUNCTION     :Configure the packet length
*INPUT        :none
*OUTPUT       :none
****************************************************************/
void ELECHOUSE_CC1101::setLengthConfig(byte v){
Split_PKTCTRL0();
_pc0LenConf = 0;
if (v>3){v=3;}
_pc0LenConf = v;
SpiWriteReg(CC1101_PKTCTRL0, _pc0WDATA+_pc0PktForm+_pc0CRC_EN+_pc0LenConf);
}
/****************************************************************
*FUNCTION NAME:Set PACKET_LENGTH
*FUNCTION     :Indicates the packet length
*INPUT        :none
*OUTPUT       :none
****************************************************************/
void ELECHOUSE_CC1101::setPacketLength(byte v){
SpiWriteReg(CC1101_PKTLEN, v);
}
/****************************************************************
*FUNCTION NAME:Set DCFILT_OFF
*FUNCTION     :Disable digital DC blocking filter before demodulator
*INPUT        :none
*OUTPUT       :none
****************************************************************/
void ELECHOUSE_CC1101::setDcFilterOff(bool v){
Split_MDMCFG2();
_m2DCOFF = 0;
if (v==1){_m2DCOFF=128;}
SpiWriteReg(CC1101_MDMCFG2, _m2DCOFF+_m2MODFM+_m2MANCH+_m2SYNCM);
}
/****************************************************************
*FUNCTION NAME:Set MANCHESTER
*FUNCTION     :Enables Manchester encoding/decoding
*INPUT        :none
*OUTPUT       :none
****************************************************************/
void ELECHOUSE_CC1101::setManchester(bool v){
Split_MDMCFG2();
_m2MANCH = 0;
if (v==1){_m2MANCH=8;}
SpiWriteReg(CC1101_MDMCFG2, _m2DCOFF+_m2MODFM+_m2MANCH+_m2SYNCM);
}
/****************************************************************
*FUNCTION NAME:Set SYNC_MODE
*FUNCTION     :Combined sync-word qualifier mode
*INPUT        :none
*OUTPUT       :none
****************************************************************/
void ELECHOUSE_CC1101::setSyncMode(byte v){
Split_MDMCFG2();
_m2SYNCM = 0;
if (v>7){v=7;}
_m2SYNCM=v;
SpiWriteReg(CC1101_MDMCFG2, _m2DCOFF+_m2MODFM+_m2MANCH+_m2SYNCM);
}
/****************************************************************
*FUNCTION NAME:Set FEC
*FUNCTION     :Enable Forward Error Correction (FEC)
*INPUT        :none
*OUTPUT       :none
****************************************************************/
void ELECHOUSE_CC1101::setFEC(bool v){
Split_MDMCFG1();
_m1FEC=0;
if (v==1){_m1FEC=128;}
SpiWriteReg(CC1101_MDMCFG1, _m1FEC+_m1PRE+_m1CHSP);
}
/****************************************************************
*FUNCTION NAME:Set PRE
*FUNCTION     :Sets the minimum number of preamble bytes to be transmitted.
*INPUT        :none
*OUTPUT       :none
****************************************************************/
void ELECHOUSE_CC1101::setPRE(byte v){
Split_MDMCFG1();
_m1PRE=0;
if (v>7){v=7;}
_m1PRE = v*16;
SpiWriteReg(CC1101_MDMCFG1, _m1FEC+_m1PRE+_m1CHSP);
}
/****************************************************************
*FUNCTION NAME:Set Channel
*FUNCTION     :none
*INPUT        :none
*OUTPUT       :none
****************************************************************/
void ELECHOUSE_CC1101::setChannel(byte ch){
_chan = ch;
SpiWriteReg(CC1101_CHANNR,   _chan);
}
/****************************************************************
*FUNCTION NAME:Set Channel spacing
*FUNCTION     :none
*INPUT        :none
*OUTPUT       :none
****************************************************************/
void ELECHOUSE_CC1101::setChsp(float f){
Split_MDMCFG1();
byte MDMCFG0 = 0;
_m1CHSP = 0;
if (f > 405.456543){f = 405.456543;}
if (f < 25.390625){f = 25.390625;}
for (int i = 0; i<5; i++){
if (f <= 50.682068){
f -= 25.390625;
f /= 0.0991825;
MDMCFG0 = f;
float s1 = (f - MDMCFG0) *10;
if (s1 >= 5){MDMCFG0++;}
i = 5;
}else{
_m1CHSP++;
f/=2;
}
}
SpiWriteReg(19,_m1CHSP+_m1FEC+_m1PRE);
SpiWriteReg(20,MDMCFG0);
}
/****************************************************************
*FUNCTION NAME:Set Receive bandwidth
*FUNCTION     :none
*INPUT        :none
*OUTPUT       :none
****************************************************************/
void ELECHOUSE_CC1101::setRxBW(float f){
Split_MDMCFG4();
int s1 = 3;
int s2 = 3;
for (int i = 0; i<3; i++){
if (f > 101.5625){f/=2; s1--;}
else{i=3;}
}
for (int i = 0; i<3; i++){
if (f > 58.1){f/=1.25; s2--;}
else{i=3;}
}
s1 *= 64;
s2 *= 16;
_m4RxBw = s1 + s2;
SpiWriteReg(16,_m4RxBw+_m4DaRa);
}
/****************************************************************
*FUNCTION NAME:Set Data Rate
*FUNCTION     :none
*INPUT        :none
*OUTPUT       :none
****************************************************************/
void ELECHOUSE_CC1101::setDRate(float d){
Split_MDMCFG4();
float c = d;
byte MDMCFG3 = 0;
if (c > 1621.83){c = 1621.83;}
if (c < 0.0247955){c = 0.0247955;}
_m4DaRa = 0;
for (int i = 0; i<20; i++){
if (c <= 0.0494942){
c = c - 0.0247955;
c = c / 0.00009685;
MDMCFG3 = c;
float s1 = (c - MDMCFG3) *10;
if (s1 >= 5){MDMCFG3++;}
i = 20;
}else{
_m4DaRa++;
c = c/2;
}
}
SpiWriteReg(16,  _m4RxBw+_m4DaRa);
SpiWriteReg(17,  MDMCFG3);
}
/****************************************************************
*FUNCTION NAME:Set Devitation
*FUNCTION     :none
*INPUT        :none
*OUTPUT       :none
****************************************************************/
void ELECHOUSE_CC1101::setDeviation(float d){
float f = 1.586914;
float v = 0.19836425;
int c = 0;
if (d > 380.859375){d = 380.859375;}
if (d < 1.586914){d = 1.586914;}
for (int i = 0; i<255; i++){
f+=v;
if (c==7){v*=2;c=-1;i+=8;}
if (f>=d){c=i;i=255;}
c++;
}
SpiWriteReg(21,c);
}
/****************************************************************
*FUNCTION NAME:Split PKTCTRL0
*FUNCTION     :none
*INPUT        :none
*OUTPUT       :none
****************************************************************/
void ELECHOUSE_CC1101::Split_PKTCTRL1(void){
int calc = SpiReadStatus(7);
_pc1PQT = 0;
_pc1CRC_AF = 0;
_pc1APP_ST = 0;
_pc1ADRCHK = 0;
for (bool i = 0; i==0;){
if (calc >= 32){calc-=32; _pc1PQT+=32;}
else if (calc >= 8){calc-=8; _pc1CRC_AF+=8;}
else if (calc >= 4){calc-=4; _pc1APP_ST+=4;}
else {_pc1ADRCHK = calc; i=1;}
}
}
/****************************************************************
*FUNCTION NAME:Split PKTCTRL0
*FUNCTION     :none
*INPUT        :none
*OUTPUT       :none
****************************************************************/
void ELECHOUSE_CC1101::Split_PKTCTRL0(void){
int calc = SpiReadStatus(8);
_pc0WDATA = 0;
_pc0PktForm = 0;
_pc0CRC_EN = 0;
_pc0LenConf = 0;
for (bool i = 0; i==0;){
if (calc >= 64){calc-=64; _pc0WDATA+=64;}
else if (calc >= 16){calc-=16; _pc0PktForm+=16;}
else if (calc >= 4){calc-=4; _pc0CRC_EN+=4;}
else {_pc0LenConf = calc; i=1;}
}
}
/****************************************************************
*FUNCTION NAME:Split MDMCFG1
*FUNCTION     :none
*INPUT        :none
*OUTPUT       :none
****************************************************************/
void ELECHOUSE_CC1101::Split_MDMCFG1(void){
int calc = SpiReadStatus(19);
_m1FEC = 0;
_m1PRE = 0;
_m1CHSP = 0;
int s2 = 0;
for (bool i = 0; i==0;){
if (calc >= 128){calc-=128; _m1FEC+=128;}
else if (calc >= 16){calc-=16; _m1PRE+=16;}
else {_m1CHSP = calc; i=1;}
}
}
/****************************************************************
*FUNCTION NAME:Split MDMCFG2
*FUNCTION     :none
*INPUT        :none
*OUTPUT       :none
****************************************************************/
void ELECHOUSE_CC1101::Split_MDMCFG2(void){
int calc = SpiReadStatus(18);
_m2DCOFF = 0;
_m2MODFM = 0;
_m2MANCH = 0;
_m2SYNCM = 0;
for (bool i = 0; i==0;){
if (calc >= 128){calc-=128; _m2DCOFF+=128;}
else if (calc >= 16){calc-=16; _m2MODFM+=16;}
else if (calc >= 8){calc-=8; _m2MANCH+=8;}
else{_m2SYNCM = calc; i=1;}
}
}
/****************************************************************
*FUNCTION NAME:Split MDMCFG4
*FUNCTION     :none
*INPUT        :none
*OUTPUT       :none
****************************************************************/
void ELECHOUSE_CC1101::Split_MDMCFG4(void){
int calc = SpiReadStatus(16);
_m4RxBw = 0;
_m4DaRa = 0;
for (bool i = 0; i==0;){
if (calc >= 64){calc-=64; _m4RxBw+=64;}
else if (calc >= 16){calc -= 16; _m4RxBw+=16;}
else{_m4DaRa = calc; i=1;}
}
}
/****************************************************************
*FUNCTION NAME:RegConfigSettings
*FUNCTION     :CC1101 register config //details refer datasheet of CC1101/CC1100//
*INPUT        :none
*OUTPUT       :none
****************************************************************/
void ELECHOUSE_CC1101::RegConfigSettings(void) 
{   
    SpiWriteReg(CC1101_FSCTRL1,  0x06);
    
    setCCMode(_ccmode);
    setMHZ(_MHz);
    
    SpiWriteReg(CC1101_MDMCFG1,  0x22);
    SpiWriteReg(CC1101_MDMCFG0,  0xF8);
    SpiWriteReg(CC1101_CHANNR,   _chan);
    SpiWriteReg(CC1101_DEVIATN,  0x47);
    SpiWriteReg(CC1101_FREND1,   0x56);
    SpiWriteReg(CC1101_MCSM0 ,   0x18);
    SpiWriteReg(CC1101_FOCCFG,   0x16);
    SpiWriteReg(CC1101_BSCFG,    0x1C);
    SpiWriteReg(CC1101_AGCCTRL2, 0xC7);
    SpiWriteReg(CC1101_AGCCTRL1, 0x00);
    SpiWriteReg(CC1101_AGCCTRL0, 0xB2);
    SpiWriteReg(CC1101_FSCAL3,   0xE9);
    SpiWriteReg(CC1101_FSCAL2,   0x2A);
    SpiWriteReg(CC1101_FSCAL1,   0x00);
    SpiWriteReg(CC1101_FSCAL0,   0x1F);
    SpiWriteReg(CC1101_FSTEST,   0x59);
    SpiWriteReg(CC1101_TEST2,    0x81);
    SpiWriteReg(CC1101_TEST1,    0x35);
    SpiWriteReg(CC1101_TEST0,    0x09);
    SpiWriteReg(CC1101_PKTCTRL1, 0x04);
    SpiWriteReg(CC1101_ADDR,     0x00);
    SpiWriteReg(CC1101_PKTLEN,   0xFF);
}
/****************************************************************
*FUNCTION NAME:SetTx
*FUNCTION     :set CC1101 send data
*INPUT        :none
*OUTPUT       :none
****************************************************************/
void ELECHOUSE_CC1101::SetTx(void)
{
  SpiStrobe(CC1101_SIDLE);
  SpiStrobe(CC1101_STX);        //start send
  _trxstate=1;
}
/****************************************************************
*FUNCTION NAME:SetRx
*FUNCTION     :set CC1101 to receive state
*INPUT        :none
*OUTPUT       :none
****************************************************************/
void ELECHOUSE_CC1101::SetRx(void)
{
  SpiStrobe(CC1101_SIDLE);
  SpiStrobe(CC1101_SRX);        //start receive
  _trxstate=2;
}
/****************************************************************
*FUNCTION NAME:SetTx
*FUNCTION     :set CC1101 send data and change frequency
*INPUT        :none
*OUTPUT       :none
****************************************************************/
void ELECHOUSE_CC1101::SetTx(float mhz)
{
  SpiStrobe(CC1101_SIDLE);
  setMHZ(mhz);
  SpiStrobe(CC1101_STX);        //start send
  _trxstate=1;
}
/****************************************************************
*FUNCTION NAME:SetRx
*FUNCTION     :set CC1101 to receive state and change frequency
*INPUT        :none
*OUTPUT       :none
****************************************************************/
void ELECHOUSE_CC1101::SetRx(float mhz)
{
  SpiStrobe(CC1101_SIDLE);
  setMHZ(mhz);
  SpiStrobe(CC1101_SRX);        //start receive
  _trxstate=2;
}
/****************************************************************
*FUNCTION NAME:RSSI Level
*FUNCTION     :Calculating the RSSI Level
*INPUT        :none
*OUTPUT       :none
****************************************************************/
int ELECHOUSE_CC1101::getRssi(void)
{
int rssi;
rssi=SpiReadStatus(CC1101_RSSI);
if (rssi >= 128){rssi = (rssi-256)/2-74;}
else{rssi = (rssi/2)-74;}
return rssi;
}
/****************************************************************
*FUNCTION NAME:LQI Level
*FUNCTION     :get Lqi state
*INPUT        :none
*OUTPUT       :none
****************************************************************/
byte ELECHOUSE_CC1101::getLqi(void)
{
byte lqi;
lqi=SpiReadStatus(CC1101_LQI);
return lqi;
}
/****************************************************************
*FUNCTION NAME:SetSres
*FUNCTION     :Reset CC1101
*INPUT        :none
*OUTPUT       :none
****************************************************************/
void ELECHOUSE_CC1101::setSres(void)
{
  SpiStrobe(CC1101_SRES);
  _trxstate=0;
}
/****************************************************************
*FUNCTION NAME:setSidle
*FUNCTION     :set Rx / TX Off
*INPUT        :none
*OUTPUT       :none
****************************************************************/
void ELECHOUSE_CC1101::setSidle(void)
{
  SpiStrobe(CC1101_SIDLE);
  _trxstate=0;
}
/****************************************************************
*FUNCTION NAME:goSleep
*FUNCTION     :set cc1101 Sleep on
*INPUT        :none
*OUTPUT       :none
****************************************************************/
void ELECHOUSE_CC1101::goSleep(void){
  _trxstate=0;
  SpiStrobe(0x36);//Exit RX / TX, turn off frequency synthesizer and exit
  SpiStrobe(0x39);//Enter power down mode when CSn goes high.
}
/****************************************************************
*FUNCTION NAME:Char direct SendData
*FUNCTION     :use CC1101 send data
*INPUT        :txBuffer: data array to send; size: number of data to send, no more than 61
*OUTPUT       :none
****************************************************************/
void ELECHOUSE_CC1101::SendData(char *txchar)
{
int len = strlen(txchar);
byte chartobyte[len];
for (int i = 0; i<len; i++){chartobyte[i] = txchar[i];}
SendData(chartobyte,len);
}
/****************************************************************
*FUNCTION NAME:SendData
*FUNCTION     :use CC1101 send data
*INPUT        :txBuffer: data array to send; size: number of data to send, no more than 61
*OUTPUT       :none
****************************************************************/
void ELECHOUSE_CC1101::SendData(byte *txBuffer,byte size)
{
  SpiWriteReg(CC1101_TXFIFO,size);
  SpiWriteBurstReg(CC1101_TXFIFO,txBuffer,size);      //write data to send
  SpiStrobe(CC1101_SIDLE);
  SpiStrobe(CC1101_STX);                  //start send
  {
    // Wait for GDO0 HIGH (sync/preamble started) — 200 ms timeout
    unsigned long _t = millis();
    while (!digitalRead(_GDO0)) {
      if (millis() - _t > 200) {
        Serial.println("  [CC1101] SendData: GDO0 HIGH timeout — TX never started (check GDO0 wiring)");
        SpiStrobe(CC1101_SFTX);
        _trxstate = 1;
        return;
      }
      yield();
    }
    // Wait for GDO0 LOW (packet end) — 200 ms timeout
    _t = millis();
    while (digitalRead(_GDO0)) {
      if (millis() - _t > 200) {
        Serial.println("  [CC1101] SendData: GDO0 LOW timeout — TX stuck high (check GDO0 wiring)");
        break;
      }
      yield();
    }
  }
  SpiStrobe(CC1101_SFTX);                 //flush TXfifo
  _trxstate=1;
}
/****************************************************************
*FUNCTION NAME:Char direct SendData
*FUNCTION     :use CC1101 send data without GDO
*INPUT        :txBuffer: data array to send; size: number of data to send, no more than 61
*OUTPUT       :none
****************************************************************/
void ELECHOUSE_CC1101::SendData(char *txchar,int t)
{
int len = strlen(txchar);
byte chartobyte[len];
for (int i = 0; i<len; i++){chartobyte[i] = txchar[i];}
SendData(chartobyte,len,t);
}
/****************************************************************
*FUNCTION NAME:SendData
*FUNCTION     :use CC1101 send data without GDO
*INPUT        :txBuffer: data array to send; size: number of data to send, no more than 61
*OUTPUT       :none
****************************************************************/
void ELECHOUSE_CC1101::SendData(byte *txBuffer,byte size,int t)
{
  SpiWriteReg(CC1101_TXFIFO,size);
  SpiWriteBurstReg(CC1101_TXFIFO,txBuffer,size);      //write data to send
  SpiStrobe(CC1101_SIDLE);
  SpiStrobe(CC1101_STX);                  //start send
  delay(t);
  SpiStrobe(CC1101_SFTX);                 //flush TXfifo
  _trxstate=1;
}
/****************************************************************
*FUNCTION NAME:Check CRC
*FUNCTION     :none
*INPUT        :none
*OUTPUT       :none
****************************************************************/
bool ELECHOUSE_CC1101::CheckCRC(void){
byte lqi=SpiReadStatus(CC1101_LQI);
bool crc_ok = bitRead(lqi,7);
if (crc_ok == 1){
return 1;
}else{
SpiStrobe(CC1101_SFRX);
SpiStrobe(CC1101_SRX);
return 0;
}
}
/****************************************************************
*FUNCTION NAME:CheckRxFifo
*FUNCTION     :check receive data or not
*INPUT        :none
*OUTPUT       :flag: 0 no data; 1 receive data 
****************************************************************/
bool ELECHOUSE_CC1101::CheckRxFifo(int t){
if(_trxstate!=2){SetRx();}
if(SpiReadStatus(CC1101_RXBYTES) & BYTES_IN_RXFIFO){
delay(t);
return 1;
}else{
return 0;
}
}
/****************************************************************
*FUNCTION NAME:CheckReceiveFlag
*FUNCTION     :check receive data or not
*INPUT        :none
*OUTPUT       :flag: 0 no data; 1 receive data 
****************************************************************/
byte ELECHOUSE_CC1101::CheckReceiveFlag(void)
{
  if(_trxstate!=2){SetRx();}
	if(digitalRead(_GDO0))			//receive data
	{
		// Wait for GDO0 LOW (end of received packet) — 100 ms timeout
		unsigned long _t = millis();
		while (digitalRead(_GDO0)) {
			if (millis() - _t > 100) {
				Serial.println("  [CC1101] CheckReceiveFlag: GDO0 stuck HIGH — packet never ended");
				break;
			}
			yield();
		}
		return 1;
	}
	else							// no data
	{
		return 0;
	}
}
/****************************************************************
*FUNCTION NAME:ReceiveData
*FUNCTION     :read data received from RXfifo
*INPUT        :rxBuffer: buffer to store data
*OUTPUT       :size of data received
****************************************************************/
byte ELECHOUSE_CC1101::ReceiveData(byte *rxBuffer)
{
	byte size;
	byte status[2];

	if(SpiReadStatus(CC1101_RXBYTES) & BYTES_IN_RXFIFO)
	{
		size=SpiReadReg(CC1101_RXFIFO);
		SpiReadBurstReg(CC1101_RXFIFO,rxBuffer,size);
		SpiReadBurstReg(CC1101_RXFIFO,status,2);
		SpiStrobe(CC1101_SFRX);
    SpiStrobe(CC1101_SRX);
		return size;
	}
	else
	{
		SpiStrobe(CC1101_SFRX);
    SpiStrobe(CC1101_SRX);
 		return 0;
	}
}
ELECHOUSE_CC1101 ELECHOUSE_cc1101;
