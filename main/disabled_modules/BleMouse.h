#ifndef BLEMOUSE_H
#define BLEMOUSE_H

// Minimal BleMouse stub for headless compilation.  Implements the
// small API that the firmware expects so we don't need the real
// library during headless builds.
class BleMouse {
public:
  BleMouse(const char* deviceName, const char* deviceName2, int battery=100) {}
  void begin() {}
  bool isConnected() { return false; }
  void move(int8_t x, int8_t y) { (void)x; (void)y; }
  void click(uint8_t btn) { (void)btn; }
};

// Basic mouse button fallbacks
#ifndef MOUSE_LEFT
#define MOUSE_LEFT 1
#endif
#ifndef MOUSE_RIGHT
#define MOUSE_RIGHT 2
#endif

#endif
