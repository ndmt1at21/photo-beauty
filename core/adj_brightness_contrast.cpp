// Brightness/Contrast (D34), on the CPU: Image › Adjustments › Brightness/Contrast and its adjustment layer, both of
// Photoshop's ways. Use Legacy moves and scales each channel along a line, as Adobe describes it (kLegacy* below); the
// default (from CS3) bends each channel along a curve that keeps black and white, a formula Adobe does not publish: a
// hypothesis (kBrightnessMid, kContrastS), its curve a table made here, for B38 to fit to D32's bc-* images.
// shaders/lib/adj_brightness_contrast.slang does the rest line by line, so both give the same bits (adjust.h says how
// a type goes).
#include "adjust.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

namespace pb {
namespace {

// ---- Hypotheses -----------------------------------------------------------------------------------------------------

// Use Legacy as Photoshop's team described it (relayed by Rik Cabanier, W3C public-fx list, 26 January 2012):
// "Brightness n adds or subtracts n to / from each channel, clamping at 0 or 255", "Contrast spreads or compresses the
// histogram linearly for each channel": Contrast +100 makes every value 0 or 255, -100 makes them 127, and -100 with
// Brightness +100 makes them 227. So each channel's v (0..1) is scaled about kLegacyPivot by k: 100 / (100 - c) for
// Contrast c above 0 (Levels' input points closing in; a step at 100), (100 + c) / 100 below (its output points), the
// task's guess; Brightness b adds b / 255 before a scaling up and, kLegacyShiftOutput, after a scaling down, which
// makes -100 with +100 227 rather than 127. D32's bc-legacy-* (B33) check them.
constexpr double kLegacyPivot = 0.5;  // 127.5 of 255: Photoshop's 8 bits truncating it would give the 127
constexpr bool kLegacyShiftOutput = true;  // false: b before the contrast below 0 too, (v + b - 0.5) k + 0.5 for all c
// The default, from CS3 (Adobe: it "copied Lightroom (camera raw)'s original process version", "curve-based"): every
// channel through one curve that keeps 0 and 1. Brightness b pushes the midpoint as a Curves point would, along the
// parabola through (0, 0), (0.5, 0.5 + d) and (1, 1), d = kBrightnessMid b / 150 (monotone while |d| <= 0.25); then
// Contrast c bends that into an S about 0.5, u + a (smoothstep(u) - u), a = kContrastS c / 100 (monotone for a in
// -2..1). Adobe gives none of it; B38 fits it to D32's bc-* (bc-c100-photo: whether it looks at the image, which this
// does not).
constexpr double kBrightnessMid = 0.25;
constexpr double kContrastS = 1;

// ---- Values ---------------------------------------------------------------------------------------------------------

// AdjustSpec's: Brightness -150..150, Contrast -50..100, Use Legacy. Use Legacy's ranges are its dialog's, -100..100
// both (D32's bc-legacy-* read so: README-adjust.md), so legacy is read before them. N37: the sliders' ranges follow
// the box (the UI's -150..150 and -50..100 are the default's).
struct Values {
  double b = 0, c = 0;
  bool legacy = false;
  void fields(AdjFields& f) {
    f.flag("legacy", legacy, false);
    f.number("b", b, 0, legacy ? -100 : -150, legacy ? 100 : 150);
    f.number("c", c, 0, legacy ? -100 : -50, 100);
  }
};

// ---- The adjustment -------------------------------------------------------------------------------------------------

// Below this a value coming in is 0 (and NaN, below 0): products of the table's interpolation and of the line's scale
// then never fall to subnormals, which the GPU may flush (blend.slang). 2^-64, adj_brightness_contrast.slang's
// kBrightnessContrastTiny.
constexpr float kTiny = 0x1p-64f;

// Words (adj_brightness_contrast.slang's): w[2] the kind: kCurve (each channel through the table: 4,098 entries, the
// curve in x, y, z and w alike), kLine (v k + d: w[3] k, w[4] d), kStep (1 from w[3] up, else 0: Use Legacy at
// Contrast 100); kNone the identity.
enum Kind : uint32_t { kNone, kCurve, kLine, kStep };

class BrightnessContrast final : public Adjustment {
 public:
  BrightnessContrast(const Values& v, const AdjustContext& c, std::string vals)
      : Adjustment(AdjType::BrightnessContrast, c, std::move(vals)) {
    if (v.b == 0 && v.c == 0) return;
    if (v.legacy) {
      const double b = v.b / 255, p = kLegacyPivot;
      if (v.c >= 100) {  // k infinite: up from where v + b is the pivot
        kind_ = kStep, k_ = float(p - b);
        return;
      }
      const double k = v.c > 0 ? 100 / (100 - v.c) : (100 + v.c) / 100;
      const double d = v.c < 0 && kLegacyShiftOutput ? p * (1 - k) + b : (b - p) * k + p;
      kind_ = kLine, k_ = float(k), d_ = float(d);
      return;
    }
    kind_ = kCurve;
    const double d = kBrightnessMid * v.b / 150, a = kContrastS * v.c / 100;
    tables_.resize(size_t(kCurveSteps) + 2);
    for (int i = 0; i <= kCurveSteps; i++) {
      const double x = double(i) / kCurveSteps;
      const double u = x + 4 * d * x * (1 - x);
      const float y = float(std::clamp(u + a * (u * u * (3 - 2 * u) - u), 0., 1.));
      tables_[size_t(i)] = {y, y, y, y};
    }
    tables_[size_t(kCurveSteps) + 1] = tables_[size_t(kCurveSteps)];
  }
  bool identity() const override { return kind_ == kNone; }

 private:
  // One channel: adj_brightness_contrast.slang's brightness_contrast_channel().
  float channel(float v) const {
    if (!(v >= kTiny)) v = 0;  // and NaN, below 0
    if (kind_ == kCurve) return adjust_curve(tables_.data(), v)[0];
    if (kind_ == kStep) return v >= k_ ? 1.f : 0.f;
    return v * k_ + d_;
  }
  void adjust_row(float* px, int n, int, int) const override {
    if (kind_ == kNone) return;
    for (int i = 0; i < n; i++, px += 4) px[0] = channel(px[0]), px[1] = channel(px[1]), px[2] = channel(px[2]);
  }
  void pack_words(AdjustParams& p) const override {
    p.w[2] = kind_;
    p.w[3] = word(k_);
    p.w[4] = word(d_);
  }

  Kind kind_ = kNone;
  float k_ = 1, d_ = 0;
};

}  // namespace

AdjustmentPtr adj::brightness_contrast(const nlohmann::json& vals, const AdjustContext& c) {
  return make_adjustment<Values, BrightnessContrast>(vals, c);
}

}  // namespace pb
