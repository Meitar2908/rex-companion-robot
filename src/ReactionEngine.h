// ReactionEngine.h — a deliberately simple, deterministic text
// classifier. This is a stand-in for the future speech-to-text +
// conversational-AI pipeline, not real language understanding — it
// exists only so the face engine has something to react to today.
// It knows nothing about rendering or hardware.
#pragma once
#include <Arduino.h>
#include "EyeTypes.h"

class ReactionEngine {
public:
  Reaction classify(const String& text) const;
};
