#include "MemoryStore.h"
#include <SPIFFS.h>
#include <ArduinoJson.h>

namespace {
constexpr const char* kPath = "/memory.json";
}

void MemoryStore::begin() {
  // true = format the partition automatically if it's never been
  // mounted before — the alternative is a permanently-failing mount on
  // first boot, which would silently disable persistence forever.
  mounted_ = SPIFFS.begin(true);
  if (!mounted_) {
    Serial.println("[MEMORY] SPIFFS mount failed — learned facts won't persist this session");
    return;
  }
  load();
}

void MemoryStore::load() {
  factCount_ = 0;
  if (!SPIFFS.exists(kPath)) return;

  File f = SPIFFS.open(kPath, "r");
  if (!f) return;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) {
    Serial.print("[MEMORY] failed to parse stored facts: ");
    Serial.println(err.c_str());
    return;
  }

  JsonArray arr = doc["facts"].as<JsonArray>();
  for (JsonVariant v : arr) {
    if (factCount_ >= kMaxFacts) break;
    const char* s = v.as<const char*>();
    if (s && s[0]) facts_[factCount_++] = String(s);
  }
  Serial.print("[MEMORY] loaded "); Serial.print(factCount_); Serial.println(" fact(s) from flash");
}

void MemoryStore::save() const {
  if (!mounted_) return;

  JsonDocument doc;
  JsonArray arr = doc["facts"].to<JsonArray>();
  for (int i = 0; i < factCount_; i++) arr.add(facts_[i]);

  File f = SPIFFS.open(kPath, "w");
  if (!f) {
    Serial.println("[MEMORY] failed to open memory.json for writing");
    return;
  }
  serializeJson(doc, f);
  f.close();
}

void MemoryStore::learn(const String& factIn) {
  String fact = factIn;
  fact.trim();
  if (fact.length() == 0) return;

  // Restating something already known isn't worth a second slot.
  for (int i = 0; i < factCount_; i++) {
    if (facts_[i].equalsIgnoreCase(fact)) return;
  }

  if (factCount_ < kMaxFacts) {
    facts_[factCount_++] = fact;
  } else {
    for (int i = 1; i < kMaxFacts; i++) facts_[i - 1] = facts_[i];
    facts_[kMaxFacts - 1] = fact;
  }
  Serial.print("[MEMORY] learned: "); Serial.println(fact);
  save();
}

String MemoryStore::summaryForPrompt() const {
  if (factCount_ == 0) return "";

  String block = "\n\nWHAT YOU REMEMBER ABOUT THIS USER (from past conversations — use it "
                 "naturally when relevant, never recite the list, never announce that you "
                 "\"remember\" something like a party trick):\n";
  for (int i = 0; i < factCount_; i++) {
    block += "- ";
    block += facts_[i];
    block += "\n";
  }
  return block;
}

void MemoryStore::printAll() const {
  if (factCount_ == 0) { Serial.println("[MEMORY] nothing learned yet"); return; }
  Serial.print("[MEMORY] "); Serial.print(factCount_); Serial.println(" fact(s):");
  for (int i = 0; i < factCount_; i++) {
    Serial.print("  "); Serial.print(i + 1); Serial.print(". "); Serial.println(facts_[i]);
  }
}

void MemoryStore::clearAll() {
  factCount_ = 0;
  save();  // writes an empty facts array — same shape load() already handles
  Serial.println("[MEMORY] cleared");
}
