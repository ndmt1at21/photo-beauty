// Brightness/Contrast (D34), on the CPU: Image › Adjustments › Brightness/Contrast and its adjustment layer. D31's
// frame: its values read and written as AdjustSpec keys them, the identity until D34 puts the formula in, and
// shaders/lib/adj_brightness_contrast.slang the same (adjust.h says how a type goes).
#include "adjust.h"

#include <nlohmann/json.hpp>

namespace pb {
namespace {

// AdjustSpec's: Brightness -150..150, Contrast -50..100 (Use Legacy's ranges are Photoshop's: D34), Use Legacy.
struct Values {
  double b = 0, c = 0;
  bool legacy = false;
  void fields(AdjFields& f) {
    f.number("b", b, 0, -150, 150);
    f.number("c", c, 0, -50, 100);
    f.flag("legacy", legacy, false);
  }
};

class BrightnessContrast final : public Adjustment {
 public:
  BrightnessContrast(const Values& v, const AdjustContext& c, std::string vals)
      : Adjustment(AdjType::BrightnessContrast, c, std::move(vals)), v_(v) {}
  bool identity() const override { return true; }  // the frame's: D34 says when

 private:
  void adjust_row(float*, int, int, int) const override {}  // the frame's: the identity (D34)
  void pack_words(AdjustParams&) const override {}         // the frame's: no words yet (D34: w[2..31], tables)
  [[maybe_unused]] Values v_;
};

}  // namespace

AdjustmentPtr adj::brightness_contrast(const nlohmann::json& vals, const AdjustContext& c) {
  return make_adjustment<Values, BrightnessContrast>(vals, c);
}

}  // namespace pb
