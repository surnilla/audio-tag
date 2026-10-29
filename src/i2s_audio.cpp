#include "i2s_audio.h"
#include <Arduino.h>
#include <nrfx_i2s.h>

// ── I2S pin configuration (XIAO nRF52840) ──────────────────────────────────
// Adjust to your wiring. Common I2S amp boards (e.g. MAX98357A):
//   SCK  → I2S bit clock
//   WS   → I2S word select (LRCK)
//   SD   → I2S serial data (SDOUT from nRF)
#define I2S_SCK_PIN   7
#define I2S_WS_PIN    8
#define I2S_SDOUT_PIN 9

// ── Audio format ────────────────────────────────────────────────────────────
#define SAMPLE_RATE     16000   // Hz
#define BITS_PER_SAMPLE 16
#define CHANNELS        1       // mono

// ── DMA double-buffer ────────────────────────────────────────────────────────
#define DMA_BUF_WORDS 256       // 256 × 32-bit words = 512 bytes per buffer
static uint32_t s_dma_buf[2][DMA_BUF_WORDS];

static const uint8_t *s_pcm_data   = nullptr;
static uint32_t       s_pcm_len    = 0;
static uint32_t       s_pcm_offset = 0;
static volatile bool  s_playing      = false;
static volatile bool  s_just_finished = false;

// Fill one DMA buffer with the next samples, zero-pad at end of stream
static void fill_dma_buf(uint32_t *buf, uint32_t words) {
    for (uint32_t i = 0; i < words; i++) {
        int16_t l = 0, r = 0;
        if (s_pcm_offset + 1 < s_pcm_len) {
            l = (int16_t)(s_pcm_data[s_pcm_offset] | (s_pcm_data[s_pcm_offset + 1] << 8));
            s_pcm_offset += 2;
            r = (CHANNELS == 1) ? l : 0;  // duplicate mono to both channels
        } else {
            s_playing = false;
        }
        // nRF I2S DMA format: left sample in upper 16 bits, right in lower 16 bits
        buf[i] = ((uint32_t)(uint16_t)l << 16) | (uint16_t)r;
    }
}

static void i2s_data_handler(nrfx_i2s_buffers_t const *p_released,
                              uint32_t                   status) {
    if (status & NRFX_I2S_STATUS_NEXT_BUFFERS_NEEDED) {
        static uint8_t buf_idx = 0;
        buf_idx ^= 1;
        fill_dma_buf(s_dma_buf[buf_idx], DMA_BUF_WORDS);
        nrfx_i2s_buffers_t next = {
            .p_rx_buffer = nullptr,
            .p_tx_buffer = s_dma_buf[buf_idx]
        };
        nrfx_i2s_next_buffers_set(&next);

        if (!s_playing) {
            s_just_finished = true;
            nrfx_i2s_stop();
        }
    }
}

void i2s_audio_init() {
    // NRFX_I2S_PIN_NOT_USED (0xFF) is the correct sentinel for uint8_t pin fields.
    // NRF_I2S_PIN_NOT_CONNECTED is 0xFFFFFFFF and causes a narrowing error.
    nrfx_i2s_config_t cfg = NRFX_I2S_DEFAULT_CONFIG(
        I2S_SCK_PIN, I2S_WS_PIN, 0xFF,
        0xFF, I2S_SDOUT_PIN);

    cfg.sample_width = NRF_I2S_SWIDTH_16BIT;
    cfg.channels     = NRF_I2S_CHANNELS_STEREO;  // nRF always outputs stereo frames
    cfg.mck_setup    = NRF_I2S_MCK_32MDIV21;     // ~1.524 MHz MCK → 16 kHz with LRCLK div 96
    cfg.ratio        = NRF_I2S_RATIO_96X;

    nrfx_i2s_init(&cfg, i2s_data_handler);
    Serial.println("I2S: initialized");
}

void i2s_audio_play_raw(const uint8_t *pcm_data, uint32_t len_bytes) {
    if (s_playing) nrfx_i2s_stop();

    s_pcm_data   = pcm_data;
    s_pcm_len    = len_bytes;
    s_pcm_offset = 0;
    s_playing    = true;

    // Prime both DMA buffers before starting
    fill_dma_buf(s_dma_buf[0], DMA_BUF_WORDS);
    fill_dma_buf(s_dma_buf[1], DMA_BUF_WORDS);

    nrfx_i2s_buffers_t initial = {
        .p_rx_buffer = nullptr,
        .p_tx_buffer = s_dma_buf[0]
    };
    nrfx_i2s_start(&initial, DMA_BUF_WORDS, 0);
    Serial.printf("I2S: playing %u bytes\n", len_bytes);
}

void i2s_audio_stop() {
    s_playing = false;
    nrfx_i2s_stop();
}

bool i2s_audio_is_playing() {
    return s_playing;
}

bool i2s_audio_just_finished() {
    if (s_just_finished) {
        s_just_finished = false;
        return true;
    }
    return false;
}
