#pragma once

// Bluetooth command keys used by SharkOS firmware.
// These string constants mirror the command keys exposed by the Android app
// (see `android/rust_ui/src/bt/commands.rs`). Use these when comparing
// incoming BLE command identifiers so the firmware and app stay in sync.

#ifndef COMMANDS_H
#define COMMANDS_H

// BLE scanner
static const char CMD_BLE_SCAN_START[] = "ble.scan.start";
static const char CMD_BLE_SCAN_STOP[]  = "ble.scan.stop";

// Wi‑Fi scan / sniffer
static const char CMD_WIFI_SCAN_START[]    = "wifi.scan.start";
static const char CMD_WIFI_SCAN_STOP[]     = "wifi.scan.stop";
// visual-only command used by the app; firmware handles it identically to
// a normal Wi‑Fi scan request.
static const char CMD_WIFI_CHANNEL_SCAN[]  = "wifi.channel.scan";
static const char CMD_WIFI_SNIFFER_START[] = "wifi.sniffer.start";
static const char CMD_WIFI_SNIFFER_STOP[]  = "wifi.sniffer.stop";

// nRF (2.4GHz)
static const char CMD_NRF_SCAN_START[] = "nrf.scan.start";
static const char CMD_NRF_SCAN_STOP[]  = "nrf.scan.stop";

// Sub‑GHz (CC1101/LoRa)
static const char CMD_SUBGHZ_READ_START[] = "subghz.read.start";
static const char CMD_SUBGHZ_READ_STOP[]  = "subghz.read.stop";
static const char CMD_SUBGHZ_RECORD_START[] = "subghz.record.start";
static const char CMD_SUBGHZ_RECORD_STOP[]  = "subghz.record.stop";
static const char CMD_SUBGHZ_PLAYBACK_START[] = "subghz.playback.start";
static const char CMD_SUBGHZ_PLAYBACK_STOP[]  = "subghz.playback.stop";
static const char CMD_SUBGHZ_PACKET_SEND[] = "subghz.packet.send";
static const char CMD_SUBGHZ_DISRUPTOR_START[] = "subghz.disruptor.start";
static const char CMD_SUBGHZ_DISRUPTOR_STOP[]  = "subghz.disruptor.stop";

// Oscilloscope / ADC
static const char CMD_OSCILLOSCOPE_START[] = "oscilloscope.start";
static const char CMD_OSCILLOSCOPE_STOP[]  = "oscilloscope.stop";

// I2C scanner
static const char CMD_I2C_SCAN_ONCE[]  = "i2c.scan.once";
static const char CMD_I2C_SCAN_START[] = "i2c.scan.start";
static const char CMD_I2C_SCAN_STOP[]  = "i2c.scan.stop";

// NFC polling
static const char CMD_NFC_POLL_START[] = "nfc.poll.start";
static const char CMD_NFC_POLL_STOP[]  = "nfc.poll.stop";

// Sensor streaming (accelerometer / gyro)
static const char CMD_SENSOR_STREAM_START[] = "sensor.stream.start";
static const char CMD_SENSOR_STREAM_STOP[]  = "sensor.stream.stop";

// Cellular / cell-scan (generic)
static const char CMD_CELL_SCAN_START[] = "cell.scan.start";
static const char CMD_CELL_SCAN_STOP[]  = "cell.scan.stop";

// SD / Files
static const char CMD_SD_INFO[]   = "sd.info";
static const char CMD_FILES_LIST[] = "files.list"; // optional param: path

// IR receive
static const char CMD_IR_RECV_START[] = "ir.recv.start";
static const char CMD_IR_RECV_STOP[]  = "ir.recv.stop";

// Convenience / control
static const char CMD_LIST_PAIRED_DEVICES[] = "list.paired.devices";
static const char CMD_PAIR_SET[]             = "pair.set"; // params: { pin: string|int }
static const char CMD_BATTERY_INFO[]         = "battery.info";
static const char CMD_STATUS_REPORT_START[]  = "status.reporting.start";
static const char CMD_STATUS_REPORT_STOP[]   = "status.reporting.stop";

// Array of all command strings (useful for registration / validation)
static const char* const SHARKOS_BT_COMMANDS[] = {
    CMD_BLE_SCAN_START,
    CMD_BLE_SCAN_STOP,
    CMD_WIFI_SCAN_START,
    CMD_WIFI_SCAN_STOP,
    CMD_WIFI_CHANNEL_SCAN,
    CMD_WIFI_SNIFFER_START,
    CMD_WIFI_SNIFFER_STOP,
    CMD_NRF_SCAN_START,
    CMD_NRF_SCAN_STOP,
    CMD_SUBGHZ_READ_START,
    CMD_SUBGHZ_READ_STOP,
    CMD_SUBGHZ_RECORD_START,
    CMD_SUBGHZ_RECORD_STOP,
    CMD_SUBGHZ_PLAYBACK_START,
    CMD_SUBGHZ_PLAYBACK_STOP,
    CMD_SUBGHZ_PACKET_SEND,
    CMD_SUBGHZ_DISRUPTOR_START,
    CMD_SUBGHZ_DISRUPTOR_STOP,
    CMD_OSCILLOSCOPE_START,
    CMD_OSCILLOSCOPE_STOP,
    CMD_I2C_SCAN_ONCE,
    CMD_I2C_SCAN_START,
    CMD_I2C_SCAN_STOP,
    CMD_NFC_POLL_START,
    CMD_NFC_POLL_STOP,
    CMD_SENSOR_STREAM_START,
    CMD_SENSOR_STREAM_STOP,
    CMD_CELL_SCAN_START,
    CMD_CELL_SCAN_STOP,
    CMD_SD_INFO,
    CMD_FILES_LIST,
    CMD_IR_RECV_START,
    CMD_IR_RECV_STOP,
    CMD_LIST_PAIRED_DEVICES,
    CMD_PAIR_SET,
    CMD_BATTERY_INFO,
    CMD_STATUS_REPORT_START,
    CMD_STATUS_REPORT_STOP
};

static const unsigned int SHARKOS_BT_COMMAND_COUNT = sizeof(SHARKOS_BT_COMMANDS) / sizeof(SHARKOS_BT_COMMANDS[0]);

#endif // COMMANDS_H
