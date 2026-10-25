// Curves (D33), on the CPU: Image › Adjustments › Curves and its adjustment layer. D31's frame: its values read and
// written as AdjustSpec keys them, the identity until D33 puts the formula in, and shaders/lib/adj_curves.slang the
// same (adjust.h says how a type goes).
#include "adjust.h"

#include <nlohmann/json.hpp>

#include <string>

namespace pb {
namespace {

// AdjustSpec's: the preset's name, the channel shown, each channel's points by its name ({"RGB": [[0, 0], [255,
// 255]]}; a channel without its own takes the preset's: AdjustSpec's curvePoints()), kept as JSON for D33.
struct Values {
  std::string pre = "Default", ch = "RGB";
  nlohmann::json pts = nlohmann::json::object();
  void fields(AdjFields& f) {
    f.text("pre", pre, "Default");
    f.choice("ch", ch, {"RGB", "Red", "Green", "Blue"});
    f.value("pts", pts, nlohmann::json::object(), [](const nlohmann::json& j) { return j.is_object(); });
  }
};

class Curves final : public Adjustment {
 public:
  Curves(const Values& v, const AdjustContext& c, std::string vals)
      : Adjustment(AdjType::Curves, c, std::move(vals)), v_(v) {}
  bool identity() const override { return true; }  // the frame's: D33 says when

 private:
  void adjust_row(float*, int, int, int) const override {}  // the frame's: the identity (D33)
  void pack_words(AdjustParams&) const override {}         // the frame's: no words yet (D33: w[2..31], tables)
  [[maybe_unused]] Values v_;
};

}  // namespace

AdjustmentPtr adj::curves(const nlohmann::json& vals, const AdjustContext& c) {
  return make_adjustment<Values, Curves>(vals, c);
}

}  // namespace pb
