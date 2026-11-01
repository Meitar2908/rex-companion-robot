// CharacterController.h — the conversation-level state machine
// (Idle/Listening/Thinking/Reacting/Speaking) AND the owner of the
// render pipeline that composes one complete frame (both eyes +
// mouth) before pushing it to the display. Deliberately separate
// from EyeAnimator/MouthAnimator's internal attack/hold/release
// animation phases — this class decides WHAT the face should be
// doing; the animators decide HOW to blend toward it smoothly.
//
// Render pipeline (see update()):
//   animators tick every call (time-based, not iteration-based)
//   actual TFT pushes are throttled to Geometry::kFrameIntervalMs
//   each push composes the WHOLE face into FaceFrame's off-screen
//   canvas first — nothing reaches the panel until it's complete.
//
// Knows nothing about TFT pixels beyond that and nothing about text
// parsing — it only consumes Reaction values and CharacterState
// transitions, so a future microphone/AI pipeline can drive it
// exactly the way the Serial prototype does.
#pragma once
#include "EyeTypes.h"
#include "EyeAnimator.h"
#include "MouthAnimator.h"
#include "EyeRenderer.h"
#include "MouthRenderer.h"
#include "FaceFrame.h"

class CharacterController {
public:
  void begin(FaceFrame* frame, uint32_t nowMs);
  void update(uint32_t nowMs);

  // A classified Reaction arrived (from ReactionEngine today; from a
  // conversational-AI response later) — play it as a one-shot.
  void onReaction(const Reaction& r, uint32_t nowMs);

  // External state transitions (future: VAD/speech events; today:
  // driven by the Serial prototype's STATE:/ /speak /stop commands).
  void setState(CharacterState s, uint32_t nowMs);

  // Debug-only direct gaze override (/look x y).
  void setGazeOverride(float x, float y);

  // Sets the active baseline expression WITHOUT changing CharacterState,
  // and — unlike onReaction()'s one-shot attack/hold/RELEASE — holds it
  // indefinitely until something else moves it on. Originally debug-only
  // ("/speak <emotion>"), now also the real sticky hold for an AI reply
  // that's about to be spoken (VoiceController) and for the idle-doze
  // behavior below (see tickIdleBehavior()).
  void setBaselineEmotion(Expression e, float intensity, uint32_t nowMs);

  // Debug-only: drives the mouth ALONE, leaving the eyes untouched —
  // for evaluating mouth shapes independently ("/mouth <name>").
  void setMouthOnly(Expression e, float intensity, uint32_t nowMs);

  CharacterState state() const { return state_; }

private:
  EyeAnimator    eyeAnimator_;
  MouthAnimator  mouthAnimator_;
  EyeRenderer    eyeRenderer_;
  MouthRenderer  mouthRenderer_;
  FaceFrame*     frame_ = nullptr;
  CharacterState state_ = CharacterState::Idle;

  uint32_t lastRenderMs_ = 0;

  // Idle ambient behavior — "REX does something when it feels like it"
  // rather than sitting frozen between conversations. See
  // tickIdleBehavior() in the .cpp for the actual timing/thresholds.
  void tickIdleBehavior(uint32_t nowMs);
  uint32_t idleSinceMs_       = 0;      // 0 = not currently idle
  uint32_t nextIdleFidgetMs_  = 0;
  bool     dozing_            = false;  // drifted into Sleepy from neglect
  bool     fidgetInFlight_    = false;  // true while OUR OWN fidget reaction
                                         // is still playing out, so it
                                         // doesn't reset its own idle clock

#ifdef FACE_RENDER_DEBUG
  uint32_t debugFrameCount_ = 0;
  uint32_t debugLastReportMs_ = 0;
  uint32_t debugLastFrameUs_ = 0;
#endif
};
