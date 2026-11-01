// TimeTool.h — current date/time, optionally in another city's
// timezone, via the ESP32's own SNTP sync plus a small hardcoded table
// of city -> POSIX TZ strings. No API key, no network call at request
// time beyond the one-time NTP sync begin() kicks off at boot.
#pragma once
#include <Arduino.h>
#include "ToolResult.h"

class TimeTool {
public:
  // Call once in setup() — starts NTP sync against the default
  // location's timezone so the first real query doesn't pay that cost.
  void begin();

  // location: city name, empty = default location's local time.
  // targetDateIso: optional "YYYY-MM-DD" — when given, the result also
  // includes how many days from now that date is (e.g. "how many days
  // until Christmas" — the AI resolves "Christmas" to a date itself
  // from its own knowledge and passes the ISO date here; this class
  // only does the deterministic day-count arithmetic).
  ToolResult fetch(const String& location, const String& targetDateIso);
};
