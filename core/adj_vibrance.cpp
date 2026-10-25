// Vibrance (D35), on the CPU: Image › Adjustments › Vibrance and its adjustment layer. Photoshop's formula is not
// public (Adobe: Vibrance "changes the saturation of all lower-saturated colors with less effect on the
// higher-saturated colors" and "prevents skin tones from becoming oversaturated"; Saturation changes "all colors
// equally"): a hypothesis (below) in Hue/Saturation's HSL, the chroma scaled about L = (max + min) / 2, in float with
// a division where HSL has one; shaders/lib/adj_vibrance.slang does the same line by line, so both give the same bits
// (adjust.h says how a type goes).
#include "adjust.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <string>

namespace pb {
namespace {

// ---- Hypotheses -----------------------------------------------------------------------------------------------------
// D32's vibrance-* cases on the 33³ lattice (B33) settle these, B38.

// Vibrance v (-1..1) scales the chroma about L by 1 + v (1 - S) w, S the HSL saturation: S becomes S (1 + v (1 - S)),
// never past 1 for v up to 1, a full colour unchanged, a grey with nothing to scale; more vibrance, more chroma, for
// every colour. w is 1 but on skin tones: hues in kSkinSpan (degrees, a span as Hue/Saturation's ranges: rising from
// its first to its second, falling from its third to its fourth), where it comes down to 1 - kSkinProtect.
constexpr double kSkinSpan[4] = {0, 15, 35, 50};
constexpr double kSkinProtect = 0.5;
// Saturation s scales the chroma by 1 + s, linear, nothing capping it at full saturation as Hue/Saturation's does:
// channels it takes past 0..1 clamp there (apply()). -100 is grey at L.

// ---- Values ---------------------------------------------------------------------------------------------------------

// AdjustSpec's: Vibrance, Saturation -100..100.
struct Values {
  double v = 0, s = 0;
  void fields(AdjFields& f) {
    f.number("v", v, 0, -100, 100);
    f.number("s", s, 0, -100, 100);
  }
};

// ---- Pixels: adj_hue_saturation.cpp's helpers, adj_hue_saturation.slang's on the GPU ---------------------------------

constexpr float kTiny = 0x1p-64f;  // as adj_hue_saturation.cpp's
inline float in(float v) { return v >= kTiny ? (v < 1 ? v : 1) : 0; }

inline float hue6(const float* c, float M, float C) {
  if (M == c[0]) {
    const float h = (c[1] - c[2]) / C;
    return h < 0 ? h + 6 : h;
  }
  if (M == c[1]) return (c[2] - c[0]) / C + 2;
  return (c[0] - c[1]) / C + 4;
}

inline float weight(const Float4& s, float rb, float rd, float h) {
  float t = h - s[0];
  if (t < 0) t = t + 6;
  if (t < s[1]) return t * rb;
  if (t <= s[2]) return 1;
  if (t < s[3]) return (s[3] - t) * rd;
  return 0;
}

double turn(double h) {  // 0..360
  h = std::fmod(h, 360);
  return h < 0 ? h + 360 : h;
}

// Words (adj_vibrance.slang's): w[2] vibrance (-1..1), w[3] 1 + saturation, w[4..7] kSkinSpan as weight() reads it
// (sextants: a, then b, c, d from a), w[8] 1 / b, w[9] 1 / (d - c), w[10] kSkinProtect.
class Vibrance final : public Adjustment {
 public:
  Vibrance(const Values& v, const AdjustContext& c, std::string vals)
      : Adjustment(AdjType::Vibrance, c, std::move(vals)) {
    vib_ = float(v.v / 100), sat_ = float(1 + v.s / 100), identity_ = v.v == 0 && v.s == 0;
    const double* d = kSkinSpan;
    const double a = turn(d[0]), b = turn(d[1] - d[0]), e = b + turn(d[2] - d[1]), f = e + turn(d[3] - d[2]);
    skin_ = {float(a / 60), float(b / 60), float(e / 60), float(f / 60)};
    rb_ = skin_[1] > 0 ? float(1 / double(skin_[1])) : 0;
    rd_ = skin_[3] > skin_[2] ? float(1 / (double(skin_[3]) - double(skin_[2]))) : 0;
    protect_ = float(kSkinProtect);
  }
  bool identity() const override { return identity_; }

 private:
  void adjust_row(float* px, int n, int, int) const override {
    for (int i = 0; i < n; i++, px += 4) {
      float c[3] = {in(px[0]), in(px[1]), in(px[2])};
      const float M = std::max(std::max(c[0], c[1]), c[2]), m = std::min(std::min(c[0], c[1]), c[2]);
      const float C = M - m;
      if (C > 0) {
        const float sum = M + m, L = sum * 0.5f, den = sum <= 1 ? sum : 2 - sum;
        float S = C / den;
        if (S > 1) S = 1;
        float a = 1 - S;
        if (vib_ != 0 && protect_ != 0) a = a * (1 - protect_ * weight(skin_, rb_, rd_, hue6(c, M, C)));
        const float q = (1 + vib_ * a) * sat_;
        if (q != 1)
          for (int j = 0; j < 3; j++) c[j] = L + (c[j] - L) * q;
      }
      px[0] = c[0], px[1] = c[1], px[2] = c[2];
    }
  }
  void pack_words(AdjustParams& p) const override {
    p.w[2] = word(vib_), p.w[3] = word(sat_);
    for (int i = 0; i < 4; i++) p.w[size_t(4 + i)] = word(skin_[size_t(i)]);
    p.w[8] = word(rb_), p.w[9] = word(rd_), p.w[10] = word(protect_);
  }

  float vib_ = 0, sat_ = 1, rb_ = 0, rd_ = 0, protect_ = 0;
  Float4 skin_{};
  bool identity_ = true;
};

}  // namespace

AdjustmentPtr adj::vibrance(const nlohmann::json& vals, const AdjustContext& c) {
  return make_adjustment<Values, Vibrance>(vals, c);
}

}  // namespace pb
