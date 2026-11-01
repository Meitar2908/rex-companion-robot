// WeatherTool.h — current conditions and short-range forecast via
// Open-Meteo (open-meteo.com): free, no API key. Returns DATA only
// (see ToolResult.h) — the AI/personality layer decides how to phrase
// it, this class never generates any spoken text itself.
#pragma once
#include <Arduino.h>
#include "ToolResult.h"

class WeatherTool {
public:
  // location: city name, empty = DEFAULT_LOCATION_* (Secrets.h).
  // date: "today", "tomorrow", a weekday name, or empty (= today).
  ToolResult fetch(const String& location, const String& date);
};
