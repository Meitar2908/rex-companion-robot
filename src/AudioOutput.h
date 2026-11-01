// AudioOutput.h — I2S speaker output (MAX98357A mono amp, I2S1 TX,
// per CLAUDE.md's pin map: BCLK=15, LRC=16, DIN=7). Pure hardware
// access: writes raw PCM samples. Knows nothing about TTS or what
// audio to play — that's TextToSpeechClient's job.
#pragma once
#include <stdint.h>
#include <stddef.h>

class AudioOutput {
public:
  static constexpr uint32_t kSampleRate = 16000;  // matches AudioInput/STT sample rate

  void begin();

  // Blocking write of `count` mono 16-bit PCM samples. Applies the
  // current digital gain (see setVolume()) with a soft-knee limiter
  // above the clip threshold, so raising volume compresses loud peaks
  // instead of chopping them flat.
  void writeSamples(const int16_t* samples, size_t count);

  // Runtime-adjustable digital gain multiplier — see AudioOutput.cpp
  // for the default and the reasoning behind it. Roughly 0.0-3.0 is
  // the sane range; above ~3.0 the soft limiter is doing most of the
  // work and it stops actually getting louder, just more compressed.
  void setVolume(float gain) { volumeGain_ = gain; }
  float volume() const { return volumeGain_; }

private:
  // 2.4 (was 1.6 originally) — see AudioOutput.cpp for the tuning
  // history. Change at runtime with setVolume() or /volume in main.cpp
  // rather than editing this if you just want it louder/quieter.
  float volumeGain_ = 2.4f;
};
