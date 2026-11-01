#include "ExpressionTable.h"

EyeShape mirrorEyeShape(const EyeShape& s) {
  EyeShape m = s;
  m.lidDropL = s.lidDropR;
  m.lidDropR = s.lidDropL;
  m.pupilBiasX = -s.pupilBiasX;
  return m;
}

// The character's default mouth: a gentle upward curve — noticeably
// short of HAPPY's full smile (0.65) but well past flat. Every
// expression blends from this at intensity 0 and releases back to
// it, per ExpressionTable.h's contract.
MouthPose restingMouthPose() {
  MouthPose m;
  m.curvature = 0.30f;
  return m;
}

// Eye:   { openness, widthMul, lidDropL, lidDropR, happyCurve, pupilBiasX, pupilBiasY, pupilScale }
// Mouth: { curvature, asymmetry, waviness, widthMul, verticalOffset }
//   Neutral/Listening/Speaking's mouth target equals restingMouthPose() itself, so
//   blending is a no-op and they just look like the character's normal resting face.
static const ExpressionDef kTable[(int)Expression::COUNT] = {
  /* Neutral — IS the resting mouth */
  { {1.00f,1.00f,0.00f,0.00f,0.00f,0.00f,0.00f,1.00f}, EyeSymmetry::Symmetric,
    {0.30f, 0.00f, 0.00f, 1.00f, 0.00f},
    0.00f, 0.00f, BlinkPattern::Single, 250, 1500, 400 },

  /* Happy */
  { {1.00f,1.00f,0.00f,0.00f,0.70f,0.00f,0.00f,1.00f}, EyeSymmetry::Symmetric,
    {0.65f, 0.00f, 0.00f, 1.00f, 0.00f},
    0.00f, 0.00f, BlinkPattern::Single, 200, 1400, 500 },

  /* Excited */
  { {1.15f,1.05f,0.00f,0.00f,0.50f,0.00f,0.00f,1.20f}, EyeSymmetry::Symmetric,
    {0.80f, 0.00f, 0.05f, 1.08f, 0.00f},
    0.00f, 0.10f, BlinkPattern::Double, 120, 1200, 350 },

  /* Amused — a real smirk: short line, slid to one side, lifted a touch */
  { {1.00f,1.00f,0.00f,0.00f,0.55f,0.00f,0.00f,1.05f}, EyeSymmetry::Symmetric,
    {0.35f, 0.15f, 0.00f, 0.50f, -0.15f, 0.35f},
    0.00f, 0.00f, BlinkPattern::Single, 180, 1300, 450 },

  /* Curious */
  { {1.05f,1.05f,0.15f,0.00f,0.00f,0.00f,0.00f,1.05f}, EyeSymmetry::LeftOnly,
    {0.08f, 0.15f, 0.00f, 1.00f, 0.00f},
    0.00f, 0.15f, BlinkPattern::Single, 220, 1200, 400 },

  /* Thinking — restrained, a shade off-center as it looks away */
  { {0.85f,1.00f,0.25f,0.10f,0.00f,0.00f,0.10f,1.00f}, EyeSymmetry::Symmetric,
    {0.00f, 0.25f, 0.10f, 0.85f, 0.00f, 0.12f},
    0.30f, 0.15f, BlinkPattern::Slow, 300, 2000, 500 },

  /* Confused — shorter and nudged off-center, on top of the wave */
  { {0.85f,1.00f,0.30f,0.05f,0.00f,0.00f,0.00f,1.00f}, EyeSymmetry::LeftOnly,
    {0.05f, 0.30f, 0.55f, 0.75f, 0.00f, 0.15f},
    0.15f, 0.00f, BlinkPattern::Single, 220, 1400, 450 },

  /* Skeptical — compressed to one side, opposite the smirk's lean */
  { {0.75f,1.00f,0.40f,0.40f,0.00f,0.20f,0.00f,0.90f}, EyeSymmetry::LeftOnly,
    {-0.10f, 0.15f, 0.00f, 0.55f, 0.05f, -0.30f},
    -0.20f, 0.00f, BlinkPattern::Single, 250, 1500, 450 },

  /* Suspicious — short and guarded, slight lean */
  { {0.70f,0.95f,0.30f,0.30f,0.00f,0.25f,0.00f,0.85f}, EyeSymmetry::Symmetric,
    {-0.05f, 0.00f, 0.00f, 0.55f, 0.00f, -0.15f},
    0.00f, 0.00f, BlinkPattern::Slow, 300, 1800, 500 },

  /* Surprised */
  { {1.30f,1.15f,0.00f,0.00f,0.00f,0.00f,0.00f,1.25f}, EyeSymmetry::Symmetric,
    {-0.10f, 0.00f, 0.00f, 0.55f, 0.00f},
    0.00f, 0.10f, BlinkPattern::Fast, 80, 900, 350 },

  /* Amazed */
  { {1.25f,1.10f,0.00f,0.00f,0.15f,0.00f,0.00f,1.30f}, EyeSymmetry::Symmetric,
    {0.25f, 0.00f, 0.00f, 0.65f, 0.00f},
    0.00f, 0.10f, BlinkPattern::Single, 100, 1500, 450 },

  /* Worried */
  { {0.85f,1.00f,0.40f,0.40f,0.00f,0.00f,0.00f,0.95f}, EyeSymmetry::Symmetric,
    {-0.30f, 0.15f, 0.15f, 0.90f, 0.00f},
    0.00f, 0.00f, BlinkPattern::Fast, 200, 1600, 500 },

  /* Sad — outer corner drops more than inner (lidDropL > lidDropR), the
     classic "worried/sad" oblique brow that slopes UP toward the nose.
     Was accidentally carrying ANGRY's furrowed (inner > outer) shape —
     the two were transposed, which is exactly why they read as each
     other. Fixed by swapping the lidDrop pair with Angry's, below. */
  { {1.00f,1.00f,0.50f,0.15f,0.00f,0.00f,0.00f,1.00f}, EyeSymmetry::Mirrored,
    {-0.55f, 0.00f, 0.00f, 1.00f, 0.00f},
    0.00f, -0.10f, BlinkPattern::Single, 280, 1600, 600 },

  /* Annoyed */
  { {0.90f,1.00f,0.35f,0.10f,0.00f,0.00f,0.00f,0.90f}, EyeSymmetry::Mirrored,
    {-0.20f, 0.00f, 0.00f, 0.85f, 0.00f},
    0.00f, 0.00f, BlinkPattern::Single, 180, 1300, 400 },

  /* Frustrated */
  { {0.85f,1.00f,0.45f,0.15f,0.00f,0.00f,0.00f,0.85f}, EyeSymmetry::Mirrored,
    {-0.30f, 0.30f, 0.20f, 0.80f, 0.00f},
    0.00f, 0.00f, BlinkPattern::Single, 150, 1300, 350 },

  /* Angry — inner corner drops more than outer (lidDropR > lidDropL),
     the furrowed brow that pulls down and together toward the nose.
     See Sad's comment above — this used to carry Sad's shape. */
  { {0.85f,1.00f,0.15f,0.50f,0.00f,0.00f,0.00f,0.85f}, EyeSymmetry::Mirrored,
    {-0.40f, 0.00f, 0.00f, 0.85f, 0.00f},
    0.00f, 0.00f, BlinkPattern::Single, 150, 1400, 350 },

  /* Scared */
  { {1.30f,1.10f,0.00f,0.00f,0.00f,0.00f,0.00f,1.30f}, EyeSymmetry::Symmetric,
    {-0.15f, 0.00f, 0.35f, 0.70f, 0.00f},
    0.00f, 0.10f, BlinkPattern::Fast, 60, 500, 300 },

  /* Sleepy — small and centered, a shy little relaxed line */
  { {0.45f,0.95f,0.60f,0.60f,0.00f,0.00f,0.00f,0.90f}, EyeSymmetry::Symmetric,
    {0.15f, 0.00f, 0.00f, 0.45f, 0.10f},
    0.00f, -0.10f, BlinkPattern::Slow, 600, 2500, 800 },

  /* Unimpressed */
  { {0.75f,1.00f,0.30f,0.30f,0.00f,0.00f,0.00f,0.90f}, EyeSymmetry::Symmetric,
    {0.00f, 0.00f, 0.00f, 1.00f, 0.00f},
    0.10f, 0.00f, BlinkPattern::Slow, 300, 1800, 500 },

  // ---- Added for on-demand/idle-fidget use. Built entirely from
  // existing primitives — the mouth is always one continuous curved
  // line (no open-mouth/teeth capability) and pupils are always plain
  // circles (no heart/star shapes), so these lean on combinations and
  // a couple of tricks already proven elsewhere in this table rather
  // than new rendering features.

  /* Evil — a cocked outer brow (lidDropL low, lidDropR higher = sly,
     not furrowed like Angry) plus a squint, under a wide lopsided grin
     slid to one side. */
  { {0.80f,1.00f,0.10f,0.35f,0.00f,0.10f,0.00f,0.95f,0.30f}, EyeSymmetry::Mirrored,
    {0.55f, 0.45f, 0.00f, 1.05f, -0.05f, 0.20f},
    0.15f, 0.00f, BlinkPattern::Single, 200, 1500, 400 },

  /* Smug — relaxed half-lidded eyes (bottomLidRise, not a hard squint)
     under a short, elevated, lopsided smirk — self-satisfied rather
     than Amused's "found something funny" open-eyed reaction. */
  { {0.75f,1.00f,0.00f,0.00f,0.10f,0.00f,0.00f,1.00f,0.25f}, EyeSymmetry::Symmetric,
    {0.30f, 0.40f, 0.00f, 0.60f, -0.10f, 0.30f},
    0.00f, 0.00f, BlinkPattern::Slow, 220, 1400, 450 },

  /* Proud — chin-up gaze (positive gazeY), a confident squinting-with-
     satisfaction happyCurve, and a wide open grin. */
  { {0.90f,1.05f,0.00f,0.00f,0.35f,0.00f,0.10f,1.10f}, EyeSymmetry::Symmetric,
    {0.60f, 0.00f, 0.00f, 1.15f, 0.00f},
    0.00f, 0.15f, BlinkPattern::Single, 200, 1500, 450 },

  /* Determined — a serious (not joyful) focused squint, dead-center
     gaze, pressed narrow lips. No curvature at all — not a frown,
     just resolute. */
  { {0.80f,0.95f,0.00f,0.00f,0.00f,0.00f,0.00f,1.00f,0.30f}, EyeSymmetry::Symmetric,
    {0.00f, 0.00f, 0.00f, 0.85f, 0.00f},
    0.00f, 0.00f, BlinkPattern::Slow, 180, 1800, 400 },

  /* Playful (a wink) — EyeSymmetry::LeftOnly means the right eye is
     ALWAYS plain EyeShape{} regardless of intensity (see
     computeTargetFromExpression), so the base/left eye alone carries
     the near-shut wink while the right stays normally open — same
     mechanism Curious/Skeptical/Confused/Suspicious already use for a
     one-sided effect, just pushed to fully closed instead of subtle. */
  { {0.10f,1.00f,0.00f,0.00f,0.20f,0.00f,0.00f,1.00f}, EyeSymmetry::LeftOnly,
    {0.40f, 0.35f, 0.00f, 0.75f, -0.05f, 0.25f},
    0.10f, 0.00f, BlinkPattern::Single, 150, 1200, 350 },

  /* Embarrassed — gaze down and away (negative gazeY = look down, per
     EyeRenderer's "+Y bias = look up" convention), a small nervous
     wavering closed-mouth smile. */
  { {0.70f,0.95f,0.15f,0.15f,0.00f,0.15f,-0.20f,0.90f,0.10f}, EyeSymmetry::Symmetric,
    {0.15f, 0.10f, 0.15f, 0.65f, 0.00f},
    0.20f, -0.20f, BlinkPattern::Fast, 180, 1300, 400 },

  /* Dizzy — EyeSymmetry::Mirrored negates pupilBiasX for the right eye
     (see mirrorEyeShape), so a strong sideways pupil drift here makes
     the two eyes point in OPPOSITE directions — a genuinely cross-eyed,
     dazed look — under a wavering, uneven mouth line. */
  { {1.10f,1.00f,0.00f,0.00f,0.00f,0.35f,0.00f,1.00f}, EyeSymmetry::Mirrored,
    {-0.10f, 0.00f, 0.55f, 0.90f, 0.05f},
    0.00f, 0.00f, BlinkPattern::Fast, 100, 1200, 400 },

  /* Smitten — no heart-shaped pupils (the renderer only draws plain
     circles), so this leans on oversized, soft, doe-eyed pupils
     instead — the achievable equivalent of the "heart eyes" look. */
  { {1.05f,1.05f,0.00f,0.00f,0.25f,0.00f,0.00f,1.70f}, EyeSymmetry::Symmetric,
    {0.35f, 0.00f, 0.00f, 0.80f, 0.00f},
    0.00f, 0.05f, BlinkPattern::Slow, 250, 1600, 500 },

  /* Cool — relaxed via lowered openness ALONE, deliberately no
     bottomLidRise — that squint reads as judgmental (see Listening/
     Unimpressed), not laid-back. Alert and relaxed, not drowsy like
     Sleepy. */
  { {0.70f,1.00f,0.00f,0.00f,0.00f,0.00f,0.00f,0.95f}, EyeSymmetry::Symmetric,
    {0.15f, 0.10f, 0.00f, 0.80f, 0.00f, 0.10f},
    0.00f, 0.00f, BlinkPattern::Slow, 250, 1800, 500 },

  /* SpacedOut — a blank, unfocused stare: small pinpoint pupils, flat
     mouth, no blinking at all (BlinkPattern::None, like Idle's own
     baseline) so it genuinely reads as "not quite present" rather than
     Unimpressed's judgment or Sleepy's drowsiness. */
  { {1.00f,1.00f,0.00f,0.00f,0.00f,0.00f,0.00f,0.65f}, EyeSymmetry::Symmetric,
    {0.00f, 0.00f, 0.00f, 0.75f, 0.00f},
    0.00f, 0.00f, BlinkPattern::None, 300, 2000, 500 },

  /* Listening — flat/straight mouth; squinted eyes via bottomLidRise, a
     lid rising from BELOW so the top edge (openness stays 1.0) is
     untouched — unlike lidDropL/R (top corners) or openness itself,
     both of which move the top edge. Also used for CharacterState::
     Thinking (see CharacterController) so listening and processing look
     identical until REX actually starts speaking. The left-right
     scanning motion is layered on top continuously by EyeAnimator::
     tickScanning() while either of those states is active, not by
     anything in this static table — gazeX below is just the resting
     value before that takes over. */
  { {1.00f,1.00f,0.00f,0.00f,0.00f,0.00f,0.00f,1.05f,0.85f}, EyeSymmetry::Symmetric,
    {0.00f, 0.00f, 0.00f, 1.00f, 0.00f},
    0.00f, 0.00f, BlinkPattern::Single, 250, 1200, 400 },

  /* Speaking — baseline only; the active reaction's shape is what's
     actually kept while talking (see CharacterController). */
  { {1.00f,1.00f,0.00f,0.00f,0.00f,0.00f,0.00f,1.00f}, EyeSymmetry::Symmetric,
    {0.30f, 0.00f, 0.00f, 1.00f, 0.00f},
    0.00f, 0.00f, BlinkPattern::Single, 200, 1200, 400 },
};

const ExpressionDef& expressionDef(Expression e) {
  int i = (int)e;
  if (i < 0 || i >= (int)Expression::COUNT) i = 0;
  return kTable[i];
}

const char* expressionName(Expression e) {
  switch (e) {
    case Expression::Neutral:     return "NEUTRAL";
    case Expression::Happy:       return "HAPPY";
    case Expression::Excited:     return "EXCITED";
    case Expression::Amused:      return "AMUSED";
    case Expression::Curious:     return "CURIOUS";
    case Expression::Thinking:    return "THINKING";
    case Expression::Confused:    return "CONFUSED";
    case Expression::Skeptical:   return "SKEPTICAL";
    case Expression::Suspicious:  return "SUSPICIOUS";
    case Expression::Surprised:   return "SURPRISED";
    case Expression::Amazed:      return "AMAZED";
    case Expression::Worried:     return "WORRIED";
    case Expression::Sad:         return "SAD";
    case Expression::Annoyed:     return "ANNOYED";
    case Expression::Frustrated:  return "FRUSTRATED";
    case Expression::Angry:       return "ANGRY";
    case Expression::Scared:      return "SCARED";
    case Expression::Sleepy:      return "SLEEPY";
    case Expression::Unimpressed: return "UNIMPRESSED";
    case Expression::Evil:        return "EVIL";
    case Expression::Smug:        return "SMUG";
    case Expression::Proud:       return "PROUD";
    case Expression::Determined:  return "DETERMINED";
    case Expression::Playful:     return "PLAYFUL";
    case Expression::Embarrassed: return "EMBARRASSED";
    case Expression::Dizzy:       return "DIZZY";
    case Expression::Smitten:     return "SMITTEN";
    case Expression::Cool:        return "COOL";
    case Expression::SpacedOut:   return "SPACEDOUT";
    case Expression::Listening:   return "LISTENING";
    case Expression::Speaking:    return "SPEAKING";
    default:                      return "NEUTRAL";
  }
}

bool expressionFromName(const String& nameIn, Expression& out) {
  String name = nameIn;
  name.trim();
  name.toLowerCase();
  static const struct { const char* n; Expression e; } kNames[] = {
    {"neutral", Expression::Neutral},     {"default", Expression::Neutral},
    {"happy", Expression::Happy},
    {"excited", Expression::Excited},     {"amused", Expression::Amused},
    {"curious", Expression::Curious},     {"thinking", Expression::Thinking},
    {"confused", Expression::Confused},   {"skeptical", Expression::Skeptical},
    {"suspicious", Expression::Suspicious}, {"surprised", Expression::Surprised},
    {"amazed", Expression::Amazed},       {"worried", Expression::Worried},
    {"sad", Expression::Sad},             {"annoyed", Expression::Annoyed},
    {"frustrated", Expression::Frustrated}, {"angry", Expression::Angry},
    {"scared", Expression::Scared},       {"sleepy", Expression::Sleepy},
    {"unimpressed", Expression::Unimpressed},
    {"evil", Expression::Evil},           {"smug", Expression::Smug},
    {"proud", Expression::Proud},         {"determined", Expression::Determined},
    {"playful", Expression::Playful},     {"embarrassed", Expression::Embarrassed},
    {"dizzy", Expression::Dizzy},         {"smitten", Expression::Smitten},
    {"cool", Expression::Cool},           {"spacedout", Expression::SpacedOut},
    {"listening", Expression::Listening},
    {"speaking", Expression::Speaking},
  };
  for (auto& row : kNames) {
    if (name == row.n) { out = row.e; return true; }
  }
  return false;
}

bool expressionCommandFromText(const String& textIn, Expression& out) {
  String text = textIn;
  text.trim();
  text.toLowerCase();
  if (text.length() == 0) return false;

  // Natural-language stand-ins for a canonical FACE-tag word — "smile"
  // itself is never valid as a FACE tag (see ExpressionTable.h's
  // comment on expressionFromName's list), but it's the obvious way a
  // person actually asks for one. Excludes Listening/Speaking — those
  // are pipeline states, never something to request as a reaction.
  static const struct { const char* word; Expression e; } kSynonyms[] = {
    {"smile", Expression::Happy}, {"grin", Expression::Happy},
    {"frown", Expression::Sad},
    {"smirk", Expression::Amused},
    {"shocked", Expression::Surprised}, {"surprise", Expression::Surprised},
    {"mad", Expression::Angry},
    {"bored", Expression::Unimpressed},
    // "wink" moved here from Amused now that Playful IS a real wink
    // (EyeSymmetry::LeftOnly, one eye shut) — a much more literal match.
    {"wink", Expression::Playful},
    {"sinister", Expression::Evil}, {"villain", Expression::Evil},
    {"shy", Expression::Embarrassed},
    {"love", Expression::Smitten}, {"lovestruck", Expression::Smitten},
    {"blank", Expression::SpacedOut}, {"zone out", Expression::SpacedOut},
    {"spaced out", Expression::SpacedOut}, {"focused", Expression::Determined},
  };
  // Bare imperative ("smile", "wink") — no action word needed, it IS one.
  for (auto& row : kSynonyms) {
    if (text == row.word) { out = row.e; return true; }
  }

  // Everything else requires an action word alongside the expression
  // word, so an unrelated sentence that just happens to mention a
  // feeling ("I'm so happy today") doesn't misfire into a face command.
  bool hasActionWord =
      text.indexOf("make") >= 0 || text.indexOf("show") >= 0 ||
      text.indexOf("give") >= 0 || text.indexOf("look") >= 0 ||
      text.indexOf("put on") >= 0 || text.indexOf("do a") >= 0 ||
      text.indexOf("can you") >= 0;
  if (!hasActionWord) return false;

  for (auto& row : kSynonyms) {
    if (text.indexOf(row.word) >= 0) { out = row.e; return true; }
  }
  static const struct { const char* word; Expression e; } kCanonical[] = {
    {"neutral", Expression::Neutral},     {"happy", Expression::Happy},
    {"excited", Expression::Excited},     {"amused", Expression::Amused},
    {"curious", Expression::Curious},     {"thinking", Expression::Thinking},
    {"confused", Expression::Confused},   {"skeptical", Expression::Skeptical},
    {"suspicious", Expression::Suspicious}, {"surprised", Expression::Surprised},
    {"amazed", Expression::Amazed},       {"worried", Expression::Worried},
    {"sad", Expression::Sad},             {"annoyed", Expression::Annoyed},
    {"frustrated", Expression::Frustrated}, {"angry", Expression::Angry},
    {"scared", Expression::Scared},       {"sleepy", Expression::Sleepy},
    {"evil", Expression::Evil},           {"smug", Expression::Smug},
    {"proud", Expression::Proud},         {"determined", Expression::Determined},
    {"playful", Expression::Playful},     {"embarrassed", Expression::Embarrassed},
    {"dizzy", Expression::Dizzy},         {"smitten", Expression::Smitten},
    {"cool", Expression::Cool},
  };
  for (auto& row : kCanonical) {
    if (text.indexOf(row.word) >= 0) { out = row.e; return true; }
  }
  return false;
}
