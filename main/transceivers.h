#ifndef TRANSCEIVERS_H
#define TRANSCEIVERS_H

#include <vector>
#include <Arduino.h>
#include <RadioLib.h>
#include <RF24.h>
#include "globals.h"

// Forward declaration of event enqueue function implemented in events.ino
extern void events_enqueue_radio_bytes(int module, const uint8_t* data, size_t len, float frequency_mhz, int32_t rssi);

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
  CC1101 *dev;
  RadioModule moduleId;
  float topFreqMHz;
  float botFreqMHz;
  ModulationType modulation;
  CC1101_1Transceiver(CC1101 *d): dev(d), moduleId(CC1101_1), topFreqMHz(0.0f), botFreqMHz(0.0f), modulation(MOD_OOK) {}
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
    if (modStr == "OOK") {
      modulation = MOD_OOK;
    } else if (modStr == "ASK") {
      modulation = MOD_ASK;
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
    dev->standby();  // important before reconfig

    switch (modulation) {
      case MOD_OOK:
      case MOD_ASK:
        dev->setOOK(true);
        dev->setBitRate(4.8);
        dev->setFrequencyDeviation(0.0);
        break;
      case MOD_2FSK:
        dev->setOOK(false);
        dev->setBitRate(4.8);
        dev->setFrequencyDeviation(5.0);
        dev->setDataShaping(RADIOLIB_SHAPING_NONE);
        break;
      case MOD_GFSK:
        dev->setOOK(false);
        dev->setBitRate(4.8);
        dev->setFrequencyDeviation(5.0);
        dev->setDataShaping(RADIOLIB_SHAPING_0_5);
        break;
      case MOD_MSK:
        dev->setOOK(false);
        dev->setBitRate(4.8);
        dev->setFrequencyDeviation(2.4);
        dev->setDataShaping(RADIOLIB_SHAPING_NONE);
        break;
      default:
        break;
    }

    dev->startReceive();  // resume RX
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
      dev->setOOK(true);
    } else {
      dev->setOOK(false);
    }

    for (float f = low; f <= high && scanningRadio; f += stepMHz) {
      dev->setFrequency(f);
      int32_t rssi = (int32_t)dev->getRSSI();
      events_enqueue_radio_bytes((int)moduleId, nullptr, 0, f, rssi);
    }
  }
};

class CC1101_2Transceiver : public Transceiver {
public:
  CC1101 *dev;
  RadioModule moduleId;
  float topFreqMHz;
  float botFreqMHz;
  ModulationType modulation;
  CC1101_2Transceiver(CC1101 *d): dev(d), moduleId(CC1101_2), topFreqMHz(928.0f), botFreqMHz(300.0f), modulation(MOD_UNKNOWN) {}
  bool sendPacket(const std::vector<uint8_t> &payload, float freq_mhz, int32_t rssi = 0, const String &extra = "") override {
    events_enqueue_radio_bytes((int)moduleId, payload.data(), payload.size(), freq_mhz, rssi);
    return true;
  }
  bool receiving = false;
  void startReceiveLoop() { receiving = true; }
  void stopReceiveLoop() { receiving = false; }
  void poll() { if (!receiving) return; cc1101Read(); }
  void setModulation(const String &modStr) {
    // interpret string and update stored value
    if (modStr == "OOK") {
      modulation = MOD_OOK;
    } else if (modStr == "ASK") {
      modulation = MOD_ASK;
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

    // reconfigure hardware - must be in standby while changing parameters
    dev->standby();
    switch (modulation) {
      case MOD_OOK:
      case MOD_ASK:
        dev->setOOK(true);
        dev->setBitRate(4.8);
        dev->setFrequencyDeviation(0.0);
        break;
      case MOD_2FSK:
        dev->setOOK(false);
        dev->setBitRate(4.8);
        dev->setFrequencyDeviation(5.0);
        dev->setDataShaping(RADIOLIB_SHAPING_NONE);
        break;
      case MOD_GFSK:
        dev->setOOK(false);
        dev->setBitRate(4.8);
        dev->setFrequencyDeviation(5.0);
        dev->setDataShaping(RADIOLIB_SHAPING_0_5);
        break;
      case MOD_MSK:
        dev->setOOK(false);
        dev->setBitRate(4.8);
        dev->setFrequencyDeviation(2.4);
        dev->setDataShaping(RADIOLIB_SHAPING_NONE);
        break;
      default:
        break;
    }
    dev->startReceive();
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

    // Sync CC1101's OOK flag
    if (modulation == MOD_OOK || modulation == MOD_ASK) {
      dev->setOOK(true);
    } else {
      dev->setOOK(false);
    }

    for (float f = low; f <= high && scanningRadio; f += stepMHz) {
      dev->setFrequency(f);
      int32_t rssi = (int32_t)dev->getRSSI();
      events_enqueue_radio_bytes((int)moduleId, nullptr, 0, f, rssi);
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
