// ToolManager.h — routes Claude's structured tool-use requests to the
// tool that can fulfill them, and describes what's available so
// ConversationalAI never hardcodes tool definitions. Adding a future
// tool (news, real web search, ...) means adding one class and
// registering it in the .cpp here — nothing in ConversationalAI changes.
#pragma once
#include <ArduinoJson.h>
#include "ToolResult.h"
#include "WeatherTool.h"
#include "TimeTool.h"
#include "CalculatorTool.h"
#include "PlacesTool.h"
#include "SportsTool.h"
#include "WebSearchTool.h"
#include "NewsTool.h"
#include "TimerManager.h"

class ToolManager {
public:
  // timers: the ONE shared TimerManager instance (owned in main.cpp) —
  // passed by reference, not owned here, so a timer set through this
  // ToolManager and one set through another instance (e.g. main.cpp's
  // debug-command ToolManager) land in the same place.
  void begin(TimerManager& timers) {
    time_.begin();
    timers_ = &timers;
  }

  // Appends each tool's Claude-facing schema (name/description/
  // parameter shape) to the given array, in the exact shape Claude's
  // "tools" request field expects.
  void appendToolSchemas(JsonArray toolsArray) const;

  // toolName is whatever Claude's tool_use block named ("get_weather",
  // "get_time", ...); params is that block's "input" object.
  ToolResult execute(const String& toolName, JsonObjectConst params);

private:
  WeatherTool    weather_;
  TimeTool       time_;
  CalculatorTool calculator_;
  PlacesTool     places_;
  SportsTool     sports_;
  WebSearchTool  webSearch_;
  NewsTool       news_;
  TimerManager*  timers_ = nullptr;
};
