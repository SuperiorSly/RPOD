/**********************************************************************
 * Phase 2 — BUTTONS (iPod project on ESP32-2432S028 "CYD")
 * Arduino-ESP32 core 2.0.17
 *
 * WHAT THIS SKETCH DOES
 *   1. Prints a RAW line ~5x per second: PLAY/HOME states + IO35 ADC.
 *   2. Decodes all 7 buttons with PRELIMINARY thresholds and prints
 *      events: DOWN / UP / HOLD / REPEAT / SHORT / DOUBLE / TRIPLE.
 *   3. First session = calibration: press each button one at a time,
 *      note the RAW IO35 numbers, report them back, and we lock the
 *      final thresholds into the constants at the top of this file.
 *
 * WIRING (project brief §3.3)
 *   CN1 socket (GND, IO22, IO27, 3V3) -> breadboard rails
 *   P3 socket : IO35 -> breadboard (ladder wiper line)
 *
 *   PLAY (btn 1): IO22 -> button -> GND   (internal pull-up, LOW = pressed)
 *   HOME (btn 3): IO27 -> button -> GND   (internal pull-up, LOW = pressed)
 *
 *   Ladder on IO35 (input-only pin! ladder provides its own pull-up):
 *      3V3 --1k-- A --1k-- B --1k-- C --1k-- D
 *      BACK : IO35 - btn - GND (point G)  ~0.00 V -> ADC ~0
 *      VOL+ : IO35 - btn - A (1k from 3V3)~1.10 V -> ADC ~1365
 *      VOL- : IO35 - btn - B (2k)         ~1.65 V -> ADC ~2048
 *      UP   : IO35 - btn - C (3k)         ~1.98 V -> ADC ~2460
 *      DOWN : IO35 - btn - D (4k)         ~2.20 V -> ADC ~2730
 *      none : pulled up to 3V3 via the 2k ~3.3  V -> ADC ~4095
 *   Buttons are 4-leg tactiles: wire across two DIAGONAL legs.
 **********************************************************************/

// ---------------- pins ----------------
const uint8_t PIN_PLAY   = 22;   // PLAY / PAUSE / SELECT
const uint8_t PIN_HOME   = 27;   // HOME
const uint8_t PIN_LADDER = 35;   // 5 buttons through the resistor ladder (ADC1)

// ------- ladder thresholds, 12-bit ADC (0..4095), PRELIMINARY -------
// Wide bands with safety gaps between them; we calibrate these from
// the RAW numbers you report.
const int TH_BACK_HI   =  250;   // BACK   : below this        (~0 V)
const int TH_VOLP_LO   =  700;   // VOL+   : 700 .. 1700      (~1.10 V)
const int TH_VOLP_HI   = 1700;
const int TH_VOLM_LO   = 1800;   // VOL-   : 1800 .. 2250     (~1.65 V)
const int TH_VOLM_HI   = 2250;
const int TH_UP_LO     = 2350;   // UP     : 2350 .. 2595     (~1.98 V)
const int TH_UP_HI     = 2595;
const int TH_DOWN_LO   = 2625;   // DOWN   : 2625 .. 2950     (~2.20 V)
const int TH_DOWN_HI   = 2950;
const int TH_IDLE_LO   = 3500;   // nothing pressed: above this (~3.3 V)

// ---------------- timing ----------------
const uint16_t DEBOUNCE_MS   = 30;    // raw reading must be stable this long
const uint16_t HOLD_MS       = 500;   // held this long -> HOLD (+repeat)
const uint16_t REPEAT_MS     = 250;   // HOLD auto-repeat interval
const uint16_t TAP_MAX_MS    = 400;   // press shorter than this counts as a tap
const uint16_t TAP_WINDOW_MS = 450;   // gap allowed between taps (triple press)
const uint16_t TICK_MS       = 5;     // engine tick
const uint16_t RAW_PRINT_MS  = 200;   // RAW line interval

// ---------------- button ids ----------------
enum BtnId : uint8_t {
  B_NONE = 0, B_PLAY, B_HOME, B_BACK, B_VOLP, B_VOLM, B_UP, B_DOWN, B_UNKNOWN
};
const char* const BTN_NAME[] = {
  "none", "PLAY", "HOME", "BACK", "VOL+", "VOL-", "UP", "DOWN", "??"
};

// ---------------- per-button state ----------------
struct BtnState {
  bool     lastRaw;      // last raw pressed/not reading
  bool     committed;    // debounced state
  uint32_t changedAt;    // when the raw reading last changed
  uint32_t pressAt;      // when the committed press started
  bool     holdAnnounced;
  uint32_t nextRepeat;
  uint16_t repeatCount;
  uint8_t  taps;         // tap counter
  bool     tapPending;   // waiting out the tap window before announcing
  uint32_t tapDeadline;
};
BtnState st[8];          // indexed by BtnId 1..7

BtnId  g_ladder = B_NONE;   // decoded ladder result for this tick
int    g_adc    = 0;
uint32_t g_nextRawPrint = 0;

// ------------------------------------------------------------------
BtnId decodeLadder(int a)
{
  if (a >= TH_IDLE_LO) return B_NONE;
  if (a <  TH_BACK_HI) return B_BACK;
  if (a >= TH_VOLP_LO && a < TH_VOLP_HI) return B_VOLP;
  if (a >= TH_VOLM_LO && a < TH_VOLM_HI) return B_VOLM;
  if (a >= TH_UP_LO   && a < TH_UP_HI)   return B_UP;
  if (a >= TH_DOWN_LO && a < TH_DOWN_HI) return B_DOWN;
  return B_UNKNOWN;                       // in a gap between bands
}

// ------------------------------------------------------------------
bool rawPressed(BtnId id)
{
  switch (id) {
    case B_PLAY: return digitalRead(PIN_PLAY) == LOW;   // active LOW
    case B_HOME: return digitalRead(PIN_HOME) == LOW;   // active LOW
    case B_BACK: return g_ladder == B_BACK;
    case B_VOLP: return g_ladder == B_VOLP;
    case B_VOLM: return g_ladder == B_VOLM;
    case B_UP:   return g_ladder == B_UP;
    case B_DOWN: return g_ladder == B_DOWN;
    default:     return false;
  }
}

// ------------------------------------------------------------------
void announceTapCount(BtnId id, uint8_t n)
{
  if (n == 1)      Serial.printf("[PRESS ] %s  SHORT\n",  BTN_NAME[id]);
  else if (n == 2) Serial.printf("[PRESS ] %s  DOUBLE\n", BTN_NAME[id]);
  else             Serial.printf("[PRESS ] %s  TRIPLE\n", BTN_NAME[id]);
}

// ------------------------------------------------------------------
void commit(BtnId id, bool pressed)
{
  BtnState &b = st[id];
  b.committed = pressed;

  if (pressed) {
    Serial.printf("[DOWN  ] %s\n", BTN_NAME[id]);
    b.pressAt       = millis();
    b.holdAnnounced = false;
    b.repeatCount   = 0;
    b.nextRepeat    = 0;
  } else {
    uint32_t dur = millis() - b.pressAt;
    Serial.printf("[UP    ] %s  (%lu ms)\n", BTN_NAME[id], (unsigned long)dur);

    if (!b.holdAnnounced && dur <= TAP_MAX_MS) {
      b.taps++;
      if (b.taps >= 3) {                       // triple = announce immediately
        announceTapCount(id, 3);
        b.taps = 0;
        b.tapPending = false;
      } else {
        b.tapPending  = true;
        b.tapDeadline = millis() + TAP_WINDOW_MS;
      }
    } else {
      b.taps       = 0;                        // a hold resets the tap counter
      b.tapPending = false;
    }
  }
}

// ------------------------------------------------------------------
void runButtonEngine()
{
  uint32_t now = millis();

  for (uint8_t id = 1; id <= 7; id++) {
    BtnState &b = st[id];
    bool raw = rawPressed((BtnId)id);

    if (raw != b.lastRaw) {                    // raw edge -> restart stability timer
      b.lastRaw  = raw;
      b.changedAt = now;
    }
    if (raw != b.committed && (now - b.changedAt) >= DEBOUNCE_MS) {
      commit((BtnId)id, raw);                  // stable long enough -> commit
    }

    if (b.committed) {                         // HOLD + auto-repeat
      if (!b.holdAnnounced && (now - b.pressAt) >= HOLD_MS) {
        b.holdAnnounced = true;
        b.repeatCount   = 0;
        b.nextRepeat    = now + REPEAT_MS;
        Serial.printf("[HOLD  ] %s  (repeats every %u ms)\n", BTN_NAME[id], REPEAT_MS);
      }
      if (b.holdAnnounced && (int32_t)(now - b.nextRepeat) >= 0) {
        b.repeatCount++;
        b.nextRepeat += REPEAT_MS;
        Serial.printf("[REPEAT] %s  #%u\n", BTN_NAME[id], b.repeatCount);
      }
    }

    if (b.tapPending && (int32_t)(now - b.tapDeadline) >= 0) {
      announceTapCount((BtnId)id, b.taps);     // window closed -> 1 or 2 taps
      b.taps       = 0;
      b.tapPending = false;
    }
  }
}

// ------------------------------------------------------------------
void setup()
{
  Serial.begin(115200);
  delay(400);

  pinMode(PIN_PLAY,   INPUT_PULLUP);
  pinMode(PIN_HOME,   INPUT_PULLUP);
  pinMode(PIN_LADDER, INPUT);          // input-only pin, external 2k pull-up
  analogSetAttenuation(ADC_11db);      // full 0..3.3 V range

  Serial.println();
  Serial.println("==================================================");
  Serial.println("  PHASE 2 - BUTTON TEST  (CYD, core 2.0.17)");
  Serial.println("==================================================");
  Serial.println("Press each button ONE at a time and watch both the");
  Serial.println("RAW lines and the [EVENT] lines.");
  Serial.println();
  Serial.println("Expected IO35 raw values (calibration reference):");
  Serial.println("  nothing pressed : ~4000-4095");
  Serial.println("  BACK            : ~0-100");
  Serial.println("  VOL+            : ~1250-1500");
  Serial.println("  VOL-            : ~1900-2200");
  Serial.println("  UP              : ~2350-2600");
  Serial.println("  DOWN            : ~2600-2850");
  Serial.println();
  Serial.println("Write down what YOU actually get for each button");
  Serial.println("(the middle of the numbers you see) - we use those");
  Serial.println("as the final thresholds.");
  Serial.println("Note: IO22/IO27 show 1 = released, 0 = pressed.");
  Serial.println("==================================================");
  Serial.println();
}

// ------------------------------------------------------------------
void loop()
{
  uint32_t now = millis();

  g_adc    = analogRead(PIN_LADDER);
  g_ladder = decodeLadder(g_adc);

  runButtonEngine();

  if ((int32_t)(now - g_nextRawPrint) >= 0) {
    g_nextRawPrint = now + RAW_PRINT_MS;
    Serial.printf("RAW   PLAY=%d HOME=%d   IO35=%4d (%.2f V)  ->  %s\n",
                  digitalRead(PIN_PLAY) == LOW ? 1 : 0,
                  digitalRead(PIN_HOME) == LOW ? 1 : 0,
                  g_adc,
                  g_adc * 3.3f / 4095.0f,
                  BTN_NAME[g_ladder]);
  }

  delay(TICK_MS);
}
