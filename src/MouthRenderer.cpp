#include "MouthRenderer.h"
#include <TFT_eSPI.h>
#include <Arduino.h>
#include <math.h>

// Normalized curve: x runs -1..1 across the mouth's current active
// width. curvature/asymmetry/waviness sum into the emotional shape;
// speech (when speaking) adds a tapered sine on top so the ends stay
// calm and the rounded caps read cleanly even while talking.
float MouthRenderer::columnY(int col, int activeHalfW, const MouthPose& pose,
                              float speechAmount, float wavePhase) const {
  float t = (float)col / (float)(activeHalfW * 2);
  float x = t * 2.0f - 1.0f;

  float arch = pose.curvature * (1.0f - x * x);
  float asym = pose.asymmetry * x * 0.5f;
  float wavy = pose.waviness * (sinf(x * 3.0f * PI) * 0.5f + sinf(x * 7.3f + 1.7f) * 0.25f);

  float edgeTaper = 1.0f - x * x * x * x;   // keeps the speech wave tame near the rounded caps
  float speech = speechAmount * 14.0f * sinf(x * 2.6f * PI + wavePhase) * edgeTaper;

  float shapeY = (arch + asym + wavy) * 26.0f + pose.verticalOffset * 8.0f;
  return Geometry::kMouthCenterY + shapeY + speech;
}

void MouthRenderer::render(TFT_eSprite& canvas, int originX, int originY,
                            const MouthPose& pose, float speechAmount, float wavePhase) {
  float widthMul = constrain(pose.widthMul, 0.35f, Geometry::kMouthMaxWidthMul);
  int activeHalfW = (int)(Geometry::kMouthHalfW * widthMul);
  int totalCols = activeHalfW * 2 + 1;
  // Length (widthMul) and placement (centerXOffset) are independent —
  // a short line can sit centered (shy) or slid to one side (smirk).
  int shiftPx = (int)(constrain(pose.centerXOffset, -1.0f, 1.0f) * Geometry::kMouthHalfW);
  int left = (Geometry::kMouthCenterX - originX) + shiftPx - activeHalfW;
  int halfT = Geometry::kMouthThickness / 2;

  for (int col = 0; col < totalCols; col++) {
    int16_t newY = (int16_t)(columnY(col, activeHalfW, pose, speechAmount, wavePhase) - originY);

    int thick = Geometry::kMouthThickness, yOff = 0;
    int distFromEnd = min(col, totalCols - 1 - col);
    if (distFromEnd < halfT) {
      int dx = halfT - distFromEnd;
      int hw = max(1, (int)sqrtf((float)(halfT * halfT - dx * dx)));
      thick = hw * 2;
      yOff  = halfT - hw;
    }

    canvas.drawFastVLine(left + col, newY - halfT + yOff, thick, TFT_WHITE);
  }
}
