// SpeechToTextClient.h — sends captured audio to a cloud
// speech-to-text service (OpenAI's Whisper API) and returns the
// transcribed text. Knows nothing about the mic, VAD, or the
// character — it's a pure "audio bytes in, text out" boundary, so
// swapping providers later only touches this file.
#pragma once
#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

class SpeechToTextClient {
public:
  // Blocking — makes an HTTP request and waits for the response
  // (typically 3-5s, almost all of it OpenAI-side inference). Callers
  // no longer need to worry about this freezing face animation:
  // VoiceController runs it on its own FreeRTOS task, same pattern as
  // TextToSpeechClient.
  //
  // pcm16: mono 16-bit PCM samples at sampleRate.
  // Returns the transcribed text, or "" on failure (see Serial for
  // the error).
  String transcribe(const int16_t* pcm16, size_t sampleCount, uint32_t sampleRate);
};
