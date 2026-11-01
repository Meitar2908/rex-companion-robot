#include "DisplayDriver.h"
#include "Geometry.h"
#include <string.h>
// Adafruit-GFX "Free Fonts" (bundled with TFT_eSPI, enabled via the
// LOAD_GFXFF build flag — TFT_eSPI.h auto-includes every font file
// itself, so FreeSansBold18pt7b/FreeSans9pt7b below are already
// declared, no explicit #include needed). Only used here, for the
// splash screen. Sizes are discrete (9/12/18/24pt, not a continuous
// scale), so these are the closest bundled sizes to "REX ~50% bigger
// and bold" / "subtitle ~15% bigger": FreeSansBold18pt7b's line
// height is ~62% taller than the previous numbered Font 4, and
// FreeSans9pt7b (the smallest bundled size) is ~37% taller than the
// previous Font 2 — exact percentages aren't achievable with fixed
// font assets, these are the nearest fit.

namespace {
// Draws `text` centered on (centerX, centerY) with `spacing` extra
// pixels between each letter — TFT_eSPI's drawString() has no built-in
// letter-spacing/tracking, so this draws one character at a time,
// advancing by that character's real width plus the gap. Requires the
// desired font to already be set (setFreeFont/setTextFont) beforehand.
// `sizeMultiplier` is TFT_eSPI's own integer text-size scale (2 = exactly
// 100% larger) — textWidth() already accounts for it, so spacing is the
// only thing that needs scaling up separately to keep gaps proportional.
void drawLetterSpacedCentered(TFT_eSPI& tft, const char* text, int centerX, int centerY,
                               int spacing, uint8_t sizeMultiplier = 1) {
  tft.setTextSize(sizeMultiplier);
  size_t len = strlen(text);
  int totalWidth = 0;
  for (size_t i = 0; i < len; i++) {
    char ch[2] = { text[i], '\0' };
    totalWidth += tft.textWidth(ch);
    if (i + 1 < len) totalWidth += spacing;
  }

  tft.setTextDatum(ML_DATUM);  // middle-left, so each char lands exactly at the running x
  int x = centerX - totalWidth / 2;
  for (size_t i = 0; i < len; i++) {
    char ch[2] = { text[i], '\0' };
    tft.drawString(ch, x, centerY);
    x += tft.textWidth(ch) + spacing;
  }
  tft.setTextSize(1);  // don't leave the multiplier on for whatever draws next
}
} // namespace

void DisplayDriver::begin() {
  tft_.init();
  tft_.setRotation(1);
  tft_.fillScreen(TFT_BLACK);
}

void DisplayDriver::showSplash(const char* title, const char* subtitle) {
  tft_.setTextColor(TFT_WHITE, TFT_BLACK);

  // REX itself sits exactly on the screen's vertical center; the
  // subtitle just sits below it with a fixed gap, not counted into any
  // centering math of its own.
  constexpr int kTitleHalfHeight    = 42;  // FreeSansBold18pt7b yAdvance(~42) * 2x / 2
  constexpr int kSubtitleHalfHeight = 11;  // FreeSans9pt7b yAdvance(~22) / 2
  constexpr int kGapBelowTitle      = 4;
  constexpr int kSubtitleCenterY    = kTitleHalfHeight + kGapBelowTitle + kSubtitleHalfHeight;

  tft_.setFreeFont(&FreeSansBold18pt7b);
  // 100% larger (exactly 2x — TFT_eSPI's own integer text-size scale,
  // the one way to get a precise doubling rather than an approximate
  // jump between fixed font sizes). Spacing doubled too, to keep the
  // letter gaps looking proportional at the bigger size.
  constexpr int kTitleLetterSpacing = 14 * 2;
  drawLetterSpacedCentered(tft_, title, Geometry::kScreenW / 2, Geometry::kScreenH / 2,
                            kTitleLetterSpacing, /*sizeMultiplier=*/2);

  tft_.setTextDatum(MC_DATUM);
  tft_.setFreeFont(&FreeSans9pt7b);
  tft_.drawString(subtitle, Geometry::kScreenW / 2, Geometry::kScreenH / 2 + kSubtitleCenterY);

  tft_.setTextFont(1);  // back to a plain numbered font for anything drawn later
}
