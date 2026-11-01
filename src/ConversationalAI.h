// ConversationalAI.h — sends transcribed user speech to Claude
// (Anthropic's Messages API) with REX's personality system prompt
// (Personality.h), and returns both the spoken reply and a parsed
// face-expression directive. Knows nothing about the mic, TTS, or
// rendering — pure "text in, {speech, expression} out" boundary.
//
// Face commands are hardware directives, not conversational content
// (see CLAUDE.md and Personality.h's FACE DIRECTIVE section) — the
// model ends its reply with a separate [FACE:expression:intensity]
// tag, which is parsed out and applied literally here, never folded
// into the spoken text or inferred from sentiment.
#pragma once
#include <Arduino.h>
#include "EyeTypes.h"
#include "MemoryStore.h"

#ifdef ENABLE_TOOLS
#include "ToolManager.h"
#endif

struct AIResponse {
  String     speech;
  Expression expression = Expression::Neutral;
  float      intensity  = 0.6f;
  bool       ok = false;
};

class ConversationalAI {
public:
#ifdef ENABLE_TOOLS
  // Starts the tools layer's NTP sync (TimeTool) — call once in setup().
  // timers: the ONE shared TimerManager instance (owned in main.cpp),
  // forwarded to this instance's ToolManager so a timer set via a real
  // conversation lands in the same place as one set via a debug command.
  // memory: the ONE shared MemoryStore instance (owned in main.cpp) —
  // forwarded here (not into ToolManager — memory isn't a "tool" Claude
  // calls, it's folded straight into the system prompt) so a fact
  // learned via either path (live mic or /ask) lands in the same
  // persistent store, and both paths see everything learned so far.
  void begin(TimerManager& timers, MemoryStore& memory) {
    toolManager_.begin(timers);
    memory_ = &memory;
  }
#else
  void begin(MemoryStore& memory) { memory_ = &memory; }
#endif

  // Blocking — makes an HTTPS request and waits for the response
  // (typically 1-3s; with ENABLE_TOOLS, a tool-triggering question costs
  // a second round-trip, see requestWithTools()). Same limitation as
  // SpeechToTextClient: this freezes face animation for the duration; a
  // future version should move it to its own FreeRTOS task.
  AIResponse converse(const String& userText);

  // Clears the rolling conversation window (a fresh session).
  void resetConversation();

private:
  static constexpr int kMaxHistoryTurns = 8;  // last 4 user + 4 assistant turns
  String historyRole_[kMaxHistoryTurns];
  String historyText_[kMaxHistoryTurns];
  int    historyCount_ = 0;

  // Not owned — points at main.cpp's ONE shared instance, set by
  // begin() above. Null until begin() runs; every use site checks for
  // that rather than assuming it's always set.
  MemoryStore* memory_ = nullptr;

  void pushHistory(const String& role, const String& text);

  // Returns the model's raw final-turn text (tag still attached, parsed
  // by converse() same as always), or "" on any failure. Only one of
  // these two is ever compiled in, selected by ENABLE_TOOLS.
#ifdef ENABLE_TOOLS
  String requestWithTools(const String& userText);
  ToolManager toolManager_;
#else
  String buildRequestBody(const String& userText) const;
  String requestSimple(const String& userText) const;
#endif
};
