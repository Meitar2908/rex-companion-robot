#include "VoiceActivityDetector.h"

void VoiceActivityDetector::reset() {
  state_ = VoiceState::Silence;
  smoothedLevel_ = 0.0f;
  lastAboveThresholdMs_ = 0;
  lastUpdateMs_ = 0;
  voicedMs_ = 0;
}

VadEvent VoiceActivityDetector::update(float rmsLevel, uint32_t nowMs) {
  smoothedLevel_ += (rmsLevel - smoothedLevel_) * 0.3f;

  // Time since the previous update, used to accumulate how much audio
  // was genuinely above threshold (see kMinSpeechMs). First call after a
  // reset has no previous sample to measure against, so it contributes 0.
  uint32_t deltaMs = (lastUpdateMs_ == 0) ? 0 : (nowMs - lastUpdateMs_);
  lastUpdateMs_ = nowMs;

  bool above = smoothedLevel_ > threshold_;
  if (above) {
    lastAboveThresholdMs_ = nowMs;
    voicedMs_ += deltaMs;
  }

  if (state_ == VoiceState::Silence) {
    if (above) {
      state_ = VoiceState::Speaking;
      voicedMs_ = 0;
      return VadEvent::SpeechStarted;
    }
    return VadEvent::None;
  }

  // Speaking: end the utterance once the room has been quiet for the
  // full hangover window.
  if ((nowMs - lastAboveThresholdMs_) >= kHangoverMs) {
    state_ = VoiceState::Silence;
    bool enoughVoiced = voicedMs_ >= kMinSpeechMs;
    voicedMs_ = 0;
    return enoughVoiced ? VadEvent::SpeechEnded : VadEvent::SpeechAborted;
  }
  return VadEvent::None;
}
