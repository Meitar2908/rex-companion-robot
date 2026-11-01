// VoiceActivityDetector.h — simple energy-threshold voice activity
// detector with hangover timing, so a brief pause mid-sentence
// doesn't end the utterance early. Not real audio-domain science,
// just dependable and cheap enough for the ESP32 to run continuously
// alongside face rendering.
#pragma once
#include <stdint.h>

enum class VoiceState { Silence, Speaking };

// What update() observed this call. Aborted is the important one: it
// means speech started but never accumulated enough actual voiced audio
// to be worth transcribing (a click, a thump, the mute button itself),
// so the caller should throw the buffer away rather than spend a
// multi-second STT round trip on it.
enum class VadEvent { None, SpeechStarted, SpeechEnded, SpeechAborted };

class VoiceActivityDetector {
public:
  // Feed each captured chunk's normalized RMS level (roughly 0..1) and
  // the current time.
  VadEvent update(float rmsLevel, uint32_t nowMs);

  // Clears all state — call when the mic is un-muted so stale timestamps
  // and a stale Speaking state from before the mute can't leak across.
  void reset();

  VoiceState state() const { return state_; }

  // Smoothed 0..1 level — useful for a live meter while tuning threshold_.
  float smoothedLevel() const { return smoothedLevel_; }

  void setThreshold(float t) { threshold_ = t; }

  // Silence needed, after speech, before an utterance is considered
  // finished (aka SPEECH_END_THRESHOLD / END_OF_SPEECH_SILENCE_MS).
  // The one knob to tune for "responds too slow" vs "cuts me off":
  //   600ms  - too short: ends mid-sentence on any natural thinking
  //            pause, so STT only ever saw the first fragment ("You").
  //   1100ms - safe but felt sluggish — a full extra second of dead
  //            air after every utterance on top of STT/AI/TTS latency.
  //   700ms  - safe, but 700ms of dead air on top of a multi-second
  //            STT/AI/TTS chain made every exchange feel sluggish.
  //   500ms  - current value. Lowered once the capture bug was fixed
  //            (VoiceController used to drop ~1/3 of every utterance,
  //            which made threshold crossings erratic and forced a
  //            longer hangover to compensate). With full capture the
  //            level is stable enough for a shorter window. Raise in
  //            100ms steps if it starts ending sentences early.
  static constexpr uint32_t kHangoverMs  = 500;
  uint32_t hangoverMs() const { return kHangoverMs; }  // for latency reporting

private:
  VoiceState state_ = VoiceState::Silence;
  float smoothedLevel_ = 0.0f;
  // (aka SPEECH_START_THRESHOLD / SPEECH_END_THRESHOLD — this detector
  // uses one symmetric threshold for both, per smoothedLevel_.)
  //
  // 0.12 was tuned against the OLD rms formula, which had no DC-offset
  // removal and so read a DC-biased mic as permanently loud (a silent
  // room measured 0.02-0.09, and worse right after an un-mute). With DC
  // removal the floor dropped ~10x and 0.12 became far too high: it sat
  // in the middle of the QUIET half of normal speech, so trailing
  // syllables fell under it and utterances were cut short. Measured live
  // (open air, pre-enclosure), one sentence at normal speaking distance:
  //   true silence      0.0046-0.0169  (n=9, p90 0.0126)
  //   quiet speech      0.04  -0.13    (word tails, unstressed syllables)
  //   stressed vowels   0.22  -0.38
  // 0.05 was ~3x the loudest measured silence and comfortably under the
  // quiet-speech band at that point.
  //
  // Lowered again once REX was sealed into the 3D-printed case — the
  // enclosure's mic hole measurably attenuates the signal (confirmed live
  // via /mictest: peak speech levels through the case topped out around
  // 0.26-0.28, versus 0.4-0.8+ with the mic fully exposed, roughly half),
  // and real multi-utterance testing showed natural mid-sentence pauses
  // now getting misread as end-of-speech — 2 of 6 questions in one test
  // run arrived as fragments ("The largest" / "...video everyone has
  // ever") split across separate utterances, same failure shape as the
  // original 0.12-era bug, just from reduced headroom instead of a
  // measurement bug this time. The silence floor through the case stayed
  // roughly the same (~0.007-0.02), but quiet-speech moments were now
  // landing closer to it than in open air — a smaller margin than before
  // to tune against. 0.025 sits above the measured silence ceiling with
  // some room, while giving quieter/trailing syllables through the case
  // a real chance to stay counted as speech. Revisit (either direction)
  // based on real use — raise it if ambient case-muffled room noise
  // starts self-triggering, lower it further if clipping persists.
  float threshold_ = 0.025f;
  uint32_t lastAboveThresholdMs_ = 0;
  uint32_t lastUpdateMs_ = 0;
  uint32_t voicedMs_ = 0;  // accumulated time actually ABOVE threshold

  // MIN_SPEECH_DURATION_MS — minimum accumulated VOICED audio before an
  // utterance is worth transcribing.
  //
  // This used to be compared against (now - speechStart), i.e. wall-clock
  // since onset, which made it dead code: the hangover (500ms) is always
  // larger than this, so by the time the silence window expired the
  // "long enough" test had *always* already passed. A single 20ms click
  // therefore produced a fully valid utterance and burned a real STT
  // round trip, which Whisper answered with a hallucinated "You" or
  // "Thank you." It now counts real above-threshold time instead, so a
  // click accumulates ~20ms and is correctly abandoned.
  static constexpr uint32_t kMinSpeechMs = 350;
};
