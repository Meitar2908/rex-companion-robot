#include "WakeWordDetector.h"
#include "WakeWordModel.h"
#include <Arduino.h>
#include <string.h>

// Pulled directly from EdgeNeuron's vendored TFLite Micro tree — used
// directly rather than through EdgeNeuron.h's own wrapper, because that
// wrapper (setModelInput/getModelOutput) only handles float32 tensors
// (tensor->data.f[index]). This model's tensors are INT8/UINT8-quantized
// (confirmed against the actual file — see WakeWordDetector.h), which that
// wrapper would silently misread rather than reject.
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_resource_variable.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/experimental/microfrontend/lib/frontend.h"
#include "tensorflow/lite/experimental/microfrontend/lib/frontend_util.h"

namespace {

// Exactly the 13 ops this specific model uses (verified by reading its
// operator_codes directly with Python's `tflite` flatbuffer bindings before
// this swap — see WakeWordDetector.h), not AllOpsResolver's
// everything-just-in-case approach, which costs meaningfully more flash for
// no benefit here. The real "Hey REX" model uses DEPTHWISE_CONV_2D and
// SPLIT_V where the "Hey Jarvis" stand-in used ADD and MUL — if a future
// swapped-in model needs an op not listed here, AllocateTensors() below
// fails with a clear "didn't find op" error naming the missing op — add it
// here.
constexpr int kNumOps = 13;
tflite::MicroMutableOpResolver<kNumOps> BuildOpResolver() {
  tflite::MicroMutableOpResolver<kNumOps> resolver;
  resolver.AddAssignVariable();
  resolver.AddCallOnce();
  resolver.AddConcatenation();
  resolver.AddConv2D();
  resolver.AddDepthwiseConv2D();
  resolver.AddFullyConnected();
  resolver.AddLogistic();
  resolver.AddQuantize();
  resolver.AddReadVariable();
  resolver.AddReshape();
  resolver.AddSplitV();
  resolver.AddStridedSlice();
  resolver.AddVarHandle();
  return resolver;
}

// Measured on real hardware, not estimated: a first attempt at 40KB failed
// with "Failed to resize buffer. Requested: 21216, available 15224,
// missing: 5992" — i.e. TFLite Micro's own allocator reported exactly how
// far short 40KB was. 64KB clears that with real margin for the allocator's
// other internal bookkeeping (that error is from one specific buffer
// resize, not necessarily the interpreter's total requirement) while still
// being plain internal RAM — comfortably inside REX's ~270KB of free
// internal RAM, which sidesteps a documented ESPHome issue with these
// models' tensor arenas failing to allocate correctly in PSRAM on this
// exact N16R8 board. If a future swapped-in model needs more, the same
// error will say exactly how much more.
constexpr int kTensorArenaSize = 64 * 1024;
alignas(16) uint8_t g_tensorArena[kTensorArenaSize];

// Covers the model's ~11 "stream_NN/states" resource-variable tensors
// (the streaming conv layers' carried-over context) with headroom.
constexpr int kMaxResourceVariables = 16;

tflite::MicroInterpreter* g_interpreter = nullptr;
TfLiteTensor* g_inputTensor = nullptr;
TfLiteTensor* g_outputTensor = nullptr;
FrontendState g_frontendState;
// Kept alive past begin() specifically so runInference() can call
// ResetAll() after a trigger — see the reset call for why.
tflite::MicroResourceVariables* g_resourceVars = nullptr;

} // namespace

bool WakeWordDetector::begin() {
  const tflite::Model* model = tflite::GetModel(kWakeWordModelData);
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    Serial.print("[WAKE] model schema version mismatch: got ");
    Serial.print(model->version());
    Serial.print(", runtime expects "); Serial.println(TFLITE_SCHEMA_VERSION);
    return false;
  }

  static tflite::MicroMutableOpResolver<kNumOps> resolver = BuildOpResolver();

  tflite::MicroAllocator* allocator =
      tflite::MicroAllocator::Create(g_tensorArena, kTensorArenaSize);
  if (!allocator) {
    Serial.println("[WAKE] failed to create MicroAllocator");
    return false;
  }
  tflite::MicroResourceVariables* resourceVars =
      tflite::MicroResourceVariables::Create(allocator, kMaxResourceVariables);
  if (!resourceVars) {
    Serial.println("[WAKE] failed to create MicroResourceVariables");
    return false;
  }
  g_resourceVars = resourceVars;

  static tflite::MicroInterpreter interpreter(model, resolver, allocator, resourceVars);
  g_interpreter = &interpreter;

  if (g_interpreter->AllocateTensors() != kTfLiteOk) {
    Serial.println("[WAKE] AllocateTensors failed — see the line above for "
                    "the exact bytes needed if it was an arena-size error, "
                    "or the missing op name if not; adjust kTensorArenaSize "
                    "or BuildOpResolver() above accordingly");
    return false;
  }

  g_inputTensor = g_interpreter->input(0);
  g_outputTensor = g_interpreter->output(0);
  if (!g_inputTensor || !g_outputTensor) {
    Serial.println("[WAKE] couldn't get input/output tensors");
    return false;
  }
  // Defensive: if a later swapped-in model has a different shape than the
  // one this code was written against, fail loudly here rather than
  // silently feed it the wrong number of features and get garbage output.
  // The real "Hey REX" model's input is [1,3,40] (3 stacked 40-value
  // feature frames per Invoke() — mixednet's default stride), not the
  // single-frame [1,1,40] the "Hey Jarvis" stand-in used.
  if (g_inputTensor->bytes != kFeatureFrameBytes * kFramesPerInference ||
      g_outputTensor->bytes != 1) {
    Serial.print("[WAKE] unexpected tensor size — input bytes=");
    Serial.print(g_inputTensor->bytes);
    Serial.print(" output bytes="); Serial.println(g_outputTensor->bytes);
    Serial.println("[WAKE] this model's shape differs from what this code "
                    "was written for (expects 3 stacked 40-value int8 "
                    "feature frames, single uint8 output) — the feature/"
                    "quantization code below needs updating to match.");
    return false;
  }

  // Every one of these fields, exactly, from ESPHome's own production
  // micro_wake_word component's preprocessor_settings.h — first-attempt
  // code here only set window.size_ms/step_size_ms/num_channels and left
  // everything else (crucially: PCAN gain control and log-scale, both
  // enable/disable toggles) at this generic library's own defaults, which
  // do NOT match what these models were trained against. Confirmed live:
  // that left real, clearly-spoken audio (peak RMS 0.28-0.33, well into
  // this project's own established "that's speech" range) producing only
  // ~0.01 probability — signal was reaching the model, just not shaped the
  // way it was trained to expect. This mismatch, not an audio-capture
  // problem, was the actual bug. step_size_ms was also simply wrong
  // (20ms — flagged as unconfirmed in the version that shipped that bug —
  // the real, confirmed value is 10ms, and a wrong stride alone would
  // scramble the timing a streaming model relies on for its carried-over
  // state to mean anything).
  FrontendConfig frontendConfig;
  FrontendFillConfigWithDefaults(&frontendConfig);
  frontendConfig.window.size_ms = 30;
  frontendConfig.window.step_size_ms = 10;
  frontendConfig.filterbank.num_channels = 40;
  frontendConfig.filterbank.lower_band_limit = 125.0f;
  frontendConfig.filterbank.upper_band_limit = 7500.0f;
  frontendConfig.noise_reduction.smoothing_bits = 10;
  frontendConfig.noise_reduction.even_smoothing = 0.025f;
  frontendConfig.noise_reduction.odd_smoothing = 0.06f;
  frontendConfig.noise_reduction.min_signal_remaining = 0.05f;
  frontendConfig.pcan_gain_control.enable_pcan = 1;
  frontendConfig.pcan_gain_control.strength = 0.95f;
  frontendConfig.pcan_gain_control.offset = 80.0f;
  frontendConfig.pcan_gain_control.gain_bits = 21;
  frontendConfig.log_scale.enable_log = 1;
  frontendConfig.log_scale.scale_shift = 6;
  if (!FrontendPopulateState(&frontendConfig, &g_frontendState, WakeWordDetector::kSampleRate)) {
    Serial.println("[WAKE] FrontendPopulateState failed");
    return false;
  }

  inited_ = true;
  Serial.println("[WAKE] ready (Hey REX)");
  return true;
}

bool WakeWordDetector::feed(const int16_t* samples, size_t sampleCount) {
  if (!inited_) return false;

  bool detected = false;
  size_t offset = 0;
  while (offset < sampleCount) {
    size_t samplesRead = 0;
    FrontendOutput out = FrontendProcessSamples(
        &g_frontendState, samples + offset, sampleCount - offset, &samplesRead);
    offset += samplesRead;
    if (samplesRead == 0) break;  // front-end wants more samples than we have left this call
    if (out.size == 0) continue; // consumed samples, but not a full window yet

    if ((int)out.size != kFeatureFrameBytes) {
      // Shouldn't happen given num_channels=40 above, but don't quantize
      // into the wrong number of input slots if it somehow does.
      continue;
    }

    // Canonical feature -> int8 conversion, confirmed against TWO
    // independent implementations (ESPHome's own production micro_wake_word
    // component, and an unrelated from-scratch reimplementation) landing on
    // the identical constants — this is NOT the tensor's own scale/
    // zero_point applied directly; it's microWakeWord's own training-time
    // feature normalization convention, which the model's quantization was
    // calibrated against.
    int8_t* slot = frameBuffer_ + framesBuffered_ * kFeatureFrameBytes;
    for (int i = 0; i < kFeatureFrameBytes; i++) {
      int32_t value = ((int32_t)out.values[i] * 256 + 333) / 666;
      value += -128;
      if (value < -128) value = -128;
      if (value > 127) value = 127;
      slot[i] = (int8_t)value;
    }
    framesBuffered_++;

    // This model's input is 3 stacked frames (see WakeWordDetector.h) —
    // only run inference once the buffer is full, then start the next
    // group of 3 from scratch. Non-overlapping, not a sliding window: the
    // model's own carried-over resource-variable state (VAR_HANDLE/
    // ASSIGN_VARIABLE/READ_VARIABLE) is what tracks context across groups,
    // the same way it already does across individual Invoke() calls.
    if (framesBuffered_ == kFramesPerInference) {
      framesBuffered_ = 0;
      if (runInference(frameBuffer_)) detected = true;
    }
  }
  return detected;
}

bool WakeWordDetector::runInference(const int8_t* featuresInt8) {
  memcpy(g_inputTensor->data.int8, featuresInt8, kFeatureFrameBytes * kFramesPerInference);

  if (g_interpreter->Invoke() != kTfLiteOk) {
    Serial.println("[WAKE] Invoke failed");
    return false;
  }

  // Output tensor is UINT8 (confirmed: TFLite type=3), NOT int8 — reading
  // it as data.int8 here would be the exact same class of bug EdgeNeuron's
  // own wrapper has for float, just one tensor over. scale=0.00390625 is
  // exactly 1/256, so this dequantizes to a clean 0..1 probability.
  uint8_t rawOut = g_outputTensor->data.uint8[0];
  float probability = g_outputTensor->params.scale *
                       ((int32_t)rawOut - g_outputTensor->params.zero_point);
  lastProbability_ = probability;

  recentProbs_[recentProbsIdx_] = probability;
  recentProbsIdx_ = (recentProbsIdx_ + 1) % kSlidingWindowSize;
  if (recentProbsCount_ < kSlidingWindowSize) recentProbsCount_++;

  float sum = 0.0f;
  for (int i = 0; i < recentProbsCount_; i++) sum += recentProbs_[i];
  float average = sum / (float)recentProbsCount_;

  uint32_t nowMs = millis();
  bool cooldownElapsed = (nowMs - lastTriggerMs_) >= kRetriggerCooldownMs;
  if (recentProbsCount_ >= kSlidingWindowSize && average >= kProbabilityCutoff && cooldownElapsed) {
    lastTriggerMs_ = nowMs;
    // Resetting recentProbsCount_/Idx_ together keeps the sliding-window
    // array's write position and summed range aligned (a real bug, but
    // fixing ONLY this did not stop the re-trigger below — see why).
    recentProbsCount_ = 0;
    recentProbsIdx_ = 0;
    // THE ACTUAL CAUSE (confirmed live — the array fix alone reproduced
    // the exact same instant-0.996-no-ramp-up re-trigger): this model is
    // explicitly stateful (VAR_HANDLE/ASSIGN_VARIABLE/READ_VARIABLE ops —
    // see this file's header comment), carrying ~11 "stream_NN/states"
    // resource-variable tensors as its own internal memory across
    // Invoke() calls, independent of anything in this .cpp. That memory
    // is never touched by resetting our own bookkeeping above. After a
    // trigger, feed() isn't called again for the whole listening_=true
    // conversation (gated by !listening_ in StreamingVoiceController), so
    // the model's internal state sits frozen exactly as it was the
    // instant it recognized "Hey REX" — primed, not neutral. The moment
    // polling resumes, minutes-old "Hey REX" memory alone was enough to
    // re-trigger with nothing actually said. ResetAll() zeroes those
    // buffers so the model starts genuinely fresh next time, same as a
    // cold boot.
    if (g_resourceVars) g_resourceVars->ResetAll();
    Serial.print("[WAKE] detected! avg probability="); Serial.println(average, 3);
    return true;
  }
  // TEMP DIAGNOSTIC — single-frame probability spikes near 1.0 on a clear
  // genuine "Hey REX" (confirmed live), but the trigger decision above
  // uses the SLIDING-WINDOW AVERAGE, not this raw value — printing both
  // together is what actually shows whether kSlidingWindowSize (now 20
  // frames = ~600ms) is diluting a short phrase's high-confidence region
  // below kProbabilityCutoff, rather than guessing at another threshold
  // blind. Only logs when the raw value is notably elevated, to avoid
  // spamming on ordinary silence/background noise.
  if (probability > 0.3f) {
    Serial.print("[WAKE] probe p="); Serial.print(probability, 3);
    Serial.print(" avg="); Serial.println(average, 3);
  }
  return false;
}
