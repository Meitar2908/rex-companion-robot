#include "StreamingVoiceController.h"
#if defined(ENABLE_VOICE) && defined(ENABLE_STREAMING_STT)
#include "ExpressionTable.h"
#include "DebugMirror.h"
#include <Arduino.h>
#include <math.h>

namespace {
constexpr size_t   kChunkSamples = 256;
constexpr uint32_t kIgnoreAfterUnmuteMs = 200;
} // namespace

#if defined(ENABLE_AI) && defined(ENABLE_TOOLS)
void StreamingVoiceController::begin(TimerManager& timers, MemoryStore& memory) {
  ai_.begin(timers, memory);
#elif defined(ENABLE_AI)
void StreamingVoiceController::begin(MemoryStore& memory) {
  ai_.begin(memory);
#else
void StreamingVoiceController::begin() {
#endif
  audio_.begin();
  // No PSRAM utterance buffer here — see this file's header comment.

#ifdef ENABLE_WAKEWORD
  wakeWordBegun_ = wakeWord_.begin();
  if (!wakeWordBegun_) {
    Serial.println("[WAKE] unavailable this session — \"Hey REX\" won't "
                    "trigger listening, button still does");
  }
#endif
}

void StreamingVoiceController::setListening(bool enabled, bool fromWakeWord) {
  listening_ = enabled;
  if (enabled) {
    audio_.flush();
    vad_.reset();
    ignoreAudioUntilMs_ = millis() + kIgnoreAfterUnmuteMs;
    dbgChunksLogged_ = 0;  // TEMP DEBUG
    listeningFromWakeWord_ = fromWakeWord;
    armFollowUpWindow(millis());
    // Pay the WS handshake now, before speech starts — not at
    // SpeechStarted, or this reintroduces the exact handshake cost
    // streaming STT exists to eliminate, just moved later in the turn.
    // Idempotent — see StreamingSpeechToTextClient::connect().
    stt_.connect();
  } else {
    followUpDeadlineMs_ = 0;
  }
  dbgListenEnabledMs_ = enabled ? millis() : 0;  // TEMP DEBUG
  dbgLastReadMs_ = 0;                            // TEMP DEBUG
}

void StreamingVoiceController::armFollowUpWindow(uint32_t nowMs) {
  followUpDeadlineMs_ = listeningFromWakeWord_ ? nowMs + kFollowUpWindowMs : 0;
}

void StreamingVoiceController::update(uint32_t nowMs, CharacterController& character,
                                       const ReactionEngine& reactionEngine, AudioOutput& audioOut) {
#ifdef ENABLE_TTS
  if (tts_.isSpeaking()) {
    if (speakingPendingAudio_ && tts_.firstAudioMs() != 0) {
      speakingPendingAudio_ = false;
      character.setState(CharacterState::Speaking, nowMs);
    }
    return;
  }
  if (wasSpeaking_) {
    wasSpeaking_ = false;
    speakingPendingAudio_ = false;
    character.setState(CharacterState::Idle, nowMs);
    if (!tts_.lastResultOk()) {
      Serial.println("[TTS] speak failed — see error above.");
    } else if (thinkWatchdogFired_) {
      Serial.println("[TTS] done speaking the watchdog apology (no latency report — nothing to report)");
    } else {
      printLatencyReport(tts_.firstAudioMs());
    }
    resumeListeningAfterPause();
  }
#endif

  if (thinkBusy_) {
    if (thinkDone_) {
      thinkBusy_ = false;
      thinkDone_ = false;
      if (thinkWatchdogFired_) {
        Serial.println("[VOICE] abandoned think job finished late — discarding");
        thinkWatchdogFired_ = false;
        resumeListeningAfterPause();
        return;
      }
      finishThinkJob(character, reactionEngine, audioOut);
      return;
    }
    if (!thinkWatchdogFired_ && (nowMs - thinkStartMs_) > kThinkWatchdogMs) {
      thinkWatchdogFired_ = true;
      Serial.println("[VOICE] think job exceeded watchdog — giving up (connection issue?)");
      usbMirror("[VOICE] think job exceeded watchdog — giving up (connection issue?)");
#ifdef ENABLE_TTS
      character.setBaselineEmotion(Expression::Worried, 0.5f, nowMs);
      tts_.speakAsync("Sorry, that is taking too long. Might be a connection issue.", audioOut);
      wasSpeaking_ = true;
      speakingPendingAudio_ = true;
#else
      character.setState(CharacterState::Idle, nowMs);
#endif
    }
    return;
  }

  // Non-blocking wait for the streaming transcript — the STT-sub-step
  // analog of thinkBusy_ above. See this file's header comment and
  // kAwaitTranscriptMs's own comment for why this should normally resolve
  // almost immediately.
  if (awaitingTranscript_) {
    stt_.poll();
    if (stt_.transcriptReady()) {
      thinkTranscript_ = stt_.takeTranscript();
      tsSttDone_ = millis();
      awaitingTranscript_ = false;
      consecutiveSttFailures_ = 0;  // a real transcript came through — not stuck
      thinkDone_ = false;
      thinkBusy_ = true;
      thinkWatchdogFired_ = false;
      thinkStartMs_ = millis();
      xTaskCreatePinnedToCore(thinkTaskEntry, "rex_think", 16384, this, 1, nullptr, 0);
      return;
    }
    if (nowMs - awaitingTranscriptStartMs_ > kAwaitTranscriptMs) {
      Serial.println("[STREAM-VOICE] timed out waiting for a streaming transcript "
                      "(streaming STT isn't delivering its near-zero premise right now)");
      usbMirror("[STREAM-VOICE] timed out waiting for a streaming transcript");
      awaitingTranscript_ = false;
      tsSttDone_ = millis();
      character.setState(CharacterState::Idle, nowMs);

      consecutiveSttFailures_++;
      if (consecutiveSttFailures_ >= kMaxConsecutiveSttFailures) {
        // Several in a row (not just one — a single timeout reliably
        // self-recovers on the next attempt, see this counter's own
        // comment in the header) means REX is genuinely stuck, almost
        // certainly on fragmented heap after long uptime. A clean reboot
        // is the actual fix — resuming the same session would just keep
        // retrying against the same fragmented heap.
        String msg = "[VOICE] " + String(consecutiveSttFailures_) +
                     " consecutive STT failures — restarting to clear a likely "
                     "fragmented heap";
        Serial.println(msg);
        usbMirror(msg);
        delay(200);  // let the log line actually transmit before the reset
        ESP.restart();
      }

      resumeListeningAfterPause();
    }
    return;
  }

  if (!listening_) {
#ifdef ENABLE_WAKEWORD
    if (wakeWordBegun_ && !wakeWordPaused_) {
      int16_t wwChunk[kChunkSamples];
      size_t wwN = audio_.readSamples(wwChunk, kChunkSamples, /*waitForData=*/false);
      while (wwN > 0) {
        if (wakeWord_.feed(wwChunk, wwN)) {
          Serial.println("[VOICE] \"Hey REX\" detected — listening");
          usbMirror("[VOICE] \"Hey REX\" detected — listening");
          setListening(true, /*fromWakeWord=*/true);
          break;
        }
        wwN = audio_.readSamples(wwChunk, kChunkSamples, /*waitForData=*/false);
      }
    }
#endif
    return;
  }

  if (listeningFromWakeWord_ && followUpDeadlineMs_ != 0 &&
      vad_.state() == VoiceState::Silence && nowMs >= followUpDeadlineMs_) {
    Serial.println("[VOICE] follow-up window elapsed — muting (say \"Hey REX\" to talk again)");
    setListening(false);
    return;
  }

  // Keep the WS connection serviced while idle/listening — no documented
  // ping/pong requirement was found for this endpoint, but this still
  // needs to run often enough to notice a dropped socket promptly (see
  // StreamingSpeechToTextClient.h's risk callout).
  stt_.poll();

  int16_t chunk[kChunkSamples];
  size_t n = audio_.readSamples(chunk, kChunkSamples);
  if (n == 0) return;

  {
    uint32_t nowReal = millis();
    if (dbgLastReadMs_ != 0) {
      uint32_t gap = nowReal - dbgLastReadMs_;
      if (gap > 100) {
        Serial.print("[DBG] mic starved "); Serial.print(gap);
        Serial.print(" ms wall-clock, ~"); Serial.print(gap > 16 ? gap - 16 : 0);
        Serial.println(" ms of audio lost");
      }
    }
    dbgLastReadMs_ = nowReal;
  }

  bool utteranceReady = false;
  while (n > 0) {
    uint32_t chunkMs = millis();

    int64_t sum = 0;
    for (size_t i = 0; i < n; i++) sum += chunk[i];
    int32_t mean = (int32_t)(sum / (int64_t)n);

    int64_t sumSq = 0;
    for (size_t i = 0; i < n; i++) {
      int32_t d = (int32_t)chunk[i] - mean;
      sumSq += (int64_t)d * (int64_t)d;
    }
    float rms = sqrtf((float)sumSq / (float)n) / 32768.0f;

    if (dbgChunksLogged_ < 400) {
      dbgChunksLogged_++;
      Serial.print("[DBG] chunk +");
      Serial.print(dbgListenEnabledMs_ ? chunkMs - dbgListenEnabledMs_ : 0);
      Serial.print("ms rms="); Serial.println(rms, 4);
    }

    if (chunkMs < ignoreAudioUntilMs_) {
      n = audio_.readSamples(chunk, kChunkSamples, /*waitForData=*/false);
      continue;
    }

    VadEvent ev = vad_.update(rms, chunkMs);

    if (ev == VadEvent::SpeechStarted) {
      tsSpeechStart_ = chunkMs;
      Serial.print("[DBG] VAD speech START t="); Serial.print(chunkMs);
      Serial.print(" rms="); Serial.print(rms, 4);
      Serial.print(" (+"); Serial.print(dbgListenEnabledMs_ ? chunkMs - dbgListenEnabledMs_ : 0);
      Serial.println(" ms after mic un-muted)");
      character.setState(CharacterState::Listening, chunkMs);
    } else if (ev == VadEvent::SpeechAborted) {
      Serial.println("[VOICE] ignored a blip (too short to be speech)");
      character.setState(CharacterState::Idle, chunkMs);
    } else if (ev == VadEvent::SpeechEnded) {
      tsSpeechEnd_ = chunkMs;
      utteranceReady = true;
      break;
    }

    if (vad_.state() == VoiceState::Speaking) {
      stt_.sendAudioChunk(chunk, n);
    }

    n = audio_.readSamples(chunk, kChunkSamples, /*waitForData=*/false);
  }

  if (!utteranceReady) return;
  nowMs = tsSpeechEnd_;

  Serial.print("[DBG] VAD speech END t="); Serial.print(nowMs);
  Serial.print(" (wall-clock utterance was ");
  Serial.print(nowMs - tsSpeechStart_); Serial.println(" ms incl. hangover)");

  character.setState(CharacterState::Thinking, nowMs);

  stt_.commitUtterance();
  awaitingTranscript_ = true;
  awaitingTranscriptStartMs_ = nowMs;
}

void StreamingVoiceController::thinkTaskEntry(void* param) {
  StreamingVoiceController* self = static_cast<StreamingVoiceController*>(param);
  self->runThinkJob();
  self->thinkDone_ = true;
  vTaskDelete(nullptr);
}

// Runs on core 0 — only the AI call now, STT already happened on core 1
// (see update()'s awaitingTranscript_ branch). Never touches stt_ — see
// this file's header comment on why that's a hard rule here, unlike
// VoiceController's runThinkJob().
void StreamingVoiceController::runThinkJob() {
  thinkHasExpressionCommand_ = false;

  if (thinkTranscript_.length() == 0) {
    Serial.println("[VOICE] (empty transcription — streaming STT returned nothing usable)");
    return;
  }
  Serial.print("[VOICE] heard: \""); Serial.print(thinkTranscript_); Serial.println("\"");
  usbMirror("[VOICE] heard: \"" + thinkTranscript_ + "\"");

  if (expressionCommandFromText(thinkTranscript_, thinkExpressionCommand_)) {
    thinkHasExpressionCommand_ = true;
    Serial.print("[VOICE] expression command -> ");
    Serial.println(expressionName(thinkExpressionCommand_));
    return;
  }

#ifdef ENABLE_AI
  thinkAi_ = ai_.converse(thinkTranscript_);
  tsAiDone_ = millis();
#endif
}

// Identical to VoiceController's — acts only on thinkTranscript_/thinkAi_,
// which are already populated by the time this runs. No reason for this
// to differ.
void StreamingVoiceController::finishThinkJob(CharacterController& character,
                                               const ReactionEngine& reactionEngine,
                                               AudioOutput& audioOut) {
  if (thinkTranscript_.length() == 0) {
    // Previously silent — REX just reset to Idle with no acknowledgment
    // at all, which from the user's side is indistinguishable from
    // being ignored. Speak up instead, same pattern as the watchdog's
    // own apology above.
    uint32_t nowMs = millis();
#ifdef ENABLE_TTS
    character.setBaselineEmotion(Expression::Confused, 0.4f, nowMs);
    tts_.speakAsync("Sorry, I didn't catch that.", audioOut);
    wasSpeaking_ = true;
    speakingPendingAudio_ = true;
#else
    character.setState(CharacterState::Idle, nowMs);
    resumeListeningAfterPause();
#endif
    return;
  }

  if (thinkHasExpressionCommand_) {
    Reaction r;
    r.expression = thinkExpressionCommand_;
    r.intensity  = 0.8f;
    const ExpressionDef& def = expressionDef(thinkExpressionCommand_);
    r.gazeX = def.gazeX; r.gazeY = def.gazeY; r.blink = def.blink;
    r.attackMs = def.attackMs; r.holdMs = def.holdMs; r.releaseMs = def.releaseMs;
    character.onReaction(r, millis());
    resumeListeningAfterPause();
    return;
  }

#ifdef ENABLE_AI
  if (thinkAi_.ok) {
    character.setBaselineEmotion(thinkAi_.expression, thinkAi_.intensity, millis());

    Serial.print("REX: ");
    Serial.println(thinkAi_.speech);
    usbMirror("REX: " + thinkAi_.speech);

#ifdef ENABLE_TTS
    lastWordCount_ = 1;
    for (size_t i = 0; i < thinkAi_.speech.length(); i++) {
      if (thinkAi_.speech[i] == ' ') lastWordCount_++;
    }
    tts_.speakAsync(thinkAi_.speech, audioOut);
    wasSpeaking_ = true;
    speakingPendingAudio_ = true;
    return;
#else
    character.setState(CharacterState::Idle, millis());
    resumeListeningAfterPause();
    return;
#endif
  }
  // Previously silent here — a failed AI call (network hiccup, rate
  // limit, anything) got only a face reaction and no spoken word, which
  // reads as REX ignoring you rather than something having gone wrong.
  // Scoped inside ENABLE_AI deliberately: this is a genuine FAILURE
  // apology, not the plain reaction fallback a no-AI build uses below —
  // those are two different situations and must not share one message.
  Serial.println("[VOICE] AI call failed — falling back to local reaction.");
  usbMirror("[VOICE] AI call failed — falling back to local reaction.");
  {
    uint32_t nowMs = millis();
    Reaction r = reactionEngine.classify(thinkTranscript_);
    character.onReaction(r, nowMs);
#ifdef ENABLE_TTS
    tts_.speakAsync("Sorry, having trouble answering right now.", audioOut);
    wasSpeaking_ = true;
    speakingPendingAudio_ = true;
    return;
#else
    character.setState(CharacterState::Idle, nowMs);
    resumeListeningAfterPause();
    return;
#endif
  }
#endif

  // No ENABLE_AI at all — the ordinary, expected fallback for a build
  // that never has a real conversational pipeline to begin with. NOT a
  // failure, so stays silent exactly as before — no apology here.
  Reaction r = reactionEngine.classify(thinkTranscript_);
  character.onReaction(r, millis());
  character.setState(CharacterState::Idle, millis());
  resumeListeningAfterPause();
}

void StreamingVoiceController::resumeListeningAfterPause() {
  audio_.flush();
  vad_.reset();
  ignoreAudioUntilMs_ = millis() + kIgnoreAfterUnmuteMs;
  armFollowUpWindow(millis());
  // Pre-warm the NEXT turn's session right now, while the follow-up
  // window (if any) gives the user time to speak again — idempotent, see
  // StreamingSpeechToTextClient::connect().
  stt_.connect();
}

// Identical formula to VoiceController's — "Speech-to-text" here measures
// commit() -> transcriptReady() (tsSttDone_ is stamped at that point in
// update()'s awaitingTranscript_ branch) instead of one blocking
// transcribe() call, but it's the same subtraction either way, and that's
// the whole point: this number collapsing toward zero is the live proof
// streaming STT is actually delivering on its premise.
void StreamingVoiceController::printLatencyReport(uint32_t audioStartMs) const {
  uint32_t hangover = vad_.hangoverMs();
  uint32_t estimatedStopMs = tsSpeechEnd_ > hangover ? tsSpeechEnd_ - hangover : tsSpeechEnd_;

  Serial.println("---- LATENCY REPORT (streaming STT) ----");
  Serial.print("Speech duration: ");
  Serial.print(estimatedStopMs - tsSpeechStart_); Serial.println(" ms");
  Serial.print("End-of-speech detection (hangover): ");
  Serial.print(hangover); Serial.println(" ms");
  Serial.print("Speech-to-text (commit -> transcript): ");
  Serial.print(tsSttDone_ - tsSpeechEnd_); Serial.println(" ms");
  Serial.print("AI response: ");
  Serial.print(tsAiDone_ - tsSttDone_); Serial.println(" ms");
  if (audioStartMs > 0) {
    Serial.print("TTS (request -> first audio): ");
    Serial.print(audioStartMs - tsAiDone_); Serial.println(" ms");
    Serial.print("TIME TO FIRST ROBOT AUDIO (end of speech -> audio): ");
    Serial.print(audioStartMs - tsSpeechEnd_); Serial.println(" ms");
  }
  Serial.print("Response length: ");
  Serial.print(lastWordCount_); Serial.println(" words");
  Serial.println("-------------------------");
}
#endif
