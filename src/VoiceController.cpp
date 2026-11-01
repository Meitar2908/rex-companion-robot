#include "VoiceController.h"
#include "ExpressionTable.h"
#include <Arduino.h>
#include <math.h>
#include <string.h>

namespace {
// 20s ceiling for one utterance — generous for a conversational
// sentence or two now that VAD hangover is longer (see
// VoiceActivityDetector.h), still bounded so PSRAM use stays sane.
constexpr uint32_t kMaxUtteranceSeconds = 20;
constexpr size_t   kChunkSamples = 256;

// Audio arriving within this window of an un-mute (or of REX finishing
// its own reply) is discarded — covers the physical button's click and
// any tail of REX's own voice still in flight.
constexpr uint32_t kIgnoreAfterUnmuteMs = 200;
} // namespace

#if defined(ENABLE_AI) && defined(ENABLE_TOOLS)
void VoiceController::begin(TimerManager& timers, MemoryStore& memory) {
  ai_.begin(timers, memory);
#elif defined(ENABLE_AI)
void VoiceController::begin(MemoryStore& memory) {
  ai_.begin(memory);
#else
void VoiceController::begin() {
#endif
  audio_.begin();
  bufferCapacity_ = AudioInput::kSampleRate * kMaxUtteranceSeconds;
  buffer_ = (int16_t*)ps_malloc(bufferCapacity_ * sizeof(int16_t));
  if (!buffer_) {
    Serial.println("[VOICE] out of PSRAM for utterance buffer");
    bufferCapacity_ = 0;
  }
  bufferLen_ = 0;

#ifdef ENABLE_WAKEWORD
  wakeWordBegun_ = wakeWord_.begin();
  if (!wakeWordBegun_) {
    Serial.println("[WAKE] unavailable this session — \"Hey REX\" won't "
                    "trigger listening, button still does");
  }
#endif
}

void VoiceController::setListening(bool enabled, bool fromWakeWord) {
  listening_ = enabled;
  if (enabled) {
    // The I2S peripheral never stopped capturing while muted, it just
    // stopped being read — so its DMA ring is holding up to ~128ms of
    // stale audio, and on the first un-mute after boot that includes the
    // start-up transient, which reads near full scale. Measured live:
    // voice detection fired at +0ms with rms=0.56 in a silent room,
    // burning a full ~7s STT/AI/TTS round trip on a hallucinated "You".
    // Dropping the backlog and clearing the detector's stale timestamps
    // is what makes un-mute actually mean "start listening now".
    audio_.flush();
    vad_.reset();
    ignoreAudioUntilMs_ = millis() + kIgnoreAfterUnmuteMs;
    bufferLen_ = 0;
    dbgChunksLogged_ = 0;  // TEMP DEBUG
    listeningFromWakeWord_ = fromWakeWord;
    // Covers "said the wake word, then never actually asked anything" —
    // the same silence budget applies before the first question as
    // after each reply (see armFollowUpWindow's header comment).
    armFollowUpWindow(millis());
  } else {
    followUpDeadlineMs_ = 0;
  }
  dbgListenEnabledMs_ = enabled ? millis() : 0;  // TEMP DEBUG
  dbgLastReadMs_ = 0;                            // TEMP DEBUG
}

void VoiceController::armFollowUpWindow(uint32_t nowMs) {
  followUpDeadlineMs_ = listeningFromWakeWord_ ? nowMs + kFollowUpWindowMs : 0;
}

void VoiceController::update(uint32_t nowMs, CharacterController& character,
                              const ReactionEngine& reactionEngine, AudioOutput& audioOut) {
#ifdef ENABLE_TTS
  if (tts_.isSpeaking()) {
    // Audio is streaming on its own task — leave the mic alone so it
    // doesn't pick up REX's own voice, and let the caller's regular
    // character.update() (unblocked now) keep the mouth animating.
    //
    // Start the mouth only once audio is genuinely playing: firstAudioMs()
    // is stamped when the first chunk reaches the speaker, which is
    // ~0.8-1.8s after the request went out.
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
      // This was the watchdog's own apology, not a real reply — the
      // ORIGINAL think job's tsAiDone_/tsSttDone_ for THIS turn were
      // never set (that's exactly why the watchdog fired), so the
      // usual report would subtract against stale values left over
      // from a previous turn and print garbage (seen live: an
      // underflowed "AI response: 4294951761 ms"). Skip it — there is
      // nothing real to report for a turn that never finished.
      Serial.println("[TTS] done speaking the watchdog apology (no latency report — nothing to report)");
    } else {
      printLatencyReport(tts_.firstAudioMs());
    }
    // REX's own voice was just filling the room; whatever leaked into the
    // mic is still sitting in the DMA ring. Drop it rather than let it
    // start a phantom utterance.
    resumeListeningAfterPause();
  }
#endif

  // A think job (STT + AI) is in flight on core 0. Don't read the mic and
  // don't start another — just return, so the caller's character.update()
  // keeps the Thinking face animating until the reply is ready.
  if (thinkBusy_) {
    if (thinkDone_) {
      thinkBusy_ = false;
      thinkDone_ = false;
      if (thinkWatchdogFired_) {
        // The abandoned task finally finished after all — REX already
        // told the user it gave up, so these results are stale by now;
        // discard rather than speak a late, out-of-context answer.
        // Safe to resume normal listening now: the task that owned
        // buffer_/thinkTranscript_/ai_ has genuinely exited.
        Serial.println("[VOICE] abandoned think job finished late — discarding");
        thinkWatchdogFired_ = false;
        resumeListeningAfterPause();
        return;
      }
      finishThinkJob(character, reactionEngine, audioOut);
      return;
    }

    // Backstop above every individual network timeout — see
    // kThinkWatchdogMs's comment in the header. Fires once per think
    // job; does NOT reset thinkBusy_ (the core-0 task may still be
    // mid-write into shared state) — just stops waiting on it silently
    // and tells the user, then waits for the real completion signal
    // above before anything touches that state again.
    if (!thinkWatchdogFired_ && (nowMs - thinkStartMs_) > kThinkWatchdogMs) {
      thinkWatchdogFired_ = true;
      Serial.println("[VOICE] think job exceeded watchdog — giving up (connection issue?)");
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

  if (!listening_) {
#ifdef ENABLE_WAKEWORD
    // Background "Hey REX" polling — only while muted (an active
    // conversation has no reason to also be checking for the wake word),
    // and only via this same audio_ instance (see the header comment on
    // wakeWord_) so there's never two readers fighting over one I2S0
    // ring. Non-blocking, same reasoning as the rest of this file's
    // mic reads: loop() still has the TFT render to get to every tick
    // regardless of whether anything's been said.
    if (wakeWordBegun_ && !wakeWordPaused_) {
      int16_t wwChunk[kChunkSamples];
      size_t wwN = audio_.readSamples(wwChunk, kChunkSamples, /*waitForData=*/false);
      while (wwN > 0) {
        if (wakeWord_.feed(wwChunk, wwN)) {
          Serial.println("[VOICE] \"Hey REX\" detected — listening");
          // flushes the mic backlog + resets VAD, same as the button —
          // fromWakeWord=true arms the follow-up window (see its header
          // comment), unlike a button/"/listen on" session.
          setListening(true, /*fromWakeWord=*/true);
          break;
        }
        wwN = audio_.readSamples(wwChunk, kChunkSamples, /*waitForData=*/false);
      }
    }
#endif
    return;
  }

  // Follow-up window elapsed with nothing said since the wake word (or
  // since REX's last reply) — re-mute so a later stray sound (TV, room
  // conversation) isn't a live trigger. Gated on vad_.state() == Silence
  // so this can't fire mid-utterance; SpeechEnded re-arms a fresh window
  // via resumeListeningAfterPause() once the reply/reaction is done.
  if (listeningFromWakeWord_ && followUpDeadlineMs_ != 0 &&
      vad_.state() == VoiceState::Silence && nowMs >= followUpDeadlineMs_) {
    Serial.println("[VOICE] follow-up window elapsed — muting (say \"Hey REX\" to talk again)");
    setListening(false);
    return;
  }

  // Drain the ENTIRE DMA ring each iteration, not one fixed chunk.
  //
  // This used to take exactly one 256-sample (16ms) read per loop()
  // iteration, but a full iteration takes ~23ms once the TFT render is
  // included — so the ring overflowed continuously and ~1/3 of every
  // utterance was silently overwritten before it was ever read.
  // Measured live: 512ms captured from a 754ms utterance (68%), 560ms
  // from 894ms (63%). Whisper was being handed speech with slices
  // spliced out of it, which is why longer sentences came back wrong.
  // The first read still blocks, which paces the loop; everything after
  // it takes only what's already waiting.
  int16_t chunk[kChunkSamples];
  size_t n = audio_.readSamples(chunk, kChunkSamples);
  if (n == 0) return;

  // TEMP DEBUG — how long since the previous mic read actually ran?
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

    // RMS of the AC component only — the chunk's mean (its DC offset) is
    // subtracted first.
    //
    // This used to be a plain sqrt(mean(x^2)), which cannot tell a loud
    // room from a DC-biased mic. The INMP441 carries a DC offset that
    // settles over several hundred ms after the read stream resumes, so
    // in a *silent* room the old formula reported rms 0.46 decaying to
    // 0.21 — well above the 0.12 speech threshold — and reliably tripped
    // voice detection on every single un-mute. Removing DC makes the
    // level reflect actual sound, which is what the threshold was
    // calibrated against in the first place.
    int64_t sum = 0;
    for (size_t i = 0; i < n; i++) sum += chunk[i];
    int32_t mean = (int32_t)(sum / (int64_t)n);

    int64_t sumSq = 0;
    for (size_t i = 0; i < n; i++) {
      int32_t d = (int32_t)chunk[i] - mean;
      sumSq += (int64_t)d * (int64_t)d;
    }
    float rms = sqrtf((float)sumSq / (float)n) / 32768.0f;

    // TEMP DEBUG — print the RMS of the first chunks after an un-mute so
    // the shape of any start-up transient is visible instead of guessed at.
    // Widened to ~6s worth so a whole spoken utterance is covered, not just
    // the transient: threshold_ needs recalibrating against DC-removed
    // levels and that calibration should come from measured speech.
    if (dbgChunksLogged_ < 400) {
      dbgChunksLogged_++;
      Serial.print("[DBG] chunk +");
      Serial.print(dbgListenEnabledMs_ ? chunkMs - dbgListenEnabledMs_ : 0);
      Serial.print("ms rms="); Serial.println(rms, 4);
    }

    // Swallow the click of the button that just un-muted us, and any
    // tail of REX's own voice, without letting it reach the detector.
    if (chunkMs < ignoreAudioUntilMs_) {
      n = audio_.readSamples(chunk, kChunkSamples, /*waitForData=*/false);
      continue;
    }

    VadEvent ev = vad_.update(rms, chunkMs);

    if (ev == VadEvent::SpeechStarted) {
      bufferLen_ = 0;
      tsSpeechStart_ = chunkMs;
      // TEMP DEBUG
      Serial.print("[DBG] VAD speech START t="); Serial.print(chunkMs);
      Serial.print(" rms="); Serial.print(rms, 4);
      Serial.print(" (+"); Serial.print(dbgListenEnabledMs_ ? chunkMs - dbgListenEnabledMs_ : 0);
      Serial.println(" ms after mic un-muted)");
      character.setState(CharacterState::Listening, chunkMs);
    } else if (ev == VadEvent::SpeechAborted) {
      // Too little actual voiced audio to be a real utterance — a click,
      // a thump, a chair. Previously this still burned a full STT round
      // trip and came back as a hallucinated word.
      Serial.println("[VOICE] ignored a blip (too short to be speech)");
      bufferLen_ = 0;
      character.setState(CharacterState::Idle, chunkMs);
    } else if (ev == VadEvent::SpeechEnded) {
      tsSpeechEnd_ = chunkMs;
      utteranceReady = true;
      break;
    }

    if (vad_.state() == VoiceState::Speaking && buffer_) {
      size_t room = bufferCapacity_ - bufferLen_;
      size_t toCopy = n < room ? n : room;
      if (toCopy > 0) {
        memcpy(buffer_ + bufferLen_, chunk, toCopy * sizeof(int16_t));
        bufferLen_ += toCopy;
      } else {
        // Hit kMaxUtteranceSeconds — the tail of a very long utterance
        // is being silently dropped. Rare, but worth knowing about
        // rather than silently mis-transcribing.
        static uint32_t lastWarnMs = 0;
        if (chunkMs - lastWarnMs > 2000) {
          lastWarnMs = chunkMs;
          Serial.println("[VOICE] utterance buffer full — tail is being dropped");
        }
      }
    }

    n = audio_.readSamples(chunk, kChunkSamples, /*waitForData=*/false);
  }

  if (!utteranceReady) return;
  if (bufferLen_ == 0 || !buffer_) return;
  nowMs = tsSpeechEnd_;

  tsSpeechEnd_ = nowMs;
  // TEMP DEBUG — what is actually being handed to Whisper? bufferLen_ is
  // samples of *captured* audio, which is NOT the same as wall-clock
  // speech duration if the mic was starved mid-utterance (see the
  // starvation log above).
  Serial.print("[DBG] VAD speech END t="); Serial.print(nowMs);
  Serial.print(" -> sending "); Serial.print((bufferLen_ * 1000UL) / AudioInput::kSampleRate);
  Serial.print(" ms of captured audio (wall-clock utterance was ");
  Serial.print(nowMs - tsSpeechStart_); Serial.println(" ms incl. hangover)");

  character.setState(CharacterState::Thinking, nowMs);

  // Hand STT + the AI call to core 0 rather than running them here.
  //
  // Both are blocking multi-second network round trips — measured live,
  // one weather question cost 4.1s in STT, then 2.2s + 1.3s (the tool's
  // own HTTP call) + 1.9s across the two AI turns a tool use requires.
  // Run inline, that froze loop() for the entire chain, so the display
  // never drew another frame: setState(Thinking) above was being applied
  // to a face that then sat motionless for ten seconds, which is why REX
  // looked switched off rather than thinking. The same stall is what the
  // "mic starved" log reports. Core 0 is where TTS already runs and the
  // three phases are strictly sequential, so they never contend, while
  // core 1 keeps animating the Thinking face throughout.
  thinkDone_ = false;
  thinkBusy_ = true;
  thinkWatchdogFired_ = false;
  thinkStartMs_ = nowMs;
  xTaskCreatePinnedToCore(thinkTaskEntry, "rex_think", 16384, this, 1, nullptr, 0);
}

void VoiceController::thinkTaskEntry(void* param) {
  VoiceController* self = static_cast<VoiceController*>(param);
  self->runThinkJob();
  self->thinkDone_ = true;  // set last — this is the completion signal
  vTaskDelete(nullptr);
}

// Runs on core 0. Touches only what the main loop agrees to leave alone
// while thinkBusy_ is true: buffer_/bufferLen_ (mic capture is skipped for
// the duration), the think-result fields, and the latency timestamps.
void VoiceController::runThinkJob() {
  thinkHasExpressionCommand_ = false;  // no stale carry-over from a prior turn

  thinkTranscript_ = stt_.transcribe(buffer_, bufferLen_, AudioInput::kSampleRate);
  tsSttDone_ = millis();
  bufferLen_ = 0;

  // Print what STT returned unconditionally, even empty — otherwise a
  // failed/garbled transcription (bad audio, API hiccup) looks
  // identical to REX silently ignoring you, with zero visible sign
  // anything happened at all.
  Serial.print("[VOICE] heard: \""); Serial.print(thinkTranscript_); Serial.println("\"");
  if (thinkTranscript_.length() == 0) {
    Serial.println("[VOICE] (empty transcription — STT returned nothing usable)");
    return;
  }

  // Text matching only — safe to run here on core 0. A match skips the
  // AI call entirely: "make a happy face" is a command, not a question,
  // and there's no reason to spend a multi-second Claude+TTS round trip
  // (and real API cost) on something answerable locally and instantly.
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

// Runs back on the main loop once runThinkJob() has finished — everything
// here drives CharacterController/AudioOutput, which belong to core 1.
void VoiceController::finishThinkJob(CharacterController& character,
                                     const ReactionEngine& reactionEngine,
                                     AudioOutput& audioOut) {
  if (thinkTranscript_.length() == 0) {
    // Previously silent — REX just reset to Idle with no acknowledgment,
    // indistinguishable from being ignored. Speak up instead, same fix
    // as StreamingVoiceController's (the active default) — kept in sync
    // since this file is the zero-risk fallback if streaming STT is ever
    // reverted.
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
    // One-shot, not sticky: this is a deliberate little performance, not
    // a mood change, so it should attack in, hold, and release back to
    // neutral on its own — exactly like the /emotion debug command.
    // Don't force Idle afterward (unlike the branches above/below) —
    // that would stomp the reaction before it's even visible. Resuming
    // the mic doesn't need to wait for it to finish; the reaction plays
    // out on its own via character.update() regardless.
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
    // Sticky, not onReaction()'s one-shot: a one-shot reaction starts
    // its attack+hold+release countdown the instant the AI decides what
    // to say, NOT when audio actually starts — and TTS's request-to-
    // first-audio gap (measured 0.85-1.8s live) can outlast a short
    // expression's hold (as little as ~900ms total for some), so the
    // face fully released back to a neutral idle look mid-wait, then
    // Speaking (which deliberately preserves whatever's already active)
    // was left holding that stale neutral pose instead of the reply's
    // actual expression. setBaselineEmotion() attacks in the same way
    // but then just holds, indefinitely, until something explicit moves
    // it on — which here is the eventual return to Idle once speech
    // finishes, not an internal timer racing the network.
    character.setBaselineEmotion(thinkAi_.expression, thinkAi_.intensity, millis());

    Serial.print("REX: ");
    Serial.println(thinkAi_.speech);

#ifdef ENABLE_TTS
    // Speaking keeps the emotion onReaction() just set as the mouth's
    // baseline and layers the talking waveform on top of it — see
    // CharacterController::setState(Speaking)'s own comment. Actual
    // playback runs on its own task (see TextToSpeechClient::speakAsync);
    // update() notices completion via tts_.isSpeaking() above and
    // drops back to Idle then.
    // Stay in Thinking until audio actually starts — update() promotes
    // to Speaking on the first chunk that reaches the speaker (see
    // speakingPendingAudio_). Setting it here animated the mouth through
    // the ~1s the TTS request takes, so REX mouthed silence first.
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
  // AI call genuinely failed (no WiFi, API error) — previously silent
  // here too (just the face reaction below, no spoken word). Scoped
  // inside ENABLE_AI deliberately: this is a FAILURE apology, distinct
  // from the no-AI-compiled build's plain silent fallback further down,
  // which stays silent on purpose since nothing there has failed.
  Serial.println("[VOICE] AI call failed — falling back to local reaction.");
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
  // with no real conversational pipeline. NOT a failure, so this stays
  // silent exactly as before — no apology here.
  Reaction r = reactionEngine.classify(thinkTranscript_);
  character.onReaction(r, millis());
  character.setState(CharacterState::Idle, millis());
  resumeListeningAfterPause();
}

// The mic's DMA ring kept filling through every second of a think job or a
// spoken reply. That audio is stale by seconds by the time we look again,
// and would otherwise read as a brand-new utterance the instant we resume.
void VoiceController::resumeListeningAfterPause() {
  audio_.flush();
  vad_.reset();
  ignoreAudioUntilMs_ = millis() + kIgnoreAfterUnmuteMs;
  // Every "a turn just finished" path funnels through here (spoken
  // reply done, reaction-only turn, empty transcript, AI unavailable,
  // abandoned-task cleanup) — one choke point to re-arm the follow-up
  // window from, rather than one call at each site that's easy to miss
  // when a new one gets added later.
  armFollowUpWindow(millis());
}

// audioStartMs: TextToSpeechClient::firstAudioMs() for the reply that
// just finished — 0 if TTS failed or never got a chunk out, in which
// case the TTS/total lines are skipped rather than printing garbage.
void VoiceController::printLatencyReport(uint32_t audioStartMs) const {
  uint32_t hangover = vad_.hangoverMs();
  // tsSpeechEnd_ is when the hangover FINISHED, i.e. VAD-detected end —
  // subtracting the hangover estimates when you actually stopped talking.
  uint32_t estimatedStopMs = tsSpeechEnd_ > hangover ? tsSpeechEnd_ - hangover : tsSpeechEnd_;

  Serial.println("---- LATENCY REPORT ----");
  Serial.print("Speech duration: ");
  Serial.print(estimatedStopMs - tsSpeechStart_); Serial.println(" ms");
  Serial.print("End-of-speech detection (hangover): ");
  Serial.print(hangover); Serial.println(" ms");
  Serial.print("Speech-to-text: ");
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
