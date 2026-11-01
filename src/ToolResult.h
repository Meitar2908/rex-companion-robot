// ToolResult.h — the shared return type every Tool (WeatherTool,
// TimeTool, and future ones) hands back to ToolManager. Kept in its
// own header rather than inside ToolManager.h so WeatherTool.h/
// TimeTool.h can include it without a circular include (ToolManager.h
// includes both of them).
#pragma once
#include <Arduino.h>

struct ToolResult {
  bool   success = false;
  // Compact plain-text summary of the facts on success, e.g.
  // "location=Tel Aviv, temp_c=29, condition=Sunny" — deliberately not
  // nested JSON: this goes straight into a tool_result block for
  // Claude to read as text, no need to make it parse JSON itself. The
  // AI/personality layer decides how to phrase it; this is just facts.
  String data;
  // Short, human-readable reason on failure — fed back to the AI the
  // same way as `data`, so it can give an honest in-character "can't
  // reach the weather service" reply instead of a raw error dump.
  String error;
};
