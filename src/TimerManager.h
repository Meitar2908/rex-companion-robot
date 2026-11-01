// TimerManager.h — pending timers/reminders. Unlike the other tools,
// this holds state that outlives a single conversational turn, and is
// shared: owned once in main.cpp, given by reference to both ToolManager
// instances (the debug one and ConversationalAI's internal one) so a
// timer set through either path lives in one place. main.cpp's loop()
// polls checkDue() directly to know when to have REX speak up on its
// own — see main.cpp's speakAiResponse()/loop() for the announcement side.
#pragma once
#include <Arduino.h>
#include "ToolResult.h"

struct PendingTimer {
  bool     active = false;
  String   label;
  uint32_t triggerAtEpoch = 0;  // absolute Unix time (time(nullptr) domain)
};

class TimerManager {
public:
  static constexpr int kMaxTimers = 5;

  // label may be empty ("timer" is used when speaking it back). Fails if
  // all kMaxTimers slots are full or secondsFromNow is unreasonable.
  ToolResult set(const String& label, uint32_t secondsFromNow);

  // Pops and returns the first timer whose time has passed — true only
  // once per timer. If nothing is due, returns false and outLabel is
  // untouched. Callers should only call this when it's actually a good
  // moment to interrupt (see main.cpp) — an overdue timer just stays
  // "due" and gets picked up on the next call after that.
  bool checkDue(String& outLabel);

private:
  PendingTimer timers_[kMaxTimers];
};
