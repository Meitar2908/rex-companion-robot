// NetworkBudget.h — a wall-clock deadline for tools that make one or
// more sequential HTTP calls (WeatherTool's geocode-then-fetch,
// PlacesTool's geocode-then-Overpass-query). Each individual HTTPClient
// call already has its OWN connect/response timeout, but those bound
// each STEP, not the total: a tool doing geocode() then a real fetch
// could stack two full timeouts back to back (previously up to
// ~28-30s combined) before ever reporting failure. A NetworkBudget caps
// the WHOLE multi-step sequence at one firm ceiling instead, so a
// genuine connection problem gets reported back in a few seconds, not
// tens of seconds — measured live, a working call typically completes
// in ~1.2-1.3s, so even a fairly tight budget stays generous for the
// normal case.
#pragma once
#include <Arduino.h>

class NetworkBudget {
public:
  explicit NetworkBudget(uint32_t totalMs) : deadlineMs_(millis() + totalMs) {}

  // Milliseconds left before the deadline — 0 if already past it. Feed
  // this into the NEXT HTTP call's own setConnectTimeout()/setTimeout()
  // (capped at a sane per-call ceiling too) so no single step can eat
  // the entire remaining budget alone and starve a later step.
  uint32_t remainingMs() const {
    uint32_t now = millis();
    return (now >= deadlineMs_) ? 0 : (deadlineMs_ - now);
  }

  // True once there isn't meaningfully enough budget left to even
  // attempt another HTTP call (a bare TCP handshake alone takes some
  // tens/hundreds of ms) — callers should bail out with a clear error
  // rather than start a request that can only fail immediately anyway.
  bool exhausted() const { return remainingMs() < 500; }

private:
  uint32_t deadlineMs_;
};
