// StreamingSpeechToTextClient.h — sends mic audio to OpenAI's realtime
// transcription endpoint continuously, over one persistent WebSocket,
// instead of buffering a whole utterance and uploading it as one HTTPS
// request (SpeechToTextClient, the original batch path — still intact,
// not removed). The point is latency: measured live, batch STT alone
// took 3.4-4.3s (TLS handshake ~1.1-1.2s + upload+inference ~2.3-3.1s)
// of the ~6-7s before REX's reply started. Streaming audio in as it's
// captured means most of that transcription work is already done by the
// time the user stops talking — live-verified STT sub-step: 824ms.
//
// Whole file is compiled out unless BOTH ENABLE_VOICE and
// ENABLE_STREAMING_STT are defined (both on by default now) — unlike
// VoiceController.cpp (which compiles unconditionally), this stays
// guarded so the original batch path remains a genuine, zero-risk
// fallback: comment out ENABLE_STREAMING_STT in platformio.ini and
// reflash to go back to it instantly if this one ever needs reverting.
// See StreamingVoiceController.h for how this plugs in (completely
// separately from VoiceController) and src/main.cpp for the one-line
// switch between them.
//
// Knows nothing about VAD, AudioInput, or the character — same "pure
// boundary" shape SpeechToTextClient already has. The caller (eventually
// StreamingVoiceController) is responsible for calling poll() every tick
// and for only calling sendAudioChunk() while real speech is in progress.
//
// Protocol (OpenAI's realtime transcription session, current as of Oct
// 2026 — sourced from OpenAI's own docs, not a first-party SDK, so the
// exact WS URL query string is UNCONFIRMED and must be resolved empirically
// as the very first test against this class, see WakeWordDetector.h-style
// "verify before trusting" discipline this project uses everywhere else):
//   - wss://api.openai.com/v1/realtime (+ an unconfirmed query param)
//   - Authorization: Bearer {OPENAI_API_KEY} — same key already in
//     Secrets.h, no new secret needed.
//   - First message: configure a "transcription" session, pcm16 @ 24kHz,
//     model "gpt-live-transcribe", turn_detection=null — WE control turn
//     boundaries via our own proven VoiceActivityDetector, not the server.
//   - Per chunk: {"type":"input_audio_buffer.append","audio":"<base64>"}
//   - On local VAD SpeechEnded: {"type":"input_audio_buffer.commit"}
//   - Server replies with .delta (partial, field "delta") and .completed
//     (final, field "transcript") events.
//
// Mic captures at 16kHz (AudioInput::kSampleRate — shared with VAD/wake-
// word, must not change), but this protocol requires 24kHz. resample()
// below does a simple 2:3 linear-interpolation upsample; verify this
// standalone (see main.cpp's /resampletest) before trusting any transcript
// quality issue to be about the model rather than mangled audio.
#pragma once
#if defined(ENABLE_VOICE) && defined(ENABLE_STREAMING_STT)
#include <Arduino.h>
#include <WebSocketsClient.h>

class StreamingSpeechToTextClient {
public:
  // Opens the WSS session and sends the session-config message. The
  // handshake happens asynchronously on the library's own schedule inside
  // poll() — call connect() once, then poll isConnected()/isFailed().
  bool connect();

  // Pump the WebSocket library and dispatch any received frames into this
  // object's state. MUST be called every tick while a session exists —
  // no documented ping/pong requirement was found for this endpoint, but
  // this still needs to run often enough to service one if the server
  // sends it, and to notice a dropped socket promptly (the prior
  // persistent-WiFiClientSecure attempt in SpeechToTextClient.cpp's own
  // history was removed for exactly that class of silent-staleness risk).
  //
  // MUST be called from the main loop (core 1) only, never from a core-0
  // task. Unlike batch STT (stt_.transcribe() is one self-contained
  // blocking call with nothing shared), this class holds a live,
  // stateful socket — touching it from two cores at once is a real
  // hazard, not a theoretical one.
  void poll();

  bool isConnected() const { return connected_; }
  bool isFailed() const { return failed_; }

  // Resamples pcm16_16k (16kHz mono) to 24kHz, base64-encodes it, and sends
  // one input_audio_buffer.append frame. Call this — and only this, no
  // separate resample step — for every captured chunk while real speech is
  // in progress (i.e. the same condition VoiceController's buffer_ copy
  // already uses: vad_.state() == VoiceState::Speaking). The resample
  // phase is carried across calls (resamplePhase_), not reset per call, so
  // consecutive chunks don't click at the boundary.
  void sendAudioChunk(const int16_t* pcm16_16k, size_t sampleCount);

  // Call exactly once, when local VAD reports SpeechEnded. Sends
  // input_audio_buffer.commit, signaling end-of-turn (this model requires
  // client-controlled turn detection — turn_detection is null in the
  // session config above).
  void commitUtterance();

  bool transcriptReady() const { return transcriptReady_; }
  // Consumes and clears the ready flag/text.
  String takeTranscript();

  void disconnect();

  // Persisted state for resample16kTo24k, carried across consecutive
  // chunks of one continuous stream. `phase` is the fractional
  // input-position offset (always in [0,1)); `lastSample`/`hasLastSample`
  // carry the final input sample from the previous call, since a correct
  // interpolation at the very start of a new chunk needs the sample that
  // came immediately before it, which lives in the PREVIOUS chunk's
  // buffer, not this one. Without this, the last fractional step of every
  // chunk would either be dropped or interpolated against the wrong
  // sample — a small but real correctness bug for a function that runs
  // ~60+ times/second. Default-construct a fresh instance for a new,
  // unrelated stream (e.g. a one-shot /resampletest run); reuse the same
  // instance across every chunk of one real utterance.
  struct ResampleState {
    float phase = 0.0f;
    int16_t lastSample = 0;
    bool hasLastSample = false;
  };

  // Public for the offline unit-style test in main.cpp (/resampletest) —
  // pure function, no WebSocket/network/mic involvement. outMax must be at
  // least (inCount*3/2 + 2) samples. Returns the number of samples written.
  static size_t resample16kTo24k(const int16_t* in, size_t inCount,
                                  int16_t* out, size_t outMax, ResampleState& state);

private:
  void onWsEvent(WStype_t type, uint8_t* payload, size_t length);
  void handleServerEvent(const char* json, size_t length);

  WebSocketsClient ws_;
  bool connected_ = false;
  bool failed_ = false;
  bool sessionConfigured_ = false;

  ResampleState resampleState_;

  bool transcriptReady_ = false;
  String transcript_;
  // TEMP DIAGNOSTIC — latest partial text, Serial-visibility only, not
  // required for correctness.
  String partial_;
};
#endif
