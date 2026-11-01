// StreamingVoiceController.h — sibling of VoiceController, and (as of
// ENABLE_STREAMING_STT being on by default) the active voice pipeline.
// Same job (mic -> VAD -> STT -> AI -> TTS, hands-free), same public
// shape, but speech-to-text is OpenAI's realtime streaming transcription
// (StreamingSpeechToTextClient) instead of one batch upload
// (SpeechToTextClient) — see StreamingSpeechToTextClient.h for the
// protocol and the latency motivation (live-verified: STT sub-step
// 824ms vs. batch's measured 3.4-4.3s).
//
// Deliberately a sibling, not a subclass: VoiceController has no virtual
// interface to inherit from, and introducing one would mean editing the
// proven file, which this whole design exists to avoid. This file
// duplicates VoiceController's structure closely on purpose — the
// isolation requirement is "zero risk to the proven path", not "zero
// duplication".
//
// Whole file compiled out unless BOTH ENABLE_VOICE and ENABLE_STREAMING_STT
// are defined — see src/main.cpp for the one-line switch between this and
// VoiceController (ActiveVoiceController), which is the ONLY existing file
// this feature touches.
//
// Differences from VoiceController, everything else copied verbatim:
//   - stt_ is a StreamingSpeechToTextClient, not a SpeechToTextClient.
//   - No PSRAM utterance buffer (buffer_/bufferCapacity_/bufferLen_) —
//     nothing ever needs the whole raw utterance at once; audio is
//     streamed out chunk-by-chunk as it's captured instead.
//   - setListening(true, ...) and resumeListeningAfterPause() also call
//     stt_.connect() (idempotent — a no-op if already connected), so the
//     WS handshake happens BEFORE speech starts, not at SpeechStarted —
//     otherwise this reintroduces the exact handshake cost it exists to
//     eliminate, just moved to a different point in the turn.
//   - On VAD's SpeechEnded, calls stt_.commitUtterance() and enters a
//     bounded, non-blocking "awaiting transcript" wait instead of
//     immediately spawning the think task — see awaitingTranscript_.
//   - The think task (runThinkJob) only does the expression-command check
//     and the AI call — the STT sub-step already happened by the time it
//     spawns.
//   - All stt_.* calls happen from update() (core 1) only, never from the
//     core-0 think task — unlike batch STT's one self-contained blocking
//     call, this class holds a live, stateful socket; touching it from
//     two cores at once is a real hazard, not a theoretical one.
#pragma once
#if defined(ENABLE_VOICE) && defined(ENABLE_STREAMING_STT)
#include <stdint.h>
#include "AudioInput.h"
#include "AudioOutput.h"
#include "VoiceActivityDetector.h"
#include "StreamingSpeechToTextClient.h"
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

class StreamingVoiceController {
public:
#if defined(ENABLE_AI) && defined(ENABLE_TOOLS)
  void begin(TimerManager& timers, MemoryStore& memory);
#elif defined(ENABLE_AI)
  void begin(MemoryStore& memory);
#else
  void begin();
#endif

  void update(uint32_t nowMs, CharacterController& character,
              const ReactionEngine& reactionEngine, AudioOutput& audioOut);

  void setListening(bool enabled, bool fromWakeWord = false);
  bool isListening() const { return listening_; }

#ifdef ENABLE_WAKEWORD
  void setWakeWordPaused(bool paused) { wakeWordPaused_ = paused; }
#endif

private:
  AudioInput                    audio_;
  VoiceActivityDetector         vad_;
  StreamingSpeechToTextClient   stt_;
#ifdef ENABLE_AI
  ConversationalAI        ai_;
#endif
#ifdef ENABLE_TTS
  TextToSpeechClient      tts_;
#endif
#ifdef ENABLE_WAKEWORD
  WakeWordDetector wakeWord_;
  bool wakeWordBegun_  = false;
  bool wakeWordPaused_ = false;
#endif

  bool     listening_ = false;

  bool     listeningFromWakeWord_ = false;
  uint32_t followUpDeadlineMs_ = 0;  // 0 = disarmed
  static constexpr uint32_t kFollowUpWindowMs = 7000;
  void armFollowUpWindow(uint32_t nowMs);

  uint32_t ignoreAudioUntilMs_ = 0;

#ifdef ENABLE_TTS
  bool wasSpeaking_ = false;
  bool speakingPendingAudio_ = false;
#endif

  static void thinkTaskEntry(void* param);
  void runThinkJob();      // core 0: the AI call only — STT already happened
  void finishThinkJob(CharacterController& character,
                      const ReactionEngine& reactionEngine,
                      AudioOutput& audioOut);  // core 1: face + speech
  void resumeListeningAfterPause();
  volatile bool thinkBusy_ = false;
  volatile bool thinkDone_ = false;

  // Non-blocking wait between stt_.commitUtterance() and a ready
  // transcript — the streaming-STT analog of thinkBusy_, but for the STT
  // sub-step instead of the AI call. Bounded well under kThinkWatchdogMs:
  // the whole point of this class is that this wait should be near-zero
  // (measured live in isolated testing: ~600ms commit-to-transcript,
  // against batch STT's 3.4-4.3s for the equivalent step) — if it's
  // regularly hitting this timeout, streaming STT isn't delivering on its
  // premise and that's worth knowing loudly, not silently falling back.
  bool     awaitingTranscript_ = false;
  uint32_t awaitingTranscriptStartMs_ = 0;
  static constexpr uint32_t kAwaitTranscriptMs = 4000;

  // Self-healing recovery for the one confirmed, repeatable failure mode
  // seen live: after long uptime, mbedTLS occasionally fails to allocate
  // the WebSocket's TLS buffers ("Failed to allocate buf memory") — almost
  // certainly heap fragmentation from many prior HTTPS/WSS sessions
  // (weather/AI/TTS/search/news/STT each open and close their own), not a
  // low-total-memory problem. A single timeout reliably self-recovers on
  // the very next attempt (observed live, twice) so this does NOT act on
  // one failure — only on several in a row, which means REX is genuinely
  // stuck rather than just hitting one transient blip, and a clean reboot
  // resets the heap to a pristine state. Incremented in the
  // awaitingTranscript_ timeout branch, reset to 0 on any successful
  // transcript.
  uint8_t  consecutiveSttFailures_ = 0;
  static constexpr uint8_t kMaxConsecutiveSttFailures = 3;

  uint32_t thinkStartMs_ = 0;
  bool     thinkWatchdogFired_ = false;
  static constexpr uint32_t kThinkWatchdogMs = 30000;

  String        thinkTranscript_;
#ifdef ENABLE_AI
  AIResponse    thinkAi_;
#endif
  bool       thinkHasExpressionCommand_ = false;
  Expression thinkExpressionCommand_ = Expression::Neutral;

  uint32_t tsSpeechStart_ = 0;
  uint32_t tsSpeechEnd_   = 0;
  uint32_t tsSttDone_     = 0;
  uint32_t tsAiDone_      = 0;
  uint16_t lastWordCount_ = 0;
  void printLatencyReport(uint32_t audioStartMs) const;

  uint32_t dbgListenEnabledMs_ = 0;
  uint32_t dbgLastReadMs_ = 0;
  uint16_t dbgChunksLogged_ = 0;
};
#endif
