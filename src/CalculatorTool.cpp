#include "CalculatorTool.h"

namespace {

// Minimal recursive-descent parser: expr := term (('+'|'-') term)*,
// term := factor (('*'|'/') factor)*, factor := '-'factor | '('expr')' | number.
// No exceptions (commonly built without RTTI/exceptions on ESP32) — errors
// propagate via the `ok` flag, checked after every sub-parse.
struct Parser {
  const String& s;
  size_t i = 0;
  bool ok = true;

  explicit Parser(const String& str) : s(str) {}

  void skipSpaces() {
    while (i < s.length() && s[i] == ' ') i++;
  }

  char peek() {
    skipSpaces();
    return i < s.length() ? s[i] : '\0';
  }

  double parseExpr() {
    double v = parseTerm();
    while (ok) {
      char c = peek();
      if (c == '+') { i++; v += parseTerm(); }
      else if (c == '-') { i++; v -= parseTerm(); }
      else break;
    }
    return v;
  }

  double parseTerm() {
    double v = parseFactor();
    while (ok) {
      char c = peek();
      if (c == '*') { i++; v *= parseFactor(); }
      else if (c == '/') {
        i++;
        double d = parseFactor();
        if (ok && d == 0.0) { ok = false; return 0.0; }
        v /= d;
      } else break;
    }
    return v;
  }

  double parseFactor() {
    char c = peek();
    if (c == '-') { i++; return -parseFactor(); }
    if (c == '+') { i++; return parseFactor(); }
    if (c == '(') {
      i++;
      double v = parseExpr();
      if (peek() != ')') { ok = false; return 0.0; }
      i++;
      return v;
    }
    return parseNumber();
  }

  double parseNumber() {
    skipSpaces();
    size_t start = i;
    while (i < s.length() && (isDigit(s[i]) || s[i] == '.')) i++;
    if (i == start) { ok = false; return 0.0; }
    return s.substring(start, i).toDouble();
  }
};

} // namespace

ToolResult CalculatorTool::evaluate(const String& expression) {
  ToolResult result;

  // Reject anything outside the allowed character set up front — never
  // hand arbitrary text to the parser.
  for (size_t i = 0; i < expression.length(); i++) {
    char c = expression[i];
    bool allowed = isDigit(c) || c == '.' || c == '+' || c == '-' ||
                   c == '*' || c == '/' || c == '(' || c == ')' || c == ' ';
    if (!allowed) {
      result.error = "expression contains something I can't compute";
      return result;
    }
  }

  Parser p(expression);
  double value = p.parseExpr();
  p.skipSpaces();
  if (!p.ok || p.i != expression.length()) {
    result.error = "couldn't parse that expression";
    return result;
  }

  result.success = true;
  result.data = "expression=" + expression + ", result=" + String(value, 6);
  return result;
}
