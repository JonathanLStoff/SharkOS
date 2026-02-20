# SharkOS

<img width="160" height="160" alt="SharkOS icon" src="android/rust_ui/icons/icon.png" />

SharkOS is an Android companion app + ESP32 firmware that provides a compact, mobile‑first toolbox for scanning, logging and interacting with radio and sensor hardware. The Android application (Tauri + web UI + Rust backend) pairs with an ESP32-based device (firmware in `main/`) over Bluetooth to perform scans, stream sensors, and control device features.

This repository contains:
- `android/rust_ui/` — Android app (frontend + Rust JNI/helpers)
- `main/` — ESP32 firmware (Arduino `.ino` files)

---

## Features

Core functionality
- BLE device picker, bonding and connection flow
- BLE scanning and device listing
- Wi‑Fi network scanning and a Wi‑Fi packet sniffer (promiscuous mode)
- nRF (2.4 GHz) channel analysis and visualization
- IR receive & save remote signals
- NFC read (PN532)
- HID / BadUSB demo and HID script execution
- SD card file browser (list, rename, delete)
- Mini apps and utilities (oscilloscope stubs, games, tools)
- Persistent saved device and runtime permission handling on Android

Notes
- Some advanced or destructive features (e.g. active jamming) are intentionally disabled or restricted for safety and legal reasons.
- Several features are under active development — see project TODOs.

---

## Getting started

Prerequisites
- Node.js + npm
- Rust toolchain (stable)
- Android SDK/NDK and Java 17
- A physical Android device (ADB) for APK install
- ESP32 (ESP32‑S3 recommended) for firmware testing

Build & install Android APK

```bash
# from repository root
cd android/rust_ui
npm install          # first time only
npm run build
cd ../..
make apk-run         # builds and installs debug APK on connected device
```

Flash ESP32 firmware

- Open the `main/` firmware in the Arduino IDE / PlatformIO and upload to your ESP32 board.
- The firmware exposes BLE characteristics the Android app uses for commands and telemetry.

---

## Development

Frontend (hot reload)

```bash
cd android/rust_ui
npm run dev
```

Run/build the mobile app

```bash
# debug APK -> install via adb
make apk-run
```

Rust checks and builds live inside `android/rust_ui` (Tauri backend + JNI helpers).

---
## Tested on:
### Android devices:
- Motorola One 5G UW ace Android 12 (stock, no root)

### ESP32 hardware:
    Chip type:          ESP32-S3 (QFN56) (revision v0.2)
    Features:           Wi-Fi, BT 5 (LE), Dual Core + LP Core, 240MHz, Embedded PSRAM 8MB (AP_3v3)
    Crystal frequency:  40MHz
    USB mode:           USB-Serial/JTAG
    MAC:                1c:db:d4:ad:dd:64
## Commands & protocol

SharkOS documents BLE control commands (start/stop scanners, sensor streams, status reports) in `src/bt/commands.rs`. The Android UI sends these to the ESP32; see the firmware sources in `main/` for how the device responds.

---

## Contributing

Contributions welcome — open issues or PRs. Keep changes lawful and safe; destructive or illegal functionality will not be accepted.

---

## License

See `LICENSE` in this repository. Short version: use and modify SharkOS freely as long as your use complies with applicable law.

---

If you want a deeper developer guide (firmware flashing commands, protocol schema, or CI instructions), tell me which section to expand and I'll add it.


# Planned future sections (not yet written):
- "i2c scanner"
- "dth11"
- "gps"
- "signal generator"
- "button tester"
- "pomdoro timer"
- "oscilloscope"
    - "Analog Read",
    - "Wave Creator"
- "sd flasher tool"
- "pass generator"
- Bad usb scripts:
    - "DEMO",
    - "KEYBOARD",
    - "HID SCRIPT",
    - "Open Notepad",
    - "Open CMD",
    - "Show IP",
    - "Shutdown",
    - "RickRoll",
    - "Create Admin",
    - "Disable Defender",
    - "Open YouTube",
    - "Lock PC",
    - "Fake Update",
    - "Endless Notepad",
    - "Fake BSOD",
    - "Flip Screen",
    - "Matrix Effect",
    - "I'm Watching U",
    - "Open Google",
    - "Open telegram",
    - "Play Alarm Sound",
    - "Endless CMD",
    - "Type Gibberish",
    - "Spam CAPSLOCK",
    - "Open Calc",
    - "Auto 'Hacked!'",
    - "Turn Off Monitor",
    - "Open RegEdit",
    - "Kill Explorer",
    - "Flash Screen",
    - "Rename Desktop",
    - "Toggle WiFi",
    - "Auto Screenshot",
    - "Spam Emojis",
    - "Open Ctrl Panel",
    - "Troll Wallpaper",
    - "Open MS Paint",
    - "Tab Switcher"
- RFID/NFC tools
    - "Read tag", 
    - "Write Data", 
    - "Saved Tags", 
    - "Emulate Tag", 
    - "P2P comunication"
- "Wi‑Fi deauth" (disabled for safety)
- "CC1101 Disruption" (disabled for safety)
- "LoRa Disruption" (disabled for safety)
- BLE tools
    - "ble scanner",
    - "BLE MOUSE", 
    - "BLE KEYBOARD", 
    - "BLE SCRIPT"
    - "Scanner", 
    - "Packet Sniffer",
    - "Spoof Device",
    - "GATT Explorer"
- "UNIVERSAL REMOTE", 
- "LEARN NEW REMOTE", 
- "SAVED SIGNALS"
- "IR BLASTER",
- "IR Disruption" (disabled for safety)
- Settings
    - "show usage"
    - "format sd", 
    - "restart", 
    - "batt info", 
    - "sd info", 
    - "about",
    - "check sys devices"
- Packet Analyzers
    - "Wi‑Fi Channel Analysis",
    - "nRF (2.4 GHz) Channel Analysis"
