// Gradient Map (D38), on the CPU: Image › Adjustments › Gradient Map and its adjustment layer. D31's frame: its values
// read and written as AdjustSpec keys them, the identity until D38 puts the formula in, and
// shaders/lib/adj_gradient_map.slang the same (adjust.h says how a type goes).
#include "adjust.h"

#include <nlohmann/json.hpp>

#include <string>

#include "gradient.h"

namespace pb {
namespace {

// AdjustSpec's: Dither, Reverse, Method (gradient.h's, as the UI spells them); and the gradient (README-psd.md:
// gradient.h's JSON; null, the mock keeping none, is D38's to say), kept for D38.
struct Values {
  bool dither = false, rev = false;
  std::string method = "Perceptual";
  nlohmann::json gradient;  // null
  void fields(AdjFields& f) {
    f.flag("dither", dither, false);
    f.flag("rev", rev, false);
    f.choice("method", method, {"Perceptual", "Linear", "Classic", "Smooth", "Stripes"});
    f.value("gradient", gradient, nullptr, [](const nlohmann::json& j) { return gradient_from(j).has_value(); });
  }
};

class GradientMap final : public Adjustment {
 public:
  GradientMap(const Values& v, const AdjustContext& c, std::string vals)
      : Adjustment(AdjType::GradientMap, c, std::move(vals)), v_(v) {}
  bool identity() const override { return true; }  // the frame's: D38 says when

 private:
  void adjust_row(float*, int, int, int) const override {}  // the frame's: the identity (D38)
  void pack_words(AdjustParams&) const override {}         // the frame's: no words yet (D38: w[2..31], tables)
  [[maybe_unused]] Values v_;
};

}  // namespace

AdjustmentPtr adj::gradient_map(const nlohmann::json& vals, const AdjustContext& c) {
  return make_adjustment<Values, GradientMap>(vals, c);
}

}  // namespace pb
