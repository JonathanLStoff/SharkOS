# Makefile for SharkOS (Consolidated)
ARDUINO_CLI ?= arduino-cli
FQBN ?= esp32:esp32:esp32
ANDROID_NDK_HOME ?= /opt/homebrew/share/android-ndk
JAVA_17 ?= /Library/Java/JavaVirtualMachines/temurin-17.jdk/Contents/Home

.PHONY: help android-run android-apk-test apk-run

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
.PHONY: log-follow
log-follow:
	@echo "Following device logcat for package com.sharkos..."
	adb logcat | grep -E 'tauri|sharkos_lib'
