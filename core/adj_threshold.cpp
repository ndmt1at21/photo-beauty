// Threshold (D38), on the CPU: Image › Adjustments › Threshold and its adjustment layer. White where the pixel's
// luminosity reaches Threshold Level t (1..255, levels of 8 bits), black elsewhere (Adobe: "all pixels lighter than the
// threshold are converted to white; all pixels darker are converted to black"): Lum x 255 >= t, Lum histogram.h's
// kLumPercent mix, the luminosity the dialog's histogram shows. shaders/lib/adj_threshold.slang the same, line by line
// (adjust.h says how a type goes).
#include "adjust.h"

#include <nlohmann/json.hpp>

#include <cmath>

#include "histogram.h"

namespace pb {
namespace {

// ---- Hypothesis -----------------------------------------------------------------------------------------------------

// Levels of 8 bits added to Lum x 255 before it meets t: 0 takes Lum as it is; 0.5 would round it to a level first, as
// the histogram counts it. The weights are kLumPercent's (histogram.h: Rec. 601's 0.299, 0.587, 0.114 rounded, a
// hypothesis too). D32's threshold-64, -128, -200 on ramp.png and cube.png settle both (B38); test_adj_simple_ps prints
// how far the others are (Rec. 601's weights unrounded, Lum rounded, the cut among Photoshop's 15-bit levels).
constexpr double kLumRound = 0;

// ---- Values ---------------------------------------------------------------------------------------------------------

// AdjustSpec's: Threshold Level 1..255 (FIELDS').
struct Values {
  double t = 128;
  void fields(AdjFields& f) {
    f.number("t", t, 128, 1, 255);
  }
};

// v (clamped to 0..1, NaN 0) at the nearest of 65536 levels: every 8- and 16-bit sample exactly (257 k for k of 8
// bits), so Lum meets t in integers: a grey at level t of 8 bits, or 257 t of 16, is white and one level below black,
// as floats near the cut would not promise.
inline uint32_t level16(float v) {
  v = v > 0 ? (v < 1 ? v : 1) : 0;
  return uint32_t(v * 65535 + 0.5f);
}

constexpr uint32_t kLumSum = kLumPercent[0] + kLumPercent[1] + kLumPercent[2];  // 100
constexpr double kPerLevel8 = 65535. / 255;                                       // 257: 16-bit levels in one of 8

class Threshold final : public Adjustment {
 public:
  // Lum x 255 + kLumRound >= t: the weighted 16-bit levels, at most kLumSum x 65535, reaching kLumSum x 257 x
  // (t - kLumRound), rounded up (t may be a fraction).
  Threshold(const Values& v, const AdjustContext& c, std::string vals)
      : Adjustment(AdjType::Threshold, c, std::move(vals)),
        cut_(uint32_t(std::ceil((v.t - kLumRound) * kLumSum * kPerLevel8))) {}
  bool identity() const override { return false; }

 private:
  void adjust_row(float* px, int n, int, int) const override {
    for (int i = 0; i < n; i++, px += 4) {
      const uint32_t l = kLumPercent[0] * level16(px[0]) + kLumPercent[1] * level16(px[1]) +
                         kLumPercent[2] * level16(px[2]);
      const float v = l >= cut_ ? 1.f : 0.f;
      px[0] = px[1] = px[2] = v;
    }
  }
  // w[2] the cut, w[3..5] the weights (kLumPercent).
  void pack_words(AdjustParams& p) const override {
    p.w[2] = cut_;
    for (int k = 0; k < 3; k++) p.w[size_t(3 + k)] = kLumPercent[k];
  }

  uint32_t cut_;
};

}  // namespace

AdjustmentPtr adj::threshold(const nlohmann::json& vals, const AdjustContext& c) {
  return make_adjustment<Values, Threshold>(vals, c);
}

}  // namespace pb
