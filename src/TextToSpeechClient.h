// TextToSpeechClient.h — sends text to ElevenLabs and streams the
// resulting speech audio to a speaker in real time, rather than
// buffering the whole clip first. Knows nothing about the mic, STT,
// or the conversational AI — pure "text in, audio played out"
// boundary, the mirror image of SpeechToTextClient on the other side
// of the pipeline. Requests raw PCM (matches CLAUDE.md's preference
// for a TTS format needing no on-device decoding) at AudioOutput's
// sample rate, so bytes go straight from the HTTP stream to I2S.
//
// Briefly tried OpenAI's TTS instead (same account as Whisper STT, one
// less credential) after hitting ElevenLabs' character quota. Its
// response is HTTP chunked-transfer-encoded, and Arduino's HTTPClient
// doesn't auto-decode chunking on a raw getStreamPtr() read (only on
// its own getString()/writeToStream() convenience methods) — that was
// eventually diagnosed and fixed (a proper chunk decoder, byte-
// alignment carry-over across reads, and a fade-in after any gap), but
// the user preferred reverting to this known-good ElevenLabs setup
// rather than keep debugging OpenAI live. This project isn't under
// version control, so that OpenAI-specific code isn't preserved
// anywhere — revisiting it later means redoing it from scratch.
#pragma once
#include <Arduino.h>
#include "AudioOutput.h"

class TextToSpeechClient {
public:
  // Blocking — streams and plays audio as it arrives, so total time
  // is roughly "network latency + audio duration", not "download all
  // then play". Returns false on any failure (see Serial for the error).
  // Called on the background task speakAsync() spawns below; call it
  // directly only if you genuinely want to block the calling thread.
  bool speak(const String& text, AudioOutput& out);

  // Runs speak() on its own FreeRTOS task (pinned to core 0, away
  // from the main loop/render thread on core 1). Earlier this ran
  // synchronously with a periodic callback into CharacterController
  // to keep the face animating — that made the SPI display push and
  // the I2S audio feed share one thread/core, and interleaving them
  // corrupted playback (audible glitching). Moving audio streaming to
  // its own core removes that contention entirely: the render loop
  // never blocks, and never shares a thread with the network/I2S work.
  //
  // Only one call may be in flight at a time — a second call while
  // isSpeaking() is true is ignored (logged, not queued).
  void speakAsync(const String& text, AudioOutput& out);
  bool isSpeaking() const { return speaking_; }
  // Valid once isSpeaking() has gone from true back to false.
  bool lastResultOk() const { return lastResultOk_; }
  // millis() timestamp of the first audio chunk written to AudioOutput
  // during the most recent speak()/speakAsync() call — 0 if none has
  // completed yet or none was reached. Used to measure "time to first
  // audio" for the latency report (see VoiceController.cpp).
  uint32_t firstAudioMs() const { return firstAudioMs_; }

private:
  static void taskEntry(void* param);

  volatile bool speaking_ = false;
  volatile bool lastResultOk_ = false;
  volatile uint32_t firstAudioMs_ = 0;
};
