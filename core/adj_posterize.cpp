// Posterize (D38), on the CPU: Image › Adjustments › Posterize and its adjustment layer. Each channel to one of n
// levels 0, 1 / (n - 1), ..., 1 (Adobe: "specify the number of tonal levels for each channel"), n Levels 2..255, a
// whole number (a file's fraction rounds). shaders/lib/adj_posterize.slang the same, line by line (adjust.h says how a
// type goes).
#include "adjust.h"

#include <nlohmann/json.hpp>

#include <cmath>

namespace pb {
namespace {

// ---- Hypothesis -----------------------------------------------------------------------------------------------------

// Which of the n levels a value v (0..1) takes. Adobe gives no formula: n bands of equal width over 0..1, floor(v n),
// the top one taking v = 1 too (4 levels: 8-bit 0-63, 64-127, 128-191, 192-255 to 0, 85, 170, 255); true would take
// GIMP's way instead, the nearest level, round(v (n - 1)). D32's posterize-2, -4, -7 on ramp.png settle where the edges
// fall (B38); test_adj_simple_ps prints how far either is.
constexpr bool kNearestLevel = false;

// ---- Values ---------------------------------------------------------------------------------------------------------

// AdjustSpec's: Levels 2..255.
struct Values {
  double lv = 4;
  void fields(AdjFields& f) {
    f.number("lv", lv, 4, 2, 255);
  }
};

// v (clamped to 0..1, NaN 0) at the nearest of 65536 levels: every 8- and 16-bit sample exactly (257 k for k of 8
// bits), so the bands below cut in integers where the formula puts their edges, as floats near a whole v n would not.
inline uint32_t level16(float v) {
  v = v > 0 ? (v < 1 ? v : 1) : 0;
  return uint32_t(v * 65535 + 0.5f);
}

class Posterize final : public Adjustment {
 public:
  Posterize(const Values& v, const AdjustContext& c, std::string vals)
      : Adjustment(AdjType::Posterize, c, std::move(vals)), n_(uint32_t(std::lround(v.lv))) {}
  bool identity() const override { return false; }  // 255 levels at most: never all 256 of 8 bits

 private:
  // v's level k, 0..n - 1, from its 16-bit level s: floor(s n / 65535) (or round(s (n - 1) / 65535)), in integers.
  float posterize(float v) const {
    const uint32_t s = level16(v), top = n_ - 1;
    const uint32_t k = kNearestLevel ? (2 * s * top + 65535) / 131070 : s * n_ / 65535;
    return float(k < top ? k : top) / float(top);
  }
  void adjust_row(float* px, int n, int, int) const override {
    for (int i = 0; i < n; i++, px += 4) px[0] = posterize(px[0]), px[1] = posterize(px[1]), px[2] = posterize(px[2]);
  }
  // w[2] the levels n, w[3] 1 for the nearest level (kNearestLevel).
  void pack_words(AdjustParams& p) const override {
    p.w[2] = n_;
    p.w[3] = kNearestLevel ? 1 : 0;
  }

  uint32_t n_;
};

}  // namespace

AdjustmentPtr adj::posterize(const nlohmann::json& vals, const AdjustContext& c) {
  return make_adjustment<Values, Posterize>(vals, c);
}

}  // namespace pb
