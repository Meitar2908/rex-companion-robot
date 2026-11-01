#include "EyeAnimator.h"
#include "ExpressionTable.h"
#include <Arduino.h>
#include <math.h>

namespace {

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

float easeInOut(float t) {
  t = clampf(t, 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

EyeShape lerpEyeShape(const EyeShape& a, const EyeShape& b, float t) {
  EyeShape r;
  r.openness    = a.openness    + (b.openness    - a.openness)    * t;
  r.widthMul    = a.widthMul    + (b.widthMul    - a.widthMul)    * t;
  r.lidDropL    = a.lidDropL    + (b.lidDropL    - a.lidDropL)    * t;
  r.lidDropR    = a.lidDropR    + (b.lidDropR    - a.lidDropR)    * t;
  r.happyCurve  = a.happyCurve  + (b.happyCurve  - a.happyCurve)  * t;
  r.bottomLidRise = a.bottomLidRise + (b.bottomLidRise - a.bottomLidRise) * t;
  r.centerXShift = a.centerXShift + (b.centerXShift - a.centerXShift) * t;
  r.pupilBiasX  = a.pupilBiasX  + (b.pupilBiasX  - a.pupilBiasX)  * t;
  r.pupilBiasY  = a.pupilBiasY  + (b.pupilBiasY  - a.pupilBiasY)  * t;
  r.pupilScale  = a.pupilScale  + (b.pupilScale  - a.pupilScale)  * t;
  return r;
}

EyePair lerpEyePair(const EyePair& a, const EyePair& b, float t) {
  return { lerpEyeShape(a.left, b.left, t), lerpEyeShape(a.right, b.right, t) };
}

uint16_t blinkSpeedForPattern(BlinkPattern p) {
  switch (p) {
    case BlinkPattern::Fast: return 60;
    case BlinkPattern::Slow: return 220;
    default:                 return 110;
  }
}

uint8_t blinkRepeatsForPattern(BlinkPattern p) {
  return p == BlinkPattern::Double ? 2 : 1;
}

uint32_t randomBlinkGapMs() {
  return 2200 + (uint32_t)random(3000);   // naturally varying, not a fixed interval
}

} // namespace

void EyeAnimator::begin(uint32_t nowMs) {
  neutralPose_ = EyePair{};
  outputPose_  = neutralPose_;
  basePose_    = neutralPose_;
  targetPose_  = neutralPose_;
  phase_       = Phase::Settled;
  phaseStartMs_ = nowMs;
  nextBlinkMs_  = nowMs + randomBlinkGapMs();
  nextWanderMs_ = nowMs + 3000;
}

void EyeAnimator::computeTargetFromExpression(Expression e, float intensity) {
  const ExpressionDef& def = expressionDef(e);
  intensity = clampf(intensity, 0.0f, 1.0f);

  EyeShape left = lerpEyeShape(EyeShape{}, def.eyeBase, intensity);
  EyeShape right;
  switch (def.symmetry) {
    case EyeSymmetry::Symmetric: right = left; break;
    case EyeSymmetry::Mirrored:  right = lerpEyeShape(EyeShape{}, mirrorEyeShape(def.eyeBase), intensity); break;
    case EyeSymmetry::LeftOnly:  right = EyeShape{}; break;
  }

  targetPose_.left  = left;
  targetPose_.right = right;

  attackMs_  = def.attackMs;
  holdMs_    = def.holdMs;
  releaseMs_ = def.releaseMs;
}

void EyeAnimator::requestBlink(BlinkPattern p, uint32_t nowMs) {
  if (p == BlinkPattern::None) return;
  pendingBlinkPattern_ = p;
  if (blinkPhase_ == BlinkPhase::Idle) nextBlinkMs_ = nowMs;
}

void EyeAnimator::applyReaction(const Reaction& r, uint32_t nowMs) {
  basePose_ = outputPose_;
  computeTargetFromExpression(r.expression, r.intensity);
  sticky_ = false;
  phase_ = Phase::Attack;
  phaseStartMs_ = nowMs;
  desiredGazeX_ = clampf(r.gazeX, -1.0f, 1.0f);
  desiredGazeY_ = clampf(r.gazeY, -1.0f, 1.0f);
  requestBlink(r.blink, nowMs);
}

void EyeAnimator::holdState(Expression e, float intensity, float gazeX, float gazeY,
                             BlinkPattern blink, uint32_t attackMs, uint32_t nowMs) {
  basePose_ = outputPose_;
  computeTargetFromExpression(e, intensity);
  attackMs_ = attackMs;
  sticky_ = true;
  phase_ = Phase::Attack;
  phaseStartMs_ = nowMs;
  desiredGazeX_ = clampf(gazeX, -1.0f, 1.0f);
  desiredGazeY_ = clampf(gazeY, -1.0f, 1.0f);
  requestBlink(blink, nowMs);
}

void EyeAnimator::setIdleWanderEnabled(bool enabled) {
  idleWanderEnabled_ = enabled;
}

void EyeAnimator::setScanningEnabled(bool enabled) {
  scanningEnabled_ = enabled;
  // tickScanning() is the only writer of desiredEyeShiftX_ and simply
  // stops calling in when disabled — without this, whatever mid-sweep
  // value it last held would stick forever instead of the eyes easing
  // back to center. Snapping the TARGET to 0 here lets the existing
  // smoothEyeShiftX_ lerp in update() ease it back out smoothly, same
  // as every other pose transition in this class.
  if (!enabled) desiredEyeShiftX_ = 0.0f;
}

void EyeAnimator::setGazeTarget(float x, float y) {
  desiredGazeX_ = clampf(x, -1.0f, 1.0f);
  desiredGazeY_ = clampf(y, -1.0f, 1.0f);
}

void EyeAnimator::tickGazeWander(uint32_t nowMs) {
  if (!idleWanderEnabled_) return;
  if (nowMs < nextWanderMs_) return;
  nextWanderMs_ = nowMs + 2500 + (uint32_t)random(3500);
  desiredGazeX_ = ((float)random(-100, 101)) / 100.0f * 0.6f;
  desiredGazeY_ = ((float)random(-100, 101)) / 100.0f * 0.4f;
}

void EyeAnimator::tickScanning(uint32_t nowMs) {
  if (!scanningEnabled_) return;
  // Continuous sine sweep rather than wander's random jump-and-hold — a
  // steady repeating left-right motion reads as "watching/listening"
  // where a random drift reads as idle daydreaming. Period/amplitude
  // chosen to look deliberate, not frantic.
  //
  // Drives desiredEyeShiftX_ (the whole eye BODY, via EyeShape::
  // centerXShift), deliberately NOT desiredGazeX_ (which only offsets
  // the pupil within a stationary eye, via pupilBiasX) — REX's whole
  // eyes should visibly slide side to side, not just the pupils inside
  // them.
  constexpr float kPeriodMs  = 3200.0f;
  constexpr float kAmplitude = 1.0f;
  float phase = fmodf((float)nowMs, kPeriodMs) / kPeriodMs;  // 0..1
  desiredEyeShiftX_ = sinf(phase * 2.0f * (float)PI) * kAmplitude;
}

void EyeAnimator::tickBlink(uint32_t nowMs) {
  switch (blinkPhase_) {
    case BlinkPhase::Idle:
      if (nowMs >= nextBlinkMs_) {
        blinkSpeedMs_ = blinkSpeedForPattern(pendingBlinkPattern_);
        blinkRepeatsLeft_ = blinkRepeatsForPattern(pendingBlinkPattern_);
        blinkPhase_ = BlinkPhase::Closing;
        blinkPhaseStartMs_ = nowMs;
      }
      break;

    case BlinkPhase::Closing: {
      float t = (float)(nowMs - blinkPhaseStartMs_) / blinkSpeedMs_;
      if (t >= 1.0f) {
        blinkOpenFactor_ = 0.0f;
        blinkPhase_ = BlinkPhase::Opening;
        blinkPhaseStartMs_ = nowMs;
      } else {
        blinkOpenFactor_ = 1.0f - t;
      }
      break;
    }

    case BlinkPhase::Opening: {
      float t = (float)(nowMs - blinkPhaseStartMs_) / blinkSpeedMs_;
      if (t >= 1.0f) {
        blinkOpenFactor_ = 1.0f;
        blinkRepeatsLeft_--;
        if (blinkRepeatsLeft_ > 0) {
          blinkPhase_ = BlinkPhase::Gap;
          blinkPhaseStartMs_ = nowMs;
        } else {
          blinkPhase_ = BlinkPhase::Idle;
          pendingBlinkPattern_ = BlinkPattern::Single;
          nextBlinkMs_ = nowMs + randomBlinkGapMs();
        }
      } else {
        blinkOpenFactor_ = t;
      }
      break;
    }

    case BlinkPhase::Gap:
      if (nowMs - blinkPhaseStartMs_ >= 90) {
        blinkPhase_ = BlinkPhase::Closing;
        blinkPhaseStartMs_ = nowMs;
      }
      break;
  }
}

void EyeAnimator::update(uint32_t nowMs) {
  EyePair blended;
  switch (phase_) {
    case Phase::Attack: {
      float t = attackMs_ == 0 ? 1.0f : (float)(nowMs - phaseStartMs_) / attackMs_;
      if (t >= 1.0f) {
        blended = targetPose_;
        phase_ = (sticky_ || holdMs_ == 0) ? Phase::StickyHold : Phase::Hold;
        phaseStartMs_ = nowMs;
      } else {
        blended = lerpEyePair(basePose_, targetPose_, easeInOut(t));
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
        blended = lerpEyePair(targetPose_, neutralPose_, easeInOut(t));
      }
      break;
    }
    case Phase::Settled:
      blended = neutralPose_;
      break;
  }

  tickGazeWander(nowMs);
  tickScanning(nowMs);
  smoothGazeX_ += (desiredGazeX_ - smoothGazeX_) * 0.12f;
  smoothGazeY_ += (desiredGazeY_ - smoothGazeY_) * 0.12f;
  smoothEyeShiftX_ += (desiredEyeShiftX_ - smoothEyeShiftX_) * 0.12f;

  blended.left.pupilBiasX  = clampf(blended.left.pupilBiasX  + smoothGazeX_, -1.0f, 1.0f);
  blended.left.pupilBiasY  = clampf(blended.left.pupilBiasY  + smoothGazeY_, -1.0f, 1.0f);
  blended.right.pupilBiasX = clampf(blended.right.pupilBiasX + smoothGazeX_, -1.0f, 1.0f);
  blended.right.pupilBiasY = clampf(blended.right.pupilBiasY + smoothGazeY_, -1.0f, 1.0f);

  // Whole-eye scanning shift — separate channel from pupilBiasX above,
  // see desiredEyeShiftX_'s declaration. Both eyes get the same value
  // (they move in unison, not converging/diverging).
  blended.left.centerXShift  = clampf(blended.left.centerXShift  + smoothEyeShiftX_, -1.0f, 1.0f);
  blended.right.centerXShift = clampf(blended.right.centerXShift + smoothEyeShiftX_, -1.0f, 1.0f);

  tickBlink(nowMs);
  blended.left.openness  *= blinkOpenFactor_;
  blended.right.openness *= blinkOpenFactor_;

  outputPose_ = blended;
}
