#include "ConversationalAI.h"
#include "Personality.h"
#include "ExpressionTable.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include <stdio.h>
#include "Secrets.h"
#include "NetworkBudget.h"

#ifdef ENABLE_TOOLS
#include <ArduinoJson.h>
#endif

namespace {

String jsonEscape(const String& in) {
  String out;
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if (c == '"' || c == '\\') { out += '\\'; out += c; }
    else if (c == '\n') out += "\\n";
    else out += c;
  }
  return out;
}

// Pulls a top-level string field's value out of a flat JSON blob —
// good enough for Anthropic's response shape without a JSON library.
// Only used by the ENABLE_TOOLS-off path — the tools path uses
// ArduinoJson throughout, since Claude's tool_use/tool_result content
// is genuinely nested (arrays of blocks), not the flat fields this
// hand-rolled parser was built for.
String extractField(const String& json, const String& key) {
  String pat = "\"" + key + "\"";
  int k = json.indexOf(pat);
  if (k < 0) return "";
  int colon = json.indexOf(':', k);
  int firstQuote = json.indexOf('"', colon + 1);
  if (firstQuote < 0) return "";

  int i = firstQuote + 1;
  String out;
  while (i < (int)json.length() && json[i] != '"') {
    if (json[i] == '\\' && i + 1 < (int)json.length()) {
      i++;
      char c = json[i];
      if (c == 'n') out += '\n';
      else if (c == 't') out += '\t';
      else out += c;
    } else {
      out += json[i];
    }
    i++;
  }
  return out;
}

// Appended to kPersonalityPrompt at REQUEST time (kPersonalityPrompt
// itself stays a compile-time constant) so REX's mood can track the
// actual time of day instead of being static regardless of when you
// talk to it. Reads the process-wide local clock TimeTool::begin()
// already configured via configTzTime() at boot — no separate NTP call
// or timezone lookup needed here, just localtime_r() against the
// default location's zone.
String timeContextBlock() {
  time_t now = time(nullptr);
  if (now < 1700000000) return "";  // SNTP hasn't synced yet — say nothing rather than guess

  struct tm t;
  localtime_r(&now, &t);
  static const char* const kWeekday[7] = {
    "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
  };
  const char* partOfDay;
  if (t.tm_hour < 5)       partOfDay = "the middle of the night";
  else if (t.tm_hour < 12) partOfDay = "morning";
  else if (t.tm_hour < 17) partOfDay = "afternoon";
  else if (t.tm_hour < 21) partOfDay = "evening";
  else                      partOfDay = "late at night";

  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);

  String block = "\n\nCURRENT CONTEXT: it's ";
  block += buf;
  block += " on ";
  block += kWeekday[t.tm_wday];
  block += ", ";
  block += partOfDay;
  block += ". Let this genuinely color your mood if it fits naturally — sharper and less "
           "patient late at night, more upbeat in the morning — but don't announce the "
           "time and don't force a time-based line into every single reply.";
  return block;
}

} // namespace

void ConversationalAI::pushHistory(const String& role, const String& text) {
  if (historyCount_ < kMaxHistoryTurns) {
    historyRole_[historyCount_] = role;
    historyText_[historyCount_] = text;
    historyCount_++;
  } else {
    for (int i = 1; i < kMaxHistoryTurns; i++) {
      historyRole_[i - 1] = historyRole_[i];
      historyText_[i - 1] = historyText_[i];
    }
    historyRole_[kMaxHistoryTurns - 1] = role;
    historyText_[kMaxHistoryTurns - 1] = text;
  }
}

void ConversationalAI::resetConversation() {
  historyCount_ = 0;
}

#ifndef ENABLE_TOOLS

String ConversationalAI::buildRequestBody(const String& userText) const {
  String body = "{";
  body += "\"model\":\"claude-haiku-4-5-20251001\",";
  // v3's default is ONE short sentence (~5-15 words, see Personality.h)
  // with longer replies only on explicit request — 150 comfortably
  // covers even an explicit "explain X" request's few sentences while
  // capping runaway verbosity (and keeping generation, hence TTS-start,
  // latency down).
  body += "\"max_tokens\":150,";
  String memoryBlock = memory_ ? memory_->summaryForPrompt() : "";
  body += "\"system\":\"" + jsonEscape(String(kPersonalityPrompt) + timeContextBlock() + memoryBlock) + "\",";
  body += "\"messages\":[";
  for (int i = 0; i < historyCount_; i++) {
    body += "{\"role\":\"" + historyRole_[i] + "\",\"content\":\"" + jsonEscape(historyText_[i]) + "\"},";
  }
  body += "{\"role\":\"user\",\"content\":\"" + jsonEscape(userText) + "\"}";
  body += "]}";
  return body;
}

String ConversationalAI::requestSimple(const String& userText) const {
  String body = buildRequestBody(userText);

  HTTPClient http;
  // Previously unset — HTTPClient's default is a 5s STALL timeout (see
  // HTTPCLIENT_DEFAULT_TCP_TIMEOUT), which resets on every byte received
  // rather than bounding the TOTAL wait, so a slow drip-feed could run
  // well past 5s cumulative without ever tripping it. Explicit here
  // instead: measured live, a real reply typically lands in ~1.8-2.4s,
  // so 10s is generous slack while still being an actual ceiling.
  http.setConnectTimeout(5000);
  http.setTimeout(10000);
  http.begin("https://api.anthropic.com/v1/messages");
  http.addHeader("x-api-key", ANTHROPIC_API_KEY);
  http.addHeader("anthropic-version", "2023-06-01");
  http.addHeader("Content-Type", "application/json");

  uint32_t reqT0 = millis();
  int code = http.POST(body);
  uint32_t reqMs = millis() - reqT0;
  Serial.print("AI REQUEST SENT: yes ("); Serial.print(reqMs); Serial.println(" ms)");
  if (code != 200) {
    Serial.print("[AI] HTTP "); Serial.println(code);
    Serial.println(http.getString());
    http.end();
    return "";
  }

  String json = http.getString();
  http.end();

  String rawText = extractField(json, "text");
  if (rawText.length() == 0) Serial.println("[AI] no text in response");
  return rawText;
}

#else  // ENABLE_TOOLS

namespace {
String jvString(JsonVariantConst v) {
  const char* s = v | "";
  return String(s);
}
} // namespace

String ConversationalAI::requestWithTools(const String& userText) {
  JsonDocument requestDoc;
  requestDoc["model"] = "claude-haiku-4-5-20251001";
  requestDoc["max_tokens"] = 150;
  String memoryBlock = memory_ ? memory_->summaryForPrompt() : "";
  requestDoc["system"] = String(kPersonalityPrompt) + timeContextBlock() + memoryBlock;

  JsonArray tools = requestDoc["tools"].to<JsonArray>();
  toolManager_.appendToolSchemas(tools);

  JsonArray messages = requestDoc["messages"].to<JsonArray>();
  for (int i = 0; i < historyCount_; i++) {
    JsonObject m = messages.add<JsonObject>();
    m["role"] = historyRole_[i];
    m["content"] = historyText_[i];
  }
  JsonObject userMsg = messages.add<JsonObject>();
  userMsg["role"] = "user";
  userMsg["content"] = userText;

  // Bounds the WHOLE exchange — up to kMaxToolIterations Claude round
  // trips plus however long each tool call's own (now individually
  // budgeted, see NetworkBudget.h) network work takes. kMaxToolIterations
  // alone only capped the ITERATION COUNT, not wall-clock time, so a
  // genuinely slow connection could still stack 3 slow iterations back
  // to back with no overall ceiling. This is the last-resort backstop —
  // 20s comfortably covers 3 real (fast) round trips (~2s each measured
  // live) plus one tool call, while still being a firm stop against a
  // real connection problem rather than letting it run indefinitely.
  NetworkBudget budget(20000);

  const int kMaxToolIterations = 3;
  for (int iter = 0; iter < kMaxToolIterations; iter++) {
    if (budget.exhausted()) {
      Serial.println("[AI] giving up — taking too long (connection trouble?)");
      return "";
    }

    String body;
    serializeJson(requestDoc, body);

    HTTPClient http;
    uint32_t remaining = budget.remainingMs();
    http.setConnectTimeout(remaining < 5000 ? remaining : 5000);
    http.setTimeout(remaining < 8000 ? remaining : 8000);
    http.begin("https://api.anthropic.com/v1/messages");
    http.addHeader("x-api-key", ANTHROPIC_API_KEY);
    http.addHeader("anthropic-version", "2023-06-01");
    http.addHeader("Content-Type", "application/json");

    uint32_t reqT0 = millis();
    int code = http.POST(body);
    uint32_t reqMs = millis() - reqT0;
    Serial.print("AI REQUEST SENT: yes ("); Serial.print(reqMs); Serial.println(" ms)");
    if (code != 200) {
      Serial.print("[AI] HTTP "); Serial.println(code);
      Serial.println(http.getString());
      http.end();
      return "";
    }

    String respBody = http.getString();
    http.end();

    JsonDocument respDoc;
    if (deserializeJson(respDoc, respBody)) {
      Serial.println("[AI] couldn't parse response JSON");
      return "";
    }

    String stopReason = jvString(respDoc["stop_reason"]);
    JsonArray content = respDoc["content"].as<JsonArray>();

    if (stopReason != "tool_use") {
      for (JsonObject block : content) {
        if (jvString(block["type"]) == "text") return jvString(block["text"]);
      }
      Serial.println("[AI] no text block in final response");
      return "";
    }

    JsonObject toolUseBlock;
    bool foundToolUse = false;
    for (JsonObject block : content) {
      if (jvString(block["type"]) == "tool_use") {
        toolUseBlock = block;
        foundToolUse = true;
        break;
      }
    }
    if (!foundToolUse) {
      Serial.println("[AI] stop_reason was tool_use but no tool_use block found");
      return "";
    }

    String toolName = jvString(toolUseBlock["name"]);
    String toolUseId = jvString(toolUseBlock["id"]);
    JsonObjectConst toolInput = toolUseBlock["input"].as<JsonObjectConst>();

    Serial.print("AI TOOL DECISION: "); Serial.println(toolName);
    Serial.print("PARAMETERS: ");
    serializeJson(toolInput, Serial);
    Serial.println();

    uint32_t toolT0 = millis();
    ToolResult toolResult = toolManager_.execute(toolName, toolInput);
    uint32_t toolMs = millis() - toolT0;
    Serial.print("TOOL REQUEST: "); Serial.println(toolResult.success ? "SUCCESS" : "FAILED");
    Serial.print("RAW TOOL RESULT: "); Serial.println(toolResult.success ? toolResult.data : toolResult.error);
    Serial.print("TOOL LATENCY: "); Serial.print(toolMs); Serial.println(" ms");

    // Echo the assistant's tool_use block verbatim (Anthropic requires
    // this), then supply the tool_result — both deep-copied into
    // requestDoc, which outlives this iteration's respDoc.
    JsonObject assistantMsg = messages.add<JsonObject>();
    assistantMsg["role"] = "assistant";
    JsonArray assistantContent = assistantMsg["content"].to<JsonArray>();
    assistantContent.add(toolUseBlock);

    JsonObject toolResultMsg = messages.add<JsonObject>();
    toolResultMsg["role"] = "user";
    JsonArray toolResultContent = toolResultMsg["content"].to<JsonArray>();
    JsonObject toolResultBlock = toolResultContent.add<JsonObject>();
    toolResultBlock["type"] = "tool_result";
    toolResultBlock["tool_use_id"] = toolUseId;
    toolResultBlock["content"] = toolResult.success ? toolResult.data : ("Error: " + toolResult.error);
    if (!toolResult.success) toolResultBlock["is_error"] = true;
    // loop continues with the extended messages array
  }

  Serial.println("[AI] gave up after max tool iterations");
  return "";
}

#endif  // ENABLE_TOOLS

AIResponse ConversationalAI::converse(const String& userText) {
  AIResponse result;

  Serial.println("============================");
  Serial.println("TRANSCRIPT:");
  Serial.println("\"" + userText + "\"");
  Serial.print("SYSTEM/PERSONALITY: "); Serial.println(kPersonalityVersion);
  Serial.print("CONVERSATION CONTEXT: "); Serial.print(historyCount_ / 2);
  Serial.println(" previous turns");

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("AI REQUEST SENT: no (WiFi not connected)");
    Serial.println("============================");
    return result;
  }

#ifdef ENABLE_TOOLS
  String rawText = requestWithTools(userText);
#else
  String rawText = requestSimple(userText);
#endif

  if (rawText.length() == 0) {
    Serial.println("============================");
    return result;
  }

  // Split the [FACE:expression:intensity] AND [LEARN:fact] directives
  // off the spoken text — both parsed and applied literally, never
  // treated as part of what REX says (see Personality.h's FACE
  // DIRECTIVE and MEMORY sections). [LEARN:] follows the exact same
  // shape [FACE:] already used, just a second tag type in the same walk.
  //
  // Spoken text is always cut at the FIRST tag of EITHER kind, no
  // matter what — seen live (of [FACE:] specifically, but the same
  // failure mode applies to any tag): the model used an invalid
  // expression name ("warm"), then narrated a self-correction and
  // emitted a SECOND tag. Cutting at only the last occurrence (the old
  // code) left that first tag and the confused narration sitting in the
  // spoken text, which TTS then read aloud verbatim. Every occurrence of
  // both tags is still walked, so a later valid FACE tag correctly
  // overrides an earlier invalid one and a LEARN tag is captured
  // regardless of where amid the tags it falls — it's just never spoken
  // either way.
  String speech = rawText;
  int firstFace  = rawText.indexOf("[FACE:");
  int firstLearn = rawText.indexOf("[LEARN:");
  int cutStart = (firstFace < 0) ? firstLearn
               : (firstLearn < 0) ? firstFace
               : min(firstFace, firstLearn);

  if (cutStart >= 0) {
    speech = rawText.substring(0, cutStart);
    speech.trim();

    int searchFrom = cutStart;
    while (true) {
      int nextFace  = rawText.indexOf("[FACE:", searchFrom);
      int nextLearn = rawText.indexOf("[LEARN:", searchFrom);
      if (nextFace < 0 && nextLearn < 0) break;
      bool isLearn = (nextLearn >= 0) && (nextFace < 0 || nextLearn < nextFace);
      int tagStart = isLearn ? nextLearn : nextFace;

      int tagEnd = rawText.indexOf(']', tagStart);
      if (tagEnd <= tagStart) break;  // malformed tail, nothing more to parse

      int prefixLen = isLearn ? 7 : 6;  // strlen("[LEARN:") / strlen("[FACE:")
      String payload = rawText.substring(tagStart + prefixLen, tagEnd);

      if (isLearn) {
        payload.trim();
        if (payload.length() > 0 && memory_) memory_->learn(payload);
      } else {
        int sep = payload.indexOf(':');
        String exprName = sep >= 0 ? payload.substring(0, sep) : payload;
        float intensity = sep >= 0 ? payload.substring(sep + 1).toFloat() : 0.6f;
        Expression e;
        if (expressionFromName(exprName, e)) {
          result.expression = e;
          result.intensity = constrain(intensity, 0.0f, 1.0f);
        }
      }
      searchFrom = tagEnd + 1;
    }
  }

  result.speech = speech;
  result.ok = true;

  Serial.println("AI RAW RESPONSE:");
  Serial.println("\"" + rawText + "\"");
  Serial.println("FINAL SPEECH:");
  Serial.println("\"" + speech + "\"");
  Serial.print("EXPRESSION: "); Serial.println(expressionName(result.expression));
  Serial.print("INTENSITY: "); Serial.println(result.intensity, 2);
  Serial.println("============================");

  // Store the cleaned speech, not rawText — if the model ever emits
  // stray tag syntax or confused narration (see above), that must not
  // get fed back in as if it were a normal part of the conversation
  // history, or it'll keep confusing later turns too. The tool_use/
  // tool_result exchange itself (if any) is never persisted here —
  // only this final Q&A pair, keeping follow-up context and token cost
  // the same regardless of whether a tool fired this turn.
  pushHistory("user", userText);
  pushHistory("assistant", speech);
  return result;
}
