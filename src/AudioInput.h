// AudioInput.h — I2S microphone capture (INMP441-class, I2S0 RX, per
// CLAUDE.md's pin map: BCLK=4, WS=5, SD=6, L/R tied to GND). Pure
// hardware access: reads raw PCM samples. Knows nothing about voice
// activity detection or speech-to-text — see VoiceActivityDetector
// and SpeechToTextClient for those layers.
//
// NOT YET VALIDATED ON HARDWARE — the mic isn't wired yet. The
// sample scaling below follows the common INMP441+ESP32 community
// pattern and will likely need tuning once real audio is flowing
// (same as SPI_FREQUENCY needed tuning for the display).
#pragma once
#include <stdint.h>
#include <stddef.h>

class AudioInput {
public:
  static constexpr uint32_t kSampleRate = 16000;  // Hz — enough for speech, keeps buffers small

  void begin();

  // Reads up to maxSamples 16-bit PCM samples. By default blocks briefly
  // (bounded by how much audio maxSamples represents at kSampleRate —
  // e.g. 256 samples is ~16ms) until that much data is available.
  // Pass waitForData=false to take only what's already sitting in the
  // DMA ring and return 0 immediately if it's empty — used to drain the
  // backlog after the first (pacing) read of each loop iteration.
  // Returns the number of samples actually read.
  size_t readSamples(int16_t* out, size_t maxSamples, bool waitForData = true);

  // Discards whatever is sitting in the I2S DMA ring. The peripheral
  // keeps capturing into that ring even while nothing is reading it, so
  // after any idle/muted period the first reads would otherwise return
  // audio up to ~128ms stale — including the I2S start-up transient,
  // which reads near full scale and reliably tripped voice detection.
  void flush();
};
