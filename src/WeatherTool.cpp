#include "WeatherTool.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include "Secrets.h"
#include "NetworkBudget.h"

namespace {

// Whole-call budget (geocode + the actual forecast fetch, when both are
// needed) — see NetworkBudget.h. A non-default city needs both steps;
// this used to let each carry its own 6s-connect+8s-response ceiling
// independently, up to ~28s combined worst case. 10s total is still
// generous slack over the ~1.2-1.3s a working call actually takes.
constexpr uint32_t kOverallBudgetMs = 10000;

// Open-Meteo returns a numeric WMO weather code, not a human string —
// collapsed into short buckets here so Claude never has to interpret
// raw codes itself (see ToolResult.h: this class hands back facts,
// not phrasing, but "condition=55" would just be a fact nobody can use).
String describeWeatherCode(int code) {
  if (code == 0) return "Clear";
  if (code == 1 || code == 2) return "Partly cloudy";
  if (code == 3) return "Overcast";
  if (code == 45 || code == 48) return "Foggy";
  if (code >= 51 && code <= 57) return "Drizzle";
  if (code >= 61 && code <= 67) return "Rain";
  if (code >= 71 && code <= 77) return "Snow";
  if (code >= 80 && code <= 82) return "Rain showers";
  if (code >= 85 && code <= 86) return "Snow showers";
  if (code >= 95 && code <= 99) return "Thunderstorm";
  return "Unknown";
}

// Maps "today"/"tomorrow"/a weekday name to an index into Open-Meteo's
// `daily` arrays (0 = today), using the device's current local time
// (already synced/timezoned by TimeTool::begin() at boot). Unrecognized
// input falls back to today rather than failing the whole request.
int resolveDayOffset(const String& dateIn) {
  String date = dateIn;
  date.trim();
  date.toLowerCase();
  if (date.length() == 0 || date == "today") return 0;
  if (date == "tomorrow") return 1;

  static const char* kWeekdays[7] = {
    "sunday", "monday", "tuesday", "wednesday", "thursday", "friday", "saturday"
  };
  time_t now = time(nullptr);
  struct tm nowTm;
  localtime_r(&now, &nowTm);
  for (int i = 0; i < 7; i++) {
    if (date == kWeekdays[i]) {
      int diff = i - nowTm.tm_wday;
      if (diff < 0) diff += 7;
      return diff;
    }
  }
  return 0;
}

// name -> lat/lon via Open-Meteo's free geocoding endpoint (no key).
// Only called when the requested location isn't the default — the
// default's coordinates are already known (Secrets.h), no need to
// geocode them on every single request.
bool geocode(const String& location, float& lat, float& lon, String& resolvedName,
             const NetworkBudget& budget) {
  if (budget.exhausted()) return false;

  String encoded = location;
  encoded.replace(" ", "%20");

  HTTPClient http;
  // Capped at BOTH a sane per-call ceiling and whatever's left of the
  // whole-call budget — whichever is smaller — so this step can't eat
  // the entire allowance and starve the forecast fetch that follows it.
  uint32_t remaining = budget.remainingMs();
  http.setConnectTimeout(remaining < 3000 ? remaining : 3000);
  http.setTimeout(remaining < 4000 ? remaining : 4000);
  http.begin("https://geocoding-api.open-meteo.com/v1/search?count=1&name=" + encoded);
  int code = http.GET();
  if (code != 200) {
    http.end();
    return false;
  }
  String body = http.getString();
  http.end();

  JsonDocument doc;
  if (deserializeJson(doc, body)) return false;
  JsonArray results = doc["results"].as<JsonArray>();
  if (results.isNull() || results.size() == 0) return false;

  JsonObject first = results[0];
  lat = first["latitude"] | 0.0f;
  lon = first["longitude"] | 0.0f;
  const char* name = first["name"] | "";
  resolvedName = String(name);
  return true;
}

} // namespace

ToolResult WeatherTool::fetch(const String& location, const String& date) {
  ToolResult result;

  if (WiFi.status() != WL_CONNECTED) {
    result.error = "no WiFi connection";
    return result;
  }

  // Covers geocode() below AND the forecast fetch further down — see
  // NetworkBudget.h. Started here so the clock includes both steps.
  NetworkBudget budget(kOverallBudgetMs);

  float lat = DEFAULT_LOCATION_LAT;
  float lon = DEFAULT_LOCATION_LON;
  String placeName = DEFAULT_LOCATION_NAME;

  String loc = location;
  loc.trim();
  bool isDefault = (loc.length() == 0) || loc.equalsIgnoreCase(DEFAULT_LOCATION_NAME);
  if (!isDefault) {
    if (!geocode(loc, lat, lon, placeName, budget)) {
      result.error = budget.exhausted()
        ? "taking too long to reach the weather service — try again in a moment"
        : "couldn't find that location";
      return result;
    }
  }

  int dayOffset = resolveDayOffset(date);
  if (dayOffset > 6) dayOffset = 6;  // stay inside forecast_days below

  if (budget.exhausted()) {
    result.error = "taking too long to reach the weather service — try again in a moment";
    return result;
  }

  HTTPClient http;
  uint32_t remaining = budget.remainingMs();
  http.setConnectTimeout(remaining < 3000 ? remaining : 3000);
  http.setTimeout(remaining < 5000 ? remaining : 5000);
  String url = "https://api.open-meteo.com/v1/forecast?latitude=" + String(lat, 4) +
               "&longitude=" + String(lon, 4) +
               "&current=temperature_2m,apparent_temperature,precipitation,weather_code,wind_speed_10m" +
               "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max" +
               "&timezone=auto&forecast_days=7";
  http.begin(url);
  Serial.print("[WEATHER] GET "); Serial.println(url);
  uint32_t t0 = millis();
  int code = http.GET();
  Serial.print("[WEATHER] returned "); Serial.print(code);
  Serial.print(" after "); Serial.print(millis() - t0); Serial.println(" ms");
  if (code != 200) {
    http.end();
    result.error = "weather service unavailable";
    return result;
  }
  String body = http.getString();
  http.end();

  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    result.error = "couldn't read weather data";
    return result;
  }

  String data = "location=" + placeName;

  if (dayOffset == 0) {
    float tempC = doc["current"]["temperature_2m"] | 0.0f;
    float feelsC = doc["current"]["apparent_temperature"] | tempC;
    float precip = doc["current"]["precipitation"] | 0.0f;
    int wcode = doc["current"]["weather_code"] | 0;
    float windKph = doc["current"]["wind_speed_10m"] | 0.0f;
    data += ", temp_c=" + String(tempC, 0);
    data += ", feels_like_c=" + String(feelsC, 0);
    data += ", condition=" + describeWeatherCode(wcode);
    data += ", precipitation_mm=" + String(precip, 1);
    data += ", wind_kph=" + String(windKph, 0);
  }

  // Include the requested day's forecast too (covers "tomorrow"/a named
  // weekday, and adds today's high/low/rain-chance alongside the live
  // reading when dayOffset == 0).
  JsonArray dailyDates = doc["daily"]["time"].as<JsonArray>();
  JsonArray dailyCodes = doc["daily"]["weather_code"].as<JsonArray>();
  JsonArray dailyMax = doc["daily"]["temperature_2m_max"].as<JsonArray>();
  JsonArray dailyMin = doc["daily"]["temperature_2m_min"].as<JsonArray>();
  JsonArray dailyRain = doc["daily"]["precipitation_probability_max"].as<JsonArray>();
  if (!dailyCodes.isNull() && dayOffset < (int)dailyCodes.size()) {
    const char* forecastDate = dailyDates[dayOffset] | "";
    int forecastCode = dailyCodes[dayOffset] | 0;
    float forecastHigh = dailyMax[dayOffset] | 0.0f;
    float forecastLow = dailyMin[dayOffset] | 0.0f;
    int forecastRain = dailyRain[dayOffset] | 0;
    data += ", forecast_date=" + String(forecastDate);
    data += ", forecast_condition=" + describeWeatherCode(forecastCode);
    data += ", forecast_high_c=" + String(forecastHigh, 0);
    data += ", forecast_low_c=" + String(forecastLow, 0);
    data += ", forecast_rain_probability_pct=" + String(forecastRain);
  }

  result.success = true;
  result.data = data;
  return result;
}
