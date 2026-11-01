// VoiceController.h — ties microphone capture, voice-activity
// detection, buffering, speech-to-text, the conversational AI, and
// text-to-speech together into the full hands-free loop: user
// speaks -> REX understands -> REX replies out loud. The
// transcribed text is purely an internal handoff between STT and
// the AI — nothing about it is ever shown to the user.
//
// Falls back to the local ReactionEngine (a silent face reaction,
// no spoken reply) if ENABLE_AI isn't compiled in, or if a given
// AI call fails (no WiFi, API error) — REX still reacts to something
// rather than going silent.
//
// Only compiled in when ENABLE_VOICE is defined (see platformio.ini)
// — off by default until the mic is wired and Secrets.h is filled
// in, so the existing Serial-text flow keeps working untouched.
#pragma once
#include <stdint.h>
#include "AudioInput.h"
#include "AudioOutput.h"
#include "VoiceActivityDetector.h"
#include "SpeechToTextClient.h"
#include "CharacterController.h"
#include "ReactionEngine.h"

#ifdef ENABLE_AI
#include "ConversationalAI.h"
#endif
#ifdef ENABLE_TTS
#include "TextToSpeechClient.h"
#endif
#if defined(ENABLE_AI) && defined(ENABLE_TOOLS)
#include "TimerManager.h"
#endif
#ifdef ENABLE_WAKEWORD
#include "WakeWordDetector.h"
#endif

class VoiceController {
public:
#if defined(ENABLE_AI) && defined(ENABLE_TOOLS)
  // timers/memory: the ONE shared TimerManager/MemoryStore instances
  // (owned in main.cpp) — forwarded to this instance's own private
  // ConversationalAI (ai_ below), which is SEPARATE from main.cpp's
  // /ask-path instance. Both need these references or only one of the
  // two paths (live mic vs. /ask) can actually set/announce timers or
  // learn/recall facts.
  void begin(TimerManager& timers, MemoryStore& memory);
#elif defined(ENABLE_AI)
  void begin(MemoryStore& memory);
#else
  void begin();
#endif

  // audioOut: the SHARED speaker output instance (owned by main.cpp,
  // already begin()-initialized) — reused here so only one I2S1
  // driver ever gets installed, whether audio comes from /say, /ask,
  // or a live spoken reply.
  void update(uint32_t nowMs, CharacterController& character,
              const ReactionEngine& reactionEngine, AudioOutput& audioOut);

  // Mutes the mic side of the pipeline (VAD/buffering/STT) without
  // tearing anything down — TTS playback in flight is unaffected.
  // Defaults to listening.
  // Un-muting flushes the mic's stale DMA backlog and resets the
  // detector — see the .cpp for why that matters.
  //
  // fromWakeWord distinguishes a "Hey REX"-triggered session (bounded —
  // see kFollowUpWindowMs below) from an explicit button/"/listen on"
  // session (stays on indefinitely, same as always — a deliberate manual
  // action shouldn't silently time out on the person who made it).
  void setListening(bool enabled, bool fromWakeWord = false);
  bool isListening() const { return listening_; }

#ifdef ENABLE_WAKEWORD
  // Suspends background "Hey REX" polling without touching listening_ —
  // for a debug tool (/mictest) that needs sole use of the same physical
  // mic hardware for a moment. Resume by calling with false; wake-word
  // polling only ever runs while already muted, so this has no effect
  // during an active conversation.
  void setWakeWordPaused(bool paused) { wakeWordPaused_ = paused; }
#endif

private:
  AudioInput              audio_;
  VoiceActivityDetector   vad_;
  SpeechToTextClient      stt_;
#ifdef ENABLE_AI
  ConversationalAI        ai_;
#endif
#ifdef ENABLE_TTS
  TextToSpeechClient      tts_;
#endif
#ifdef ENABLE_WAKEWORD
  // Owns the same audio_ instance as the rest of this class — there is
  // exactly one AudioInput/I2S0 reader in this build, whether the mic is
  // being drained for wake-word polling (while muted) or for VAD (while
  // listening). See update()'s `if (!listening_)` branch.
  WakeWordDetector wakeWord_;
  bool wakeWordBegun_  = false;
  bool wakeWordPaused_ = false;
#endif

  int16_t* buffer_ = nullptr;
  size_t   bufferCapacity_ = 0;
  size_t   bufferLen_ = 0;

  // Boots muted — any serial connection (this board's CP2102 resets
  // on open) wipes runtime state, so defaulting to "on" meant mute
  // never stuck. Turn it on explicitly with /listen on.
  bool     listening_ = false;

  // "Follow-up window", same shape as classic Alexa's: once a "Hey REX"
  // session has nothing left to wait on (no reply in flight, no utterance
  // in progress), there's a bounded window to speak again with no wake
  // word needed. Elapse it in silence and REX re-mutes on its own —
  // without this, a wake-word session had no way to ever end itself, so
  // a stray later sound (TV, room conversation) stayed a live trigger
  // indefinitely. Does NOT apply to a button/"/listen on" session
  // (listeningFromWakeWord_ false) — that's explicit manual control and
  // stays on until explicitly turned off, same as always.
  bool     listeningFromWakeWord_ = false;
  uint32_t followUpDeadlineMs_ = 0;  // 0 = disarmed
  static constexpr uint32_t kFollowUpWindowMs = 7000;
  void armFollowUpWindow(uint32_t nowMs);

  // Audio captured before this time is ignored after an un-mute — the
  // physical button's own click lands a few ms after the press and would
  // otherwise be detected as the start of an utterance.
  uint32_t ignoreAudioUntilMs_ = 0;

#ifdef ENABLE_TTS
  // True from the moment tts_.speakAsync() is kicked off until this
  // class notices it finished (tts_.isSpeaking() goes false) — lets
  // update() skip mic capture entirely while REX is talking, both to
  // avoid picking up its own voice through the mic and because the
  // speech is already streaming on its own FreeRTOS task by then.
  bool wasSpeaking_ = false;

  // The mouth's talking waveform must not start until audio is actually
  // leaving the speaker. speakAsync() returns immediately but the TTS
  // request takes ~0.8-1.8s before the first byte plays, so setting
  // Speaking at kick-off made REX mouth silence for a second first.
  // This stays true from kick-off until the first audio chunk lands.
  bool speakingPendingAudio_ = false;
#endif

  // Speech-to-text and the AI call are blocking multi-second network
  // round trips, so they run on core 0 (like TTS) instead of on loop().
  // thinkBusy_ is the main loop's "hands off" flag: while it is true the
  // mic isn't read and buffer_/bufferLen_ belong to the think task.
  // thinkDone_ is written last by that task and polled by update().
  static void thinkTaskEntry(void* param);
  void runThinkJob();      // core 0: STT, then the AI call
  void finishThinkJob(CharacterController& character,
                      const ReactionEngine& reactionEngine,
                      AudioOutput& audioOut);  // core 1: face + speech
  void resumeListeningAfterPause();
  volatile bool thinkBusy_ = false;
  volatile bool thinkDone_ = false;

  // Top-level backstop on top of every individual network timeout
  // (STT's TLS handshake/response, each AI call, each tool's own
  // NetworkBudget) — those bound each PIECE, but nothing previously
  // bounded runThinkJob() as a WHOLE, so any one of them behaving
  // unexpectedly (or a bug none of them catches) left the main loop
  // waiting on thinkDone_ with no way out. thinkStartMs_ is stamped
  // when the core-0 task is spawned; see update()'s thinkBusy_ branch
  // for the actual watchdog check and recovery.
  uint32_t thinkStartMs_ = 0;
  // True once the watchdog has given up waiting and told the user —
  // guards against acting a SECOND time if the abandoned task
  // eventually does complete (thinkDone_ turns true after all): its
  // results are discarded rather than spoken, since REX already said
  // it gave up. Prevents spawning a new think job — and touching
  // shared state (buffer_, thinkTranscript_, ai_'s history) the old
  // task might still be mid-write into — until that task genuinely
  // finishes and this is cleared.
  bool     thinkWatchdogFired_ = false;
  // Confirmed live (see the comment history in VoiceController.cpp's
  // update()): a DNS resolution stall — which has its own ~31s internal
  // timeout on this platform, invisible to and unbounded by every
  // per-request HTTPClient timeout in this codebase — tripped this
  // watchdog at the 55s value it originally held. 30s is a deliberate
  // trade: faster feedback when something is genuinely stuck, at the
  // cost of being somewhat more likely to cut off a legitimately slow
  // multi-tool-call turn (2+ tool calls plus 3 AI round trips can
  // itself approach this without anything being wrong).
  static constexpr uint32_t kThinkWatchdogMs = 30000;

  String        thinkTranscript_;
#ifdef ENABLE_AI
  AIResponse    thinkAi_;
#endif
  // Set by runThinkJob() (core 0), right after transcription and BEFORE
  // the AI call — expressionCommandFromText() is pure text matching, so
  // it's safe to run there. When true, finishThinkJob() performs the
  // expression directly and skips AI+TTS entirely: the whole point is
  // to shortcut past that multi-second round trip for something that's
  // really just a command ("make a happy face"), not a question.
  bool       thinkHasExpressionCommand_ = false;
  Expression thinkExpressionCommand_ = Expression::Neutral;

  // Timestamps for the end-to-end latency report (see printLatencyReport()
  // in the .cpp) — one utterance's worth, overwritten each turn.
  uint32_t tsSpeechStart_ = 0;   // VAD Silence -> Speaking
  uint32_t tsSpeechEnd_   = 0;   // VAD Speaking -> Silence (i.e. after the hangover)
  uint32_t tsSttDone_     = 0;   // transcribe() returned
  uint32_t tsAiDone_      = 0;   // converse() returned
  uint16_t lastWordCount_ = 0;   // words in the spoken reply, for the report
  void printLatencyReport(uint32_t audioStartMs) const;

  // TEMP DEBUG — voice-pipeline investigation instrumentation. Remove
  // once the timing/failure questions it exists to answer are settled.
  uint32_t dbgListenEnabledMs_ = 0;  // when the mic was last un-muted
  uint32_t dbgLastReadMs_ = 0;       // last time readSamples() actually ran
  uint16_t dbgChunksLogged_ = 0;     // per-chunk RMS trace after un-mute
};
