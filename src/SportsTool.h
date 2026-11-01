// SportsTool.h — recent result and next fixture for a team via
// TheSportsDB's public free test key ("3") — documented, no personal
// signup needed. Free tier is schedule/completed-results only, not
// truly live in-progress scores.
#pragma once
#include <Arduino.h>
#include "ToolResult.h"

class SportsTool {
public:
  // team: a team name as the user said it ("Lakers", "Barcelona"). Returns
  // both the last result and next fixture in one result — the AI picks
  // whichever half answers the question actually asked.
  ToolResult lookup(const String& team);
};
