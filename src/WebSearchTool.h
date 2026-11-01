// WebSearchTool.h — general web search via Tavily (app.tavily.com, free
// tier, signed-up key required — see Secrets.h's TAVILY_API_KEY).
//
// Previously this wrapped DuckDuckGo's keyless Instant Answer API, which
// is infobox/definition lookups only (no general web index) — it came
// back empty for anything like "box office of X" or most real-world
// specific-fact questions. Tavily is a real indexed web search with an
// LLM-generated answer synthesized from the top results, which is what
// this tool now returns.
#pragma once
#include <Arduino.h>
#include "ToolResult.h"

class WebSearchTool {
public:
  ToolResult search(const String& query);
};
