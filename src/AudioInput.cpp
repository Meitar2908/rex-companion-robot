#include "AudioInput.h"
#include <Arduino.h>
#include <driver/i2s.h>

namespace {
constexpr i2s_port_t kI2SPort = I2S_NUM_0;
constexpr int kPinBCLK = 4;
constexpr int kPinWS   = 5;
constexpr int kPinSD   = 6;
constexpr size_t kRawChunkMax = 256;
} // namespace

void AudioInput::begin() {
  i2s_config_t config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = kSampleRate,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,  // INMP441 sends 24-bit in a 32-bit frame
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,    // mic's L/R pin is tied to GND (left channel)
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    // 8*256 = 2048 samples = 128ms cushion (was 4*256 = 64ms) — same
    // reasoning as AudioOutput's DMA buffer: gives more slack against
    // loop() jitter (TFT frame pushes, etc.) before samples get dropped.
    .dma_buf_count = 8,
    .dma_buf_len = 256,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0,
  };
  i2s_pin_config_t pins = {
    .bck_io_num = kPinBCLK,
    .ws_io_num = kPinWS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = kPinSD,
  };
  i2s_driver_install(kI2SPort, &config, 0, nullptr);
  i2s_set_pin(kI2SPort, &pins);
}

void AudioInput::flush() {
  // Drain by READING rather than i2s_zero_dma_buffer(): the RX DMA engine
  // owns those buffers and is actively writing into them, so zeroing them
  // underneath it left the next read returning a corrupted near-full-scale
  // frame (measured: rms 0.75 right after an un-mute). Reading until the
  // ring reports empty leaves it genuinely drained instead. Bounded so a
  // continuously-fed ring can't spin here forever.
  static int32_t scratch[kRawChunkMax];
  for (int i = 0; i < 64; i++) {
    size_t bytesRead = 0;
    i2s_read(kI2SPort, scratch, sizeof(scratch), &bytesRead, 0);
    if (bytesRead == 0) break;
  }
}

size_t AudioInput::readSamples(int16_t* out, size_t maxSamples, bool waitForData) {
  static int32_t raw[kRawChunkMax];
  size_t rawCount = maxSamples > kRawChunkMax ? kRawChunkMax : maxSamples;

  size_t bytesRead = 0;
  i2s_read(kI2SPort, raw, rawCount * sizeof(int32_t), &bytesRead,
           waitForData ? portMAX_DELAY : 0);
  size_t samplesRead = bytesRead / sizeof(int32_t);

  for (size_t i = 0; i < samplesRead; i++) {
    // Right-shift the 32-bit frame down to a usable 16-bit-ish range
    // — the standard starting point for INMP441 on ESP32. Tune this
    // once real hardware is wired and you can see actual levels.
    out[i] = (int16_t)(raw[i] >> 14);
  }
  return samplesRead;
}
