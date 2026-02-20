#include "globals.h"
#define GRAPH_TOP       10
#define MAX_POINTS      128
#define SPIKE_THRESHOLD 30

struct SnifferGraph {
  uint8_t graphData[MAX_POINTS];
  uint8_t currentChannel = 1;
  volatile uint16_t packetCounter = 0;
  unsigned long lastChannelSwitch = 0;
  unsigned long lastUpdate = 0;
};

SnifferGraph sniffer;

void IRAM_ATTR snifferCallback(void *buf, wifi_promiscuous_pkt_type_t type) {
  if (type == WIFI_PKT_MGMT || type == WIFI_PKT_DATA || type == WIFI_PKT_CTRL) {
    sniffer.packetCounter++;
  }
}



void initSniffer(struct SnifferGraph &g) {
  WiFi.disconnect(true, true);
  esp_wifi_stop();
  delay(200);
  esp_wifi_deinit();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  esp_wifi_init(&cfg);
  esp_wifi_set_storage(WIFI_STORAGE_RAM);
  esp_wifi_set_mode(WIFI_MODE_NULL);
  esp_wifi_start();

  esp_wifi_set_channel(g.currentChannel, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous_rx_cb(snifferCallback);
  esp_wifi_set_promiscuous(true);
}

void switchChannel(struct SnifferGraph &g) {
  g.currentChannel++;
  if (g.currentChannel > 13) g.currentChannel = 1;
  esp_wifi_set_channel(g.currentChannel, WIFI_SECOND_CHAN_NONE);
}

void updateGraphData(struct SnifferGraph &g, uint8_t value) {
  for (int i = 0; i < MAX_POINTS - 1; i++) {
    g.graphData[i] = g.graphData[i + 1];
  }
  g.graphData[MAX_POINTS - 1] = value;
}



void setupSnifferGraph() {
  //initDisplay();
  initSniffer(sniffer);
}

void updateSnifferGraph() {
  unsigned long now = millis();

  if (now - sniffer.lastChannelSwitch >= 1000) {
    sniffer.lastChannelSwitch = now;
    switchChannel(sniffer);
  }

  if (now - sniffer.lastUpdate >= 200) {
    sniffer.lastUpdate = now;

    uint16_t pktCount = sniffer.packetCounter;
    uint8_t scaled = pktCount * 2;
    uint8_t value = min(scaled, (uint8_t)GRAPH_HEIGHT);

    updateGraphData(sniffer, value);
    drawGraph(sniffer, pktCount);

    sniffer.packetCounter = 0;
  }
}
