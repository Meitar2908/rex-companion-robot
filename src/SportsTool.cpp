#include "SportsTool.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <string.h>

namespace {
// TheSportsDB's documented public test key — free, no personal signup,
// rate-limited/basic-tier data (schedule + completed results, not live
// in-play scores).
constexpr const char* kApiBase = "https://www.thesportsdb.com/api/v1/json/3";

bool httpGetJson(const String& url, JsonDocument& doc) {
  HTTPClient http;
  http.setConnectTimeout(6000);
  http.setTimeout(8000);
  http.begin(url);
  Serial.print("[SPORTS] GET "); Serial.println(url);
  uint32_t t0 = millis();
  int code = http.GET();
  Serial.print("[SPORTS] returned "); Serial.print(code);
  Serial.print(" after "); Serial.print(millis() - t0); Serial.println(" ms");
  if (code != 200) {
    http.end();
    return false;
  }
  String body = http.getString();
  http.end();
  return deserializeJson(doc, body) == DeserializationError::Ok;
}
} // namespace

ToolResult SportsTool::lookup(const String& team) {
  ToolResult result;

  if (team.length() == 0) {
    result.error = "no team given";
    return result;
  }
  if (WiFi.status() != WL_CONNECTED) {
    result.error = "no WiFi connection";
    return result;
  }

  String encodedTeam = team;
  encodedTeam.replace(" ", "%20");

  JsonDocument searchDoc;
  if (!httpGetJson(String(kApiBase) + "/searchteams.php?t=" + encodedTeam, searchDoc)) {
    result.error = "sports service unavailable";
    return result;
  }
  JsonArray teams = searchDoc["teams"].as<JsonArray>();
  if (teams.isNull() || teams.size() == 0) {
    result.error = "couldn't find that team";
    return result;
  }
  JsonObject firstTeam = teams[0];
  const char* teamId = firstTeam["idTeam"] | "";
  const char* resolvedName = firstTeam["strTeam"] | team.c_str();
  if (strlen(teamId) == 0) {
    result.error = "couldn't find that team";
    return result;
  }

  String data = "team=" + String(resolvedName);

  JsonDocument lastDoc;
  if (httpGetJson(String(kApiBase) + "/eventslast.php?id=" + teamId, lastDoc)) {
    JsonArray lastResults = lastDoc["results"].as<JsonArray>();
    if (!lastResults.isNull() && lastResults.size() > 0) {
      JsonObject e = lastResults[0];
      const char* home = e["strHomeTeam"] | "?";
      const char* away = e["strAwayTeam"] | "?";
      const char* homeScore = e["intHomeScore"] | "?";
      const char* awayScore = e["intAwayScore"] | "?";
      const char* date = e["dateEvent"] | "?";
      data += ", last_event=" + String(home) + " " + String(homeScore) + "-" +
              String(awayScore) + " " + String(away) + " (" + String(date) + ")";
    }
  }

  JsonDocument nextDoc;
  if (httpGetJson(String(kApiBase) + "/eventsnext.php?id=" + teamId, nextDoc)) {
    JsonArray nextEvents = nextDoc["events"].as<JsonArray>();
    if (!nextEvents.isNull() && nextEvents.size() > 0) {
      JsonObject e = nextEvents[0];
      const char* home = e["strHomeTeam"] | "?";
      const char* away = e["strAwayTeam"] | "?";
      const char* date = e["dateEvent"] | "?";
      data += ", next_event=" + String(home) + " vs " + String(away) + " (" + String(date) + ")";
    }
  }

  if (data == "team=" + String(resolvedName)) {
    result.error = "found the team but no recent or upcoming games on the free tier";
    return result;
  }

  result.success = true;
  result.data = data;
  return result;
}
