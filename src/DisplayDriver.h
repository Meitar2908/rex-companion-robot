// DisplayDriver.h — the ONLY place that touches TFT_eSPI init
// directly. EyeRenderer and MouthRenderer are handed a reference to
// the raw TFT_eSPI object and don't know or care what controller
// backs it. Driver/controller is ILI9341 via TFT_eSPI, configured in
// platformio.ini build_flags — confirmed working on this project's
// actual hardware, so this wrapper commits to it rather than staying
// abstract for its own sake.
#pragma once
#include <TFT_eSPI.h>

class DisplayDriver {
public:
  void begin();
  void showSplash(const char* title, const char* subtitle);
  TFT_eSPI& raw() { return tft_; }

private:
  TFT_eSPI tft_;
};
