# Makefile for SharkOS (Consolidated)
ARDUINO_CLI ?= arduino-cli
FQBN        ?= esp32:esp32:esp32s3:PartitionScheme=huge_app
# NDK version used when auto-installing via sdkmanager.  Override to pin a different release.
NDK_VERSION ?= 27.2.12479018
ANDROID_API ?= 35

# ---------------------------------------------------------------------------
# Java 17 auto-detection (runs at parse time, not per recipe).
# Override at any time:  JAVA_17=/path/to/jdk17 make apk-run
# ---------------------------------------------------------------------------
UNAME_S := $(shell uname -s 2>/dev/null)

ifneq ($(filter MINGW% MSYS% CYGWIN%,$(UNAME_S)),)
  # Windows (Git Bash / MSYS2) — scan common JDK 17 install directories.
  # Chocolatey/winget Temurin lands in "Eclipse Adoptium"; Microsoft JDK in "Microsoft".
  JAVA_17 ?= $(shell \
    for base in \
        "/c/Program Files/Eclipse Adoptium" \
        "/c/Program Files/Microsoft" \
        "/c/Program Files/Java" \
        "/c/Program Files/Amazon Corretto" \
        "/c/Program Files/BellSoft"; do \
      found=$$(ls -d "$$base"/jdk-17* 2>/dev/null | sort -rV | head -1); \
      [ -n "$$found" ] && echo "$$found" && exit 0; \
    done)
else ifeq ($(UNAME_S),Darwin)
  # macOS — use the java_home helper.
  JAVA_17 ?= $(shell /usr/libexec/java_home -v 17 2>/dev/null)
else
  # Linux / WSL — fall back to whatever JAVA_HOME is already set to.
  JAVA_17 ?= $(JAVA_HOME)
endif

.PHONY: help android-run android-apk-test apk-run flash flash_v2 flash-serial install check-java

help:
	@echo "Usage: make <target>"
	@echo "  install        - install all dev prerequisites (Rust targets, Tauri CLI, npm deps)"
	@echo "  apk-run        - build, deploy, and launch Android app"
	@echo "  android-run    - install, run and log Android app"
	@echo "  deps           - install Arduino core + libraries for ESP32 firmware"

# ---------------------------------------------------------------------------
# install — set up everything needed to build SharkOS from scratch.
#
# Run once after cloning.  Works on macOS and Windows (Git Bash / MSYS2).
# Automatically installs Node.js via Homebrew (macOS) or Chocolatey (Windows)
# if npm is not found, then adds Android Rust targets and the Tauri CLI.
# ---------------------------------------------------------------------------
install:
	@echo ""
	@echo "========================================="
	@echo " SharkOS — install dev prerequisites"
	@echo "========================================="
	@echo ""
	@echo "[1/4] Checking for Node.js / npm..."
	@NPM_VER=$$(npm --version 2>/dev/null); \
	if [ -z "$$NPM_VER" ]; then NPM_VER=$$(npm.cmd --version 2>/dev/null); fi; \
	if [ -n "$$NPM_VER" ]; then \
		echo "  npm found: $$NPM_VER"; \
	else \
		echo "  npm not found — attempting automatic install..."; \
		UNAME=$$(uname -s 2>/dev/null || echo Windows); \
		if echo "$$UNAME" | grep -qiE 'MINGW|MSYS|CYGWIN'; then \
			if choco --version >/dev/null 2>&1; then \
				echo "  Installing Node.js via Chocolatey..."; \
				choco install nodejs-lts -y || true; \
				export PATH="/c/Program Files/nodejs:$$HOME/AppData/Roaming/npm:$$PATH"; \
			else \
				echo ""; \
				echo "  ERROR: npm not found and Chocolatey is not installed."; \
				echo "  Install Chocolatey first (run in an admin PowerShell):"; \
				echo "    Set-ExecutionPolicy Bypass -Scope Process -Force"; \
				echo "    [System.Net.ServicePointManager]::SecurityProtocol = 3072"; \
				echo "    iex ((New-Object System.Net.WebClient).DownloadString('https://community.chocolatey.org/install.ps1'))"; \
				echo "  Then re-open Git Bash and run: make install"; \
				echo "  Or install Node.js manually from: https://nodejs.org"; \
				echo ""; \
				exit 1; \
			fi; \
		elif [ "$$UNAME" = "Darwin" ]; then \
			if brew --version >/dev/null 2>&1; then \
				echo "  Installing Node.js via Homebrew..."; \
				brew install node; \
				export PATH="/opt/homebrew/bin:/usr/local/bin:$$PATH"; \
			else \
				echo ""; \
				echo "  ERROR: npm not found and Homebrew is not installed."; \
				echo "  Install Homebrew first:"; \
				echo "    /bin/bash -c \"\$$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)\""; \
				echo "  Then re-run: make install"; \
				echo "  Or install Node.js manually from: https://nodejs.org"; \
				echo ""; \
				exit 1; \
			fi; \
		else \
			echo "  ERROR: npm not found. Install Node.js from https://nodejs.org"; \
			exit 1; \
		fi; \
		NPM_VER=$$(npm --version 2>/dev/null); \
		if [ -z "$$NPM_VER" ]; then NPM_VER=$$(npm.cmd --version 2>/dev/null); fi; \
		if [ -z "$$NPM_VER" ]; then \
			echo "  Node.js was installed but npm is not yet in PATH."; \
			echo "  Close and re-open your terminal, then re-run: make install"; \
			exit 1; \
		fi; \
		echo "  npm now available: $$NPM_VER"; \
	fi
	@echo ""
	@echo "[2/4] Checking for rustup / Rust..."
	@if rustup --version >/dev/null 2>&1; then \
		echo "  rustup found: $$(rustup --version)"; \
	else \
		echo "  rustup not found — installing via rustup.rs..."; \
		curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y; \
		export PATH="$$HOME/.cargo/bin:$$PATH"; \
	fi
	@echo ""
	@echo "[3/4] Adding Android Rust cross-compilation targets..."
	rustup target add \
		aarch64-linux-android \
		armv7-linux-androideabi \
		x86_64-linux-android \
		i686-linux-android
	@echo ""
	@echo "[4/4] Installing Tauri CLI v2..."
	@# Never run bare 'cargo install' from this repo root — it has no crate here.
	@# The Rust crate lives in android/rust_ui; this installs the CLI globally.
	cargo install tauri-cli --version "^2" --locked
	@echo ""
	@echo "[5/5] Installing Android SDK components (NDK, build-tools, platform)..."
	@if [ -z "$$ANDROID_HOME" ]; then \
		echo "  ANDROID_HOME not set — skipping SDK component install."; \
		echo "  Set ANDROID_HOME and re-run 'make install' to auto-install the NDK."; \
	else \
		if command -v cygpath >/dev/null 2>&1; then \
			AH=$$(cygpath -u "$$ANDROID_HOME"); \
		else \
			AH="$$ANDROID_HOME"; \
		fi; \
		SDKMGR=""; \
		for candidate in \
				"$$AH/cmdline-tools/latest/bin/sdkmanager" \
				"$$AH/cmdline-tools/bin/sdkmanager" \
				$$(ls -d "$$AH/cmdline-tools/"*/bin/sdkmanager 2>/dev/null | sort -rV | head -1); do \
			[ -f "$$candidate" ] && SDKMGR="$$candidate" && break; \
		done; \
		if [ -z "$$SDKMGR" ]; then \
			echo "  sdkmanager not found under $$AH/cmdline-tools/."; \
			echo "  Download Android command-line tools from https://developer.android.com/studio#command-tools"; \
			echo "  Unzip to $$AH/cmdline-tools/latest/ then re-run 'make install'."; \
		else \
			echo "  Using sdkmanager: $$SDKMGR"; \
			echo "  Installing: ndk;$(NDK_VERSION)  build-tools;$(ANDROID_API).0.0  platforms;android-$(ANDROID_API)  platform-tools"; \
			"$$SDKMGR" --licenses <<< $$(yes 2>/dev/null | head -20) >/dev/null 2>&1 || true; \
			"$$SDKMGR" \
				"ndk;$(NDK_VERSION)" \
				"build-tools;$(ANDROID_API).0.0" \
				"platforms;android-$(ANDROID_API)" \
				"platform-tools"; \
			echo "  NDK installed at: $$AH/ndk/$(NDK_VERSION)"; \
		fi; \
	fi
	@echo ""
	@echo "[6/6] Installing npm dependencies (android/rust_ui)..."
	cd android/rust_ui && (npm install 2>/dev/null || npm.cmd install)
	@echo ""
	@echo "========================================="
	@echo " Done!"
	@echo " Make sure these env vars are set before building:"
	@echo ""
	@echo " macOS (add to ~/.zshrc):"
	@echo "   export ANDROID_HOME=\$$HOME/Library/Android/sdk"
	@echo "   export JAVA_HOME=\$$(/usr/libexec/java_home -v 17)"
	@echo ""
	@echo " Windows (System > Advanced > Environment Variables):"
	@echo "   ANDROID_HOME = C:\\Users\\<you>\\AppData\\Local\\Android\\Sdk"
	@echo "   JAVA_HOME    = C:\\Program Files\\Java\\jdk-17  (or Eclipse Adoptium path)"
	@echo "  (NDK_HOME is no longer required — found automatically)"
	@echo ""
	@echo " Then run:  make apk-run"
	@echo "========================================="
	@echo ""

# Validates that JAVA_17 was found before any target that needs it.
check-java:
	@if [ -z "$(JAVA_17)" ] || [ ! -d "$(JAVA_17)" ]; then \
		echo ""; \
		echo "ERROR: Java 17 JDK not found (resolved to: '$(JAVA_17)')."; \
		echo "  Install Temurin 17, then re-run.  Or set the path explicitly:"; \
		echo "    JAVA_17=\"/c/Program Files/Eclipse Adoptium/jdk-17.x.x\" make apk-run"; \
		echo ""; \
		echo "  macOS:   brew install --cask temurin@17"; \
		echo "  Windows: winget install EclipseAdoptium.Temurin.17.JDK"; \
		echo "           (or: choco install temurin17 -y  in an admin shell)"; \
		echo ""; \
		exit 1; \
	fi
	@echo "Java 17: $(JAVA_17)"

apk-run: check-java
	@echo "Building APK, then deploying and launching on connected adb device..."
	@set -e; \
	JAVA_P="$(JAVA_17)"; \
	if command -v cygpath >/dev/null 2>&1; then \
		AH=$$(cygpath -u "$$ANDROID_HOME" 2>/dev/null || echo "$$ANDROID_HOME"); \
		NH=$$([ -n "$$NDK_HOME"          ] && cygpath -u "$$NDK_HOME"          2>/dev/null || echo ""); \
		[ -z "$$NH" ] && NH=$$([ -n "$$ANDROID_NDK_HOME" ] && cygpath -u "$$ANDROID_NDK_HOME" 2>/dev/null || echo ""); \
		[ -z "$$NH" ] && NH=$$([ -n "$$ANDROID_NDK_ROOT" ] && cygpath -u "$$ANDROID_NDK_ROOT" 2>/dev/null || echo ""); \
	else \
		AH="$$ANDROID_HOME"; \
		NH="$${NDK_HOME:-$${ANDROID_NDK_HOME:-$$ANDROID_NDK_ROOT}}"; \
	fi; \
	if [ -z "$$AH" ] || [ ! -d "$$AH" ]; then \
		echo "ERROR: ANDROID_HOME not set or directory not found ('$$AH')."; \
		echo "  Set ANDROID_HOME to your Android SDK path and re-run."; \
		exit 1; \
	fi; \
	if [ -z "$$NH" ] || [ ! -d "$$NH" ]; then \
		NH=$$(ls -d "$$AH/ndk/"* 2>/dev/null | sort -rV | head -1); \
	fi; \
	if [ -z "$$NH" ] || [ ! -d "$$NH" ]; then \
		[ -d "$$AH/ndk-bundle" ] && NH="$$AH/ndk-bundle"; \
	fi; \
	if [ -z "$$NH" ] || [ ! -d "$$NH" ]; then \
		echo ""; \
		echo "ERROR: Android NDK not found. Searched:"; \
		echo "  $$AH/ndk/*           (SDK Manager layout)"; \
		echo "  $$AH/ndk-bundle      (legacy layout)"; \
		echo "  NDK_HOME / ANDROID_NDK_HOME / ANDROID_NDK_ROOT env vars"; \
		echo ""; \
		echo "SDK contents ($$AH):"; \
		ls "$$AH" 2>/dev/null | sed 's/^/  /' || echo "  (directory unreadable)"; \
		echo ""; \
		echo "Run  'make install'  to auto-install the NDK via sdkmanager ($(NDK_VERSION))."; \
		echo "Or install manually: Android Studio > SDK Manager > SDK Tools > NDK (Side by side)"; \
		exit 1; \
	fi; \
	echo "  JAVA_HOME:    $$JAVA_P"; \
	echo "  ANDROID_HOME: $$AH"; \
	echo "  NDK_HOME:     $$NH"; \
	export JAVA_HOME="$$JAVA_P" ANDROID_HOME="$$AH" NDK_HOME="$$NH"; \
	export PATH="$$JAVA_HOME/bin:$$PATH"; \
	cd android/rust_ui && \
	cargo tauri android build --debug && \
	APK_UNSIGNED="gen/android/app/build/outputs/apk/universal/release/app-universal-release-unsigned.apk"; \
	APK_DEBUG_STD="gen/android/app/build/outputs/apk/debug/app-debug.apk"; \
	APK_DEBUG="gen/android/app/build/outputs/apk/universal/debug/app-universal-debug.apk"; \
	APK_TO_INSTALL=$$(if [ -f $$APK_DEBUG ]; then echo $$APK_DEBUG; elif [ -f $$APK_DEBUG_STD ]; then echo $$APK_DEBUG_STD; elif [ -f $$APK_UNSIGNED ]; then echo $$APK_UNSIGNED; else echo ""; fi); \
	if [ -z "$$APK_TO_INSTALL" ]; then echo "No APK found to install."; exit 1; fi; \
	adb wait-for-device && adb get-state | grep -q device && \
	echo "Installing $$APK_TO_INSTALL" && adb install -r "$$APK_TO_INSTALL" && \
	adb shell am force-stop com.sharkos || true; \
	adb shell am start -n com.sharkos/.MainActivity
apk-run-clean: check-java
	@echo "Uninstalling existing com.sharkos, then building and deploying fresh..."
	@set -e; \
	JAVA_P="$(JAVA_17)"; \
	if command -v cygpath >/dev/null 2>&1; then \
		AH=$$(cygpath -u "$$ANDROID_HOME" 2>/dev/null || echo "$$ANDROID_HOME"); \
		NH=$$([ -n "$$NDK_HOME"          ] && cygpath -u "$$NDK_HOME"          2>/dev/null || echo ""); \
		[ -z "$$NH" ] && NH=$$([ -n "$$ANDROID_NDK_HOME" ] && cygpath -u "$$ANDROID_NDK_HOME" 2>/dev/null || echo ""); \
		[ -z "$$NH" ] && NH=$$([ -n "$$ANDROID_NDK_ROOT" ] && cygpath -u "$$ANDROID_NDK_ROOT" 2>/dev/null || echo ""); \
	else \
		AH="$$ANDROID_HOME"; \
		NH="$${NDK_HOME:-$${ANDROID_NDK_HOME:-$$ANDROID_NDK_ROOT}}"; \
	fi; \
	if [ -z "$$AH" ] || [ ! -d "$$AH" ]; then \
		echo "ERROR: ANDROID_HOME not set or directory not found ('$$AH')."; \
		echo "  Set ANDROID_HOME to your Android SDK path and re-run."; \
		exit 1; \
	fi; \
	if [ -z "$$NH" ] || [ ! -d "$$NH" ]; then \
		NH=$$(ls -d "$$AH/ndk/"* 2>/dev/null | sort -rV | head -1); \
	fi; \
	if [ -z "$$NH" ] || [ ! -d "$$NH" ]; then \
		[ -d "$$AH/ndk-bundle" ] && NH="$$AH/ndk-bundle"; \
	fi; \
	if [ -z "$$NH" ] || [ ! -d "$$NH" ]; then \
		echo ""; \
		echo "ERROR: Android NDK not found. Searched:"; \
		echo "  $$AH/ndk/*           (SDK Manager layout)"; \
		echo "  $$AH/ndk-bundle      (legacy layout)"; \
		echo "  NDK_HOME / ANDROID_NDK_HOME / ANDROID_NDK_ROOT env vars"; \
		echo ""; \
		echo "Run  'make install'  to auto-install the NDK via sdkmanager ($(NDK_VERSION))."; \
		echo "Or install manually: Android Studio > SDK Manager > SDK Tools > NDK (Side by side)"; \
		exit 1; \
	fi; \
	echo "  JAVA_HOME:    $$JAVA_P"; \
	echo "  ANDROID_HOME: $$AH"; \
	echo "  NDK_HOME:     $$NH"; \
	export JAVA_HOME="$$JAVA_P" ANDROID_HOME="$$AH" NDK_HOME="$$NH"; \
	export PATH="$$JAVA_HOME/bin:$$PATH"; \
	cd android/rust_ui && \
	cargo tauri android build --debug && \
	APK_UNSIGNED="gen/android/app/build/outputs/apk/universal/release/app-universal-release-unsigned.apk"; \
	APK_DEBUG_STD="gen/android/app/build/outputs/apk/debug/app-debug.apk"; \
	APK_DEBUG="gen/android/app/build/outputs/apk/universal/debug/app-universal-debug.apk"; \
	APK_TO_INSTALL=$$(if [ -f $$APK_DEBUG ]; then echo $$APK_DEBUG; elif [ -f $$APK_DEBUG_STD ]; then echo $$APK_DEBUG_STD; elif [ -f $$APK_UNSIGNED ]; then echo $$APK_UNSIGNED; else echo ""; fi); \
	if [ -z "$$APK_TO_INSTALL" ]; then echo "No APK found to install."; exit 1; fi; \
	adb wait-for-device && adb get-state | grep -q device && \
	adb uninstall com.sharkos 2>/dev/null || true; \
	echo "Installing $$APK_TO_INSTALL" && adb install "$$APK_TO_INSTALL" && \
	adb shell am force-stop com.sharkos || true; \
	adb shell am start -n com.sharkos/.MainActivity
cargo-build: check-java
	@set -e; \
	JAVA_P="$(JAVA_17)"; \
	if command -v cygpath >/dev/null 2>&1; then \
		AH=$$(cygpath -u "$$ANDROID_HOME" 2>/dev/null || echo "$$ANDROID_HOME"); \
		NH=$$([ -n "$$NDK_HOME"          ] && cygpath -u "$$NDK_HOME"          2>/dev/null || echo ""); \
		[ -z "$$NH" ] && NH=$$([ -n "$$ANDROID_NDK_HOME" ] && cygpath -u "$$ANDROID_NDK_HOME" 2>/dev/null || echo ""); \
		[ -z "$$NH" ] && NH=$$([ -n "$$ANDROID_NDK_ROOT" ] && cygpath -u "$$ANDROID_NDK_ROOT" 2>/dev/null || echo ""); \
	else \
		AH="$$ANDROID_HOME"; \
		NH="$${NDK_HOME:-$${ANDROID_NDK_HOME:-$$ANDROID_NDK_ROOT}}"; \
	fi; \
	if [ -z "$$NH" ] || [ ! -d "$$NH" ]; then \
		NH=$$(ls -d "$$AH/ndk/"* 2>/dev/null | sort -rV | head -1); \
	fi; \
	if [ -z "$$NH" ] || [ ! -d "$$NH" ]; then \
		[ -d "$$AH/ndk-bundle" ] && NH="$$AH/ndk-bundle"; \
	fi; \
	export JAVA_HOME="$$JAVA_P" ANDROID_HOME="$$AH" NDK_HOME="$$NH"; \
	export PATH="$$JAVA_HOME/bin:$$PATH"; \
	cd android/rust_ui && cargo tauri android build --debug

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
	@$(ARDUINO_CLI) lib install "SmartRC-CC1101-Driver-Lib" || true
	@$(ARDUINO_CLI) lib install "Adafruit NeoPixel" || true
	@$(ARDUINO_CLI) lib install "Adafruit PN532" || true
	@$(ARDUINO_CLI) lib install "RadioLib" || true
	@$(ARDUINO_CLI) lib install "IRremote" || true
	@$(ARDUINO_CLI) lib install "PubSubClient" || true
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
	# Port priority: positional arg (make flash COM4), then PORT env var, then auto-detect
	@_ARG="$(filter-out flash,$(MAKECMDGOALS))"; \
	FQBN_V2=$${FQBN_V2:-esp32:esp32:esp32s3:CDCOnBoot=cdc,UploadSpeed=921600,FlashMode=qio,FlashSize=$${FLASHSIZE:-16M},PartitionScheme=app3M_fat9M_16MB,PSRAM=opi}; \
	if [ -n "$$_ARG" ]; then \
		PORT="$$_ARG"; \
	else \
		PORT=$${PORT:-$$(ls /dev/cu.* /dev/tty.* 2>/dev/null | egrep -i 'usbmodem' | head -n1)}; \
		if [ -z "$$PORT" ]; then \
			PORT=$$(ls /dev/cu.* /dev/tty.* 2>/dev/null | egrep -i 'usbserial|ttyUSB|ttyACM|CP210|FTDI|wch' | grep -vi Bluetooth | head -n1); \
		fi; \
	fi; \
	if [ -z "$$PORT" ]; then echo "No serial port detected. Usage: make flash COM4  (or set PORT=)"; exit 1; fi; \
	echo "Using port: $$PORT"; \
	echo "Using FQBN: $$FQBN_V2"; \
	$(ARDUINO_CLI) compile --fqbn "$$FQBN_V2" main && \
	$(ARDUINO_CLI) upload -p "$$PORT" --fqbn "$$FQBN_V2" main

# Absorb the COM port argument so make doesn't try to build it as a target
COM%:
	@true

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
