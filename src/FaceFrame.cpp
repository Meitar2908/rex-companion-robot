#include "FaceFrame.h"
#include <TFT_eSPI.h>

void FaceFrame::begin(TFT_eSPI& display) {
  sprite_ = new TFT_eSprite(&display);
  sprite_->setColorDepth(16);
  sprite_->createSprite(Geometry::kFaceW, Geometry::kFaceH);
}

void FaceFrame::beginFrame() {
  sprite_->fillSprite(TFT_BLACK);
}

void FaceFrame::pushFrame() {
  sprite_->pushSprite(Geometry::kFaceLeft, Geometry::kFaceTop);
}
