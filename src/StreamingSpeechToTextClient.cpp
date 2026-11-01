#include "StreamingSpeechToTextClient.h"
#if defined(ENABLE_VOICE) && defined(ENABLE_STREAMING_STT)
#include "Secrets.h"
#include <ArduinoJson.h>
#include <mbedtls/base64.h>
#include <string.h>

bool StreamingSpeechToTextClient::connect() {
  // Idempotent — StreamingVoiceController calls this both at
  // setListening(true,...) and at the end of every turn (to pre-warm the
  // NEXT one), and those two call sites can legitimately land close
  // together. Already connected means there's nothing to do; connected_
  // is maintained purely from this class's own WStype_CONNECTED/
  // DISCONNECTED event handling, so it's a reliable liveness check.
  if (connected_) return true;

  connected_ = false;
  failed_ = false;
  sessionConfigured_ = false;
  transcriptReady_ = false;
  transcript_ = "";
  partial_ = "";
  resampleState_ = ResampleState();

  ws_.onEvent([this](WStype_t type, uint8_t* payload, size_t length) {
    onWsEvent(type, payload, length);
  });

  String auth = String("Authorization: Bearer ") + OPENAI_API_KEY;
  ws_.setExtraHeaders(auth.c_str());

  // beginSSL's 3-arg overload with no fingerprint/CA cert -> on ESP32 the
  // library calls the underlying WiFiClientSecure's own setInsecure()
  // automatically (confirmed by reading WebSocketsClient.cpp directly, not
  // assumed) — same no-cert-validation posture every other HTTPS call in
  // this codebase already uses.
  //
  // UNCONFIRMED from secondary docs — the exact query string here is the
  // first thing to resolve empirically, via /streamtest connect in
  // main.cpp, against real connection/auth responses. ?intent=transcription
  // is the first attempt.
  ws_.beginSSL("api.openai.com", 443, "/v1/realtime?intent=transcription");
  return true;  // handshake is async — poll isConnected()/isFailed()
}

void StreamingSpeechToTextClient::poll() {
  ws_.loop();
}

void StreamingSpeechToTextClient::onWsEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      Serial.println("[STREAM-STT] WS connected");
      connected_ = true;
      failed_ = false;
      {
        // pcm16 @ 24kHz, gpt-live-transcribe, client-controlled turns (we
        // drive turn boundaries via our own proven VoiceActivityDetector,
        // not the server's) — see StreamingSpeechToTextClient.h's header
        // comment for why turn_detection must be null for this model.
        const char* cfg =
          "{\"type\":\"session.update\",\"session\":{\"type\":\"transcription\","
          "\"audio\":{\"input\":{\"format\":{\"type\":\"audio/pcm\",\"rate\":24000},"
          "\"transcription\":{\"model\":\"gpt-live-transcribe\"},\"turn_detection\":null}}}}";
        ws_.sendTXT(cfg);
        sessionConfigured_ = true;
      }
      break;
    case WStype_DISCONNECTED:
      Serial.println("[STREAM-STT] WS disconnected");
      connected_ = false;
      sessionConfigured_ = false;
      break;
    case WStype_TEXT:
      handleServerEvent(reinterpret_cast<const char*>(payload), length);
      break;
    case WStype_ERROR:
      Serial.print("[STREAM-STT] WS error");
      if (length > 0) {
        Serial.print(": ");
        Serial.write(payload, length);
      }
      Serial.println();
      failed_ = true;
      break;
    default:
      break;  // BIN/PING/PONG/fragment types: nothing to do for this protocol
  }
}

void StreamingSpeechToTextClient::handleServerEvent(const char* json, size_t length) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json, length);
  if (err) {
    Serial.print("[STREAM-STT] bad JSON from server: ");
    Serial.println(err.c_str());
    return;
  }
  const char* type = doc["type"] | "";
  if (strcmp(type, "conversation.item.input_audio_transcription.delta") == 0) {
    const char* delta = doc["delta"] | "";
    partial_ += delta;
    Serial.print("[STREAM-STT] delta: "); Serial.println(delta);
  } else if (strcmp(type, "conversation.item.input_audio_transcription.completed") == 0) {
    const char* transcript = doc["transcript"] | "";
    transcript_ = transcript;
    transcriptReady_ = true;
    Serial.print("[STREAM-STT] completed: \""); Serial.print(transcript_); Serial.println("\"");
  } else if (strcmp(type, "error") == 0) {
    const char* msg = doc["error"]["message"] | "(no message)";
    Serial.print("[STREAM-STT] server error: "); Serial.println(msg);
    failed_ = true;
  } else {
    // session.created / session.updated / input_audio_buffer.committed /
    // etc. — log the type so Milestone 1 testing can see the whole real
    // handshake, not just the events this class currently acts on.
    Serial.print("[STREAM-STT] event: "); Serial.println(type);
  }
}

void StreamingSpeechToTextClient::sendAudioChunk(const int16_t* pcm16_16k, size_t sampleCount) {
  if (!connected_ || !sessionConfigured_ || sampleCount == 0) return;

  // 256 input samples (this project's one real chunk size, kChunkSamples)
  // -> at most 256*3/2 = 384 output samples; 400 leaves headroom.
  static int16_t resampled[400];
  size_t outCount = resample16kTo24k(pcm16_16k, sampleCount, resampled, 400, resampleState_);
  if (outCount == 0) return;

  size_t rawBytes = outCount * sizeof(int16_t);
  // base64: ceil(n/3)*4 bytes, +1 for the NUL this code adds itself.
  static uint8_t b64[((400 * sizeof(int16_t)) + 2) / 3 * 4 + 1];
  size_t b64Len = 0;
  int ret = mbedtls_base64_encode(b64, sizeof(b64), &b64Len,
                                   reinterpret_cast<const uint8_t*>(resampled), rawBytes);
  if (ret != 0) {
    Serial.println("[STREAM-STT] base64 encode failed (buffer too small?)");
    return;
  }
  b64[b64Len] = '\0';

  String msg;
  msg.reserve(b64Len + 40);
  msg = "{\"type\":\"input_audio_buffer.append\",\"audio\":\"";
  msg += reinterpret_cast<const char*>(b64);
  msg += "\"}";
  ws_.sendTXT(msg);
}

void StreamingSpeechToTextClient::commitUtterance() {
  if (!connected_) return;
  transcriptReady_ = false;
  transcript_ = "";
  partial_ = "";
  ws_.sendTXT("{\"type\":\"input_audio_buffer.commit\"}");
}

String StreamingSpeechToTextClient::takeTranscript() {
  transcriptReady_ = false;
  String t = transcript_;
  transcript_ = "";
  return t;
}

void StreamingSpeechToTextClient::disconnect() {
  ws_.disconnect();
  connected_ = false;
  sessionConfigured_ = false;
}

size_t StreamingSpeechToTextClient::resample16kTo24k(const int16_t* in, size_t inCount,
                                                       int16_t* out, size_t outMax,
                                                       ResampleState& state) {
  if (inCount == 0) return 0;

  // Exact 2:3 ratio (16000*3 == 24000*2) — each output sample advances the
  // virtual input position by 2/3. `state.phase` is the fractional part of
  // that position, carried across calls so consecutive chunks of one
  // continuous stream don't click at the boundary. `state.lastSample` is
  // the final real sample from the PREVIOUS call, standing in for
  // "virtual index -1" so the first fractional step of a new chunk
  // interpolates against the sample that actually preceded it, not
  // against garbage or a duplicate of in[0].
  constexpr float kStep = 2.0f / 3.0f;

  long idx = state.hasLastSample ? -1 : 0;
  float pos = state.phase;
  size_t outCount = 0;

  while (outCount < outMax) {
    long i1 = idx + 1;
    if (i1 >= static_cast<long>(inCount)) break;  // need a sample from the NEXT call to continue
    int16_t a = (idx < 0) ? state.lastSample : in[static_cast<size_t>(idx)];
    int16_t b = in[static_cast<size_t>(i1)];
    out[outCount++] = static_cast<int16_t>(static_cast<float>(a) +
                                            static_cast<float>(b - a) * pos);
    pos += kStep;
    while (pos >= 1.0f) {
      pos -= 1.0f;
      idx++;
    }
  }

  state.phase = pos;
  state.lastSample = in[inCount - 1];
  state.hasLastSample = true;
  return outCount;
}
#endif
