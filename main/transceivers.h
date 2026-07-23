#ifndef TRANSCEIVERS_H
#define TRANSCEIVERS_H

#include <vector>
#include <Arduino.h>
#include <RadioLib.h>
#include <RF24.h>
#include "globals.h"
#include <ELECHOUSE_CC1101_SRC_DRV.h>

// Forward declaration of event enqueue function implemented in events.ino
extern void events_enqueue_radio_bytes(int module, const uint8_t* data, size_t len, float frequency_mhz, int32_t rssi);
extern void hw_send_radio_signal_protobuf(int module, float frequency_mhz, int32_t rssi, const uint8_t* data, size_t len, const char* extra);

// Forward declarations of hardware helpers (must be available at link time)
void cc1101Read();
void loraRead();
void nrfscanner();

// --- Transceiver base class ---
class Transceiver {
public:
  virtual bool sendPacket(const std::vector<uint8_t> &payload, float freq_mhz, int32_t rssi = 0, const String &extra = "") = 0;
};

class CC1101_1Transceiver : public Transceiver {
public:
  ELECHOUSE_CC1101 *dev;
  RadioModule moduleId;
  float topFreqMHz;
  float botFreqMHz;
  ModulationType modulation;
  
  CC1101_1Transceiver(ELECHOUSE_CC1101 *d): dev(d), moduleId(CC1101_1), topFreqMHz(433.0f), botFreqMHz(400.0f), modulation(MOD_OOK) {}
  
  bool sendPacket(const std::vector<uint8_t> &payload, float freq_mhz, int32_t rssi = 0, const String &extra = "") override {
    // enqueue raw bytes into events subsystem which will batch and notify
    events_enqueue_radio_bytes((int)moduleId, payload.data(), payload.size(), freq_mhz, rssi);
    return true;
  }
  // start/stop loop mode and polling
  bool receiving = false;
  void startReceiveLoop() { receiving = true; }
  void stopReceiveLoop() { receiving = false; }
  void poll() {
    if (!receiving) return;
    // Delegate to existing cc1101Read helper (non-blocking placeholder)
    cc1101Read();
  }
  void setModulation(const String &modStr) {
    // -------- Parse string ----------
    if (modStr == "OOK" || modStr == "ASK") {
      modulation = MOD_OOK;
    } else if (modStr == "2-FSK") {
      modulation = MOD_2FSK;
    } else if (modStr == "GFSK") {
      modulation = MOD_GFSK;
    } else if (modStr == "MSK") {
      modulation = MOD_MSK;
    } else {
      modulation = MOD_UNKNOWN;
      return;
    }

    // -------- Apply to CC1101 --------
    dev->SpiStrobe(CC1101_SIDLE); // Enter IDLE state
    
    switch (modulation) {
      case MOD_OOK: // ASK/OOK
        dev->setModulation(2); 
        dev->setDRate(4.8); 
        dev->setDeviation(0.0);
        break;
      case MOD_2FSK:
        dev->setModulation(0); // 2-FSK
        dev->setDRate(4.8);
        dev->setDeviation(5.0);
        break;
      case MOD_GFSK:
        dev->setModulation(1); // GFSK
        dev->setDRate(4.8);
        dev->setDeviation(5.0);
        break;
      case MOD_MSK:
        dev->setModulation(4); // MSK
        dev->setDRate(4.8);
        dev->setDeviation(2.4);
        break;
      default:
        break;
    }
    
    dev->SetRx(); // Resume RX
  }
  void setTopFrequency(float freqMHz) {
    topFreqMHz = freqMHz;
  }
  void setBotFrequency(float freqMHz) {
    botFreqMHz = freqMHz;
  }
  void scan_range() {
    float low = botFreqMHz;
    float high = topFreqMHz;
    if (high < low) { float t = low; low = high; high = t; }

    float stepMHz = 0.20f;
    if (modulation == MOD_OOK || modulation == MOD_ASK) stepMHz = 0.10f;

    extern bool scanningRadio;

    switch (modulation) {
      case MOD_OOK: case MOD_ASK: dev->setModulation(2); break;
      case MOD_2FSK:              dev->setModulation(0); break;
      case MOD_GFSK:              dev->setModulation(1); break;
      case MOD_MSK:               dev->setModulation(4); break;
      default:                    dev->setModulation(0); break;
    }

    // ── Diagnostic header: verify SPI is alive before sweeping ──
    {
      byte pn  = dev->SpiReadStatus(0x30); // PARTNUM — should be 0x00
      byte ver = dev->SpiReadStatus(0x31); // VERSION — 0x14 genuine, varies on clones
      // Force IDLE then SRX on first step so we can read MARCSTATE
      dev->SpiStrobe(CC1101_SIDLE);
      dev->setMHZ(low);
      dev->SpiStrobe(CC1101_SRX);
      delayMicroseconds(2000);
      byte marc = dev->SpiReadStatus(0x35) & 0x1F; // MARCSTATE register
      byte rssi0 = dev->SpiReadStatus(0x34);
      int32_t rssi0_dbm = (rssi0 >= 128) ? ((int32_t)rssi0 - 256) / 2 - 74
                                          : (int32_t)rssi0 / 2 - 74;
      Serial.printf("[ScanDiag] Module=%d mod=%d PARTNUM=0x%02X VER=0x%02X MARCSTATE=0x%02X rssi_raw=0x%02X (%d dBm) f=%.2f\n",
                    (int)moduleId, (int)modulation, pn, ver, marc, rssi0, (int)rssi0_dbm, low);
      if (pn != 0x00 || ver == 0x00 || ver == 0xFF) {
        Serial.printf("[ScanDiag] *** Module=%d SPI FAIL: bad PARTNUM/VERSION — radio not responding ***\n", (int)moduleId);
      }
      if (marc != 0x0D) { // 0x0D = RX state
        Serial.printf("[ScanDiag] *** Module=%d NOT in RX after SRX strobe (MARCSTATE=0x%02X, expected 0x0D) ***\n",
                      (int)moduleId, marc);
      }
    }

    int diagStep = 0; // print raw values for first 5 steps to catch stuck RSSI
    int sweepCount = 0;

    for (float f = low; f <= high && scanningRadio; f += stepMHz) {
      dev->SpiStrobe(CC1101_SIDLE);
      dev->setMHZ(f);
      dev->SpiStrobe(CC1101_SRX);
      delayMicroseconds(4000);

      byte rssi_raw = dev->SpiReadStatus(0x34);
      int32_t rssi = (rssi_raw >= 128) ? ((int32_t)rssi_raw - 256) / 2 - 74
                                       : (int32_t)rssi_raw / 2 - 74;

      if (diagStep < 5) {
        Serial.printf("[ScanDiag] Module=%d step=%d f=%.2f rssi_raw=0x%02X rssi=%d\n",
                      (int)moduleId, diagStep, f, rssi_raw, (int)rssi);
        diagStep++;
      }
      sweepCount++;

      uint32_t freq_khz = (uint32_t)(f * 1000.0f);
      uint8_t sample[7];
      sample[0] = (uint8_t)modulation;
      sample[1] = (uint8_t)(freq_khz & 0xFF);
      sample[2] = (uint8_t)((freq_khz >> 8) & 0xFF);
      sample[3] = (uint8_t)((freq_khz >> 16) & 0xFF);
      sample[4] = (uint8_t)((freq_khz >> 24) & 0xFF);
      sample[5] = (uint8_t)(rssi & 0xFF);
      sample[6] = (uint8_t)moduleId;

      events_enqueue_radio_bytes((int)moduleId, sample, sizeof(sample), f, rssi);
      hw_send_radio_signal_protobuf((int)moduleId, f, rssi, sample, sizeof(sample), "scan_range");
    }
    Serial.printf("[ScanDiag] Module=%d sweep done %d steps\n", (int)moduleId, sweepCount);
    dev->SpiStrobe(CC1101_SIDLE);
  }
};

class CC1101_2Transceiver : public Transceiver {
public:
  ELECHOUSE_CC1101 *dev;
  RadioModule moduleId;
  float topFreqMHz;
  float botFreqMHz;
  ModulationType modulation;
  CC1101_2Transceiver(ELECHOUSE_CC1101 *d): dev(d), moduleId(CC1101_2), topFreqMHz(433.0f), botFreqMHz(400.0f), modulation(MOD_2FSK) {}
  bool sendPacket(const std::vector<uint8_t> &payload, float freq_mhz, int32_t rssi = 0, const String &extra = "") override {
    events_enqueue_radio_bytes((int)moduleId, payload.data(), payload.size(), freq_mhz, rssi);
    return true;
  }
  bool receiving = false;
  void startReceiveLoop() { receiving = true; }
  void stopReceiveLoop() { receiving = false; }
  void poll() { if (!receiving) return; cc1101Read(); }
  void setModulation(const String &modStr) {
    if (modStr == "OOK" || modStr == "ASK") {
      modulation = MOD_OOK;
    } else if (modStr == "2-FSK") {
      modulation = MOD_2FSK;
    } else if (modStr == "GFSK") {
      modulation = MOD_GFSK;
    } else if (modStr == "MSK") {
      modulation = MOD_MSK;
    } else {
      modulation = MOD_UNKNOWN;
      return;
    }

    dev->SpiStrobe(CC1101_SIDLE);
    switch (modulation) {
      case MOD_OOK:
        dev->setModulation(2);
        dev->setDRate(4.8);
        dev->setDeviation(0.0);
        break;
      case MOD_2FSK:
        dev->setModulation(0);
        dev->setDRate(4.8);
        dev->setDeviation(5.0);
        break;
      case MOD_GFSK:
        dev->setModulation(1);
        dev->setDRate(4.8);
        dev->setDeviation(5.0);
        break;
      case MOD_MSK:
        dev->setModulation(4);
        dev->setDRate(4.8);
        dev->setDeviation(2.4);
        break;
      default:
        break;
    }
    dev->SetRx();
  }
  void setTopFrequency(float freqMHz) {
    topFreqMHz = freqMHz;
  }
  void setBotFrequency(float freqMHz) {
    botFreqMHz = freqMHz;
  }
  void scan_range() {
    float low = botFreqMHz;
    float high = topFreqMHz;
    if (high < low) { float t = low; low = high; high = t; }

    float stepMHz = 0.20f;
    if (modulation == MOD_OOK || modulation == MOD_ASK) stepMHz = 0.10f;

    extern bool scanningRadio;

    switch (modulation) {
      case MOD_OOK: case MOD_ASK: dev->setModulation(2); break;
      case MOD_2FSK:              dev->setModulation(0); break;
      case MOD_GFSK:              dev->setModulation(1); break;
      case MOD_MSK:               dev->setModulation(4); break;
      default:                    dev->setModulation(0); break;
    }

    // ── Diagnostic header ──
    {
      byte pn  = dev->SpiReadStatus(0x30);
      byte ver = dev->SpiReadStatus(0x31);
      dev->SpiStrobe(CC1101_SIDLE);
      dev->setMHZ(low);
      dev->SpiStrobe(CC1101_SRX);
      delayMicroseconds(2000);
      byte marc = dev->SpiReadStatus(0x35) & 0x1F;
      byte rssi0 = dev->SpiReadStatus(0x34);
      int32_t rssi0_dbm = (rssi0 >= 128) ? ((int32_t)rssi0 - 256) / 2 - 74
                                          : (int32_t)rssi0 / 2 - 74;
      Serial.printf("[ScanDiag] Module=%d mod=%d PARTNUM=0x%02X VER=0x%02X MARCSTATE=0x%02X rssi_raw=0x%02X (%d dBm) f=%.2f\n",
                    (int)moduleId, (int)modulation, pn, ver, marc, rssi0, (int)rssi0_dbm, low);
      if (pn != 0x00 || ver == 0x00 || ver == 0xFF) {
        Serial.printf("[ScanDiag] *** Module=%d SPI FAIL: bad PARTNUM/VERSION ***\n", (int)moduleId);
      }
      if (marc != 0x0D) {
        Serial.printf("[ScanDiag] *** Module=%d NOT in RX after SRX strobe (MARCSTATE=0x%02X) ***\n",
                      (int)moduleId, marc);
      }
    }

    int diagStep = 0;
    int sweepCount = 0;

    for (float f = low; f <= high && scanningRadio; f += stepMHz) {
      dev->SpiStrobe(CC1101_SIDLE);
      dev->setMHZ(f);
      dev->SpiStrobe(CC1101_SRX);
      delayMicroseconds(4000);

      byte rssi_raw = dev->SpiReadStatus(0x34);
      int32_t rssi = (rssi_raw >= 128) ? ((int32_t)rssi_raw - 256) / 2 - 74
                                       : (int32_t)rssi_raw / 2 - 74;

      if (diagStep < 5) {
        Serial.printf("[ScanDiag] Module=%d step=%d f=%.2f rssi_raw=0x%02X rssi=%d\n",
                      (int)moduleId, diagStep, f, rssi_raw, (int)rssi);
        diagStep++;
      }
      sweepCount++;

      uint32_t freq_khz = (uint32_t)(f * 1000.0f);
      uint8_t sample[7];
      sample[0] = (uint8_t)modulation;
      sample[1] = (uint8_t)(freq_khz & 0xFF);
      sample[2] = (uint8_t)((freq_khz >> 8) & 0xFF);
      sample[3] = (uint8_t)((freq_khz >> 16) & 0xFF);
      sample[4] = (uint8_t)((freq_khz >> 24) & 0xFF);
      sample[5] = (uint8_t)(rssi & 0xFF);
      sample[6] = (uint8_t)moduleId;

      events_enqueue_radio_bytes((int)moduleId, sample, sizeof(sample), f, rssi);
      hw_send_radio_signal_protobuf((int)moduleId, f, rssi, sample, sizeof(sample), "scan_range");
    }
    Serial.printf("[ScanDiag] Module=%d sweep done %d steps\n", (int)moduleId, sweepCount);
    dev->SpiStrobe(CC1101_SIDLE);
  }
};

class LoRaTransceiver : public Transceiver {
public:
  LLCC68 *dev;
  float topFreqMHz;
  float botFreqMHz;
  LoRaTransceiver(LLCC68 *d): dev(d), topFreqMHz(933.0f), botFreqMHz(900.0f) {}
  bool sendPacket(const std::vector<uint8_t> &payload, float freq_mhz, int32_t rssi = 0, const String &extra = "") override {
    events_enqueue_radio_bytes((int)LORA, payload.data(), payload.size(), freq_mhz, rssi);
    return true;
  }
  bool receiving = false;
  void startReceiveLoop() { receiving = true; }
  void stopReceiveLoop() { receiving = false; }
  void poll() { if (!receiving) return; loraRead(); }
  void setTopFrequency(float freqMHz) { topFreqMHz = freqMHz; }
  void setBotFrequency(float freqMHz) { botFreqMHz = freqMHz; }
  void scan_range() {
    float low  = botFreqMHz;
    float high = topFreqMHz;
    if (high < low) { float t = low; low = high; high = t; }
    // LLCC68 operates 150-960 MHz; clamp
    if (low  < 150.0f) low  = 150.0f;
    if (high > 960.0f) high = 960.0f;
    const float stepMHz = 0.5f;

    extern bool scanningRadio;

    // Initialize LLCC68 at the low end of the range
    int16_t state = dev->setFrequency(low);
    if (state != RADIOLIB_ERR_NONE) {
      Serial.printf("[LoRa] setFrequency(%.1f) FAIL state=%d\n", low, state);
      return;
    }
    // Put into receive so RSSI register populates
    dev->startReceive();
    delay(2);

    for (float f = low; f <= high && scanningRadio; f += stepMHz) {
      dev->setFrequency(f);
      delayMicroseconds(800); // allow PLL to settle + RSSI to update
      float rssiF = dev->getRSSI(false); // instantaneous RSSI (not packet RSSI)
      int32_t rssi = (int32_t)rssiF;

      uint32_t freq_khz = (uint32_t)(f * 1000.0f);
      uint8_t sample[7];
      sample[0] = 0xFF; // special modulation marker for LoRa
      sample[1] = (uint8_t)(freq_khz & 0xFF);
      sample[2] = (uint8_t)((freq_khz >> 8) & 0xFF);
      sample[3] = (uint8_t)((freq_khz >> 16) & 0xFF);
      sample[4] = (uint8_t)((freq_khz >> 24) & 0xFF);
      sample[5] = (uint8_t)(rssi & 0xFF);
      sample[6] = (uint8_t)LORA;

      events_enqueue_radio_bytes((int)LORA, sample, sizeof(sample), f, rssi);
      hw_send_radio_signal_protobuf((int)LORA, f, rssi, sample, sizeof(sample), "lora_scan");
    }
    // Return to standby after sweep
    dev->standby();
  }
};

class NRF24Transceiver : public Transceiver {
public:
  RF24 *dev;
  NRF24Transceiver(RF24 *d): dev(d) {}
  bool sendPacket(const std::vector<uint8_t> &payload, float freq_mhz, int32_t rssi = 0, const String &extra = "") override {
    events_enqueue_radio_bytes((int)BLUETOOTH, payload.data(), payload.size(), freq_mhz, rssi);
    return true;
  }
  bool receiving = false;
  void startReceiveLoop() { receiving = true; }
  void stopReceiveLoop() { receiving = false; }
  void poll() { if (!receiving) return; nrfscanner(); }
};

// Extern declarations for the global instances
extern CC1101_1Transceiver *cc1101Tx;
extern CC1101_2Transceiver *cc1101Tx2;
extern LoRaTransceiver *loraTx;
extern NRF24Transceiver *nrf1Tx;
extern NRF24Transceiver *nrf2Tx;

#endif
