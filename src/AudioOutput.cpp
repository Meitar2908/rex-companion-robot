#include "AudioOutput.h"
#include <Arduino.h>
#include <driver/i2s.h>
#include <math.h>

namespace {
constexpr i2s_port_t kI2SPort = I2S_NUM_1;
constexpr int kPinBCLK = 15;
constexpr int kPinLRC  = 16;
constexpr int kPinDIN  = 7;

// Loud passages are soft-limited (tanh knee) above kSoftClipThreshold
// rather than hard-clamped to full scale — a hard clamp chopped
// waveform peaks flat, which sounded like distortion/glitching.
// AudioOutput::volumeGain_ (default 2.4, see AudioOutput.h) is the
// actual gain applied — raising the threshold's headroom here widens
// how much of a loud passage gets compressed rather than left alone;
// lowering kSoftClipThreshold raises perceived loudness further
// (more of the signal gets pulled up toward full scale) at the cost
// of more compression/less dynamic range.
//
// If 2.4x digital gain still isn't loud enough: the real ceiling is
// hardware, not software. MAX98357A's GAIN pin is currently wired
// "open" = 9dB (see CLAUDE.md's pin map). Tying that pin to GND
// instead gives 15dB — a real +6dB (~2x) the digital gain here can't
// safely reach without audible distortion.
constexpr float kSoftClipThreshold = 22000.0f;

// 8 * 512 samples = 4096 samples = 256ms of buffered audio at 16kHz —
// enough cushion to absorb a ~33ms TFT frame push (CharacterController
// keeps rendering while TTS streams, see TextToSpeechClient::TickFn)
// without the DMA running dry and glitching. The old 4*256 (64ms) was
// too tight for that.
constexpr int kDmaBufCount = 8;
constexpr int kDmaBufLen   = 512;
} // namespace

void AudioOutput::begin() {
  i2s_config_t config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = kSampleRate,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,   // MAX98357A's SD left floating -> left channel
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = kDmaBufCount,
    .dma_buf_len = kDmaBufLen,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0,
  };
  i2s_pin_config_t pins = {
    .bck_io_num = kPinBCLK,
    .ws_io_num = kPinLRC,
    .data_out_num = kPinDIN,
    .data_in_num = I2S_PIN_NO_CHANGE,
  };
  i2s_driver_install(kI2SPort, &config, 0, nullptr);
  i2s_set_pin(kI2SPort, &pins);
}

void AudioOutput::writeSamples(const int16_t* samples, size_t count) {
  constexpr size_t kChunk = 256;
  int16_t scaled[kChunk];
  size_t offset = 0;
  while (offset < count) {
    size_t n = count - offset < kChunk ? count - offset : kChunk;
    for (size_t i = 0; i < n; i++) {
      float v = samples[offset + i] * volumeGain_;
      float mag = fabsf(v);
      if (mag > kSoftClipThreshold) {
        float headroom = 32767.0f - kSoftClipThreshold;
        float over = mag - kSoftClipThreshold;
        mag = kSoftClipThreshold + headroom * tanhf(over / headroom);
        v = (v < 0) ? -mag : mag;
      }
      scaled[i] = (int16_t)v;
    }
    size_t bytesWritten = 0;
    i2s_write(kI2SPort, scaled, n * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
    offset += n;
  }
}
