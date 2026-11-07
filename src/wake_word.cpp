// Wake-word detection: "Hey Mobi"
// Uses Edge Impulse run_classifier_continuous() on 250-ms audio slices (16 kHz mono).
// Feed one 10-ms stereo I2S chunk at a time via ww_feed(); it returns true the
// moment "Hey Mobi" is detected above WW_THRESHOLD.

#include "wake_word.h"
#include <Arduino.h>
#include <string.h>
#include <math.h>

// Edge Impulse model include — paths relative to lib/ei-hey-mobi/ (via -Ilib/ei-hey-mobi)
#include "edge-impulse-sdk/classifier/ei_run_classifier.h"

// -----------------------------------------------------------------------
// Constants
// -----------------------------------------------------------------------
// EI_CLASSIFIER_SLICE_SIZE = 16000 / 4 = 4000 samples (250 ms at 16 kHz)
static_assert(EI_CLASSIFIER_SLICE_SIZE == 4000,
              "EI_CLASSIFIER_SLICE_SIZE must be 4000 (1 s / 4 slices at 16 kHz)");

#define WW_THRESHOLD        0.80f   // minimum Hey_mobi confidence per slice
#define WW_CONSEC_SLICES    2       // must exceed threshold this many slices in a row
#define WW_COOLDOWN_MS      8000    // ignore re-detections for 8 s after a trigger

// -----------------------------------------------------------------------
// Module state
// -----------------------------------------------------------------------
static float    s_slice[EI_CLASSIFIER_SLICE_SIZE];  // current 250-ms float slice
static int      s_slice_idx  = 0;    // samples accumulated in s_slice so far
static uint32_t s_last_det_ms = 0;   // millis() of last detection (cooldown)
static int      s_consec_det  = 0;   // consecutive slices that scored above threshold
static bool     s_ei_inited   = false;

// EI signal callback — feeds s_slice to the classifier
static int ww_get_data(size_t offset, size_t length, float *out_ptr) {
    memcpy(out_ptr, s_slice + offset, length * sizeof(float));
    return 0;
}

// -----------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------

void ww_init() {
    run_classifier_init();
    s_slice_idx   = 0;
    s_consec_det  = 0;
    s_last_det_ms = 0;
    s_ei_inited   = true;
}

void ww_reset() {
    // Clear EI rolling window so stale "Hey Mobi" features don't re-trigger immediately
    run_classifier_init();
    s_slice_idx   = 0;
    s_consec_det  = 0;
    s_last_det_ms = millis();  // start 8 s cooldown from now
}

bool ww_feed(const uint8_t *stereo_chunk, int chunk_bytes) {
    if (!s_ei_inited) return false;

    // Each I2S frame = 8 bytes: [L:4 bytes 32-bit][R:4 bytes 32-bit].
    // Sample is in the upper 16 bits of each 32-bit word.
    // Mix L+R → mono, convert int16 → float [-1, 1].
    int frames = chunk_bytes / 8;
    for (int i = 0; i < frames; i++) {
        int16_t L = (int16_t)((*(const int32_t *)(stereo_chunk + i * 8))     >> 16);
        int16_t R = (int16_t)((*(const int32_t *)(stereo_chunk + i * 8 + 4)) >> 16);
        int16_t mono = (int16_t)(((int32_t)L + R) / 2);
        s_slice[s_slice_idx++] = (float)mono / 32768.0f;

        if (s_slice_idx >= EI_CLASSIFIER_SLICE_SIZE) {
            // Slice is full — run inference
            s_slice_idx = 0;

            signal_t signal;
            signal.total_length = EI_CLASSIFIER_SLICE_SIZE;
            signal.get_data     = ww_get_data;

            ei_impulse_result_t result;
            EI_IMPULSE_ERROR err = run_classifier_continuous(&signal, &result, false);
            if (err != EI_IMPULSE_OK) continue;

            // Label 0 = "Hey_mobi", label 1 = "unknown"
            float hey_mobi_score = result.classification[0].value;
            uint32_t now = millis();
            if ((now - s_last_det_ms) <= WW_COOLDOWN_MS) {
                s_consec_det = 0;  // in cooldown — don't accumulate
                continue;
            }
            if (hey_mobi_score > WW_THRESHOLD) {
                s_consec_det++;
                if (s_consec_det >= WW_CONSEC_SLICES) {
                    s_consec_det  = 0;
                    s_last_det_ms = now;
                    return true;
                }
            } else {
                s_consec_det = 0;  // broken streak — reset
            }
        }
    }
    return false;
}
