/*
 * disrupt-stubs.ino
 *
 * Safe dummy implementations of all disruptor / jamming functions.
 *
 * All stubs are marked __attribute__((weak)) so that when disrupt.ino
 * IS present, its strong symbols silently override these at link time.
 * When disrupt.ino is ABSENT, the weak stubs supply harmless no-ops so
 * the rest of the sketch still links and runs.
 *
 * cc1101Jam() and loraJam() are thin wrappers declared in globals.h —
 * they are always defined here regardless of whether disrupt.ino exists.
 */

#include <Arduino.h>

// Forward-declare only what stubs need (avoid pulling in globals.h
// which drags IRremote headers that have non-inline defs causing ODR issues).
extern void notifyStatus(const char *s);

// ── Weak stubs — overridden by disrupt.ino when present ─────────────────

// Use __attribute__((weak)) so the real implementations in disrupt.ino
// silently override these stubs at link time.

__attribute__((weak))
void cc1101Disrupt(float startFreq_r1, float stopFreq_r1, float powerDbm_r1,
                   float startFreq_r2, float stopFreq_r2, float powerDbm_r2) {
  (void)startFreq_r1; (void)stopFreq_r1; (void)powerDbm_r1;
  (void)startFreq_r2; (void)stopFreq_r2; (void)powerDbm_r2;
  Serial.println("[stub] cc1101Disrupt() — disrupt.ino not present, skipping.");
  notifyStatus("ERROR: disruptor_not_available");
}

__attribute__((weak))
void cc1101SmartDisrupt(float powerDbm, float minRSSI, float durationSec) {
  (void)powerDbm; (void)minRSSI; (void)durationSec;
  Serial.println("[stub] cc1101SmartDisrupt() — disrupt.ino not present, skipping.");
  notifyStatus("ERROR: disruptor_not_available");
}

__attribute__((weak))
void loraDisrupt() {
  Serial.println("[stub] loraDisrupt() — disrupt.ino not present, skipping.");
  notifyStatus("ERROR: disruptor_not_available");
}

// ── Wrappers always available (called from sub-ghz-menu / events) ───────

void cc1101Jam() {
  Serial.println("[cc1101Jam] delegating to cc1101Disrupt()");
  cc1101Disrupt(433.0, 433.0, 0, 433.0, 433.0, 0);
}

void loraJam() {
  Serial.println("[loraJam] delegating to loraDisrupt()");
  loraDisrupt();
}
