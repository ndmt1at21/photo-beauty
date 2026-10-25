// Levels (D33), on the CPU: Image › Adjustments › Levels and its adjustment layer. D31's frame: its values read and
// written as AdjustSpec keys them, the identity until D33 puts the formula in, and shaders/lib/adj_levels.slang the
// same (adjust.h says how a type goes).
#include "adjust.h"

#include <nlohmann/json.hpp>

#include <string>

namespace pb {
namespace {

// AdjustSpec's: the preset's name, the channel shown (RGB, Red, Green, Blue), input black 0..253, gamma 0.01..9.99,
// input white 2..255, output black and white 0..255 (FIELDS'): the set the mock shows; and each channel's set by
// its name, kept for D33 (README-psd.md: what E33, D32 and M43 write).
struct Values {
  std::string pre = "Default", ch = "RGB";
  double in0 = 0, gam = 1, in1 = 255, out0 = 0, out1 = 255;
  static constexpr const char* kSets[] = {"RGB", "Red", "Green", "Blue"};
  nlohmann::json sets[4];
  void fields(AdjFields& f) {
    f.text("pre", pre, "Default");
    f.choice("ch", ch, {"RGB", "Red", "Green", "Blue"});
    f.number("in0", in0, 0, 0, 253);
    f.number("gam", gam, 1, 0.01, 9.99);
    f.number("in1", in1, 255, 2, 255);
    f.number("out0", out0, 0, 0, 255);
    f.number("out1", out1, 255, 0, 255);
    for (int i = 0; i < 4; i++) f.object(kSets[i], sets[i]);
  }
};

class Levels final : public Adjustment {
 public:
  Levels(const Values& v, const AdjustContext& c, std::string vals)
      : Adjustment(AdjType::Levels, c, std::move(vals)), v_(v) {}
  bool identity() const override { return true; }  // the frame's: D33 says when

 private:
  void adjust_row(float*, int, int, int) const override {}  // the frame's: the identity (D33)
  void pack_words(AdjustParams&) const override {}         // the frame's: no words yet (D33: w[2..31], tables)
  [[maybe_unused]] Values v_;
};

}  // namespace

AdjustmentPtr adj::levels(const nlohmann::json& vals, const AdjustContext& c) {
  return make_adjustment<Values, Levels>(vals, c);
}

}  // namespace pb
