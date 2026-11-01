#include "WebSearchTool.h"
#include "Secrets.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <string.h>

namespace {
constexpr int kMaxResults = 3;
// Raw web extracts aren't pre-sized for REX's reply the way the other
// tools' fields are — bound each one so a long page excerpt doesn't blow
// up the JSON doc size or the follow-up AI call's token budget.
constexpr int kMaxSnippetChars = 220;
} // namespace

ToolResult WebSearchTool::search(const String& query) {
  ToolResult result;

  if (query.length() == 0) {
    result.error = "no search query given";
    return result;
  }
  if (WiFi.status() != WL_CONNECTED) {
    result.error = "no WiFi connection";
    return result;
  }

  JsonDocument requestDoc;
  requestDoc["query"] = query;
  requestDoc["max_results"] = kMaxResults;
  requestDoc["include_answer"] = true;
  String body;
  serializeJson(requestDoc, body);

  HTTPClient http;
  http.setConnectTimeout(6000);
  http.setTimeout(10000);
  http.begin("https://api.tavily.com/search");
  http.addHeader("Authorization", String("Bearer ") + TAVILY_API_KEY);
  http.addHeader("Content-Type", "application/json");

  Serial.print("[SEARCH] POST query="); Serial.println(query);
  uint32_t t0 = millis();
  int code = http.POST(body);
  Serial.print("[SEARCH] returned "); Serial.print(code);
  Serial.print(" after "); Serial.print(millis() - t0); Serial.println(" ms");

  if (code != 200) {
    Serial.println("[SEARCH] body: " + http.getString());
    http.end();
    // 401/403 here almost always means a bad/missing key, 429 means the
    // free tier's quota is used up — either way, say so honestly rather
    // than a generic "unavailable" (same precedent as NewsTool.cpp).
    if (code == 401 || code == 403) {
      result.error = "search service rejected the request (API key issue)";
    } else if (code == 429) {
      result.error = "search service's free quota is used up for now";
    } else {
      result.error = "search service unavailable";
    }
    return result;
  }

  String respBody = http.getString();
  http.end();

  JsonDocument respDoc;
  if (deserializeJson(respDoc, respBody)) {
    result.error = "couldn't read search results";
    return result;
  }

  const char* answer = respDoc["answer"] | "";
  String data = "query=" + query;

  if (strlen(answer) > 0) {
    // The common case: Tavily's own LLM already synthesized an answer
    // from the top web results — nothing more REX needs.
    data += ", answer=" + String(answer);
    result.success = true;
    result.data = data;
    return result;
  }

  // Fallback — no synthesized answer (can happen on ambiguous/sparse
  // queries). Hand the AI raw title+snippet pairs instead, same shape
  // NewsTool.cpp uses for its headline list.
  JsonArray results = respDoc["results"].as<JsonArray>();
  int count = 0;
  for (JsonObject r : results) {
    if (count >= kMaxResults) break;
    const char* title = r["title"] | "";
    const char* content = r["content"] | "";
    if (strlen(title) == 0 && strlen(content) == 0) continue;
    String snippet = String(content);
    if (snippet.length() > kMaxSnippetChars) {
      snippet = snippet.substring(0, kMaxSnippetChars) + "...";
    }
    data += ", result" + String(count + 1) + "=\"" + String(title) + "\": " + snippet;
    count++;
  }

  if (count == 0) {
    result.error = "no search results found for that query";
    return result;
  }

  result.success = true;
  result.data = data;
  return result;
}
