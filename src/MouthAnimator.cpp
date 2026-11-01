#include "MouthAnimator.h"
#include "ExpressionTable.h"
#include <Arduino.h>

namespace {

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

float easeInOut(float t) {
  t = clampf(t, 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

MouthPose lerpMouthPose(const MouthPose& a, const MouthPose& b, float t) {
  MouthPose r;
  r.curvature       = a.curvature       + (b.curvature       - a.curvature)       * t;
  r.asymmetry       = a.asymmetry       + (b.asymmetry       - a.asymmetry)       * t;
  r.waviness        = a.waviness        + (b.waviness        - a.waviness)        * t;
  r.widthMul        = a.widthMul        + (b.widthMul        - a.widthMul)        * t;
  r.verticalOffset  = a.verticalOffset  + (b.verticalOffset  - a.verticalOffset)  * t;
  r.centerXOffset   = a.centerXOffset   + (b.centerXOffset   - a.centerXOffset)   * t;
  return r;
}

constexpr float kSpeechRampMs  = 220.0f;  // start/stop transition (never pops)
constexpr float kWaveSpeedRad  = 4.2f;    // radians/sec — deliberate, not oscilloscope-fast

} // namespace

void MouthAnimator::begin(uint32_t nowMs) {
  // The character's actual resting mouth is a gentle upward curve,
  // not a flat line — see ExpressionTable::restingMouthPose(). Every
  // expression blends from this and releases back to it.
  neutralPose_ = restingMouthPose();
  outputPose_  = neutralPose_;
  basePose_    = neutralPose_;
  targetPose_  = neutralPose_;
  phase_ = Phase::Settled;
  phaseStartMs_ = nowMs;
  lastUpdateMs_ = nowMs;
}

void MouthAnimator::computeTargetFromExpression(Expression e, float intensity) {
  const ExpressionDef& def = expressionDef(e);
  intensity = clampf(intensity, 0.0f, 1.0f);
  targetPose_ = lerpMouthPose(restingMouthPose(), def.mouthBase, intensity);
  attackMs_  = def.attackMs;
  holdMs_    = def.holdMs;
  releaseMs_ = def.releaseMs;
}

void MouthAnimator::applyReaction(const Reaction& r, uint32_t nowMs) {
  basePose_ = outputPose_;
  computeTargetFromExpression(r.expression, r.intensity);
  sticky_ = false;
  phase_ = Phase::Attack;
  phaseStartMs_ = nowMs;
}

void MouthAnimator::holdState(Expression e, float intensity, uint32_t attackMs, uint32_t nowMs) {
  basePose_ = outputPose_;
  computeTargetFromExpression(e, intensity);
  attackMs_ = attackMs;
  sticky_ = true;
  phase_ = Phase::Attack;
  phaseStartMs_ = nowMs;
}

void MouthAnimator::setSpeaking(bool speaking) {
  speaking_ = speaking;
}

void MouthAnimator::update(uint32_t nowMs) {
  uint32_t dtMs = nowMs - lastUpdateMs_;
  lastUpdateMs_ = nowMs;
  if (dtMs > 250) dtMs = 250;  // clamp huge gaps (e.g. after a blocking op elsewhere)

  MouthPose blended;
  switch (phase_) {
    case Phase::Attack: {
      float t = attackMs_ == 0 ? 1.0f : (float)(nowMs - phaseStartMs_) / attackMs_;
      if (t >= 1.0f) {
        blended = targetPose_;
        phase_ = (sticky_ || holdMs_ == 0) ? Phase::StickyHold : Phase::Hold;
        phaseStartMs_ = nowMs;
      } else {
        blended = lerpMouthPose(basePose_, targetPose_, easeInOut(t));
      }
      break;
    }
    case Phase::Hold: {
      blended = targetPose_;
      if (nowMs - phaseStartMs_ >= holdMs_) {
        phase_ = Phase::Release;
        phaseStartMs_ = nowMs;
      }
      break;
    }
    case Phase::StickyHold:
      blended = targetPose_;
      break;
    case Phase::Release: {
      float t = releaseMs_ == 0 ? 1.0f : (float)(nowMs - phaseStartMs_) / releaseMs_;
      if (t >= 1.0f) {
        blended = neutralPose_;
        phase_ = Phase::Settled;
      } else {
        blended = lerpMouthPose(targetPose_, neutralPose_, easeInOut(t));
      }
      break;
    }
    case Phase::Settled:
      blended = neutralPose_;
      break;
  }
  outputPose_ = blended;

  // Speech ramp: smooth 0<->1, never an instant switch.
  float speechTarget = speaking_ ? 1.0f : 0.0f;
  float step = (float)dtMs / kSpeechRampMs;
  if (speechAmount_ < speechTarget) speechAmount_ = min(1.0f, speechAmount_ + step);
  else if (speechAmount_ > speechTarget) speechAmount_ = max(0.0f, speechAmount_ - step);

  if (speechAmount_ > 0.001f) {
    wavePhase_ += kWaveSpeedRad * ((float)dtMs / 1000.0f);
    if (wavePhase_ > 62.83f) wavePhase_ -= 62.83f;  // wrap after many cycles, keeps float small
  }
}
