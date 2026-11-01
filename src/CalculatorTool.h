// CalculatorTool.h — deterministic arithmetic, no external service. The
// AI is expected to translate the natural-language question into a plain
// expression itself ("18% of 450" -> "450*0.18", "70F to C" ->
// "(70-32)*5/9") — this class's only job is to compute it exactly, so a
// small/fast model never has to be trusted with arithmetic it might get
// subtly wrong.
#pragma once
#include <Arduino.h>
#include "ToolResult.h"

class CalculatorTool {
public:
  // expression: digits, decimals, + - * / ( ), unary minus. Anything else
  // (letters, unbalanced parens, division by zero) fails cleanly via
  // ToolResult.error rather than a garbage result.
  ToolResult evaluate(const String& expression);
};
