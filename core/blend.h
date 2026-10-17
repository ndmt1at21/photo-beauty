#pragma once
// Blend modes and source-over for one pixel: the formulas composite_tile() runs (composite.cpp), which
// shaders/lib/blend.slang (B2) copies line by line. Plain functions of floats 0..1 in the document's encoding. Cb, Cs:
// a colour channel of the backdrop (what lies below) and of the source (the layer on it), not premultiplied; cb_p: the
// backdrop premultiplied (Cb x ab), as the composite accumulates it; ab, as: their alphas, the source's already times
// the layer's opacity and fill.
#include <cmath>

#include "doc.h"  // Blend

namespace pb::blend {

// B(Cb, Cs) of the separable modes: W3C Compositing and Blending Level 1's where Photoshop agrees with it.
inline float multiply(float cb, float cs) { return cb * cs; }
inline float screen(float cb, float cs) { return cb + cs - cb * cs; }
inline float overlay(float cb, float cs) { return cb <= 0.5f ? multiply(cs, 2 * cb) : screen(cs, 2 * cb - 1); }
inline float darken(float cb, float cs) { return cb < cs ? cb : cs; }
inline float lighten(float cb, float cs) { return cb > cs ? cb : cs; }
inline float difference(float cb, float cs) { return std::fabs(cb - cs); }
// Black stays black whatever dodges it and white stays white whatever burns it, as in Photoshop and W3C; <= and >= so
// that a backdrop rounded a hair past them does too.
inline float color_dodge(float cb, float cs) {
  if (cb <= 0) return 0;
  if (cs >= 1) return 1;
  const float v = cb / (1 - cs);
  return v < 1 ? v : 1;
}
inline float color_burn(float cb, float cs) {
  if (cb >= 1) return 1;
  if (cs <= 0) return 0;
  const float v = (1 - cb) / cs;
  return v < 1 ? 1 - v : 0;
}
// Photoshop's: the square root of Cb above Cs 0.5 for every Cb (W3C has a polynomial there for Cb up to 0.25).
inline float soft_light(float cb, float cs) {
  return cs <= 0.5f ? cb - (1 - 2 * cs) * cb * (1 - cb) : cb + (2 * cs - 1) * (std::sqrt(cb) - cb);
}
// Photoshop's (not in W3C): Cb + 2 Cs - 1, clamped.
inline float linear_light(float cb, float cs) {
  const float v = cb + 2 * cs - 1;
  return v < 0 ? 0 : v < 1 ? v : 1;
}

// B of the modes P0 composites; the others blend as Normal until they come (P1).
inline float separable(Blend mode, float cb, float cs) {
  switch (mode) {
    case Blend::Darken: return darken(cb, cs);
    case Blend::Multiply: return multiply(cb, cs);
    case Blend::ColorBurn: return color_burn(cb, cs);
    case Blend::Lighten: return lighten(cb, cs);
    case Blend::Screen: return screen(cb, cs);
    case Blend::ColorDodge: return color_dodge(cb, cs);
    case Blend::Overlay: return overlay(cb, cs);
    case Blend::SoftLight: return soft_light(cb, cs);
    case Blend::LinearLight: return linear_light(cb, cs);
    case Blend::Difference: return difference(cb, cs);
    default: return cs;  // Normal
  }
}

// Cb of the accumulated backdrop: 0 where it is transparent (B weighs nothing there), at most 1.
inline float unpremultiply(float c_p, float a) { return a > 0 ? (c_p < a ? c_p / a : 1) : 0; }

// Source-over with blending, one colour channel (W3C's, premultiplied): the source counts as b = B(Cb, Cs) where the
// backdrop is (ab) and as Cs where it is not. Gives the new cb_p; the new alpha is over_alpha()'s.
inline float over(float cb_p, float ab, float cs, float as, float b) {
  return (1 - as) * cb_p + as * ((1 - ab) * cs + ab * b);
}
inline float over_alpha(float ab, float as) { return as + ab * (1 - as); }
// A layer's channel over the backdrop's in the layer's mode. Normal's B is Cs, which needs no Cb.
inline float composite(Blend mode, float cb_p, float ab, float cs, float as) {
  if (mode == Blend::Normal) return (1 - as) * cb_p + as * cs;
  return over(cb_p, ab, cs, as, separable(mode, unpremultiply(cb_p, ab), cs));
}

// A Pass Through group below full opacity o: what its layers made of the backdrop, mixed with the backdrop as it was
// before them. Every channel premultiplied, alpha too.
inline float mix(float before, float after, float o) { return (1 - o) * before + o * after; }

// The stroke being painted, over its layer's pixel (render.h's StrokeOverlay), k = its opacity x coverage there.
// Painting lays the colour over the pixel as Normal does: paint() is a channel c of the pixel after, a_out its alpha
// after (paint_alpha()). Erasing takes alpha away.
inline float paint_alpha(float a, float k) { return k + a * (1 - k); }
inline float paint(float c, float color, float k, float a_out) { return k > 0 ? c + (color - c) * (k / a_out) : c; }
inline float erase_alpha(float a, float k) { return a * (1 - k); }

}  // namespace pb::blend
