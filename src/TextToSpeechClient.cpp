#include "TextToSpeechClient.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "Secrets.h"

namespace {

// Voice ID carried over from the earlier ElevenLabs setup — swap for
// a different one from https://elevenlabs.io/app/speech-synthesis.
constexpr const char* kVoiceId = "JBFqnCBsd6RMkjVDRZzb";

String jsonEscape(const String& in) {
  String out;
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if (c == '"' || c == '\\') { out += '\\'; out += c; }
    else if (c == '\n') out += "\\n";
    else out += c;
  }
  return out;
}

// speakAsync()'s task owns one of these: a heap copy of the text (so
// it outlives the caller's String) plus the pointers it needs to call
// back into the instance and AudioOutput. Freed by the task itself.
struct TtsTaskArgs {
  TextToSpeechClient* self;
  AudioOutput* out;
  String* text;
};

} // namespace

void TextToSpeechClient::taskEntry(void* param) {
  TtsTaskArgs* args = static_cast<TtsTaskArgs*>(param);
  bool ok = args->self->speak(*args->text, *args->out);
  args->self->lastResultOk_ = ok;
  args->self->speaking_ = false;  // set last — this is the completion signal
  delete args->text;
  delete args;
  vTaskDelete(nullptr);
}

void TextToSpeechClient::speakAsync(const String& text, AudioOutput& out) {
  if (speaking_) {
    Serial.println("[TTS] speakAsync called while already speaking — ignored");
    return;
  }
  // Clear this BEFORE speaking_ goes true. Callers poll firstAudioMs()
  // to know when audio actually started (that's what gates the mouth
  // animation) — speak() also zeroes it, but that runs on the new task,
  // so there's a window where a poller would still see the PREVIOUS
  // utterance's timestamp and start the mouth immediately.
  firstAudioMs_ = 0;
  speaking_ = true;
  TtsTaskArgs* args = new TtsTaskArgs{this, &out, new String(text)};
  // Pinned to core 0 so the network/I2S work never shares a core with
  // the main loop's SPI display pushes on core 1 — see TextToSpeechClient.h.
  xTaskCreatePinnedToCore(taskEntry, "tts_speak", 8192, args, 1, nullptr, 0);
}

bool TextToSpeechClient::speak(const String& text, AudioOutput& out) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[TTS] WiFi not connected");
    return false;
  }
  if (text.length() == 0) return false;

  firstAudioMs_ = 0;  // reset for this call; set once, on the first chunk written below

  String url = String("https://api.elevenlabs.io/v1/text-to-speech/") + kVoiceId
             + "?output_format=pcm_" + String(AudioOutput::kSampleRate);
  // eleven_multilingual_v2 (the old default) is ElevenLabs' highest-quality
  // but slowest model — it was the single biggest latency cost in the
  // whole pipeline (~2-2.4s just for the POST to return, before any audio
  // streamed, per live logs). eleven_flash_v2_5 is built for real-time
  // conversational agents (ElevenLabs advertises ~75ms model latency)
  // at a modest quality cost that's not very noticeable on a small speaker.
  String body = "{\"text\":\"" + jsonEscape(text) + "\",\"model_id\":\"eleven_flash_v2_5\"}";

  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(15000);
  http.begin(url);
  http.addHeader("xi-api-key", ELEVENLABS_API_KEY);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "audio/pcm");

  Serial.print("[TTS] POST "); Serial.println(url);
  uint32_t t0 = millis();
  int code = http.POST(body);
  Serial.print("[TTS] POST returned code="); Serial.print(code);
  Serial.print(" after "); Serial.print(millis() - t0); Serial.println(" ms");

  if (code != 200) {
    Serial.println(http.getString());
    http.end();
    return false;
  }

  WiFiClient* stream = http.getStreamPtr();
  int contentLen = http.getSize();  // -1/0 for a chunked response
  Serial.print("[TTS] contentLen="); Serial.println(contentLen);

  const size_t kChunkSamples = 512;
  int16_t buf[kChunkSamples];
  size_t totalBytesRead = 0;
  uint32_t lastProgressMs = millis();

  if (contentLen > 0) {
    // Known length: read directly with readBytes(), which blocks
    // efficiently on the stream's own buffering until data arrives.
    // Polling available() in small increments (the fallback path
    // below) turned out to be extremely slow over TLS — tens of
    // seconds for a ~48KB response — so use this path whenever we can.
    while (totalBytesRead < (size_t)contentLen) {
      size_t remaining = (size_t)contentLen - totalBytesRead;
      size_t want = kChunkSamples * sizeof(int16_t);
      if (want > remaining) want = remaining;
      want -= (want % sizeof(int16_t));
      if (want == 0) break;

      int got = stream->readBytes((uint8_t*)buf, want);
      if (got <= 0) {
        Serial.println("[TTS] readBytes returned 0, stopping");
        break;
      }
      totalBytesRead += got;
      if (firstAudioMs_ == 0) firstAudioMs_ = millis();
      out.writeSamples(buf, got / sizeof(int16_t));

      if (millis() - lastProgressMs > 2000) {
        lastProgressMs = millis();
        Serial.print("[TTS] progress "); Serial.print(totalBytesRead);
        Serial.print("/"); Serial.println(contentLen);
      }
    }
  } else {
    // Unknown/chunked length: poll available() with a silence timeout.
    uint32_t lastDataMs = millis();
    while (http.connected()) {
      size_t avail = stream->available();
      if (avail == 0) {
        if (millis() - lastDataMs > 5000) {
          Serial.println("[TTS] 5s silence, giving up");
          break;
        }
        delay(2);
        continue;
      }
      size_t wantBytes = kChunkSamples * sizeof(int16_t);
      size_t toRead = avail < wantBytes ? avail : wantBytes;
      toRead -= (toRead % sizeof(int16_t));
      if (toRead == 0) { delay(1); continue; }

      int got = stream->readBytes((uint8_t*)buf, toRead);
      if (got <= 0) break;
      lastDataMs = millis();
      totalBytesRead += got;
      if (firstAudioMs_ == 0) firstAudioMs_ = millis();
      out.writeSamples(buf, got / sizeof(int16_t));
    }
  }

  Serial.print("[TTS] finished, totalBytesRead="); Serial.println(totalBytesRead);
  http.end();
  return totalBytesRead > 0;
}
