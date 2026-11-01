#include "NewsTool.h"
#include "Secrets.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <string.h>

namespace {
constexpr int kMaxArticles = 3;
} // namespace

ToolResult NewsTool::search(const String& topic) {
  ToolResult result;

  if (topic.length() == 0) {
    result.error = "no topic given";
    return result;
  }
  if (WiFi.status() != WL_CONNECTED) {
    result.error = "no WiFi connection";
    return result;
  }

  String encodedTopic = topic;
  encodedTopic.replace(" ", "%20");

  String url = "https://gnews.io/api/v4/search?q=" + encodedTopic +
               "&lang=en&max=" + String(kMaxArticles) +
               "&apikey=" + GNEWS_API_KEY;

  HTTPClient http;
  http.setConnectTimeout(6000);
  http.setTimeout(8000);
  http.begin(url);
  Serial.print("[NEWS] GET "); Serial.println(url);
  uint32_t t0 = millis();
  int code = http.GET();
  Serial.print("[NEWS] returned "); Serial.print(code);
  Serial.print(" after "); Serial.print(millis() - t0); Serial.println(" ms");
  if (code != 200) {
    http.end();
    // 403/401 here almost always means a bad/missing key, 429 means the
    // free tier's daily quota (100/day) is used up — either way, say so
    // honestly rather than a generic "unavailable".
    if (code == 403 || code == 401) {
      result.error = "news service rejected the request (API key issue)";
    } else if (code == 429) {
      result.error = "news service's daily free quota is used up for today";
    } else {
      result.error = "news service unavailable";
    }
    return result;
  }
  String body = http.getString();
  http.end();

  JsonDocument doc;
  if (deserializeJson(doc, body) != DeserializationError::Ok) {
    result.error = "couldn't read the news service's response";
    return result;
  }

  JsonArray articles = doc["articles"].as<JsonArray>();
  if (articles.isNull() || articles.size() == 0) {
    result.error = "no recent news found on that topic";
    return result;
  }

  String data = "topic=" + topic;
  int count = 0;
  for (JsonObject article : articles) {
    if (count >= kMaxArticles) break;
    const char* title = article["title"] | "";
    const char* sourceName = article["source"]["name"] | "unknown source";
    const char* publishedAt = article["publishedAt"] | "";
    // publishedAt is a full UTC timestamp ("2026-09-30T19:38:25Z") — just
    // the date is plenty for REX's reply, no need for the AI to parse ISO.
    String dateOnly = String(publishedAt).substring(0, 10);
    if (strlen(title) == 0) continue;
    data += ", headline" + String(count + 1) + "=\"" + String(title) +
            "\" (" + String(sourceName) + (dateOnly.length() ? ", " + dateOnly : "") + ")";
    count++;
  }

  if (count == 0) {
    result.error = "no recent news found on that topic";
    return result;
  }

  result.success = true;
  result.data = data;
  return result;
}
