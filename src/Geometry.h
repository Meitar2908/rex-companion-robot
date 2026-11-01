// Geometry.h — logical screen layout, shared by the eye and mouth
// renderers (and the display driver's splash text). Everything is
// derived from screen fractions rather than hardcoded positions, so
// moving to a different panel means changing kScreenW/kScreenH only.
#pragma once
#include <stdint.h>

namespace Geometry {
  constexpr int kScreenW = 320;
  constexpr int kScreenH = 240;

  constexpr int kEyeCenterX_L = (int)(kScreenW * 0.30f);
  constexpr int kEyeCenterX_R = (int)(kScreenW * 0.70f);
  constexpr int kEyeCenterY   = (int)(kScreenH * 0.46f);

  constexpr int kMouthCenterX   = (int)(kScreenW * 0.50f);
  constexpr int kMouthCenterY   = (int)(kScreenH * 0.80f);
  constexpr int kMouthHalfW     = (int)(kScreenW * 0.30f);
  constexpr int kMouthThickness = 10;

  // Mouth column-array capacity — sized with headroom above
  // kMouthHalfW so MouthPose::widthMul can exceed 1.0 (e.g. Excited)
  // without overrunning the fixed-size buffer.
  constexpr float kMouthMaxWidthMul = 1.3f;
  constexpr int   kMouthPixels = (int)(kMouthHalfW * kMouthMaxWidthMul) * 2 + 1;

  // The single off-screen sprite the whole face (both eyes + mouth)
  // is composed into before one atomic push to the panel. Sized to
  // the face's actual bounding box, not the full 320x240 screen, to
  // leave RAM headroom for future audio/network buffers. Bounds were
  // hand-computed from the eye sprite half-extent (64px) and the
  // mouth's worst-case vertical excursion (curvature + speech wave +
  // thickness), then given a few px of margin on each side.
  constexpr int kFaceLeft = 20;
  constexpr int kFaceTop  = 36;
  constexpr int kFaceW    = 280;
  constexpr int kFaceH    = 204;

  // Render loop pacing — see FaceFrame / CharacterController.
  constexpr uint32_t kFrameIntervalMs = 33;  // ~30 FPS
}
