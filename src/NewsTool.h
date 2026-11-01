// NewsTool.h — recent headlines for a topic via GNews
// (gnews.io/dashboard — free tier, no credit card: 100 requests/day,
// results have a 12h delay, restricted to non-commercial use). Fills
// the gap WebSearchTool's DuckDuckGo instant-answer lookup explicitly
// can't: "latest news about X"-style questions.
#pragma once
#include <Arduino.h>
#include "ToolResult.h"

class NewsTool {
public:
  // topic: a subject as the user said it ("SpaceX", "the Lakers",
  // "AI regulation"). Returns up to 3 recent headlines with source and
  // date in one result — the AI picks and summarizes whichever are
  // relevant to what was actually asked.
  ToolResult search(const String& topic);
};
