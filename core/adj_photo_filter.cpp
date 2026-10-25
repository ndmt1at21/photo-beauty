// Photo Filter (D37), on the CPU: Image › Adjustments › Photo Filter and its adjustment layer. D31's frame: its values
// read and written as AdjustSpec keys them, the identity until D37 puts the formula in, and
// shaders/lib/adj_photo_filter.slang the same (adjust.h says how a type goes).
#include "adjust.h"

#include <nlohmann/json.hpp>

#include <string>

namespace pb {
namespace {

// AdjustSpec's: Filter (0) or Color (1), the filter (its list), the colour, Density 1..100 %, Preserve Luminosity.
struct Values {
  int pf = 0;
  std::string filter = "Warming Filter (85)", color = "#ec8a00";
  double d = 25;
  bool pl = true;
  void fields(AdjFields& f) {
    f.index("pf", pf, 0, 2);
    f.choice("pf0", filter, {"Warming Filter (85)", "Warming Filter (LBA)", "Warming Filter (81)",
                              "Cooling Filter (80)", "Cooling Filter (LBB)", "Cooling Filter (82)", "Red",
                              "Orange", "Yellow", "Green", "Cyan", "Blue", "Violet", "Magenta", "Sepia",
                              "Underwater"});
    f.color("pf1", color, "#ec8a00");
    f.number("d", d, 25, 1, 100);
    f.flag("pl", pl, true);
  }
};

class PhotoFilter final : public Adjustment {
 public:
  PhotoFilter(const Values& v, const AdjustContext& c, std::string vals)
      : Adjustment(AdjType::PhotoFilter, c, std::move(vals)), v_(v) {}
  bool identity() const override { return true; }  // the frame's: D37 says when

 private:
  void adjust_row(float*, int, int, int) const override {}  // the frame's: the identity (D37)
  void pack_words(AdjustParams&) const override {}         // the frame's: no words yet (D37: w[2..31], tables)
  [[maybe_unused]] Values v_;
};

}  // namespace

AdjustmentPtr adj::photo_filter(const nlohmann::json& vals, const AdjustContext& c) {
  return make_adjustment<Values, PhotoFilter>(vals, c);
}

}  // namespace pb
