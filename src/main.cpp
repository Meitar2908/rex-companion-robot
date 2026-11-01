// REX — expressive face prototype
//
// Serial text (always) or live microphone (ENABLE_VOICE build flag)
//   -> ReactionEngine      (deterministic keyword classifier — a stand-in
//                           for a future full conversational AI)
//   -> CharacterController (Idle/Listening/Thinking/Reacting/Speaking)
//   -> EyeAnimator / MouthAnimator (non-blocking attack/hold/release blending)
//   -> EyeRenderer / MouthRenderer (procedural TFT drawing, sharing one DisplayDriver)
//
// Type a sentence and press enter to react to it. Debug commands:
//   STATE:idle|listening|thinking|speaking
//   /emotion <name>       apply an expression directly, at the current /intensity
//   /intensity <0..1>     set the intensity used by the next /emotion
//   /look <x> <y>         direct gaze override, -1..1 each axis
//   /speak [emotion]      enter Speaking (mouth waveform), optionally setting the baseline emotion first
//   /stop                 back to Idle
//   /mouth <name>
//
// Voice input (off by default — see platformio.ini's ENABLE_VOICE):
//   mic (AudioInput, I2S0) -> VoiceActivityDetector -> buffered PCM
//   -> SpeechToTextClient (OpenAI Whisper API over WiFi) -> text.
//   That text is purely an internal handoff, never shown to the
//   user — it goes straight to ConversationalAI (if ENABLE_AI) for a
//   real reply + face expression, spoken back via TextToSpeechClient
//   (if ENABLE_TTS). Falls back to the local ReactionEngine keyword
//   classifier (a silent face reaction) if AI is unavailable or a
//   call fails, so REX still reacts to something. See
//   VoiceController.cpp for the full state machine. Requires the mic
//   wired per CLAUDE.md's pin map and src/Secrets.h filled in (copy
//   from Secrets.example.h — never commit Secrets.h). Mic and
//   amp/speaker are both confirmed wired and working in isolation
//   (see /mictest, /tonetest); the full round-trip is not yet
//   validated end to end.
//
// Conversational AI (off by default — see platformio.ini's
// ENABLE_AI): REX's actual personality (Personality.h, ported from
// an earlier build) via Claude — /ask <text>, or transcribed speech
// via VoiceController above — returns a spoken reply plus a literal
// [FACE:expression:intensity] directive parsed out of the model's
// own output (see CLAUDE.md: face commands are hardware directives,
// not conversational content). Independent of ENABLE_VOICE so it can
// be tested via Serial on its own first.
//
// Text-to-speech (off by default — see platformio.ini's ENABLE_TTS):
// TextToSpeechClient streams ElevenLabs' raw PCM output straight to
// AudioOutput (the amp/speaker) as it arrives. Independent of
// ENABLE_AI/ENABLE_VOICE too — /say <text> plays arbitrary text on
// its own; when combined with either, REX's actual replies get
// spoken instead of only printed.
//
// Architecture is layered so voice, AI, and speech output can call
// CharacterController::onReaction() and ::setState() exactly the way
// the Serial loop does — nothing in EyeAnimator, MouthAnimator,
// EyeRenderer, or MouthRenderer needs to change.

#include <Arduino.h>
#include <math.h>
#include "DebugMirror.h"
#include "EyeTypes.h"
#include "ExpressionTable.h"
#include "ReactionEngine.h"
#include "CharacterController.h"
#include "DisplayDriver.h"
#include "FaceFrame.h"
#include "AudioInput.h"
#include "AudioOutput.h"

#if defined(ENABLE_VOICE) || defined(ENABLE_AI) || defined(ENABLE_TTS) || defined(ENABLE_TOOLS)
#include <WiFi.h>
#include "Secrets.h"
#endif
#ifdef ENABLE_VOICE
// EXPERIMENTAL switch — see StreamingVoiceController.h. With
// ENABLE_STREAMING_STT off, this preprocesses to exactly what it always
// was: #include "VoiceController.h" + using ActiveVoiceController =
// VoiceController. VoiceController.cpp/.h are never touched by this
// switch either way.
#if defined(ENABLE_STREAMING_STT)
#include "StreamingVoiceController.h"
using ActiveVoiceController = StreamingVoiceController;
#else
#include "VoiceController.h"
using ActiveVoiceController = VoiceController;
#endif
#endif
#if defined(ENABLE_VOICE) && defined(ENABLE_STREAMING_STT)
// EXPERIMENTAL — see StreamingSpeechToTextClient.h. Also used directly by
// the /resampletest and /streamtest debug commands below (Milestones 0-2
// of the isolated streaming-STT plan).
#include "StreamingSpeechToTextClient.h"
#endif
#ifdef ENABLE_AI
#include "ConversationalAI.h"
#endif
#ifdef ENABLE_TTS
#include "TextToSpeechClient.h"
#endif
#ifdef ENABLE_TOOLS
#include "ToolManager.h"
#endif
// Standalone mic RMS-level test (/mictest on|off) — no WiFi/API keys
// needed, just AudioInput. Verifies the physical I2S wiring works
// before the full voice pipeline (which also needs WiFi + Secrets.h).
static AudioInput micTest;
static bool       micTestBegun  = false;
static bool       micTestActive = false;
static uint32_t   lastMicPrintMs = 0;

#if defined(ENABLE_VOICE) && defined(ENABLE_STREAMING_STT)
// EXPERIMENTAL — standalone test instance for /streamtest, kept
// completely separate from VoiceController/StreamingVoiceController so
// Milestones 0-2 can be verified before any real wiring exists.
static StreamingSpeechToTextClient streamTestClient;
static bool                        streamTestActive = false;
#endif

// Standalone amp/speaker test tone (/tonetest [hz]) — no WiFi needed,
// just AudioOutput. Verifies the amp+speaker path (build order item
// 3). The same AudioOutput instance is reused by /say and /ask's
// spoken replies, so there's only ever one I2S1 TX driver installed.
static AudioOutput audioOut;
static bool        audioOutBegun = false;

static void ensureAudioOutBegun() {
  if (!audioOutBegun) { audioOut.begin(); audioOutBegun = true; }
}

static DisplayDriver        display;
static FaceFrame            faceFrame;
static CharacterController  character;
static ReactionEngine       reactionEngine;
static String               lineBuf;

#ifdef ENABLE_VOICE
static ActiveVoiceController voiceController;

// Physical push-button toggles the live mic on/off — one leg to this
// pin, the other to the shared ground (per the perfboard rewiring),
// using the internal pull-up so idle reads HIGH and a press reads
// LOW. GPIO18 is free: not used by the display (9-14, 21), mic
// (4-6), or amp (7, 15, 16), and not one of CLAUDE.md's reserved
// pins (19/20 native USB, 35-37 octal PSRAM).
constexpr int kListenButtonPin = 18;
constexpr uint32_t kListenButtonDebounceMs = 30;
static bool     listenButtonLastRaw  = HIGH;
static bool     listenButtonStable   = HIGH;
static uint32_t listenButtonChangeMs = 0;
#endif
#ifdef ENABLE_AI
static ConversationalAI conversationalAI;

// The ONE shared persistent-memory store — both conversationalAI's and
// voiceController's internal ConversationalAI are given a reference to
// this same instance (see setup()), same reasoning as TimerManager
// below: a fact learned via /ask or via a real spoken conversation must
// land in the one place both paths read from, not two separate stores
// that silently diverge.
static MemoryStore memoryStore;
#endif
#ifdef ENABLE_TTS
static TextToSpeechClient ttsClient;
// Set when /say or /ask kicks off ttsClient.speakAsync() — polled in
// loop() so it can drop the character back to Idle and print the
// result once the background task finishes. See VoiceController.cpp
// for the same pattern on the live-voice path.
static bool ttsCmdPending = false;
// True between kicking off speech and the first audio actually reaching
// the speaker — gates when the mouth's talking waveform starts.
static bool ttsPendingAudio = false;
#endif
#ifdef ENABLE_TOOLS
// Standalone instance for /weather, /time, /calc, /places, /sports,
// /search — lets those be tested without ENABLE_AI wired in, same
// "validate each piece alone first" pattern as /mictest and /tonetest.
// ConversationalAI owns its own separate ToolManager for the real
// conversational path; both are stateless enough (Open-Meteo/SNTP, no
// hardware singleton) that having two instances costs nothing
// meaningful — EXCEPT timers, which is exactly why TimerManager below
// is a single shared instance passed into both rather than owned by
// either ToolManager.
static ToolManager toolManagerDebug;

// The ONE shared timer/reminder store — both toolManagerDebug's and
// conversationalAI's internal ToolManager are given a reference to this
// same instance (see setup()), so a timer set through /timer or through
// a real spoken "set a timer for five minutes" lands in the same place.
// loop() polls checkDue() on this directly to know when REX should
// speak up on its own — see speakAiResponse()/loop() below.
static TimerManager timerManager;
#endif
static float                pendingIntensity = 0.8f;

#ifdef ENABLE_VOICE
// Debounced edge-detect: toggles listening on the press (HIGH->LOW),
// ignores the release. Mirrors the /listen serial command's behavior,
// including dropping the face back to Idle when muting mid-reaction.
static void pollListenButton() {
  bool raw = digitalRead(kListenButtonPin);
  uint32_t now = millis();
  if (raw != listenButtonLastRaw) {
    listenButtonLastRaw = raw;
    listenButtonChangeMs = now;
  }
  if (now - listenButtonChangeMs > kListenButtonDebounceMs && listenButtonStable != listenButtonLastRaw) {
    listenButtonStable = listenButtonLastRaw;
    if (listenButtonStable == LOW) {
      bool enable = !voiceController.isListening();
      voiceController.setListening(enable);
      if (!enable) character.setState(CharacterState::Idle, now);
      Serial.println(enable ? "[VOICE] mic listening (button)" : "[VOICE] mic muted (button)");
      usbMirror(enable ? "[VOICE] mic listening (button)" : "[VOICE] mic muted (button)");
      Serial.print("[DBG] button press t="); Serial.println(now);  // TEMP DEBUG
    }
  }
}
#endif

#ifdef ENABLE_AI
// Applies an AIResponse's face reaction — no speech. Factored out so
// both /ask (which may or may not have TTS) and speakAiResponse() below
// share one implementation instead of three copies of the same reaction
// logic.
//
// sticky selects which of CharacterController's two persistence models
// is right for the caller:
//   false — onReaction()'s one-shot: attacks in, holds, then RELEASES
//           back to neutral on its own. Correct when nothing else is
//           ever going to move the face on (the no-TTS /ask fallback —
//           there's no "speaking" phase coming to take over).
//   true  — setBaselineEmotion()'s sticky hold: attacks in and then just
//           stays, indefinitely. Required when speech IS about to
//           follow (speakAiResponse()): a one-shot's attack+hold+release
//           can be as short as ~900ms total, well under the 0.85-1.8s
//           TTS measured live between the request going out and the
//           first audio byte arriving — so REX would flash back to a
//           static neutral face mid-wait, then start speaking with that
//           stale neutral pose already showing (Speaking deliberately
//           doesn't re-apply an expression of its own). Sticky holds
//           through that whole gap; the eventual return to Idle sets
//           its own neutral pose on purpose, not as an accidental race.
static void reactToAi(const AIResponse& ai, uint32_t now, bool sticky) {
  Serial.print("REX: "); Serial.println(ai.speech);
  Serial.print("[FACE] "); Serial.print(expressionName(ai.expression));
  Serial.print(" @ "); Serial.println(ai.intensity, 2);

  if (sticky) {
    character.setBaselineEmotion(ai.expression, ai.intensity, now);
    return;
  }
  Reaction r;
  r.expression = ai.expression;
  r.intensity  = ai.intensity;
  const ExpressionDef& def = expressionDef(ai.expression);
  r.gazeX = def.gazeX; r.gazeY = def.gazeY; r.blink = def.blink;
  r.attackMs = def.attackMs; r.holdMs = def.holdMs; r.releaseMs = def.releaseMs;
  character.onReaction(r, now);
}
#endif

#if defined(ENABLE_AI) && defined(ENABLE_TTS)
// Reacts AND speaks — the shared tail used by /ask and (see loop()) by
// a timer firing on its own. Speaking keeps the emotion reactToAi() just
// set as the mouth's baseline and layers the talking waveform on top of
// it — see CharacterController::setState(Speaking)'s own comment.
static void speakAiResponse(const AIResponse& ai, uint32_t now) {
  reactToAi(ai, now, /*sticky=*/true);
  ensureAudioOutBegun();
  // Speaking is NOT set here — loop() promotes to it once
  // ttsClient.firstAudioMs() shows audio actually reaching the speaker.
  // The TTS request takes ~0.8-1.8s, and animating the mouth through
  // that made REX look like it was talking before any sound came out.
  ttsClient.speakAsync(ai.speech, audioOut);
  ttsCmdPending = true;
  ttsPendingAudio = true;
}
#endif

static const char* blinkName(BlinkPattern p) {
  switch (p) {
    case BlinkPattern::Single: return "SINGLE";
    case BlinkPattern::Double: return "DOUBLE";
    case BlinkPattern::Fast:   return "FAST";
    case BlinkPattern::Slow:   return "SLOW";
    default:                   return "NONE";
  }
}

static void printReaction(const String& input, const Reaction& r) {
  Serial.println();
  Serial.println("---");
  Serial.println();
  Serial.println("INPUT:");
  Serial.println(input);
  Serial.println();
  Serial.println("REACTION:");
  Serial.println(expressionName(r.expression));
  Serial.println();
  Serial.println("INTENSITY:");
  Serial.println(r.intensity, 2);
  Serial.println();
  Serial.println("GAZE:");
  Serial.print(r.gazeX, 2); Serial.print(", "); Serial.println(r.gazeY, 2);
  Serial.println();
  Serial.println("BLINK:");
  Serial.println(blinkName(r.blink));
  Serial.println();
  Serial.println("ATTACK:");
  Serial.print(r.attackMs); Serial.println(" ms");
  Serial.println();
  Serial.println("HOLD:");
  Serial.print(r.holdMs); Serial.println(" ms");
  Serial.println();
  Serial.println("RELEASE:");
  Serial.print(r.releaseMs); Serial.println(" ms");
  Serial.println();
  Serial.println("---");
}

static void handleSlashCommand(const String& line) {
  int sp = line.indexOf(' ');
  String cmd = sp < 0 ? line : line.substring(0, sp);
  String rest = sp < 0 ? String("") : line.substring(sp + 1);
  rest.trim();
  cmd.toLowerCase();
  uint32_t now = millis();

  if (cmd == "/emotion") {
    Expression e;
    if (!expressionFromName(rest, e)) { Serial.println("Unknown emotion."); return; }
    Reaction r;
    r.expression = e;
    r.intensity = pendingIntensity;
    const ExpressionDef& def = expressionDef(e);
    r.gazeX = def.gazeX; r.gazeY = def.gazeY; r.blink = def.blink;
    r.attackMs = def.attackMs; r.holdMs = def.holdMs; r.releaseMs = def.releaseMs;
    printReaction(line, r);
    character.onReaction(r, now);

  } else if (cmd == "/intensity") {
    float v = rest.toFloat();
    pendingIntensity = constrain(v, 0.0f, 1.0f);
    Serial.print("intensity -> "); Serial.println(pendingIntensity, 2);

  } else if (cmd == "/look") {
    int sp2 = rest.indexOf(' ');
    if (sp2 < 0) { Serial.println("Usage: /look <x> <y>"); return; }
    float x = rest.substring(0, sp2).toFloat();
    float y = rest.substring(sp2 + 1).toFloat();
    character.setGazeOverride(x, y);
    Serial.print("look -> "); Serial.print(x, 2); Serial.print(", "); Serial.println(y, 2);

  } else if (cmd == "/speak") {
    if (rest == "off") {
      character.setState(CharacterState::Idle, now);
      Serial.println("STATE -> idle");
    } else {
      if (rest.length() > 0 && rest != "on") {
        Expression e;
        if (expressionFromName(rest, e)) character.setBaselineEmotion(e, pendingIntensity, now);
      }
      character.setState(CharacterState::Speaking, now);
      Serial.println("STATE -> speaking");
    }

  } else if (cmd == "/stop") {
    character.setState(CharacterState::Idle, now);
    Serial.println("STATE -> idle");

  } else if (cmd == "/listen") {
#ifdef ENABLE_VOICE
    if (rest == "off") {
      voiceController.setListening(false);
      character.setState(CharacterState::Idle, now);
      Serial.println("[VOICE] mic muted");
    } else if (rest == "on") {
      voiceController.setListening(true);
      Serial.println("[VOICE] mic listening");
    } else {
      Serial.print("Listening: "); Serial.println(voiceController.isListening() ? "on" : "off");
      Serial.println("Usage: /listen on|off");
    }
#else
    Serial.println("Not compiled in — build with -DENABLE_VOICE.");
#endif

  } else if (cmd == "/volume") {
    // Maps 0-100 onto AudioOutput's gain range (0.0-3.0 — see
    // AudioOutput.h: above ~3.0 the soft limiter dominates and it
    // stops getting louder, just more compressed). Default (2.4)
    // corresponds to 80.
    constexpr float kMaxVolumeGain = 3.0f;
    if (rest.length() == 0) {
      Serial.print("Volume: "); Serial.print((int)(audioOut.volume() / kMaxVolumeGain * 100.0f));
      Serial.println("  (usage: /volume 0-100)");
    } else {
      int pct = rest.toInt();
      if (pct < 0) pct = 0;
      if (pct > 100) pct = 100;
      audioOut.setVolume((pct / 100.0f) * kMaxVolumeGain);
      Serial.print("Volume -> "); Serial.print(pct); Serial.println("%");
    }

  } else if (cmd == "/weather") {
#ifdef ENABLE_TOOLS
    JsonDocument params;
    if (rest.length() > 0) params["location"] = rest;
    ToolResult r = toolManagerDebug.execute("get_weather", params.as<JsonObjectConst>());
    Serial.println(r.success ? ("[WEATHER] " + r.data) : ("[WEATHER] failed: " + r.error));
#else
    Serial.println("Not compiled in — build with -DENABLE_TOOLS.");
#endif

  } else if (cmd == "/time") {
#ifdef ENABLE_TOOLS
    JsonDocument params;
    if (rest.length() > 0) params["location"] = rest;
    ToolResult r = toolManagerDebug.execute("get_time", params.as<JsonObjectConst>());
    Serial.println(r.success ? ("[TIME] " + r.data) : ("[TIME] failed: " + r.error));
#else
    Serial.println("Not compiled in — build with -DENABLE_TOOLS.");
#endif

  } else if (cmd == "/calc") {
#ifdef ENABLE_TOOLS
    if (rest.length() == 0) { Serial.println("Usage: /calc <expression>"); return; }
    JsonDocument params;
    params["expression"] = rest;
    ToolResult r = toolManagerDebug.execute("calculate", params.as<JsonObjectConst>());
    Serial.println(r.success ? ("[CALC] " + r.data) : ("[CALC] failed: " + r.error));
#else
    Serial.println("Not compiled in — build with -DENABLE_TOOLS.");
#endif

  } else if (cmd == "/places") {
#ifdef ENABLE_TOOLS
    if (rest.length() == 0) { Serial.println("Usage: /places <category> [city]"); return; }
    String category = rest;
    String location;
    int sp = rest.indexOf(' ');
    if (sp >= 0) {
      category = rest.substring(0, sp);
      location = rest.substring(sp + 1);
      location.trim();
    }
    JsonDocument params;
    params["category"] = category;
    if (location.length() > 0) params["location"] = location;
    ToolResult r = toolManagerDebug.execute("find_places", params.as<JsonObjectConst>());
    Serial.println(r.success ? ("[PLACES] " + r.data) : ("[PLACES] failed: " + r.error));
#else
    Serial.println("Not compiled in — build with -DENABLE_TOOLS.");
#endif

  } else if (cmd == "/sports") {
#ifdef ENABLE_TOOLS
    if (rest.length() == 0) { Serial.println("Usage: /sports <team>"); return; }
    JsonDocument params;
    params["team"] = rest;
    ToolResult r = toolManagerDebug.execute("get_sports_results", params.as<JsonObjectConst>());
    Serial.println(r.success ? ("[SPORTS] " + r.data) : ("[SPORTS] failed: " + r.error));
#else
    Serial.println("Not compiled in — build with -DENABLE_TOOLS.");
#endif

  } else if (cmd == "/search") {
#ifdef ENABLE_TOOLS
    if (rest.length() == 0) { Serial.println("Usage: /search <query>"); return; }
    JsonDocument params;
    params["query"] = rest;
    ToolResult r = toolManagerDebug.execute("web_search", params.as<JsonObjectConst>());
    Serial.println(r.success ? ("[SEARCH] " + r.data) : ("[SEARCH] failed: " + r.error));
#else
    Serial.println("Not compiled in — build with -DENABLE_TOOLS.");
#endif

  } else if (cmd == "/news") {
#ifdef ENABLE_TOOLS
    if (rest.length() == 0) { Serial.println("Usage: /news <topic>"); return; }
    JsonDocument params;
    params["topic"] = rest;
    ToolResult r = toolManagerDebug.execute("get_news", params.as<JsonObjectConst>());
    Serial.println(r.success ? ("[NEWS] " + r.data) : ("[NEWS] failed: " + r.error));
#else
    Serial.println("Not compiled in — build with -DENABLE_TOOLS.");
#endif

  } else if (cmd == "/timer") {
#ifdef ENABLE_TOOLS
    if (rest.length() == 0) { Serial.println("Usage: /timer <seconds> [label]"); return; }
    String secondsStr = rest;
    String label;
    int sp = rest.indexOf(' ');
    if (sp >= 0) {
      secondsStr = rest.substring(0, sp);
      label = rest.substring(sp + 1);
      label.trim();
    }
    JsonDocument params;
    params["seconds"] = secondsStr.toInt();
    if (label.length() > 0) params["label"] = label;
    ToolResult r = toolManagerDebug.execute("set_timer", params.as<JsonObjectConst>());
    Serial.println(r.success ? ("[TIMER] set: " + r.data) : ("[TIMER] failed: " + r.error));
#else
    Serial.println("Not compiled in — build with -DENABLE_TOOLS.");
#endif

  } else if (cmd == "/memory") {
#ifdef ENABLE_AI
    memoryStore.printAll();
#else
    Serial.println("Not compiled in — build with -DENABLE_AI.");
#endif

  } else if (cmd == "/forget") {
#ifdef ENABLE_AI
    memoryStore.clearAll();
#else
    Serial.println("Not compiled in — build with -DENABLE_AI.");
#endif

  } else if (cmd == "/mouth") {
    Expression e;
    if (!expressionFromName(rest, e)) { Serial.println("Unknown emotion."); return; }
    character.setMouthOnly(e, pendingIntensity, now);
    Serial.print("mouth -> "); Serial.println(expressionName(e));

  } else if (cmd == "/mictest") {
    if (rest == "on") {
      // Same reasoning as the old /wakeword's handler used to have: skip
      // a second I2S0 install when voiceController.begin() already did
      // one at boot.
#ifndef ENABLE_VOICE
      if (!micTestBegun) { micTest.begin(); micTestBegun = true; }
#endif
#if defined(ENABLE_VOICE) && defined(ENABLE_WAKEWORD)
      // VoiceController polls the mic for "Hey REX" continuously while
      // muted (see VoiceController::update()) — pause that so this test's
      // own reads off the same I2S0 hardware aren't split between two
      // readers.
      voiceController.setWakeWordPaused(true);
#endif
      micTestActive = true;
      Serial.println("mic test -> on (printing RMS levels)");
      usbMirror("mic test -> on (printing RMS levels)");
    } else {
      micTestActive = false;
#if defined(ENABLE_VOICE) && defined(ENABLE_WAKEWORD)
      voiceController.setWakeWordPaused(false);
#endif
      Serial.println("mic test -> off");
      usbMirror("mic test -> off");
    }

  } else if (cmd == "/tonetest") {
    ensureAudioOutBegun();
    float freq = rest.length() > 0 ? rest.toFloat() : 440.0f;
    if (freq <= 0) freq = 440.0f;
    Serial.print("tone test -> playing "); Serial.print(freq, 0); Serial.println(" Hz for 1s");

    const uint32_t sr = AudioOutput::kSampleRate;
    const int totalSamples = (int)sr;  // 1 second
    const int chunkSize = 256;
    int16_t chunk[chunkSize];
    float phase = 0.0f;
    float phaseInc = 2.0f * PI * freq / (float)sr;
    int samplesLeft = totalSamples;
    while (samplesLeft > 0) {
      int n = samplesLeft < chunkSize ? samplesLeft : chunkSize;
      for (int i = 0; i < n; i++) {
        chunk[i] = (int16_t)(sinf(phase) * 8000.0f);  // moderate volume, avoid clipping
        phase += phaseInc;
        if (phase > 2.0f * PI) phase -= 2.0f * PI;
      }
      audioOut.writeSamples(chunk, n);
      samplesLeft -= n;
    }
    Serial.println("tone test -> done");

  } else if (cmd == "/resampletest") {
#if defined(ENABLE_VOICE) && defined(ENABLE_STREAMING_STT)
    // Milestone 0 of the isolated streaming-STT plan — pure offline check
    // of StreamingSpeechToTextClient::resample16kTo24k, zero network, zero
    // mic. Feeds a synthetic 16kHz sine through it in the same 256-sample
    // chunks the real pipeline uses, and checks (a) the output count lands
    // very close to input*3/2 and (b) no sample-to-sample jump is wildly
    // larger than a smooth sine's own max slope would produce — a chunk-
    // boundary bug (dropped/duplicated/misaligned sample) would show up
    // as an outlier here.
    Serial.println("resample test -> 1s of 440Hz @ 16kHz, through resample16kTo24k in 256-sample chunks");
    const uint32_t sr16 = 16000;
    const int totalIn = (int)sr16;
    const int chunkSize = 256;
    const float freq = 440.0f;
    float phase = 0.0f;
    const float phaseInc = 2.0f * PI * freq / (float)sr16;
    StreamingSpeechToTextClient::ResampleState state;
    int16_t inChunk[chunkSize];
    int16_t outChunk[400];
    size_t totalOut = 0;
    bool havePrevOut = false;
    int16_t prevOut = 0;
    int maxJump = 0;
    int samplesLeft = totalIn;
    while (samplesLeft > 0) {
      int n = samplesLeft < chunkSize ? samplesLeft : chunkSize;
      for (int i = 0; i < n; i++) {
        inChunk[i] = (int16_t)(sinf(phase) * 8000.0f);
        phase += phaseInc;
        if (phase > 2.0f * PI) phase -= 2.0f * PI;
      }
      size_t outN = StreamingSpeechToTextClient::resample16kTo24k(inChunk, n, outChunk, 400, state);
      for (size_t i = 0; i < outN; i++) {
        if (havePrevOut) {
          int jump = abs((int)outChunk[i] - (int)prevOut);
          if (jump > maxJump) maxJump = jump;
        }
        prevOut = outChunk[i];
        havePrevOut = true;
      }
      totalOut += outN;
      samplesLeft -= n;
    }
    // Max slope of this sine at 24kHz: amplitude * 2*pi*freq/samplerate.
    int expectedMaxSlope = (int)(8000.0f * 2.0f * PI * freq / 24000.0f);
    Serial.print("resample test -> input="); Serial.print(totalIn);
    Serial.print(" output="); Serial.print(totalOut);
    Serial.print(" expected~="); Serial.println(totalIn * 3 / 2);
    Serial.print("resample test -> max sample-to-sample jump="); Serial.print(maxJump);
    Serial.print(" (smooth-sine expected~<="); Serial.print(expectedMaxSlope);
    Serial.println(" — a much larger spike means a chunk-boundary bug)");
#else
    Serial.println("Not compiled in — build with -DENABLE_VOICE and -DENABLE_STREAMING_STT.");
#endif

  } else if (cmd == "/streamtest") {
#if defined(ENABLE_VOICE) && defined(ENABLE_STREAMING_STT)
    // Milestones 1-2 of the isolated streaming-STT plan. Standalone —
    // streamTestClient is never touched by VoiceController. "talk" reuses
    // micTest's own AudioInput instance (same single-mic-reader discipline
    // /mictest already established) and pauses VoiceController's
    // background wake-word polling for the duration, same as /mictest does.
    if (rest == "connect") {
      Serial.println("stream test -> connecting...");
      streamTestClient.connect();
      streamTestActive = true;
    } else if (rest == "disconnect") {
      streamTestClient.disconnect();
      streamTestActive = false;
      Serial.println("stream test -> disconnected");
    } else if (rest.startsWith("talk")) {
      if (!streamTestActive || !streamTestClient.isConnected()) {
        Serial.println("stream test -> not connected, run /streamtest connect first");
        return;
      }
      if (voiceController.isListening()) {
        Serial.println("stream test -> VoiceController is actively listening right now, aborting (would fight over the mic)");
        return;
      }
      int sp = rest.indexOf(' ');
      float seconds = sp > 0 ? rest.substring(sp + 1).toFloat() : 3.0f;
      if (seconds <= 0) seconds = 3.0f;
#ifdef ENABLE_WAKEWORD
      voiceController.setWakeWordPaused(true);
#endif
#ifndef ENABLE_VOICE
      if (!micTestBegun) { micTest.begin(); micTestBegun = true; }
#endif
      Serial.print("stream test -> capturing "); Serial.print(seconds, 1);
      Serial.println("s and streaming to OpenAI as it's captured...");
      uint32_t totalSamples = (uint32_t)(seconds * AudioInput::kSampleRate);
      uint32_t sampled = 0;
      int16_t chunk[256];
      while (sampled < totalSamples) {
        size_t n = micTest.readSamples(chunk, 256);
        if (n > 0) {
          streamTestClient.sendAudioChunk(chunk, n);
          sampled += n;
        }
        streamTestClient.poll();
      }
      streamTestClient.commitUtterance();
      Serial.println("stream test -> committed, waiting for transcript...");
      uint32_t waitStart = millis();
      while (!streamTestClient.transcriptReady() && millis() - waitStart < 8000) {
        streamTestClient.poll();
        delay(5);
      }
      if (streamTestClient.transcriptReady()) {
        Serial.print("stream test -> TRANSCRIPT: \"");
        Serial.print(streamTestClient.takeTranscript());
        Serial.println("\"");
        Serial.print("stream test -> commit -> transcript took ");
        Serial.print(millis() - waitStart); Serial.println(" ms");
      } else {
        Serial.println("stream test -> timed out waiting for a transcript");
      }
#ifdef ENABLE_WAKEWORD
      voiceController.setWakeWordPaused(false);
#endif
    } else {
      Serial.println("Usage: /streamtest connect|disconnect|talk [seconds]");
    }
#else
    Serial.println("Not compiled in — build with -DENABLE_VOICE and -DENABLE_STREAMING_STT.");
#endif

  } else if (cmd == "/say") {
#ifdef ENABLE_TTS
    if (rest.length() == 0) { Serial.println("Usage: /say <text>"); return; }
    ensureAudioOutBegun();
    Serial.print("[TTS] speaking: "); Serial.println(rest);
    ttsClient.speakAsync(rest, audioOut);
    ttsCmdPending = true;
    ttsPendingAudio = true;  // loop() starts the mouth when audio actually begins
#else
    Serial.println("Not compiled in — build with -DENABLE_TTS and fill in Secrets.h.");
#endif

  } else if (cmd == "/ask") {
#ifdef ENABLE_AI
    if (rest.length() == 0) { Serial.println("Usage: /ask <text>"); return; }
    Serial.print("[ASK] "); Serial.println(rest);

    // Same local fast-path as the live mic pipeline (see VoiceController::
    // runThinkJob) — "make a happy face" is a command, not a question, so
    // skip the AI+TTS round trip and just perform it. One-shot, not
    // sticky: a deliberate little performance, not a mood change.
    Expression exprCmd;
    if (expressionCommandFromText(rest, exprCmd)) {
      Serial.print("[ASK] expression command -> "); Serial.println(expressionName(exprCmd));
      Reaction r;
      r.expression = exprCmd;
      r.intensity  = 0.8f;
      const ExpressionDef& def = expressionDef(exprCmd);
      r.gazeX = def.gazeX; r.gazeY = def.gazeY; r.blink = def.blink;
      r.attackMs = def.attackMs; r.holdMs = def.holdMs; r.releaseMs = def.releaseMs;
      character.onReaction(r, now);
      return;
    }

    AIResponse ai = conversationalAI.converse(rest);
    if (!ai.ok) { Serial.println("[ASK] failed — see error above."); return; }
#ifdef ENABLE_TTS
    speakAiResponse(ai, now);
#else
    reactToAi(ai, now, /*sticky=*/false);  // no TTS coming — self-release as before
#endif
#else
    Serial.println("Not compiled in — build with -DENABLE_AI and fill in Secrets.h.");
#endif

  } else {
    Serial.println("Unknown command.");
  }
}

static void handleLine(const String& raw) {
  String line = raw;
  line.trim();
  if (line.length() == 0) return;

  if (line.startsWith("/")) {
    handleSlashCommand(line);
    return;
  }

  if (line.startsWith("STATE:")) {
    String val = line.substring(6);
    val.trim();
    val.toLowerCase();
    uint32_t now = millis();
    if      (val == "idle")      character.setState(CharacterState::Idle, now);
    else if (val == "listening") character.setState(CharacterState::Listening, now);
    else if (val == "thinking")  character.setState(CharacterState::Thinking, now);
    else if (val == "speaking")  character.setState(CharacterState::Speaking, now);
    else { Serial.println("Unknown state. Use idle|listening|thinking|speaking"); return; }
    Serial.print("STATE -> "); Serial.println(val);
    return;
  }

  Reaction r = reactionEngine.classify(line);
  printReaction(line, r);
  character.onReaction(r, millis());
}

void setup() {
  Serial.begin(115200);
  usbConsoleBegin();
  delay(200);

  display.begin();
  faceFrame.begin(display.raw());
  display.showSplash("REX", "initializing...");
  delay(700);

  character.begin(&faceFrame, millis());

#if defined(ENABLE_VOICE) || defined(ENABLE_AI) || defined(ENABLE_TTS) || defined(ENABLE_TOOLS)
  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  uint32_t wifiStartMs = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStartMs < 15000) {
    delay(250);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("\nWiFi connected, IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWiFi connect failed — voice/AI/TTS features won't work until it connects.");
  }
#endif

#ifdef ENABLE_AI
  memoryStore.begin();  // mounts SPIFFS + loads any facts learned in past sessions
#endif

#ifdef ENABLE_VOICE
  ensureAudioOutBegun();  // voice-triggered spoken replies share this same AudioOutput
#if defined(ENABLE_AI) && defined(ENABLE_TOOLS)
  voiceController.begin(timerManager, memoryStore);  // same shared instances as /ask's
#elif defined(ENABLE_AI)
  voiceController.begin(memoryStore);
#else
  voiceController.begin();
#endif
  pinMode(kListenButtonPin, INPUT_PULLUP);
  Serial.println("Voice input compiled in but muted by default — /listen on or the button to start listening.");
#endif
#ifdef ENABLE_AI
#ifdef ENABLE_TOOLS
  conversationalAI.begin(timerManager, memoryStore);  // starts TimeTool's NTP sync
#else
  conversationalAI.begin(memoryStore);
#endif
  Serial.println("Conversational AI enabled — try /ask <text>.");
  if (memoryStore.count() > 0) {
    Serial.print("Remembers "); Serial.print(memoryStore.count());
    Serial.println(" thing(s) from before — try /memory.");
  }
#endif
#ifdef ENABLE_TTS
  Serial.println("Text-to-speech enabled — try /say <text>.");
#endif
#ifdef ENABLE_TOOLS
  toolManagerDebug.begin(timerManager);  // second NTP-sync call is harmless/idempotent
  Serial.println("Tools enabled — try /weather, /time, /calc, /places, /sports, /search, /timer.");
#endif

  Serial.println("REX_READY");
  usbMirror("REX_READY");
  Serial.println("Type a sentence and press enter, or a debug command:");
  Serial.println("  STATE:idle|listening|thinking|speaking");
  Serial.println("  /emotion <name>  /intensity <0..1>  /look <x> <y>  /speak [emotion|on|off]  /stop");
  Serial.println("  /mouth <name>");
  Serial.println("  /mictest on|off   raw I2S mic RMS level, no WiFi needed");
#if defined(ENABLE_VOICE) && defined(ENABLE_WAKEWORD)
  Serial.println("  (say \"Hey REX\" anytime the mic is muted — starts listening on its own)");
#endif
  Serial.println("  /tonetest [hz]    play a 1s test tone through the amp+speaker, no WiFi needed");
#if defined(ENABLE_VOICE) && defined(ENABLE_STREAMING_STT)
  Serial.println("  /resampletest     offline check of the streaming-STT resampler");
  Serial.println("  /streamtest connect|disconnect|talk [s]   streaming-STT test, isolated from the real pipeline");
#endif
  Serial.println("  /volume [0-100]   set (or show) speaker volume");
#ifdef ENABLE_TOOLS
  Serial.println("  /weather [city]   real weather data (Open-Meteo), no AI needed");
  Serial.println("  /time [city]      real date/time, no AI needed");
  Serial.println("  /calc <expr>      exact arithmetic, e.g. /calc (70-32)*5/9");
  Serial.println("  /places <cat> [city]   nearby places by category, e.g. /places cafe");
  Serial.println("  /sports <team>    last result + next fixture (TheSportsDB)");
  Serial.println("  /search <query>   general web search (Tavily)");
  Serial.println("  /news <topic>     recent headlines on a topic (GNews)");
  Serial.println("  /timer <secs> [label]  set a timer; REX announces it on its own when due");
#endif
#ifdef ENABLE_VOICE
  Serial.println("  /listen on|off    mute/unmute the live mic (VAD/STT/AI/TTS loop)");
  Serial.println("  (or press the physical button on GPIO18 to toggle the same thing)");
#endif
#ifdef ENABLE_AI
  Serial.println("  /ask <text>       REX's real personality via Claude, speaks + reacts");
  Serial.println("  /memory           list what REX has learned about you so far");
  Serial.println("  /forget           wipe REX's persistent memory");
#endif
#ifdef ENABLE_TTS
  Serial.println("  /say <text>       speak arbitrary text through the amp+speaker (ElevenLabs)");
#endif
}

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      handleLine(lineBuf);
      lineBuf = "";
    } else if (c != '\r') {
      lineBuf += c;
    }
  }

#ifdef ENABLE_USB_CONSOLE
  // Same command dispatch, fed from the native USB port too — shares
  // lineBuf with the block above rather than a separate buffer, which is
  // fine given only one of the two ports is ever connected at a time in
  // practice (see DebugMirror.h). Output from the command handlers below
  // still only goes to Serial/CP2102 except where explicitly mirrored
  // (e.g. /mictest's line) — not a full two-way console yet.
  while (USBSerial.available()) {
    char c = USBSerial.read();
    if (c == '\n') {
      handleLine(lineBuf);
      lineBuf = "";
    } else if (c != '\r') {
      lineBuf += c;
    }
  }
#endif

#ifdef ENABLE_VOICE
  pollListenButton();
  voiceController.update(millis(), character, reactionEngine, audioOut);
#endif

#if defined(ENABLE_VOICE) && defined(ENABLE_STREAMING_STT)
  // Services the async WS handshake/keepalive between /streamtest
  // commands — "talk" also polls directly inside its own blocking wait,
  // this covers the gap after "connect" returns.
  if (streamTestActive) {
    streamTestClient.poll();
  }
#endif

  if (micTestActive) {
    int16_t chunk[256];
    size_t n = micTest.readSamples(chunk, 256);
    if (n > 0) {
      int64_t sumSq = 0;
      for (size_t i = 0; i < n; i++) sumSq += (int32_t)chunk[i] * (int32_t)chunk[i];
      float rms = sqrtf((float)sumSq / (float)n) / 32768.0f;

      uint32_t now = millis();
      if (now - lastMicPrintMs > 150) {
        lastMicPrintMs = now;
        int bars = (int)(rms * 80.0f);    // tuned from a live reading — 40 bars ~= rms 0.5
        if (bars > 40) bars = 40;
        if (bars < 0) bars = 0;
        String line = "MIC [";
        for (int i = 0; i < 40; i++) line += (i < bars ? '#' : '-');
        line += "] rms=" + String(rms, 4);
        Serial.println(line);
        usbMirror(line);
      }
    }
  }

  character.update(millis());

#ifdef ENABLE_TTS
  if (ttsCmdPending) {
    // Start the mouth only once audio is genuinely coming out — see
    // speakAiResponse(). firstAudioMs() is stamped on the first chunk
    // written to the speaker.
    if (ttsPendingAudio && ttsClient.firstAudioMs() != 0) {
      ttsPendingAudio = false;
      character.setState(CharacterState::Speaking, millis());
    }
    if (!ttsClient.isSpeaking()) {
      ttsCmdPending = false;
      ttsPendingAudio = false;
      character.setState(CharacterState::Idle, millis());
      Serial.println(ttsClient.lastResultOk() ? "[TTS] done" : "[TTS] failed — see error above.");
    }
  }
#endif

#ifdef ENABLE_TOOLS
  // Only check for a due timer when nothing conversational is in
  // flight — character.state() == Idle is the one existing signal that
  // covers both the live-voice path and /ask's TTS, since both already
  // set/clear it around their own work. If REX is busy, checkDue() just
  // isn't called this tick, so a fired timer stays "due" and gets
  // picked up on the first idle tick afterward — nothing is dropped.
  if (character.state() == CharacterState::Idle) {
    String firedLabel;
    if (timerManager.checkDue(firedLabel)) {
      Serial.print("[TIMER] '"); Serial.print(firedLabel); Serial.println("' is up!");
#if defined(ENABLE_AI) && defined(ENABLE_TTS)
      String trigger = "(a timer labeled '" + firedLabel + "' just went off — let the user know)";
      AIResponse ai = conversationalAI.converse(trigger);
      if (ai.ok) speakAiResponse(ai, millis());
#endif
    }
  }
#endif
}
