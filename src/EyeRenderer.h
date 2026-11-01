// EyeRenderer.h — procedural drawing of an EyePair. Stateless: draws
// into whatever sprite it's given (the shared FaceFrame canvas), at
// a position relative to that canvas's screen origin, so eyes and
// mouth always end up composed in the same off-screen buffer before
// anything reaches the panel.
#pragma once
#include "EyeTypes.h"

class TFT_eSprite;

class EyeRenderer {
public:
  void render(TFT_eSprite& canvas, int originX, int originY, const EyePair& pose);

private:
  void drawEye(TFT_eSprite& canvas, int cx, int cy, const EyeShape& shape);
};
