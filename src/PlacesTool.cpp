#include "PlacesTool.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <math.h>
#include <string.h>
#include "Secrets.h"
#include "NetworkBudget.h"

namespace {

// Whole-call budget (geocode + the Overpass query, when both are
// needed) — see NetworkBudget.h and WeatherTool.cpp's identical fix.
// Slightly more than weather's 10s since Overpass is documented as
// genuinely slower than a simple REST call, but still far tighter than
// the old ~30s worst case (6s+8s geocode, then 6s+10s Overpass, fully
// independent of each other).
constexpr uint32_t kOverallBudgetMs = 12000;

// Straight-line distance in km — good enough for "closest first" without
// pulling in routing data.
double haversineKm(double lat1, double lon1, double lat2, double lon2) {
  const double kR = 6371.0;
  double dLat = radians(lat2 - lat1);
  double dLon = radians(lon2 - lon1);
  double a = sin(dLat / 2) * sin(dLat / 2) +
             cos(radians(lat1)) * cos(radians(lat2)) * sin(dLon / 2) * sin(dLon / 2);
  double c = 2 * atan2(sqrt(a), sqrt(1 - a));
  return kR * c;
}

// Same geocoding approach as WeatherTool's private helper — kept as its
// own small copy here rather than a shared header, so each Tool file
// stays self-contained.
bool geocode(const String& location, double& lat, double& lon, const NetworkBudget& budget) {
  if (budget.exhausted()) return false;

  String encoded = location;
  encoded.replace(" ", "%20");

  HTTPClient http;
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
  lat = first["latitude"] | 0.0;
  lon = first["longitude"] | 0.0;
  return true;
}

} // namespace

ToolResult PlacesTool::search(const String& category, const String& location) {
  ToolResult result;

  if (category.length() == 0) {
    result.error = "no category given";
    return result;
  }
  if (WiFi.status() != WL_CONNECTED) {
    result.error = "no WiFi connection";
    return result;
  }

  // Covers geocode() below AND the Overpass query further down — see
  // NetworkBudget.h and kOverallBudgetMs's comment above.
  NetworkBudget budget(kOverallBudgetMs);

  double lat = DEFAULT_LOCATION_LAT;
  double lon = DEFAULT_LOCATION_LON;
  String loc = location;
  loc.trim();
  bool isDefault = (loc.length() == 0) || loc.equalsIgnoreCase(DEFAULT_LOCATION_NAME);
  if (!isDefault) {
    if (!geocode(loc, lat, lon, budget)) {
      result.error = budget.exhausted()
        ? "taking too long to reach the places service — try again in a moment"
        : "couldn't find that location";
      return result;
    }
  }

  if (budget.exhausted()) {
    result.error = "taking too long to reach the places service — try again in a moment";
    return result;
  }

  // Overpass QL, POSTed as the raw body (the documented way to send a
  // longer/structured query — no URL-encoding needed, unlike GET).
  String query = "[out:json][timeout:8];node[\"amenity\"=\"" + category +
                  "\"](around:3000," + String(lat, 5) + "," + String(lon, 5) +
                  ");out body 5;";

  HTTPClient http;
  uint32_t remaining = budget.remainingMs();
  http.setConnectTimeout(remaining < 3000 ? remaining : 3000);
  http.setTimeout(remaining < 6000 ? remaining : 6000);  // Overpass can be slower than a simple REST call
  http.begin("https://overpass-api.de/api/interpreter");
  http.addHeader("Content-Type", "text/plain");
  // Overpass's usage policy asks for an identifying User-Agent on
  // automated clients.
  http.addHeader("User-Agent", "REX-robot/1.0 (personal hobby project)");

  Serial.print("[PLACES] POST overpass-api.de, category="); Serial.println(category);
  uint32_t t0 = millis();
  int code = http.POST(query);
  Serial.print("[PLACES] returned "); Serial.print(code);
  Serial.print(" after "); Serial.print(millis() - t0); Serial.println(" ms");
  if (code != 200) {
    http.end();
    result.error = "places service unavailable";
    return result;
  }
  String body = http.getString();
  http.end();

  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    result.error = "couldn't read places data";
    return result;
  }

  JsonArray elements = doc["elements"].as<JsonArray>();
  if (elements.isNull() || elements.size() == 0) {
    result.error = "no " + category + " found nearby";
    return result;
  }

  // Collect named results with distance, then simple insertion-sort by
  // distance — at most 5 elements, no need for anything fancier.
  struct Place { String name; double distanceKm; };
  Place places[5];
  int count = 0;
  for (JsonObject el : elements) {
    if (count >= 5) break;
    const char* name = el["tags"]["name"] | "";
    if (strlen(name) == 0) continue;  // skip unnamed nodes
    double plat = el["lat"] | 0.0;
    double plon = el["lon"] | 0.0;
    double dist = haversineKm(lat, lon, plat, plon);
    int insertAt = count;
    while (insertAt > 0 && places[insertAt - 1].distanceKm > dist) {
      places[insertAt] = places[insertAt - 1];
      insertAt--;
    }
    places[insertAt].name = String(name);
    places[insertAt].distanceKm = dist;
    count++;
  }

  if (count == 0) {
    result.error = "no named " + category + " results nearby";
    return result;
  }

  String data = "category=" + category + ", results=";
  for (int i = 0; i < count; i++) {
    if (i > 0) data += "; ";
    data += places[i].name + " (" + String(places[i].distanceKm, 1) + "km)";
  }

  result.success = true;
  result.data = data;
  return result;
}
