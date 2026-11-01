#include "CharacterController.h"
#include "ExpressionTable.h"
#include <Arduino.h>  // random(), and Serial for tickIdleBehavior()'s debug line

namespace {
// Idle fidget (Tier 1) — deliberately excludes anything that would read
// as alarming or confused without a real cause (Angry, Scared, Sad,
// Worried, Frustrated, Annoyed): these should look like REX is quietly
// present and a little curious about the room, not reacting to
// something. Picked at low intensity, one-shot (auto-releases).
constexpr Expression kIdleFidgetPool[] = {
  Expression::Curious, Expression::Amused, Expression::Thinking, Expression::Unimpressed,
};
constexpr size_t kIdleFidgetPoolCount = sizeof(kIdleFidgetPool) / sizeof(kIdleFidgetPool[0]);

constexpr uint32_t kIdleFidgetMinMs   = 25000;   // floor before another fidget can fire
constexpr uint32_t kIdleFidgetRangeMs = 45000;   // + up to this much more (25-70s total)
constexpr uint32_t kDozeAfterMs       = 180000;  // 3 min of continuous idle -> Sleepy
} // namespace

void CharacterController::begin(FaceFrame* frame, uint32_t nowMs) {
  frame_ = frame;
  eyeAnimator_.begin(nowMs);
  mouthAnimator_.begin(nowMs);
  eyeAnimator_.setIdleWanderEnabled(true);
  state_ = CharacterState::Idle;
  lastRenderMs_ = nowMs;
#ifdef FACE_RENDER_DEBUG
  debugLastReportMs_ = nowMs;
#endif
}

void CharacterController::onReaction(const Reaction& r, uint32_t nowMs) {
  state_ = CharacterState::Reacting;
  eyeAnimator_.setIdleWanderEnabled(false);
  eyeAnimator_.setScanningEnabled(false);
  eyeAnimator_.applyReaction(r, nowMs);
  mouthAnimator_.applyReaction(r, nowMs);
}

void CharacterController::setGazeOverride(float x, float y) {
  eyeAnimator_.setGazeTarget(x, y);
}

void CharacterController::setBaselineEmotion(Expression e, float intensity, uint32_t nowMs) {
  const ExpressionDef& def = expressionDef(e);
  eyeAnimator_.holdState(e, intensity, def.gazeX, def.gazeY, def.blink, def.attackMs, nowMs);
  mouthAnimator_.holdState(e, intensity, def.attackMs, nowMs);
}

void CharacterController::setMouthOnly(Expression e, float intensity, uint32_t nowMs) {
  const ExpressionDef& def = expressionDef(e);
  mouthAnimator_.holdState(e, intensity, def.attackMs, nowMs);
}

void CharacterController::setState(CharacterState s, uint32_t nowMs) {
  state_ = s;

  switch (s) {
    case CharacterState::Idle: {
      eyeAnimator_.setIdleWanderEnabled(true);
      eyeAnimator_.setScanningEnabled(false);
      eyeAnimator_.holdState(Expression::Neutral, 0.0f, 0.0f, 0.0f, BlinkPattern::None, 400, nowMs);
      mouthAnimator_.holdState(Expression::Neutral, 0.0f, 400, nowMs);
      mouthAnimator_.setSpeaking(false);
      break;
    }
    // Listening (waiting for you to speak) and Thinking (STT/AI in
    // flight) are drawn identically on purpose — squinting eyes doing a
    // continuous left-right scan, mouth held flat — so nothing about the
    // face changes across that boundary; the distinction is internal
    // only (state_ above still records which one this is for callers
    // like VoiceController/main.cpp). Expression::Thinking's own look
    // (off-center gaze, restrained mouth) is untouched and still used
    // elsewhere, e.g. ReactionEngine's "hmm"/"let me think" -> onReaction.
    case CharacterState::Listening:
    case CharacterState::Thinking: {
      eyeAnimator_.setIdleWanderEnabled(false);
      // Scanning drives gazeX continuously from here on (see
      // EyeAnimator::tickScanning) — the def.gazeX passed to holdState()
      // below only matters for the single frame before that kicks in.
      // Stops only in onReaction()/Speaking below, i.e. once REX
      // actually has something to say.
      eyeAnimator_.setScanningEnabled(true);
      const ExpressionDef& def = expressionDef(Expression::Listening);
      eyeAnimator_.holdState(Expression::Listening, 0.7f, def.gazeX, def.gazeY,
                              def.blink, def.attackMs, nowMs);
      mouthAnimator_.holdState(Expression::Listening, 0.7f, def.attackMs, nowMs);
      mouthAnimator_.setSpeaking(false);
      break;
    }
    case CharacterState::Speaking:
      // Deliberately does NOT change the current expression on either
      // eyes or mouth — the face keeps whatever emotional reaction is
      // already active while talking. Only the mouth's speech
      // waveform ramps in, layered on top of that baseline curve.
      eyeAnimator_.setIdleWanderEnabled(false);
      eyeAnimator_.setScanningEnabled(false);
      mouthAnimator_.setSpeaking(true);
      break;
    case CharacterState::Reacting:
      // Entered via onReaction(), not directly.
      break;
  }
}

void CharacterController::update(uint32_t nowMs) {
  // Animation logic runs every call — time-based (elapsed ms), never
  // tied to how many times loop() happens to iterate.
  eyeAnimator_.update(nowMs);
  mouthAnimator_.update(nowMs);

  // Actual TFT pushes are throttled and always compose a COMPLETE
  // frame off-screen first — see FaceFrame's header for why.
  if (frame_ && nowMs - lastRenderMs_ >= Geometry::kFrameIntervalMs) {
    lastRenderMs_ = nowMs;

#ifdef FACE_RENDER_DEBUG
    uint32_t t0 = micros();
#endif

    frame_->beginFrame();
    eyeRenderer_.render(frame_->canvas(), frame_->originX(), frame_->originY(), eyeAnimator_.pose());
    mouthRenderer_.render(frame_->canvas(), frame_->originX(), frame_->originY(),
                           mouthAnimator_.emotionalPose(), mouthAnimator_.speechAmount(),
                           mouthAnimator_.wavePhase());
    frame_->pushFrame();

#ifdef FACE_RENDER_DEBUG
    debugLastFrameUs_ = micros() - t0;
    debugFrameCount_++;
    if (nowMs - debugLastReportMs_ >= 1000) {
      float fps = debugFrameCount_ * 1000.0f / (nowMs - debugLastReportMs_);
      Serial.print("[FACE_DEBUG] fps="); Serial.print(fps, 1);
      Serial.print(" frameUs="); Serial.print(debugLastFrameUs_);
      Serial.print(" spriteBytes="); Serial.println(Geometry::kFaceW * Geometry::kFaceH * 2);
      debugFrameCount_ = 0;
      debugLastReportMs_ = nowMs;
    }
#endif
  }

  if (state_ == CharacterState::Reacting && eyeAnimator_.isSettled() && mouthAnimator_.isSettled()) {
    state_ = CharacterState::Idle;
    eyeAnimator_.setIdleWanderEnabled(true);
    fidgetInFlight_ = false;  // if this settle was our own idle fidget, it's done
  }

  tickIdleBehavior(nowMs);
}

// "REX does something when it feels like it" — two tiers, both only
// while state_ is genuinely Idle (never interrupts a real conversation):
//   Tier 1: an occasional small, low-intensity micro-expression every
//     ~25-70s so REX reads as quietly present rather than a frozen
//     photo between idle wander/blink, which already run continuously
//     but are subtle enough to not really register as "doing something".
//   Tier 2: after several minutes of genuinely uninterrupted idle,
//     drift into Sleepy and just hold it (sticky, via setBaselineEmotion)
//     until real activity resumes — a button press alone doesn't count
//     (VoiceController::setListening() deliberately doesn't touch
//     CharacterController — see its own header comment), actual detected
//     speech does, same as any other interaction.
//
// idleSinceMs_ tracks continuous idle time, but must NOT be reset by our
// OWN Tier-1 fidgets (onReaction() always flips state_ to Reacting,
// indistinguishable from a real reaction from the outside) — that's what
// fidgetInFlight_ is for: only genuine non-Idle activity (Listening/
// Thinking/Speaking, or a Reacting that ISN'T our own fidget) resets it.
void CharacterController::tickIdleBehavior(uint32_t nowMs) {
  if (state_ != CharacterState::Idle) {
    bool isOwnFidget = (state_ == CharacterState::Reacting) && fidgetInFlight_;
    if (!isOwnFidget) {
      idleSinceMs_ = 0;
      dozing_ = false;
    }
    return;
  }

  if (idleSinceMs_ == 0) {
    idleSinceMs_ = nowMs;
    nextIdleFidgetMs_ = nowMs + kIdleFidgetMinMs + (uint32_t)random(kIdleFidgetRangeMs);
    return;
  }

  if (!dozing_ && (nowMs - idleSinceMs_) >= kDozeAfterMs) {
    dozing_ = true;
    Serial.println("[IDLE] neglected long enough to doze off");
    setBaselineEmotion(Expression::Sleepy, 0.55f, nowMs);
    return;
  }

  if (!dozing_ && nowMs >= nextIdleFidgetMs_) {
    Expression e = kIdleFidgetPool[random(kIdleFidgetPoolCount)];
    Reaction r;
    r.expression = e;
    r.intensity  = 0.30f + (random(16) / 100.0f);  // 0.30-0.45, kept subtle
    const ExpressionDef& def = expressionDef(e);
    r.gazeX = def.gazeX; r.gazeY = def.gazeY; r.blink = def.blink;
    r.attackMs = def.attackMs; r.holdMs = def.holdMs; r.releaseMs = def.releaseMs;
    fidgetInFlight_ = true;
    onReaction(r, nowMs);
    nextIdleFidgetMs_ = nowMs + kIdleFidgetMinMs + (uint32_t)random(kIdleFidgetRangeMs);
  }
}
