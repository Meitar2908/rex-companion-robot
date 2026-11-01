// FaceFrame.h — owns the ONE off-screen buffer the entire face
// (both eyes + mouth) is composed into each frame. Nothing reaches
// the physical panel until pushFrame() runs, so the display only
// ever shows a fully-finished frame — never a partial eye, a pupil
// without its eyelid, or one eye updated ahead of the other.
//
// Pipeline (see CharacterController::update):
//   beginFrame()            — clear the off-screen canvas
//   EyeRenderer::render()   — draw both eyes into it
//   MouthRenderer::render() — draw the mouth into it
//   pushFrame()             — one atomic blit to the TFT
#pragma once
#include "Geometry.h"

class TFT_eSPI;
class TFT_eSprite;

class FaceFrame {
public:
  void begin(TFT_eSPI& display);
  void beginFrame();
  void pushFrame();

  TFT_eSprite& canvas() { return *sprite_; }
  int originX() const { return Geometry::kFaceLeft; }
  int originY() const { return Geometry::kFaceTop; }

private:
  TFT_eSprite* sprite_ = nullptr;
};
