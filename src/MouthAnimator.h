// MouthAnimator.h — non-blocking blending of the mouth's emotional
// curve, plus the separate "how much speech waveform is showing"
// state. Deliberately mirrors EyeAnimator's attack/hold/release
// pattern but stays its own small class rather than a shared
// template — the two are simple enough that generalizing them would
// cost more clarity than it saves.
//
// Speaking is modelled as an ADDITIVE layer, not a pose swap: the
// emotional MouthPose keeps blending/holding exactly as it would
// without speech, and MouthRenderer adds a waveform on top scaled by
// speechAmount(). That's what keeps a sad reaction looking sad while
// the character talks, instead of every sentence using one generic
// waveform shape.
#pragma once
#include "EyeTypes.h"

class MouthAnimator {
public:
  void begin(uint32_t nowMs);
  void update(uint32_t nowMs);

  void applyReaction(const Reaction& r, uint32_t nowMs);
  void holdState(Expression e, float intensity, uint32_t attackMs, uint32_t nowMs);

  // Smoothly ramps the speech waveform in/out — never snaps.
  void setSpeaking(bool speaking);

  const MouthPose& emotionalPose() const { return outputPose_; }
  float speechAmount() const { return speechAmount_; }
  float wavePhase() const { return wavePhase_; }

  bool isSettled() const { return phase_ == Phase::Settled || phase_ == Phase::StickyHold; }

private:
  enum class Phase { Attack, Hold, StickyHold, Release, Settled };

  void computeTargetFromExpression(Expression e, float intensity);

  MouthPose basePose_{};
  MouthPose targetPose_{};
  MouthPose neutralPose_{};
  MouthPose outputPose_{};

  Phase    phase_ = Phase::Settled;
  bool     sticky_ = false;
  uint32_t phaseStartMs_ = 0;
  uint32_t attackMs_ = 200, holdMs_ = 1200, releaseMs_ = 400;

  bool     speaking_ = false;
  float    speechAmount_ = 0.0f;
  float    wavePhase_ = 0.0f;
  uint32_t lastUpdateMs_ = 0;
};
