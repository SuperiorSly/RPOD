// ================================================================
//  DIY iPod MP3 Player — PHASE 1: AUDIO JACK TEST
//  ------------------------------------------------------------
//  Tests the earphone jack ONLY, standalone, no SD / WiFi / screen.
//
//  Signal path on the CYD:
//    ESP32 internal DAC (GPIO26) -> on-board amp (LTK8002D)
//    -> "SPEAK" socket -> 1k resistors -> 3.5mm jack -> earphones.
//  The jack is a BRIDGE output: NO pin of the jack is ground.
//
//  How to use:
//    1. Open Serial Monitor at 115200 baud (or let the assistant do it).
//    2. Wear earphones! Start with the quietest tests.
//    3. Type a command and press Enter. List of commands is printed at boot,
//       and again with '?'.
//
//  What we are listening for:
//    - hiss with nothing playing      (baseline, command 'a')
//    - whine / buzzing                (compare with/without backlight, command 'p')
//    - pops / clicks at start & stop  (we ramp everything to avoid them;
//                                      if you still hear pops, tell us)
//    - distortion at volumes          (ladder command 'c', increase with '+')
//    - balance (mono anyway)          (noise command 'e')
//
//  NOTE from the brief: this setup amplifies the DAC a lot.
//  The ladder starts VERY quiet on purpose. You raise it with '+'.
//
//  Requires: esp32:esp32@2.0.17   (NOT 3.x)
// ================================================================

#include <Arduino.h>
#include <driver/i2s.h>
#include <driver/dac.h>

// ------------------------- configuration -------------------------
#define SERIAL_BAUD   115200
#define SAMPLE_RATE   22050          // Hz, fine for a test tone
#define BUF_FRAMES    128            // stereo frames per i2s_write call (~5.8 ms)
#define TONE_FREQ     440.0f         // Hz
#define LADDER_STEP_MS 3000          // 3 s per ladder level
#define AMP_MAX       127.0f         // 8-bit DAC amplitude ceiling
#define RAMP_RATE     220.0f         // amplitude units per second (anti-pop ramp)
#define BACKLIGHT_PIN 21             // ONLY ever used for backlight

// Volume ladder for the automatic test 'c': very quiet -> medium
static const float kLadder[] = { 3.0f, 9.0f, 22.0f, 44.0f };
static const int kLadderCount = sizeof(kLadder) / sizeof(kLadder[0]);

// --------------------------- sound state -------------------------
enum SoundMode { S_OFF, S_TONE, S_FADE, S_NOISE, S_DAC_OFF, S_SWEEP, S_CLICKS, S_PROBE };
volatile SoundMode g_mode = S_OFF;

volatile float g_ampTarget = 0.0f;   // what amplitude we are aiming for
volatile float g_baseAmp   = kLadder[0]; // user-adjustable base volume ('+'/'-')
volatile float g_ampCur    = 0.0f;   // ramped current amplitude
volatile int   g_packMode  = 0;      // 0 = DAC value in HIGH byte (val<<8) [standard]
                                     // 1 = DAC value in LOW byte (diagnostic)

// --------------------------- audio data --------------------------
static uint8_t sine256[256];         // 256-entry sine table, 0..255, mid 128
static uint16_t outBuf[BUF_FRAMES * 2]; // stereo: L,R frames (16-bit slots)
static float tonePhase = 0.0f;       // 0..256 phase accumulator for tone/fade
static float fadePhase = 0.0f;       // 0..1 triangle envelope position
static float sweepPos  = 0.0f;       // 0..SWEEP_PERIOD s position for the sweep
static float clickT    = 0.0f;       // time inside the click pattern (s)
static uint32_t noiseLfsr = 0xACE1;  // white noise generator state

#define SWEEP_LO   400.0f            // Hz
#define SWEEP_HI   3000.0f           // Hz
#define SWEEP_UP   2.0f              // s to sweep up
#define SWEEP_DOWN 2.0f              // s to sweep back down
#define CLICK_EVERY 0.25f            // s between clicks
#define CLICK_LEN   0.008f           // s click burst length

// -----------------------------------------------------------------
//  I2S + built-in DAC, pattern verified against ESP-IDF docs:
//  - 16-bit slots, DAC takes ONLY the high (MSB) byte -> value << 8
//  - BOTH channels enabled (GPIO25+GPIO26); the amp listens on GPIO26.
void i2sInit()
{
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN);
  cfg.sample_rate = SAMPLE_RATE;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT; // DAC takes MSB 8 bits of these
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT; // stereo, both channels driven
  cfg.communication_format = (i2s_comm_format_t)I2S_COMM_FORMAT_STAND_MSB;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = 8;
  cfg.dma_buf_len = 256;             // samples per dma buffer (samples, not bytes)
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = true;     // DMA auto-fills silence on underrun
  // unused legacy fields stay zero-initialised

  i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL);
  i2s_set_dac_mode(I2S_DAC_CHANNEL_BOTH_EN); // GPIO25 + GPIO26; idle midline comes from data
}

/* Audio task: pinned to core 0, high priority.
   loop() (core 1) handles Serial, so the two never fight. */
void audioTask(void *arg)
{
  size_t written = 0;
  for (;;) {
    // ease the amplitude toward the target (anti-pop ramp)
    float dt = (float)BUF_FRAMES / (float)SAMPLE_RATE; // seconds per buffer
    float cur = g_ampCur;
    float tgt = g_ampTarget;
    float step = RAMP_RATE * dt;
    if (cur < tgt) cur = (tgt - cur < step) ? tgt : (cur + step);
    else if (cur > tgt) cur = (cur - tgt < step) ? tgt : (cur - step);
    g_ampCur = cur;

    float vol = cur;
    SoundMode mode = g_mode;

    for (int f = 0; f < BUF_FRAMES; f++) {
      float v = 0.0f;
      switch (mode) {
        case S_TONE: {                       // 440 Hz sine at base volume
          int idx = (int)tonePhase & 0xFF;
          v = (sine256[idx] - 128) / 128.0f * vol;
          tonePhase += (float)TONE_FREQ / (float)SAMPLE_RATE * 256.0f;
          if (tonePhase >= 256.0f) tonePhase -= 256.0f;
          break;
        }
        case S_FADE: {                       // fade in and out forever (4 s cycle)
          int idx = (int)tonePhase & 0xFF;
          float env = (fadePhase < 0.5f) ? fadePhase * 2.0f : (1.0f - fadePhase) * 2.0f;
          v = (sine256[idx] - 128) / 128.0f * vol * env;
          tonePhase += (float)TONE_FREQ / (float)SAMPLE_RATE * 256.0f;
          if (tonePhase >= 256.0f) tonePhase -= 256.0f;
          fadePhase += 1.0f / ((float)SAMPLE_RATE * 4.0f); // cycle = 4 s
          if (fadePhase >= 1.0f) fadePhase -= 1.0f;
          break;
        }
        case S_NOISE: {                      // very low white noise
          noiseLfsr ^= noiseLfsr << 7;
          noiseLfsr ^= noiseLfsr >> 9;
          noiseLfsr ^= noiseLfsr << 8;
          v = ((int)(noiseLfsr & 0xFF) - 128) / 128.0f * (vol / 3.0f);
          break;
        }
        case S_SWEEP: {                      // rising and falling sweep 400..3000 Hz
          float up = (sweepPos < SWEEP_UP);
          float env = up ? (sweepPos / SWEEP_UP) : 1.0f - (sweepPos - SWEEP_UP) / SWEEP_DOWN;
          float f = SWEEP_LO + (SWEEP_HI - SWEEP_LO) * env;
          int idx = (int)tonePhase & 0xFF;
          v = (sine256[idx] - 128) / 127.0f * vol;
          tonePhase += f / (float)SAMPLE_RATE * 256.0f;
          if (tonePhase >= 256.0f) tonePhase -= 256.0f;
          sweepPos += 1.0f / (float)SAMPLE_RATE;
          if (sweepPos >= SWEEP_UP + SWEEP_DOWN) sweepPos = 0.0f;
          break;
        }
        case S_CLICKS: {                     // short full-amplitude bursts every 0.25 s
          float inside = clickT - (float)((int)(clickT / CLICK_EVERY) * CLICK_EVERY);
          if (inside < CLICK_LEN) {
            int dir = ((int)(clickT / CLICK_EVERY) & 1) ? 1 : -1;
            v = dir * vol;
          }
          clickT += 1.0f / (float)SAMPLE_RATE;
          break;
        }
        case S_PROBE: {                      // tonePhase reused as staircase clock
          tonePhase += 1.0f / (float)SAMPLE_RATE;
          sweepPos = tonePhase;
          break;
        }
        case S_OFF:
        case S_DAC_OFF:
        default:
          v = 0.0f;                          // silence = DAC idle at midpoint
          break;
      }
      float clamped = constrain(v, -127.0f, 127.0f);
      int val = 128 + (int)clamped;
      val = constrain(val, 0, 255);
      // default: both slots identical (true mono BTL)
      int valL = val, valR = val;
      if (mode == S_PROBE) {               // staircase only on the LEFT slot
        float inside = sweepPos - (float)((int)(sweepPos / 4.0f) * 4.0f);
        (void)inside;
        int band = (int)(sweepPos / 4.0f) % 3;
        valL = 128 + ((band == 1) ? -48 : ((band == 2) ? 48 : 0));
        valR = 128;                        // right slot stays at DC mid
        // TIP<->SLEEVE (multimeter DC) should read ~0.0 -> -0.6 -> +0.6 V steps
      }
      uint16_t slotL = g_packMode ? (uint16_t)valL : (uint16_t)(valL << 8);
      uint16_t slotR = g_packMode ? (uint16_t)valR : (uint16_t)(valR << 8);
      outBuf[f * 2]     = slotL;            // right slot engine ordering (GPIO25)
      outBuf[f * 2 + 1] = slotR;            // left slot (GPIO26 = amp input)
    }

    i2s_write(I2S_NUM_0, outBuf, sizeof(outBuf), &written, portMAX_DELAY);
  }
}

// -----------------------------------------------------------------
// Called by commands when leaving the "DAC fully off" state:
// puts the pins back under the I2S engine and soft re-ramps from zero.
void resumeFromDACOff()
{
  i2s_set_dac_mode(I2S_DAC_CHANNEL_BOTH_EN);
  dac_output_enable(DAC_CHANNEL_2);
  i2s_start(I2S_NUM_0);
  g_ampCur = 0.0f;
}

// -----------------------------------------------------------------
void printSplash()
{
  Serial.println();
  Serial.println("==================================================");
  Serial.println(" PHASE 1 : AUDIO JACK TEST   (diagnostics edition)");
  Serial.println(" Type a letter, then Enter. '?' lists commands.");
  Serial.println("--------------------------------------------------");
  Serial.println(" NORMAL TESTS:");
  Serial.println(" a - SILENCE: DAC idle at midpoint (HISS BASELINE)");
  Serial.println(" 0 - DAC completely disabled (amp without signal)");
  Serial.println(" b - quiet 440 Hz tone at current volume");
  Serial.println(" c - VOLUME LADDER: 4 levels, 3 s each (auto)");
  Serial.println(" d - fade in / fade out loop");
  Serial.println(" e - very low white noise (balance check)");
  Serial.println(" + / - - volume up / down");
  Serial.println(" p - toggle backlight PWM (does PWM add noise?)");
  Serial.println("--------------------------------------------------");
  Serial.println(" DIAGNOSTICS (if you hear NONE of the normal tests):");
  Serial.println(" s - SWEEP 400-3000 Hz (finds frequency gaps)");
  Serial.println(" k - CLICK train (proves any connection at all)");
  Serial.println(" z - DC staircase for the MULTIMETER (no sound)");
  Serial.println(" D - DAC pin STEP test — GPIO26 hops 0.8V/2.6V, multimeter only");
  Serial.println(" v - tone at MAXIMUM volume (I2S + DAC, MSB packing)");
  Serial.println(" y - same tone, LOW-byte packing (diagnostic toggle)");
  Serial.println(" w - SIREN via dacWrite on GPIO25 then GPIO26,");
  Serial.println("     alternating, no I2S at all (hardware/path check)");
  Serial.println("--------------------------------------------------");
  Serial.println(" ? - this menu");
  Serial.println("--------------------------------------------------");
  Serial.println(" IMPORTANT: earphones in. Keep it quiet to start.");
  Serial.println(" Louder volumes come later, with '+'.");
  Serial.println("==================================================");
}

void statusNow()
{
  const char *m = "?";
  switch (g_mode) {
    case S_TONE:    m = "tone 440 Hz"; break;
    case S_FADE:    m = "fade in/out"; break;
    case S_NOISE:   m = "noise (low)"; break;
    case S_SWEEP:   m = "sweep 400-3000 Hz"; break;
    case S_CLICKS:  m = "click train"; break;
    case S_PROBE:   m = "DC staircase (probe)"; break;
    case S_DAC_OFF: m = "DAC OFF"; break;
    default:        m = "silence (DAC idle)"; break;
  }
  Serial.printf("[now] %s | volume ~%.0f/127 (base ~%.0f)\n", m, g_ampTarget, g_baseAmp);
}

void rampOff(const char *why)
{
  g_ampTarget = 0.0f;                 // fade to zero first (no pop)
  Serial.println(why);
}

// backlight PWM test: does the backlight PWM inject audible noise?
bool g_backlightOn = false;
void backlightToggle()
{
  if (g_backlightOn) {
    ledcWrite(0, 0);
    g_backlightOn = false;
    Serial.println("[backlight] PWM OFF");
  } else {
    ledcWrite(0, 128);                // 50 % duty on IO21 (no screen driver running)
    g_backlightOn = true;
    Serial.println("[backlight] PWM ON 50% @5 kHz — listen: louder buzz/whine now?");
  }
}

// -----------------------------------------------------------------
void setup()
{
  Serial.begin(SERIAL_BAUD);
  delay(300);

  // backlight control channel (LEDC ch 0, 5 kHz PWM) — only for IO21
  ledcSetup(0, 5000, 8);
  ledcAttachPin(BACKLIGHT_PIN, 0);
  ledcWrite(0, 0);                     // dark until the user asks for the PWM test

  // sine table
  for (int i = 0; i < 256; i++)
    sine256[i] = 128 + (int)(127.0f * sinf(2.0f * PI * i / 256.0f) + 0.5f);

  i2sInit();
  g_ampCur = 0.0f;
  g_ampTarget = 0.0f;

  xTaskCreatePinnedToCore(audioTask, "audio_tx", 4096, NULL, 5, NULL, 0);

  Serial.printf("Audio task running. Sample rate %d Hz. DAC idle...\n", SAMPLE_RATE);
  printSplash();
}

void loop()
{
  if (Serial.available()) {
    char c = (char)Serial.read();
    switch (c) {
      case 'a':
      case 'x':
        rampOff("[mode] silence / DAC idle (hiss baseline)");
        g_mode = S_OFF;
        statusNow();
        break;

      case '0': {                      // fully mute the DAC (amp without signal)
        g_mode = S_OFF;                // first: silence + let the ramp land at 0
        g_ampTarget = 0.0f;
        unsigned long t0 = millis();
        while (g_ampCur > 0.5f && millis() - t0 < 2000) delay(10);
        i2s_stop(I2S_NUM_0);
        dac_output_disable(DAC_CHANNEL_2);
        g_mode = S_DAC_OFF;
        Serial.println("[mode] DAC OFF (amp input floats) — listen: amp idle hiss");
        break;
      }

      case 'b':
        if (g_mode == S_DAC_OFF) {
          dac_output_enable(DAC_CHANNEL_2);
          i2s_start(I2S_NUM_0);
          g_ampCur = 0.0f;             // re-ramp from zero to avoid pop
        }
        g_mode = S_TONE;
        g_ampTarget = g_baseAmp;
        Serial.println("[mode] 440 Hz tone (quiet)");
        statusNow();
        break;

      case 'd':
        if (g_mode == S_DAC_OFF) {
          resumeFromDACOff();
        }
        fadePhase = 0.0f;
        g_mode = S_FADE;
        g_ampTarget = g_baseAmp;
        Serial.println("[mode] fade in / fade out (4 s cycle)");
        statusNow();
        break;

      case 'e':
        if (g_mode == S_DAC_OFF) {
          resumeFromDACOff();
        }
        g_mode = S_NOISE;
        g_ampTarget = g_baseAmp;
        Serial.println("[mode] low white noise");
        statusNow();
        break;

      // 's' = linear sweep 400..3000 Hz — sweeps catch bad contacts better than
      // a fixed tone because the path impedance is frequency sensitive.
      case 's':
        if (g_mode == S_DAC_OFF) {
          resumeFromDACOff();
        }
        sweepPos = 0.0f;
        g_mode = S_SWEEP;
        g_ampTarget = 90.0f;
        Serial.println("[diag] SWEEP 400-3000 Hz — listen for volume dips/gaps");
        Serial.println("       across the sweep. Along which part does it vanish?");
        statusNow();
        break;

      // 'k' = click train: ticks prove ANY kind of connection exists.
      case 'k':
        if (g_mode == S_DAC_OFF) {
          resumeFromDACOff();
        }
        clickT = 0.0f;
        g_mode = S_CLICKS;
        g_ampTarget = 120.0f;
        Serial.println("[diag] CLICK TRAIN — tick every 0.25 s. Count the ticks:");
        Serial.println("       0 ticks / ticks only by touch / ticks always.");
        statusNow();
        break;

      case 'c': {                      // automatic volume ladder
        if (g_mode == S_DAC_OFF) {
          resumeFromDACOff();
        }
        Serial.println("[mode] VOLUME LADDER — listen for distortion at each step");
        g_mode = S_TONE;
        for (int i = 0; i < kLadderCount; i++) {
          g_ampTarget = kLadder[i];
          Serial.printf("   step %d/%d : amplitude ~%.0f/127 — listen now...\n",
                        i + 1, kLadderCount, kLadder[i]);
          unsigned long t0 = millis();
          while (millis() - t0 < LADDER_STEP_MS) delay(20);
        }
        rampOff("[done] ladder finished -> silence. How was it? (pop? hiss? whine?)");
        g_mode = S_OFF;
        break;
      }

      case '+': {
        float nv = g_baseAmp * 1.6f;
        if (nv > AMP_MAX) nv = AMP_MAX;
        g_baseAmp = nv;
        if (g_mode == S_TONE || g_mode == S_FADE || g_mode == S_NOISE) g_ampTarget = g_baseAmp;
        Serial.printf("[vol] base volume now ~%.0f/127%s\n", g_baseAmp,
                      (nv >= AMP_MAX) ? "  (MAX — could get loud!)" : "");
        statusNow();
        break;
      }

      case '-': {
        float nv = g_baseAmp / 1.6f;
        if (nv < 1.0f) nv = 1.0f;
        g_baseAmp = nv;
        if (g_mode == S_TONE || g_mode == S_FADE || g_mode == S_NOISE) g_ampTarget = g_baseAmp;
        Serial.printf("[vol] base volume now ~%.0f/127\n", g_baseAmp);
        statusNow();
        break;
      }

      // 'v' = max-volume tone through the normal I2S/DAC path
      case 'v': {
        if (g_mode == S_DAC_OFF) {
          resumeFromDACOff();
        }
        g_packMode = 0;                    // standard MSB packing
        g_mode = S_TONE;
        g_baseAmp = AMP_MAX;               // temporarily max
        g_ampTarget = AMP_MAX;
        Serial.println("[diag] MAX volume 440 Hz — I2S path, value in HIGH byte.");
        Serial.println("       (If you hear this but not 'b', it was just volume.)");
        statusNow();
        break;
      }

      // 'y' = same tone, but DAC value placed in the LOW byte instead
      case 'y': {
        g_packMode = !g_packMode;
        g_mode = S_TONE;
        g_baseAmp = AMP_MAX;
        g_ampTarget = AMP_MAX;
        Serial.printf("[diag] packing switched to %s byte, MAX volume tone playing.\n",
                      g_packMode ? "LOW" : "HIGH");
        Serial.println("       Tell me which of v / y sounds correct (pure 440 Hz).");
        statusNow();
        break;
      }

      // 'w' = hardware path siren WITHOUT i2s: dacWrite on DAC1 (GPIO25),
      // then DAC2 (GPIO26), alternating — tells us which pin feeds the amp.
      case 'w': {
        Serial.println("[diag] SIREN TEST — stops the I2S task output first...");
        g_mode = S_OFF;
        g_ampTarget = 0.0f;
        unsigned long t0 = millis();
        while (g_ampCur > 0.5f && millis() - t0 < 2000) delay(10);
        i2s_stop(I2S_NUM_0);               // keep the DMA engine out of the way
        i2s_set_dac_mode(I2S_DAC_CHANNEL_DISABLE); // HAND the DAC pins back to RTC dacWrite!
        dac_output_disable(DAC_CHANNEL_2);
        g_mode = S_DAC_OFF;
        for (int round = 0; round < 2; round++) {
          for (int dac = 1; dac <= 2; dac++) {
            Serial.printf("   SILENCE THEN SIREN on %s (GPIO%d) for 3 s — now.\n",
                          dac == 1 ? "DAC1" : "DAC2", dac == 1 ? 25 : 26);
            dac_channel_t ch = (dac == 1) ? DAC_CHANNEL_1 : DAC_CHANNEL_2;
            dac_output_enable(ch);
            int pin = (dac == 1) ? 25 : 26;
            unsigned long t1 = millis();
            while (millis() - t1 < 3000) {
              // 500 Hz soft siren: 40 steps per cycle
              for (int s = 0; s < 40; s++) {
                int v = 128 + (int)(110.0f * sinf(2.0f * PI * s / 40.0f));
                dacWrite(pin, constrain(v, 0, 255));
                delayMicroseconds(50);     // ~2 ms period => 500 Hz
              }
            }
            dac_output_disable(ch);
            // snap back to midline quietly
          }
        }
        Serial.println("[diag] siren finished. Did you hear it on DAC1, DAC2, both, or neither?");
        resumeFromDACOff();                // give the pins back to the I2S engine
        break;
      }

      // 'D' = DAC pin DC STEP TEST: GPIO26 jumps between two fixed DC levels
      // every 4 s, using the RTC dacWrite path (no I2S pipeline at all).
      // If the multimeter sees the pin step, the DAC hardware is alive.
      case 'D': {
        Serial.println("[diag] DAC pin STEP TEST — GPIO26 jumps low/high every 4 s");
        Serial.println("       Multimeter (DC V): black probe on GND, red probe on");
        Serial.println("       the pin you are holding on R4. Expect the meter to");
        Serial.println("       hop between ~0.8 V and ~2.6 V every 4 seconds.");
        g_mode = S_OFF;
        g_ampTarget = 0.0f;
        unsigned long t0 = millis();
        while (g_ampCur > 0.5f && millis() - t0 < 2000) delay(10);
        i2s_stop(I2S_NUM_0);
        i2s_set_dac_mode(I2S_DAC_CHANNEL_DISABLE);   // hand the pins to RTC dacWrite
        dac_output_disable(DAC_CHANNEL_1);
        dac_output_enable(DAC_CHANNEL_2);
        for (int k = 0; k < 3; k++) {
          Serial.println("   LOW level (expect ~0.8 V)...");
          dacWrite(26, 60);
          delay(4000);
          Serial.println("   HIGH level (expect ~2.6 V)...");
          dacWrite(26, 200);
          delay(4000);
        }
        dacWrite(26, 128);
        g_mode = S_DAC_OFF;
        resumeFromDACOff();                // give the pins back to the I2S engine
        Serial.println("[diag] step test done. Meter never stepped? Then the pin we");
        Serial.println("       are holding is NOT the DAC output -> try R4's other end,");
        Serial.println("       or send me a photo of the board (front + back + amp area).");
        break;
      }

      // 'z' = DC staircase for the multimeter: LEFT side steps every 4 s
      // between mid, mid-48, mid+48 -> the DMM (DC volts) sees ~0 / -0.6 / +0.6 V
      case 'z': {
        tonePhase = 0.0f; sweepPos = 0.0f;
        g_mode = S_PROBE;
        g_ampTarget = 0.0f;                // staircase ignores the amp envelope
        Serial.println("[probe] DC STAIRCASE. Multimeter set to DC Volts:");
        Serial.println("       probes: TIP of the plug <-> SLEEVE of the plug.");
        Serial.println("       Expect: ~0.0 V for 4 s, ~-0.6 V for 4 s, ~+0.6 V for 4 s,");
        Serial.println("       repeating with a change every 4 s (minus sign = needle/");
        Serial.println("       reading polarity, not an error).");
        Serial.println("       Write down the values you see on each step!");
        statusNow();
        break;
      }

      case 'p':
        backlightToggle();
        break;

      case '?':
        printSplash();
        statusNow();
        break;

      default:
        break;                       // ignore line endings etc.
    }
  }
  delay(10);
}
