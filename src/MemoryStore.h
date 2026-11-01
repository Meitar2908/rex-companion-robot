// MemoryStore.h — REX's small persistent memory: short facts learned
// about the user across conversations, written via a [LEARN:fact] tag
// in the AI's own output (see ConversationalAI.cpp — same parsing shape
// as the existing [FACE:] directive) and reloaded at boot so REX
// doesn't start every session from zero. Backed by one JSON file on
// SPIFFS — the N16R8's default_16MB.csv partition table already
// provisions a spiffs partition (see platformio.ini), no extra setup.
//
// Deliberately a flat list of short strings, not a structured profile —
// simple enough for the AI itself to both write ("the user's dog is
// named Waffles") and read back naturally as prompt context, and simple
// enough to review/reset from Serial (/memory, /forget).
#pragma once
#include <Arduino.h>

class MemoryStore {
public:
  // Mounts SPIFFS and loads any existing facts — call once in setup().
  void begin();

  // Appends a new fact and saves immediately (so a mid-session power
  // loss doesn't lose it). Cheap de-dup: restating an existing fact is
  // a no-op. If already at kMaxFacts, the OLDEST fact is dropped to make
  // room — recent context matters more than an ever-growing log, and
  // this stays small enough to fit comfortably in one system-prompt
  // block.
  void learn(const String& fact);

  // The "what I remember about you" block folded into the system
  // prompt — empty string if nothing's been learned yet, so the prompt
  // doesn't carry a pointless empty section.
  String summaryForPrompt() const;

  // Debug: dumps every stored fact to Serial (/memory).
  void printAll() const;

  // Debug: wipes everything, on flash and in memory (/forget).
  void clearAll();

  int count() const { return factCount_; }

private:
  static constexpr int kMaxFacts = 24;
  String facts_[kMaxFacts];
  int    factCount_ = 0;
  bool   mounted_ = false;

  void load();
  void save() const;
};
