// MouthRenderer.h — draws the mouth as a single thick line with
// rounded ends, generated from a normalized curve (x in -1..1) built
// from MouthPose plus an additive speech waveform. No lips, no
// teeth, no bitmaps — the same graphical element for every state,
// just deformed differently.
//
// Stateless: draws into whatever sprite it's given (the shared
// FaceFrame canvas), at a position relative to that canvas's screen
// origin. Because the canvas is cleared fresh every frame by
// FaceFrame::beginFrame(), this renderer needs no erase/diff
// bookkeeping — draw the current shape and nothing more.
#pragma once
#include "EyeTypes.h"
#include "Geometry.h"

class TFT_eSprite;

class MouthRenderer {
public:
  // pose: the current (already-blended) emotional mouth curve.
  // speechAmount: 0 = pure emotional curve, 1 = full speech waveform
  // layered on top. wavePhase: advances continuously while speaking.
  void render(TFT_eSprite& canvas, int originX, int originY,
              const MouthPose& pose, float speechAmount, float wavePhase);

private:
  float columnY(int col, int activeHalfW, const MouthPose& pose,
                float speechAmount, float wavePhase) const;
};
