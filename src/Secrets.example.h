// Secrets.example.h — copy this file to Secrets.h and fill in your
// own values. Secrets.h is gitignored and must NEVER be committed.
//
//   cp src/Secrets.example.h src/Secrets.h
//
#pragma once

#define WIFI_SSID       "your-wifi-name"
#define WIFI_PASSWORD   "your-wifi-password"

// https://platform.openai.com/api-keys — used for Whisper transcription
#define OPENAI_API_KEY  "sk-..."

// https://console.anthropic.com/settings/keys — used for REX's conversational AI
#define ANTHROPIC_API_KEY  "sk-ant-..."

// https://elevenlabs.io/app/settings/api-keys — used for text-to-speech
#define ELEVENLABS_API_KEY  "..."

// https://gnews.io/dashboard — used for /news and the get_news tool
#define GNEWS_API_KEY  "..."

// https://app.tavily.com — used for /search and the web_search tool
#define TAVILY_API_KEY  "tvly-..."

// Default location for weather/time questions when no city is named
// ("what's the weather today"). Not a real secret (WeatherTool/TimeTool
// need no API key — Open-Meteo + the ESP32's own SNTP), just kept here
// alongside the other device-specific config. Override per-question by
// naming a different city ("weather in Paris").
#define DEFAULT_LOCATION_NAME "New York"
#define DEFAULT_LOCATION_LAT  40.7128f
#define DEFAULT_LOCATION_LON  -74.0060f
// POSIX TZ string (DST-aware) — look yours up against
// /usr/share/zoneinfo on Linux/macOS, or https://ftp.iana.org/tz/.
#define DEFAULT_LOCATION_TZ   "EST5EDT,M3.2.0,M11.1.0"
