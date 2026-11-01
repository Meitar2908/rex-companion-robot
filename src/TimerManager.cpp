#include "TimerManager.h"
#include <time.h>

ToolResult TimerManager::set(const String& label, uint32_t secondsFromNow) {
  ToolResult result;

  if (secondsFromNow == 0 || secondsFromNow > 24 * 3600) {
    result.error = "that duration doesn't make sense (must be under 24 hours)";
    return result;
  }

  time_t now = time(nullptr);
  if (now < 1700000000) {  // before 2023-11 => clock not synced yet
    // Only possible in the first few seconds after boot — configTzTime()
    // kicks off SNTP asynchronously (see TimeTool::begin()) and doesn't
    // block, so time(nullptr) returns a bogus pre-2023 value until the
    // first reply lands. Actionable, not just a bare fact: tells the
    // model what to actually SAY rather than leaving it to guess at (or
    // silently retry against) an opaque failure.
    result.error = "REX just started up and hasn't finished syncing its clock yet — "
                    "tell the user honestly and to try again in a few seconds; "
                    "don't retry this call yourself.";
    return result;
  }

  for (int i = 0; i < kMaxTimers; i++) {
    if (!timers_[i].active) {
      timers_[i].active = true;
      timers_[i].label = label.length() > 0 ? label : "timer";
      timers_[i].triggerAtEpoch = (uint32_t)now + secondsFromNow;

      result.success = true;
      result.data = "label=" + timers_[i].label + ", seconds=" + String(secondsFromNow);
      return result;
    }
  }

  result.error = "too many timers already running";
  return result;
}

bool TimerManager::checkDue(String& outLabel) {
  time_t now = time(nullptr);
  for (int i = 0; i < kMaxTimers; i++) {
    if (timers_[i].active && now >= (time_t)timers_[i].triggerAtEpoch) {
      outLabel = timers_[i].label;
      timers_[i].active = false;
      return true;
    }
  }
  return false;
}
