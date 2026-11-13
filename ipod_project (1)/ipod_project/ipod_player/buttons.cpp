#include "buttons.h"
#include "config.h"

static const uint8_t QN = 24;
static BtnEvent q[QN];
static uint8_t qHead = 0, qTail = 0;

static void push(BtnId id, BtnEvtType t, uint32_t held) {
  uint8_t n = (qHead + 1) % QN;
  if (n == qTail) return;            // queue full: drop
  q[qHead] = { id, t, held };
  qHead = n;
}

bool buttonsGet(BtnEvent& e) {
  if (qTail == qHead) return false;
  e = q[qTail];
  qTail = (qTail + 1) % QN;
  return true;
}

const char* buttonName(BtnId id) {
  switch (id) {
    case BTN_PLAY:  return "PLAY";
    case BTN_BACK:  return "BACK";
    case BTN_HOME:  return "HOME";
    case BTN_VOLUP: return "VOL+";
    case BTN_VOLDN: return "VOL-";
    case BTN_UP:    return "UP";
    case BTN_DOWN:  return "DOWN";
    default:        return "-";
  }
}

struct BtnState {
  bool     raw = false;      // last raw reading
  uint32_t rawSince = 0;     // when raw last changed
  bool     stable = false;   // debounced state
  uint32_t downAt = 0;
  uint32_t lastRepeat = 0;
  bool     repeating = false;
};
static BtnState st[BTN_COUNT];

void buttonsInit() {
  pinMode(PIN_BTN_PLAY, INPUT_PULLUP);
  pinMode(PIN_BTN_HOME, INPUT_PULLUP);
  pinMode(PIN_LADDER, INPUT);                 // IO35 is input-only, no pull-up
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_LADDER, ADC_11db);
}

uint32_t buttonsReadLadderMv() {
  // median of 3 reads to ignore single noisy samples
  uint32_t a = analogReadMilliVolts(PIN_LADDER);
  uint32_t b = analogReadMilliVolts(PIN_LADDER);
  uint32_t c = analogReadMilliVolts(PIN_LADDER);
  if (a > b) { uint32_t t = a; a = b; b = t; }
  if (b > c) { b = c; }
  return (a > b) ? a : b;
}

BtnId buttonsDecodeLadder(uint32_t mv) {
  if (mv <= LADDER_MV_BACK_MAX)  return BTN_BACK;
  if (mv <= LADDER_MV_VOLUP_MAX) return BTN_VOLUP;
  if (mv <= LADDER_MV_VOLDN_MAX) return BTN_VOLDN;
  if (mv <= LADDER_MV_UP_MAX)    return BTN_UP;
  if (mv <= LADDER_MV_DOWN_MAX)  return BTN_DOWN;
  return BTN_NONE;
}

void buttonsPoll() {
  uint32_t now = millis();
  bool rawNow[BTN_COUNT];
  for (int i = 0; i < BTN_COUNT; i++) rawNow[i] = false;
  rawNow[BTN_PLAY] = (digitalRead(PIN_BTN_PLAY) == LOW);
  rawNow[BTN_HOME] = (digitalRead(PIN_BTN_HOME) == LOW);
  BtnId l = buttonsDecodeLadder(buttonsReadLadderMv());
  if (l != BTN_NONE) rawNow[l] = true;

  for (int i = 0; i < BTN_COUNT; i++) {
    BtnState& s = st[i];
    if (rawNow[i] != s.raw) { s.raw = rawNow[i]; s.rawSince = now; }
    if (s.raw != s.stable && now - s.rawSince >= BTN_DEBOUNCE_MS) {
      s.stable = s.raw;
      if (s.stable) {
        s.downAt = now; s.lastRepeat = now; s.repeating = false;
        push((BtnId)i, BE_PRESS, 0);
      } else {
        push((BtnId)i, BE_RELEASE, now - s.downAt);
      }
    }
    if (s.stable) {
      uint32_t held = now - s.downAt;
      if (!s.repeating && held >= BTN_REPEAT_DELAY_MS) {
        s.repeating = true; s.lastRepeat = now;
        push((BtnId)i, BE_REPEAT, held);
      } else if (s.repeating && now - s.lastRepeat >= BTN_REPEAT_MS) {
        s.lastRepeat = now;
        push((BtnId)i, BE_REPEAT, held);
      }
    }
  }
}
