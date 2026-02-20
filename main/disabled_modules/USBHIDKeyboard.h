#pragma once

// Minimal USBHIDKeyboard shim for build-time compatibility.
// The real implementation should come from the platform-specific USB/HID library.
// This stub provides the symbols used across the code so the project compiles
// when the proper library isn't available in the environment.

#include <Arduino.h>

// Key code fallbacks (values chosen only for compile-time; not full HID codes)
#ifndef KEY_RETURN
#define KEY_RETURN '\n'
#endif
#ifndef KEY_TAB
#define KEY_TAB '\t'
#endif
#ifndef KEY_ESC
#define KEY_ESC 0x1B
#endif
#ifndef KEY_UP_ARROW
#define KEY_UP_ARROW 0x80
#endif
#ifndef KEY_DOWN_ARROW
#define KEY_DOWN_ARROW 0x81
#endif
#ifndef KEY_LEFT_ARROW
#define KEY_LEFT_ARROW 0x82
#endif
#ifndef KEY_RIGHT_ARROW
#define KEY_RIGHT_ARROW 0x83
#endif
#ifndef KEY_DELETE
#define KEY_DELETE 0x7F
#endif
#ifndef KEY_BACKSPACE
#define KEY_BACKSPACE 0x08
#endif
#ifndef KEY_INSERT
#define KEY_INSERT 0x84
#endif
#ifndef KEY_HOME
#define KEY_HOME 0x85
#endif
#ifndef KEY_END
#define KEY_END 0x86
#endif
#ifndef KEY_PAGE_UP
#define KEY_PAGE_UP 0x87
#endif
#ifndef KEY_PAGE_DOWN
#define KEY_PAGE_DOWN 0x88
#endif
#ifndef KEY_LEFT_SHIFT
#define KEY_LEFT_SHIFT 0x90
#endif
#ifndef KEY_LEFT_CTRL
#define KEY_LEFT_CTRL 0x91
#endif
#ifndef KEY_LEFT_ALT
#define KEY_LEFT_ALT 0x92
#endif
#ifndef KEY_LEFT_GUI
#define KEY_LEFT_GUI 0x93
#endif
#ifndef KEY_CAPS_LOCK
#define KEY_CAPS_LOCK 0x94
#endif

class USBHIDKeyboard {
public:
  void begin() {}
  void end() {}
  void press(uint8_t k) { (void)k; }
  void release(uint8_t k) { (void)k; }
  void releaseAll() {}
  void print(const String &s) { (void)s; }
  void print(const char *s) { (void)s; }
  void print(char c) { char buf[2] = {c, 0}; print(buf); }
  void println(const String &s) { (void)s; }
  void write(uint8_t c) { (void)c; }
};

// One definition exists in `HIZMOS_OLED_U8G2lib.ino` (actual instance),
// declare extern here so other files can reference it.
extern USBHIDKeyboard Keyboard;
