// EchoS3R ES8311 test tone
// Plays a 1kHz sine wave through the built-in speaker.
// Confirms ES8311 DAC + I2S + amplifier chain works.
//
// EchoS3R pinout:
//   I2C:  SDA=G45, SCL=G0
//   I2S:  MCLK=G11, BCLK=G17, WS=G3, DOUT=G48, DIN=G4
//   AMP:  enable=G18 (active high)

#include <Arduino.h>
#include <M5EchoBase.h>
#include <math.h>

#define SAMPLE_RATE   16000
#define AMP_ENABLE    18

// EchoS3R I2C + I2S pins
#define PIN_I2C_SDA   45
#define PIN_I2C_SCL    0
#define PIN_I2S_DIN    4
#define PIN_I2S_WS     3
#define PIN_I2S_DOUT  48
#define PIN_I2S_BCLK  17

M5EchoBase echo;

// Generate 1 second of 1kHz sine wave at 16kHz sample rate, 16-bit mono
// = 16000 samples * 2 bytes = 32000 bytes
static const int TONE_SAMPLES = SAMPLE_RATE;
static int16_t tone_buf[TONE_SAMPLES];

static void generate_tone(float freq_hz) {
    for (int i = 0; i < TONE_SAMPLES; i++) {
        tone_buf[i] = (int16_t)(16000.0f * sinf(2.0f * M_PI * freq_hz * i / SAMPLE_RATE));
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("EchoS3R tone test starting...");

    // Enable amplifier
    pinMode(AMP_ENABLE, OUTPUT);
    digitalWrite(AMP_ENABLE, HIGH);

    // Init codec + I2S
    if (!echo.init(SAMPLE_RATE, PIN_I2C_SDA, PIN_I2C_SCL,
                   PIN_I2S_DIN, PIN_I2S_WS, PIN_I2S_DOUT, PIN_I2S_BCLK)) {
        Serial.println("ERROR: ES8311 init failed");
        while (true) delay(1000);
    }

    echo.setSpeakerVolume(70);
    Serial.println("ES8311 init OK — playing 1kHz tone");

    generate_tone(1000.0f);
}

void loop() {
    echo.play((const uint8_t*)tone_buf, sizeof(tone_buf));
}
