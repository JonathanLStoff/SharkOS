#pragma once

#include <Arduino.h>

// Initialize the event subsystem. Call from setup().
void events_init();

// Enqueue a raw command string received via BLE. Returns true if queued.
bool events_enqueue_command(const String &cmd);

// Helper: accept a Bluetooth command (wrapper that forwards into the events queue)
// - this centralises pairing checks and queue handling for all BLE->events entrypoints
void bluetooth_receive_command(const String &cmd);

// Process one pending event (non-blocking). Call regularly from loop().
void events_process_one();

// Validate whether a received payload is an accepted topic/command.
bool events_validate_topic(const String &payload);

// Handle a legacy JSON 'Command' message (was previously in firmware files)
void handleBLECommand(const String &jsonCmd);

// Send a response over BLE back to the client(s). If `inReplyTo` is
// non-empty the response will include that correlation id.
void bluetooth_send_response(const String &payload, const String &inReplyTo);
