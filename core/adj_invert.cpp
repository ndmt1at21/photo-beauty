// Invert (D38), on the CPU: Image › Adjustments › Invert and its adjustment layer. Each channel to 1 - v (Adobe:
// "the brightness value of each pixel in the channels is converted to the inverse value"), on a mask too
// (apply_adjustment() takes a mask's grid: Invert there swaps what it hides and shows). Twice is the identity on every
// 8- and 16-bit sample. shaders/lib/adj_invert.slang the same, line by line (adjust.h says how a type goes).
#include "adjust.h"

#include <nlohmann/json.hpp>

namespace pb {
namespace {

// None (AdjustSpec's NOTE).
struct Values {
  void fields(AdjFields&) {}
};

class Invert final : public Adjustment {
 public:
  Invert(const Values&, const AdjustContext& c, std::string vals) : Adjustment(AdjType::Invert, c, std::move(vals)) {}
  bool identity() const override { return false; }

 private:
  void adjust_row(float* px, int n, int, int) const override {
    for (int i = 0; i < n; i++, px += 4) px[0] = 1 - px[0], px[1] = 1 - px[1], px[2] = 1 - px[2];
  }
  void pack_words(AdjustParams&) const override {}  // none
};

}  // namespace

AdjustmentPtr adj::invert(const nlohmann::json& vals, const AdjustContext& c) {
  return make_adjustment<Values, Invert>(vals, c);
}

}  // namespace pb
