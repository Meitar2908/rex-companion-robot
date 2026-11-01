// ExpressionTable.h — original per-expression parameter table.
// Maps each Expression to a target EyeShape (for the left eye; the
// right eye is derived per its EyeSymmetry) AND a target MouthPose,
// plus default gaze/blink and attack/hold/release timing shared by
// both — this is the single point where "one Reaction drives both
// eyes and mouth" is defined. Values were picked by hand for this
// project, not copied from any reference.
#pragma once
#include <Arduino.h>
#include "EyeTypes.h"

struct ExpressionDef {
  EyeShape     eyeBase;
  EyeSymmetry  symmetry;
  MouthPose    mouthBase;
  float        gazeX, gazeY;
  BlinkPattern blink;
  uint32_t     attackMs, holdMs, releaseMs;
};

// Look up the definition for an expression.
const ExpressionDef& expressionDef(Expression e);

// The character's actual resting mouth — a gentle upward curve,
// noticeably short of a full HAPPY smile but well past flat. Every
// expression's target blends from this (at intensity 0) rather than
// from a flat line, and everything releases back to it, not to a
// mathematically flat mouth.
MouthPose restingMouthPose();

// Mirrors the left/right-corner-dependent fields of a shape — used
// when EyeSymmetry::Mirrored derives the right eye from eyeBase.
EyeShape mirrorEyeShape(const EyeShape& s);

// Name <-> Expression lookups, shared by anything that needs to
// print or parse expression names (Serial debug commands, the
// conversational AI's [FACE:name:intensity] directive parser, ...).
const char* expressionName(Expression e);
bool expressionFromName(const String& nameIn, Expression& out);

// Loosely scans free-form text (a voice transcript, an /ask string) for
// an explicit request to PERFORM a specific expression — "make a happy
// face", "look surprised", "can you smile?" — as opposed to
// expressionFromName() above, which requires the whole string to be
// exactly one canonical name (used by the /emotion debug command).
// Lets callers short-circuit straight to a local reaction instead of
// spending a full AI+TTS round trip on something that's really just a
// command, not a question. Returns false (out untouched) on no match —
// deliberately conservative: requires an action word ("make"/"show"/
// "look"/...) alongside an expression word, so an unrelated sentence
// that merely mentions an emotion in passing ("I'm so happy today")
// doesn't misfire. Not NLP — a cheap heuristic, same spirit as the rest
// of this project's "good enough for the ESP32" matching.
bool expressionCommandFromText(const String& textIn, Expression& out);
