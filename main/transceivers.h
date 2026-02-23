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
    // Fast low->high sweep using the currently selected modulation.
    // Runs with no artificial delay to scan as fast as the radio can tune.
    float low = botFreqMHz;
    float high = topFreqMHz;
    if (high < low) {
      float t = low;
      low = high;
      high = t;
    }

    // Step size tuned by modulation (MHz)
    float stepMHz = 0.20f;
    if (modulation == MOD_OOK || modulation == MOD_ASK) {
      stepMHz = 0.10f;
    } else if (modulation == MOD_2FSK || modulation == MOD_GFSK || modulation == MOD_MSK) {
      stepMHz = 0.20f;
    }

    extern bool scanningRadio; // from hardware-utils or events

    // Sync CC1101's OOK flag for simple modulations
    if (modulation == MOD_OOK || modulation == MOD_ASK) {
      dev->setModulation(2);
    } 

    // Put radio into receive mode once before sweep
    dev->SetRx();
    delay(2); // initial RX settle

    for (float f = low; f <= high && scanningRadio; f += stepMHz) {
      dev->setMHZ(f);
      // Re-enter RX after frequency change for RSSI to update
      dev->SetRx();
      delay(1); // 1ms settle for RSSI register to update
      int32_t rssi = (int32_t)dev->getRssi();

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
    if (high < low) {
      float t = low;
      low = high;
      high = t;
    }

    float stepMHz = 0.20f;
    if (modulation == MOD_OOK || modulation == MOD_ASK) {
      stepMHz = 0.10f;
    } else if (modulation == MOD_2FSK || modulation == MOD_GFSK || modulation == MOD_MSK) {
      stepMHz = 0.20f;
    }

    extern bool scanningRadio; // from hardware-utils or events

    if (modulation == MOD_OOK || modulation == MOD_ASK) {
      dev->setModulation(2);
    } 

    dev->SetRx();
    delay(2); 

    for (float f = low; f <= high && scanningRadio; f += stepMHz) {
      dev->setMHZ(f);
      dev->SetRx();
      delay(1); 
      int32_t rssi = (int32_t)dev->getRssi();

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
  }
};

class LoRaTransceiver : public Transceiver {
public:
  SX1276 *dev;
  LoRaTransceiver(SX1276 *d): dev(d) {}
  bool sendPacket(const std::vector<uint8_t> &payload, float freq_mhz, int32_t rssi = 0, const String &extra = "") override {
    events_enqueue_radio_bytes((int)LORA, payload.data(), payload.size(), freq_mhz, rssi);
    return true;
  }
  bool receiving = false;
  void startReceiveLoop() { receiving = true; }
  void stopReceiveLoop() { receiving = false; }
  void poll() { if (!receiving) return; loraRead(); }
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
