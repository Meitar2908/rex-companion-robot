#include "SpeechToTextClient.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <string.h>
#include "Secrets.h"

namespace {

// Minimal 44-byte canonical WAV header for mono 16-bit PCM.
void writeWavHeader(uint8_t* hdr, uint32_t sampleRate, uint32_t dataBytes) {
  uint32_t byteRate    = sampleRate * 2;
  uint32_t riffSize    = 36 + dataBytes;
  uint32_t fmtSize     = 16;
  uint16_t audioFormat = 1;
  uint16_t numChannels = 1;
  uint16_t blockAlign  = 2;
  uint16_t bitsPerSample = 16;

  memcpy(hdr + 0,  "RIFF", 4);
  memcpy(hdr + 4,  &riffSize, 4);
  memcpy(hdr + 8,  "WAVE", 4);
  memcpy(hdr + 12, "fmt ", 4);
  memcpy(hdr + 16, &fmtSize, 4);
  memcpy(hdr + 20, &audioFormat, 2);
  memcpy(hdr + 22, &numChannels, 2);
  memcpy(hdr + 24, &sampleRate, 4);
  memcpy(hdr + 28, &byteRate, 4);
  memcpy(hdr + 32, &blockAlign, 2);
  memcpy(hdr + 34, &bitsPerSample, 2);
  memcpy(hdr + 36, "data", 4);
  memcpy(hdr + 40, &dataBytes, 4);
}

// Whisper's default response is {"text": "..."} — pull the value out
// directly rather than pulling in a JSON library for one field.
String extractTextField(const String& json) {
  int key = json.indexOf("\"text\"");
  if (key < 0) return "";
  int colon = json.indexOf(':', key);
  int firstQuote = json.indexOf('"', colon + 1);
  if (firstQuote < 0) return "";

  int i = firstQuote + 1;
  String out;
  while (i < (int)json.length() && json[i] != '"') {
    if (json[i] == '\\' && i + 1 < (int)json.length()) {
      i++;
      char c = json[i];
      if (c == 'n') out += '\n';
      else if (c == 't') out += '\t';
      else out += c;
    } else {
      out += json[i];
    }
    i++;
  }
  return out;
}

} // namespace

String SpeechToTextClient::transcribe(const int16_t* pcm16, size_t sampleCount, uint32_t sampleRate) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[STT] WiFi not connected");
    return "";
  }

  uint32_t dataBytes = (uint32_t)(sampleCount * sizeof(int16_t));
  uint8_t wavHeader[44];
  writeWavHeader(wavHeader, sampleRate, dataBytes);

  String boundary = "REXBoundary7331";
  // Tried gpt-4o-mini-transcribe for latency (it's faster) but live
  // testing showed it repeatedly hallucinating fluent-sounding text in
  // random languages (Chinese, Russian — including outright invented
  // non-words) on short/ambiguous audio, roughly 1 in 4 utterances.
  // whisper-1 doesn't do this anywhere near as often in practice, so
  // reliability wins here — back to whisper-1. Keeping the explicit
  // "language" hint since it doesn't hurt and may still help whisper-1
  // avoid wrong-language guesses on marginal audio.
  String partHeader =
    "--" + boundary + "\r\n"
    "Content-Disposition: form-data; name=\"model\"\r\n\r\nwhisper-1\r\n"
    "--" + boundary + "\r\n"
    "Content-Disposition: form-data; name=\"language\"\r\n\r\nen\r\n"
    "--" + boundary + "\r\n"
    "Content-Disposition: form-data; name=\"file\"; filename=\"speech.wav\"\r\n"
    "Content-Type: audio/wav\r\n\r\n";
  String partFooter = "\r\n--" + boundary + "--\r\n";

  size_t totalLen = partHeader.length() + sizeof(wavHeader) + dataBytes + partFooter.length();

  // Built in PSRAM — utterances can be a few hundred KB, comfortably
  // more than internal SRAM should be spending on a transient buffer.
  uint8_t* body = (uint8_t*)ps_malloc(totalLen);
  if (!body) {
    Serial.println("[STT] out of PSRAM for request body");
    return "";
  }

  size_t offset = 0;
  memcpy(body + offset, partHeader.c_str(), partHeader.length()); offset += partHeader.length();
  memcpy(body + offset, wavHeader, sizeof(wavHeader));            offset += sizeof(wavHeader);
  memcpy(body + offset, pcm16, dataBytes);                        offset += dataBytes;
  memcpy(body + offset, partFooter.c_str(), partFooter.length()); offset += partFooter.length();

  // A fresh connection every call, deliberately NOT reused across
  // requests. A prior version of this held a WiFiClientSecure member
  // open to try reuse, but this session's own measurements showed it
  // never actually triggered — api.openai.com closes the connection
  // every time — so it had zero observed benefit. It also turned out to
  // carry a real risk: connected() is a local socket-state flag, not a
  // liveness check, so a connection gone stale on the OTHER end (a WiFi
  // hiccup, a NAT timeout) could still report "connected" here, skip
  // the normal connect() path, and hang the POST below on a dead
  // socket — with NO timeout catching it, since WiFiClientSecure's own
  // defaults are a 30s read/write stall and a 120s handshake (see
  // WiFiClientSecure.cpp), and neither was ever overridden. That's the
  // likely explanation for a transcription that "took forever" until
  // manually stopped. Both are set explicitly below, tight enough to
  // match what a real request actually needs (~800-1200ms handshake,
  // a few seconds of inference, measured live) while still being a
  // firm, real ceiling instead of tens of seconds to two full minutes.
  WiFiClientSecure tls;
  tls.setInsecure();
  tls.setHandshakeTimeout(8);  // seconds — WiFiClientSecure's own API, not ms
  tls.setTimeout(8);           // seconds — read/write stall bound

  uint32_t tc0 = millis();
  if (!tls.connect("api.openai.com", 443)) {
    Serial.print("[STT] TLS connect failed after ");
    Serial.print(millis() - tc0); Serial.println(" ms");
    free(body);
    return "";
  }
  uint32_t connectMs = millis() - tc0;

  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(10000);
  http.begin(tls, "https://api.openai.com/v1/audio/transcriptions");
  http.addHeader("Authorization", String("Bearer ") + OPENAI_API_KEY);
  http.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);

  uint32_t t0 = millis();
  int code = http.POST(body, totalLen);
  uint32_t postMs = millis() - t0;
  Serial.print("[STT] TLS connect "); Serial.print(connectMs);
  Serial.print(" ms, upload+inference "); Serial.print(postMs);
  Serial.print(" ms (body "); Serial.print(totalLen / 1024); Serial.print(" KB), code=");
  Serial.println(code);
  String result;
  if (code == 200) {
    result = extractTextField(http.getString());
  } else {
    Serial.print("[STT] HTTP "); Serial.println(code);
    Serial.println(http.getString());
  }

  http.end();
  free(body);
  return result;
}
