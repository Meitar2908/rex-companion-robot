// EyeTypes.h — core data model for REX's expressive face.
// Pure data: no drawing, no hardware, no timing logic here.
#pragma once
#include <stdint.h>

enum class Expression : uint8_t {
  Neutral, Happy, Excited, Amused, Curious, Thinking, Confused, Skeptical,
  Suspicious, Surprised, Amazed, Worried, Sad, Annoyed, Frustrated, Angry,
  Scared, Sleepy, Unimpressed,
  // Added for on-demand/idle-fidget use — see ExpressionTable.cpp's
  // kTable for the design notes on each.
  Evil, Smug, Proud, Determined, Playful, Embarrassed, Dizzy, Smitten,
  Cool, SpacedOut,
  Listening, Speaking,
  COUNT
};

enum class BlinkPattern : uint8_t { None, Single, Double, Fast, Slow };

enum class CharacterState : uint8_t { Idle, Listening, Thinking, Reacting, Speaking };

// How the right eye's shape is derived from the left eye's target
// when an expression isn't simply symmetric.
enum class EyeSymmetry : uint8_t {
  Symmetric,  // right eye = same shape as left
  Mirrored,   // right eye = left/right corner fields swapped (e.g. furrowed brow)
  LeftOnly,   // right eye stays neutral — only the left eye carries the shape
};

// One eye's procedural shape. All fields are relative multipliers or
// 0..1-ish fractions so the whole face scales cleanly with screen size.
struct EyeShape {
  float openness    = 1.0f;  // 0 = fully closed (blink), 1 = fully open
  float widthMul     = 1.0f;  // multiplies the baseline eye width
  float lidDropL     = 0.0f;  // top-lid mask depth at the eye's screen-left corner
  float lidDropR     = 0.0f;  // top-lid mask depth at the eye's screen-right corner
  float happyCurve   = 0.0f;  // 0..1, strength of the upward smiling-eye crescent
  float pupilBiasX    = 0.0f;  // extra pupil offset layered on top of gaze, -1..1
  float pupilBiasY    = 0.0f;
  float pupilScale    = 1.0f;  // pupil size multiplier
  // Both fields below are appended at the end, not inserted earlier in
  // the struct, so every existing positional initializer in
  // ExpressionTable.cpp stays correct — C++ aggregate init zero-fills a
  // trailing field that isn't listed.
  float bottomLidRise = 0.0f;  // 0..1, eyelid rising from BELOW (squint) — unlike
                                // openness/lidDrop*, leaves the top edge untouched
  float centerXShift  = 0.0f;  // -1..1, slides the WHOLE eye body left/right —
                                // unlike pupilBiasX, which only moves the pupil
                                // within a stationary eye. Driven by
                                // EyeAnimator::tickScanning(), not the static
                                // per-expression table (always 0 there).
};

// A pair of eye shapes — the unit EyeAnimator blends and EyeRenderer draws.
struct EyePair {
  EyeShape left;
  EyeShape right;
};

// The mouth: a single thick line with rounded ends, deformed by a
// handful of independent knobs rather than stored per-expression
// bitmaps. curvature/asymmetry/waviness combine additively; speech
// (when speaking) layers a waveform on top of whichever curve this
// produces, so the emotional shape is never fully replaced.
// widthMul and centerXOffset are independent — length and position
// aren't coupled, so an expression can be short AND centered (shy),
// or short AND shifted to one side (a smirk), not just narrower.
struct MouthPose {
  float curvature      = 0.0f;  // -1 (frown, ends down) .. +1 (smile, ends up)
  float asymmetry       = 0.0f;  // -1..1, tilts one side relative to the other
  float waviness        = 0.0f;  // 0..1, irregular wobble amount (confused)
  float widthMul         = 1.0f;  // relative to baseline mouth width (length)
  float verticalOffset   = 0.0f;  // -1..1, small overall vertical shift
  float centerXOffset    = 0.0f;  // -1..1, shifts the whole line left/right of center
};

struct FacePose {
  EyeShape  left;
  EyeShape  right;
  MouthPose mouth;
};

// A single classified reaction — what ReactionEngine hands to
// CharacterController.
struct Reaction {
  Expression   expression = Expression::Neutral;
  float        intensity  = 0.0f;   // 0..1
  float        gazeX      = 0.0f;   // -1..1, left..right
  float        gazeY      = 0.0f;   // -1..1, down..up
  BlinkPattern blink      = BlinkPattern::None;
  uint32_t     attackMs   = 200;
  uint32_t     holdMs     = 1200;
  uint32_t     releaseMs  = 400;
};
