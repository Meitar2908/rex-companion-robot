// PlacesTool.h — nearby points of interest via OpenStreetMap's free,
// keyless Overpass API. No account, no key — same spirit as Open-Meteo.
#pragma once
#include <Arduino.h>
#include "ToolResult.h"

class PlacesTool {
public:
  // category: a short OSM amenity-style keyword ("cafe", "restaurant",
  // "pharmacy", "fast_food", "bar", ...) — the AI picks this from the
  // user's phrasing. location: city name, empty = DEFAULT_LOCATION_*.
  ToolResult search(const String& category, const String& location);
};
