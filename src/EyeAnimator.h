// EyeAnimator.h — non-blocking pose blending, blink scheduling, and
// gaze smoothing for the eyes only. Knows nothing about pixels or
// the mouth; it only produces an EyePair each tick for EyeRenderer
// to draw. See MouthAnimator for the mouth's equivalent — the two
// stay separate internally and are synchronized by CharacterController.
#pragma once
#include "EyeTypes.h"

class EyeAnimator {
public:
  void begin(uint32_t nowMs);

  // Call every loop iteration. Advances attack/hold/release blending,
  // blink scheduling, and gaze smoothing. Non-blocking — never delay()s.
  void update(uint32_t nowMs);

  // One-shot reaction: blends in over attackMs, holds, then relaxes
  // back to neutral over releaseMs.
  void applyReaction(const Reaction& r, uint32_t nowMs);

  // Persistent state-driven expression (Listening/Thinking/Speaking):
  // blends in over attackMs and then just stays until the next call
  // to applyReaction() or holdState().
  void holdState(Expression e, float intensity, float gazeX, float gazeY,
                 BlinkPattern blink, uint32_t attackMs, uint32_t nowMs);

  void setIdleWanderEnabled(bool enabled);

  // A slow, continuous side-to-side gaze sweep — used for
  // CharacterState::Listening so REX visibly "scans" while waiting for
  // you to speak. Deliberately separate from idle wander: wander picks
  // a new RANDOM target every few seconds and drifts to it, which reads
  // as idle daydreaming; scanning is a steady, repeating left-right
  // motion, which reads as attentive. The two are mutually exclusive in
  // practice (CharacterController only ever enables one at a time) but
  // nothing here enforces that — whichever tick function runs later in
  // update() wins for that frame.
  void setScanningEnabled(bool enabled);

  // Direct gaze override (debug/testing) — doesn't touch the
  // expression pose at all, just where the pupils are aiming.
  void setGazeTarget(float x, float y);

  const EyePair& pose() const { return outputPose_; }

  // True once a one-shot reaction has fully released back to neutral
  // (or a sticky hold has finished attacking in).
  bool isSettled() const { return phase_ == Phase::Settled || phase_ == Phase::StickyHold; }

private:
  enum class Phase { Attack, Hold, StickyHold, Release, Settled };

  void computeTargetFromExpression(Expression e, float intensity);
  void requestBlink(BlinkPattern p, uint32_t nowMs);
  void tickBlink(uint32_t nowMs);
  void tickGazeWander(uint32_t nowMs);
  void tickScanning(uint32_t nowMs);

  EyePair basePose_{};
  EyePair targetPose_{};
  EyePair neutralPose_{};

  Phase    phase_ = Phase::Settled;
  bool     sticky_ = false;
  uint32_t phaseStartMs_ = 0;
  uint32_t attackMs_ = 200, holdMs_ = 1200, releaseMs_ = 400;

  float desiredGazeX_ = 0.0f, desiredGazeY_ = 0.0f;
  float smoothGazeX_  = 0.0f, smoothGazeY_  = 0.0f;

  // Separate from the gaze/pupil-bias pair above — this drives
  // EyeShape::centerXShift (the whole eye body), not pupilBiasX (the
  // pupil alone within a stationary eye). Only tickScanning() writes to
  // desiredEyeShiftX_; nothing else touches it, so it's always exactly 0
  // whenever scanning isn't enabled.
  float desiredEyeShiftX_ = 0.0f;
  float smoothEyeShiftX_  = 0.0f;

  bool     idleWanderEnabled_ = false;
  uint32_t nextWanderMs_ = 0;

  bool     scanningEnabled_ = false;

  enum class BlinkPhase { Idle, Closing, Opening, Gap };
  BlinkPhase   blinkPhase_ = BlinkPhase::Idle;
  uint32_t     blinkPhaseStartMs_ = 0;
  uint32_t     nextBlinkMs_ = 0;
  uint16_t     blinkSpeedMs_ = 110;
  uint8_t      blinkRepeatsLeft_ = 0;
  BlinkPattern pendingBlinkPattern_ = BlinkPattern::Single;
  float        blinkOpenFactor_ = 1.0f;

  EyePair outputPose_{};
};
