// Color Balance (D36), on the CPU: Image › Adjustments › Color Balance and its adjustment layer. D31's frame: its
// values read and written as AdjustSpec keys them, the identity until D36 puts the formula in, and
// shaders/lib/adj_color_balance.slang the same (adjust.h says how a type goes).
#include "adjust.h"

#include <nlohmann/json.hpp>

namespace pb {
namespace {

// AdjustSpec's: the tone shown (0 Shadows, 1 Midtones, 2 Highlights), Cyan-Red, Magenta-Green, Yellow-Blue
// -100..100, Preserve Luminosity: the set the mock shows; and each tone's set by its name, kept for D36
// (README-psd.md: what E33, D32 and M43 write).
struct Values {
  int tone = 1;
  double cr = 0, mg = 0, yb = 0;
  bool pl = true;
  static constexpr const char* kSets[] = {"Shadows", "Midtones", "Highlights"};
  nlohmann::json sets[3];
  void fields(AdjFields& f) {
    f.index("tone", tone, 1, 3);
    f.number("cr", cr, 0, -100, 100);
    f.number("mg", mg, 0, -100, 100);
    f.number("yb", yb, 0, -100, 100);
    f.flag("pl", pl, true);
    for (int i = 0; i < 3; i++) f.object(kSets[i], sets[i]);
  }
};

class ColorBalance final : public Adjustment {
 public:
  ColorBalance(const Values& v, const AdjustContext& c, std::string vals)
      : Adjustment(AdjType::ColorBalance, c, std::move(vals)), v_(v) {}
  bool identity() const override { return true; }  // the frame's: D36 says when

 private:
  void adjust_row(float*, int, int, int) const override {}  // the frame's: the identity (D36)
  void pack_words(AdjustParams&) const override {}         // the frame's: no words yet (D36: w[2..31], tables)
  [[maybe_unused]] Values v_;
};

}  // namespace

AdjustmentPtr adj::color_balance(const nlohmann::json& vals, const AdjustContext& c) {
  return make_adjustment<Values, ColorBalance>(vals, c);
}

}  // namespace pb
