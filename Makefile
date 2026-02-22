# Makefile for SharkOS (Consolidated)
ARDUINO_CLI ?= arduino-cli
FQBN ?= esp32:esp32:esp32s3:PartitionScheme=huge_app
ANDROID_NDK_HOME ?= /opt/homebrew/share/android-ndk
JAVA_17 ?= /Library/Java/JavaVirtualMachines/temurin-17.jdk/Contents/Home

.PHONY: help android-run android-apk-test apk-run flash flash_v2 flash-serial

help:
	@echo "Usage: make <target>"
	@echo "  apk-run        - build, deploy, and launch Android app"
	@echo "  android-run    - install, run and log Android app"

apk-run:
	@echo "Building APK, then deploying and launching on connected adb device..."
	cd android/rust_ui && \
	export JAVA_HOME="$(JAVA_17)" && export PATH="$$JAVA_HOME/bin:$$PATH" && cargo tauri android build --debug && \
	APK_UNSIGNED="gen/android/app/build/outputs/apk/universal/release/app-universal-release-unsigned.apk"; \
	APK_DEBUG_STD="gen/android/app/build/outputs/apk/debug/app-debug.apk"; \
	APK_DEBUG="gen/android/app/build/outputs/apk/universal/debug/app-universal-debug.apk"; \
	APK_TO_INSTALL=$$(if [ -f $$APK_DEBUG ]; then echo $$APK_DEBUG; elif [ -f $$APK_DEBUG_STD ]; then echo $$APK_DEBUG_STD; elif [ -f $$APK_UNSIGNED ]; then echo $$APK_UNSIGNED; else echo ""; fi); \
	if [ -z "$$APK_TO_INSTALL" ]; then echo "No APK found to install."; exit 1; fi; \
	adb wait-for-device && adb get-state | grep -q device && \
	echo "Installing $$APK_TO_INSTALL" && adb install -r "$$APK_TO_INSTALL" && \
	adb shell am force-stop com.sharkos || true; \
	adb shell am start -n com.sharkos/.MainActivity
cargo-build:
	cd android/rust_ui && export JAVA_HOME="$(JAVA_17)" && export PATH="$$JAVA_HOME/bin:$$PATH" && cargo tauri android build --debug

# Install Arduino core & libraries required to build the firmware
# Usage: `make deps`
.PHONY: deps install-libs
deps: install-libs

install-libs:
	@echo "Ensuring ESP32 core + required libraries are installed (using $(ARDUINO_CLI))..."
	@$(ARDUINO_CLI) core update-index || true
	@$(ARDUINO_CLI) core install esp32:esp32 || true
	@$(ARDUINO_CLI) lib install "ArduinoJson" || true
	@$(ARDUINO_CLI) lib install "RF24" || true
	@$(ARDUINO_CLI) lib install "Adafruit NeoPixel" || true
	@$(ARDUINO_CLI) lib install "Adafruit PN532" || true
	@$(ARDUINO_CLI) lib install "RadioLib" || true
	@$(ARDUINO_CLI) lib install "IRremote" || true
	@$(ARDUINO_CLI) lib install "BleMouse" || true
	@$(ARDUINO_CLI) lib install "HID-Project" || true
	@# ESP32 BLE Arduino is NOT installed — use the core-bundled BLE library instead
	@$(ARDUINO_CLI) lib uninstall "ESP32 BLE Arduino" 2>/dev/null || true
	@$(ARDUINO_CLI) lib install "SD" || true
	@echo "Library installation finished. Re-run 'make flash' to compile/upload."


# New: flash_v2 — recommended settings for YD-ESP32-S3 (YD modules: N16R8 / N8R2)
# Usage examples:
#   make flash_v2            -> defaults to 16MB flash (N16R8)
#   FLASHSIZE=8MB make flash_v2   -> use 8MB flash (N8R2)
#   PORT=/dev/cu.SLAB_USBtoUART flash_v2  -> override detected port
flash:
	@echo "Compiling + uploading using recommended ESP32-S3 FQBN (CDCOnBoot=cdc,PSRAM=opi)..."
	# Allow override of exact FQBN or FLASHSIZE via environment variables
	@FQBN_V2=$${FQBN_V2:-esp32:esp32:esp32s3:CDCOnBoot=cdc,UploadSpeed=921600,FlashMode=qio,FlashSize=$${FLASHSIZE:-16M},PartitionScheme=app3M_fat9M_16MB,PSRAM=opi}; \
	PORT=$${PORT:-$$(ls /dev/cu.* /dev/tty.* 2>/dev/null | egrep -i 'usbmodem' | head -n1)}; \
	if [ -z "$$PORT" ]; then \
		PORT=$$(ls /dev/cu.* /dev/tty.* 2>/dev/null | egrep -i 'usbserial|ttyUSB|ttyACM|CP210|FTDI|wch' | grep -vi Bluetooth | head -n1); \
	fi; \
	if [ -z "$$PORT" ]; then echo "No serial port detected. Set PORT=/dev/ttyUSB0 (or PORT=/dev/cu.SLAB_USBtoUART) and connect board."; exit 1; fi; \
	echo "Using port: $$PORT"; \
	echo "Using FQBN: $$FQBN_V2"; \
	$(ARDUINO_CLI) compile --fqbn "$$FQBN_V2" main && \
	$(ARDUINO_CLI) upload -p "$$PORT" --fqbn "$$FQBN_V2" main

# Android Targets
# Using Tauri for UI, so standard cargo-apk/gradle targets are replaced by tauri-mobile

# Serve built frontend `dist` via Vite preview
.PHONY: serve-dist
serve-dist:
	@echo "Serving android/rust_ui/dist via Vite preview on port 5173..."
	@if [ -d android/rust_ui/dist ]; then \
		cd android/rust_ui && npx --yes vite preview --port 5173 --host 0.0.0.0; \
	else \
		echo "No dist directory found at android/rust_ui/dist. Build first (run make android-build-apk or build frontend)."; exit 1; \
	fi

# Follow device logcat filtered for the app package
.PHONY: apk-logs
apk-logs:
	@echo "Following device logcat for package com.sharkos..."
	adb logcat | grep -E 'tauri|sharkos_lib'

# Generate a compilation database for clangd/IntelliSense
#
# Running this target will invoke Arduino CLI to export all of the
# compiler flags used for the current sketch.  clangd and many IDEs
# will read the resulting compile_commands.json in the project root
# and provide accurate include paths, macros, etc.
#
# Example:
#   make compile_commands              # creates ./compile_commands.json
# or if using a custom board/FQBN:
#   make compile_commands FQBN=esp32:esp32:esp32dev
#
.PHONY: compile_commands
compile_commands:
	@echo "Exporting compile_commands.json via $(ARDUINO_CLI)"
	@$(ARDUINO_CLI) compile --fqbn "$(FQBN)" main --export-compile-commands

# Formatting / lint targets
.PHONY: format lint
format:
	@echo "Formatting source files (HTML, TypeScript, Rust, C/INO)"
	# JavaScript/TypeScript/HTML via Prettier
	if command -v npx >/dev/null 2>&1; then \
		npx prettier --write "**/*.{html,ts,tsx,js,css,json,md}"; \
	else \
		echo "npx not available, install Node.js to format web files"; \
	fi
	# Rust formatting
	if command -v cargo >/dev/null 2>&1; then \
		cargo fmt --all || true; \
	else \
		echo "cargo not available, skipping Rust format"; \
	fi
	# C/INO formatting using clang-format if installed
	if command -v clang-format >/dev/null 2>&1; then \
		clang-format -i $(find . -name "*.ino" -o -name "*.cpp" -o -name "*.h" 2>/dev/null); \
	else \
		echo "clang-format not installed, skipping C/C++ format"; \
	fi

lint:
	@echo "Linting source files (TypeScript via ESLint, Rust via Clippy)"
	if command -v npx >/dev/null 2>&1; then \
		npx eslint "**/*.{ts,tsx}" --max-warnings=0 || true; \
	else \
		echo "npx not available, skipping JS/TS lint"; \
	fi
	if command -v cargo >/dev/null 2>&1; then \
		cargo clippy --workspace -- -D warnings || true; \
	else \
		echo "cargo not available, skipping Rust lint"; \
	fi
