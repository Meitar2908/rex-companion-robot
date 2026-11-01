// WakeWordDetector.h — always-on "Hey REX" listener.
//
// Runs a small on-device neural network (TFLite Micro) continuously over
// whatever the mic captures, so REX can respond to being addressed by name
// the whole time it's powered on, instead of requiring the physical button
// for every single turn. The button still works as a manual override — this
// is an additional way in, not a replacement (see VoiceController).
//
// Loaded with a custom-trained "Hey REX" model (WakeWordModel.h), trained
// via the microWakeWord Colab notebook in
// notebooks/basic_training_notebook.ipynb. Proved out first against
// "Hey Jarvis", one of the free, open, pre-trained microWakeWord models,
// to validate this entire pipeline (audio front-end, INT8 TFLite Micro
// inference, the always-listening integration) on real REX hardware before
// the custom model existed.
//
// Everything below was verified against the actual model file (read
// directly with Python's `tflite` flatbuffer bindings, not assumed) and the
// actual vendored TFLite Micro / microfrontend sources before being
// written:
//   - Input tensor:  [1,3,40] int8,  scale=0.10196078568696976, zero_point=-128
//     (3 stacked 40-value feature frames per Invoke() call — mixednet's
//     default stride; the "Hey Jarvis" stand-in used a single-frame
//     [1,1,40] input, so feed() below accumulates 3 frames before running
//     inference)
//   - Output tensor: [1,1]    uint8, scale=0.00390625, zero_point=0
//   - Ops used: ASSIGN_VARIABLE, CALL_ONCE, CONCATENATION, CONV_2D,
//     DEPTHWISE_CONV_2D, FULLY_CONNECTED, LOGISTIC, QUANTIZE, READ_VARIABLE,
//     RESHAPE, SPLIT_V, STRIDED_SLICE, VAR_HANDLE — this is a genuinely
//     STATEFUL "streaming" model (the VAR_HANDLE/*_VARIABLE ops carry
//     temporal context between Invoke() calls via TFLite's
//     resource-variable mechanism), which is why the interpreter is
//     constructed with an explicit MicroResourceVariables — without it
//     these ops either fail to allocate or silently don't persist state,
//     and the model would never accumulate the context it needs across a
//     spoken phrase.
//   - Feature quantization formula confirmed from TWO independent sources
//     (ESPHome's own production micro_wake_word component, and an
//     unrelated from-scratch reimplementation) landing on the identical
//     constants — see the .cpp. This is microWakeWord's own fixed
//     training-time normalization convention, independent of this specific
//     model's own tensor scale/zero_point, so it didn't need to change when
//     the model did.
#pragma once
#include <stdint.h>
#include <stddef.h>

class WakeWordDetector {
public:
  // Loads the embedded model, builds the TFLite Micro interpreter and the
  // audio front-end. Call once in setup(). Returns false if anything failed
  // (schema mismatch, tensor arena too small, a missing op) — the Serial
  // log says exactly what and, for an undersized arena, exactly how many
  // bytes are actually needed (TFLite Micro's own allocator reports this).
  // Callers should treat false as "wake word unavailable this session, but
  // don't crash" — the physical button still works either way.
  bool begin();

  bool ready() const { return inited_; }

  // Feed one chunk of mono 16-bit PCM at kSampleRate — call this
  // continuously with whatever the mic captures while wake-word listening
  // is active. Internally batches samples into the front-end's fixed
  // analysis window and only runs inference once a full new window is
  // ready, so it's cheap to call with small chunks. Returns true the
  // instant "Hey REX" is detected; the caller should then start the normal
  // VAD/STT/AI pipeline exactly as if the button had been pressed.
  bool feed(const int16_t* samples, size_t sampleCount);

  // Debug: the most recent single-frame probability (0..1), for tuning
  // sensitivity from Serial without needing to wait for an actual trigger.
  float lastProbability() const { return lastProbability_; }

  static constexpr uint32_t kSampleRate = 16000;  // must match AudioInput::kSampleRate

private:
  bool runInference(const int8_t* featuresInt8);

  bool inited_ = false;
  float lastProbability_ = 0.0f;

  // This model's input is 3 stacked feature frames, not 1 (see the comment
  // block above) — feed() fills this buffer one 40-value frame at a time
  // and only calls runInference() once it holds a full set of 3.
  static constexpr int kFeatureFrameBytes = 40;
  static constexpr int kFramesPerInference = 3;
  int8_t frameBuffer_[kFeatureFrameBytes * kFramesPerInference] = {0};
  int framesBuffered_ = 0;

  // Rolling average over the last N per-frame probabilities. History:
  // started as the "Hey Jarvis" stand-in model's own manifest values
  // (0.5 cutoff / 10 frames) -> raised to 0.6/14 after one observed false
  // positive (0.545 avg on an ordinary sentence) -> raised again to
  // 0.75/20 after that still persisted in extended ambient conversation.
  // That last jump (20 frames = ~600ms) turned out to be the wrong lever:
  // live-captured probe data (single-frame probability AND the live
  // average, printed side by side — see runInference()) showed genuine,
  // clearly-recognized "Hey REX" attempts (per-frame confidence hitting
  // 0.9-0.98) failing to trigger because a normally-paced saying only
  // sustains that confidence for ~150-250ms (5-8 inferences), nowhere near
  // long enough to pull a 600ms-wide average over 0.75 — three confirmed
  // live misses (peak averages 0.509, 0.301, 0.275) despite the model
  // clearly recognizing the phrase each time. The window was simply wider
  // than how quickly people actually say it, independent of the cutoff
  // value. Narrowed back down well below even the original 14 (10 frames
  // = ~300ms, close to the burst length actually observed) so a normal-
  // paced saying carries enough weight in the average to cross threshold,
  // with cutoff raised only modestly (0.6->0.65, not 0.75) since a
  // shorter window alone should already cut false positives by requiring
  // sustained — not just momentary — confidence. Still a judgment call
  // pending more live data in both directions (missed triggers AND false
  // positives) — revisit either way based on real use.
  static constexpr int kSlidingWindowSize = 10;
  static constexpr float kProbabilityCutoff = 0.65f;
  float recentProbs_[kSlidingWindowSize] = {0};
  int   recentProbsIdx_ = 0;
  int   recentProbsCount_ = 0;

  // Once triggered, ignore further detections for a bit — otherwise the
  // tail of "Hey Jarvis" itself, or the user's very next word, can
  // re-trigger before the normal VAD/STT/AI pipeline has even started
  // listening for the actual command.
  uint32_t lastTriggerMs_ = 0;
  static constexpr uint32_t kRetriggerCooldownMs = 1500;
};
