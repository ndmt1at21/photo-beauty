// Black & White (D37), on the CPU: Image › Adjustments › Black & White and its adjustment layer. D31's frame: its
// values read and written as AdjustSpec keys them, the identity until D37 puts the formula in, and
// shaders/lib/adj_black_white.slang the same (adjust.h says how a type goes).
#include "adjust.h"

#include <nlohmann/json.hpp>

#include <string>

namespace pb {
namespace {

// AdjustSpec's: the preset's name, Tint and its colour, Reds 40, Yellows 60, Greens 40, Cyans 60, Blues 20,
// Magentas 80 (%, -200..300).
struct Values {
  std::string pre = "Default", tint_color = "#c8a27c";
  bool tint = false;
  double r = 40, y = 60, g = 40, c = 60, b = 20, m = 80;
  void fields(AdjFields& f) {
    f.text("pre", pre, "Default");
    f.flag("tint", tint, false);
    f.color("tintColor", tint_color, "#c8a27c");
    f.number("r", r, 40, -200, 300);
    f.number("y", y, 60, -200, 300);
    f.number("g", g, 40, -200, 300);
    f.number("c", c, 60, -200, 300);
    f.number("b", b, 20, -200, 300);
    f.number("m", m, 80, -200, 300);
  }
};

class BlackWhite final : public Adjustment {
 public:
  BlackWhite(const Values& v, const AdjustContext& c, std::string vals)
      : Adjustment(AdjType::BlackWhite, c, std::move(vals)), v_(v) {}
  bool identity() const override { return true; }  // the frame's: D37 says when

 private:
  void adjust_row(float*, int, int, int) const override {}  // the frame's: the identity (D37)
  void pack_words(AdjustParams&) const override {}         // the frame's: no words yet (D37: w[2..31], tables)
  [[maybe_unused]] Values v_;
};

}  // namespace

AdjustmentPtr adj::black_white(const nlohmann::json& vals, const AdjustContext& c) {
  return make_adjustment<Values, BlackWhite>(vals, c);
}

}  // namespace pb
