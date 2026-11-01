#include "EyeRenderer.h"
#include "Geometry.h"
#include <TFT_eSPI.h>
#include <Arduino.h>

namespace {
constexpr int   kEyeBaseSize = 92;   // full width/height at widthMul=1, openness=1
constexpr int   kEyeRadius   = 46;
constexpr float kPupilBaseR  = 20.0f;
constexpr float kHighlightR  = 6.0f;
// Max pixel travel for EyeShape::centerXShift (at |centerXShift|==1).
// Eyes are 128px apart center-to-center (Geometry::kEyeCenterX_L/_R) and
// both shift by the same signed amount, so they never approach each
// other — the binding constraint is the face sprite's own edge
// (Geometry::kFaceLeft..kFaceLeft+kFaceW). At rest each 92px-wide eye
// clears that edge by 30px; 22px of travel keeps a small margin.
constexpr int kEyeShiftMaxPx = 22;
} // namespace

// Layering: body -> pupil+highlight -> eyelid masks. Masks are drawn
// LAST, in the background colour, so they occlude whatever pupil/
// highlight pixels happen to fall underneath them — a real eyelid
// covering a pupil, achieved by paint order rather than geometric
// clipping. This also makes blink/squint occlusion "just work": as
// openness shrinks and lid drop grows, more of the mask overlaps the
// pupil's clamped travel area and correctly covers more of it.
void EyeRenderer::drawEye(TFT_eSprite& canvas, int cx, int cy, const EyeShape& shape) {
  // Shift the WHOLE eye — every shape drawn below (body, pupil, both lid
  // masks) uses cx from this point on, so the squint/lid rides along
  // with the eye rather than staying fixed while just the pupil moves.
  cx += (int)(shape.centerXShift * kEyeShiftMaxPx);

  int w = (int)(kEyeBaseSize * shape.widthMul);
  int h = max(4, (int)(kEyeBaseSize * shape.openness));
  int r = min(kEyeRadius, min(w, h) / 2);

  canvas.fillRoundRect(cx - w / 2, cy - h / 2, w, h, r, TFT_WHITE);

  // Pupil + highlight. Visibility (not just presence) fades smoothly
  // as the eye nears fully closed, instead of popping at a threshold.
  float pupilVisibility = constrain((shape.openness - 0.05f) / 0.25f, 0.0f, 1.0f);
  if (pupilVisibility > 0.02f) {
    float travelX = max(0.0f, (w / 2.0f - kPupilBaseR * shape.pupilScale) * 0.55f);
    float travelY = max(0.0f, (h / 2.0f - kPupilBaseR * shape.pupilScale) * 0.55f);
    int px = cx + (int)(shape.pupilBiasX * travelX);
    int py = cy - (int)(shape.pupilBiasY * travelY);   // +Y bias = look up
    float sizeFactor = 0.4f + 0.6f * pupilVisibility;
    int pr = max(2, (int)(kPupilBaseR * shape.pupilScale * sizeFactor));
    canvas.fillCircle(px, py, pr, TFT_BLACK);
    if (h / 2 > pr) {
      int hlR = max(1, (int)(kHighlightR * shape.pupilScale * sizeFactor));
      canvas.fillCircle(px - pr / 3, py - pr / 3, hlR, TFT_WHITE);
    }
  }

  // Top-lid corner cut: a quad from the flat top edge down to each
  // corner's own drop depth, filled as two triangles sharing a
  // diagonal. Drawn after the pupil so it correctly covers it.
  if (shape.lidDropL > 0.001f || shape.lidDropR > 0.001f) {
    int leftX  = cx - w / 2 - 2;
    int rightX = cx + w / 2 + 2;
    int topY   = cy - h / 2 - 2;
    int dropL  = topY + (int)(shape.lidDropL * (h / 2) * 1.3f);
    int dropR  = topY + (int)(shape.lidDropR * (h / 2) * 1.3f);
    canvas.fillTriangle(leftX, topY, rightX, topY, rightX, dropR, TFT_BLACK);
    canvas.fillTriangle(leftX, topY, rightX, dropR, leftX, dropL, TFT_BLACK);
  }

  // Bottom eyelid (squint): a rounded shape rising from BELOW the eye,
  // unlike lidDropL/R above (top corners) or openness (shrinks the whole
  // body symmetrically from both edges at once) — this is the only mask
  // that narrows the eye while leaving the top edge exactly where it
  // is. maskH is generous so the mask's own bottom corners stay well
  // below the eye's visible area and only its rounded TOP edge reads as
  // a lid. Drawn after the pupil so it correctly covers whatever falls
  // underneath, same paint-order trick as the other masks.
  if (shape.bottomLidRise > 0.001f) {
    int maskW = w + 8;
    int maskH = h;
    int riseY = cy + h / 2 - (int)(shape.bottomLidRise * h);
    canvas.fillRoundRect(cx - maskW / 2, riseY, maskW, maskH, r, TFT_BLACK);
  }

  // Happy crescent: a large disc masks the lower portion, leaving a
  // curved smiling bottom edge. Also drawn after the pupil.
  if (shape.happyCurve > 0.001f) {
    int discR  = (int)(h * 0.5f + 16 + shape.happyCurve * 10.0f);
    int discCy = cy + (int)(h * 0.5f + 10.0f - shape.happyCurve * 4.0f);
    canvas.fillCircle(cx, discCy, discR, TFT_BLACK);
  }
}

void EyeRenderer::render(TFT_eSprite& canvas, int originX, int originY, const EyePair& pose) {
  drawEye(canvas, Geometry::kEyeCenterX_L - originX, Geometry::kEyeCenterY - originY, pose.left);
  drawEye(canvas, Geometry::kEyeCenterX_R - originX, Geometry::kEyeCenterY - originY, pose.right);
}
