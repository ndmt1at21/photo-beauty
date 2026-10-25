// Vibrance (D35), on the CPU: Image › Adjustments › Vibrance and its adjustment layer. D31's frame: its values read and
// written as AdjustSpec keys them, the identity until D35 puts the formula in, and shaders/lib/adj_vibrance.slang the
// same (adjust.h says how a type goes).
#include "adjust.h"

#include <nlohmann/json.hpp>

namespace pb {
namespace {

// AdjustSpec's: Vibrance, Saturation -100..100.
struct Values {
  double v = 0, s = 0;
  void fields(AdjFields& f) {
    f.number("v", v, 0, -100, 100);
    f.number("s", s, 0, -100, 100);
  }
};

class Vibrance final : public Adjustment {
 public:
  Vibrance(const Values& v, const AdjustContext& c, std::string vals)
      : Adjustment(AdjType::Vibrance, c, std::move(vals)), v_(v) {}
  bool identity() const override { return true; }  // the frame's: D35 says when

 private:
  void adjust_row(float*, int, int, int) const override {}  // the frame's: the identity (D35)
  void pack_words(AdjustParams&) const override {}         // the frame's: no words yet (D35: w[2..31], tables)
  [[maybe_unused]] Values v_;
};

}  // namespace

AdjustmentPtr adj::vibrance(const nlohmann::json& vals, const AdjustContext& c) {
  return make_adjustment<Values, Vibrance>(vals, c);
}

}  // namespace pb
