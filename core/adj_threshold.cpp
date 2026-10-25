// Threshold (D38), on the CPU: Image › Adjustments › Threshold and its adjustment layer. D31's frame: its values read
// and written as AdjustSpec keys them, the identity until D38 puts the formula in, and shaders/lib/adj_threshold.slang
// the same (adjust.h says how a type goes).
#include "adjust.h"

#include <nlohmann/json.hpp>

namespace pb {
namespace {

// AdjustSpec's: Threshold Level 1..255 (FIELDS').
struct Values {
  double t = 128;
  void fields(AdjFields& f) {
    f.number("t", t, 128, 1, 255);
  }
};

class Threshold final : public Adjustment {
 public:
  Threshold(const Values& v, const AdjustContext& c, std::string vals)
      : Adjustment(AdjType::Threshold, c, std::move(vals)), v_(v) {}
  bool identity() const override { return true; }  // the frame's: D38 says when

 private:
  void adjust_row(float*, int, int, int) const override {}  // the frame's: the identity (D38)
  void pack_words(AdjustParams&) const override {}         // the frame's: no words yet (D38: w[2..31], tables)
  [[maybe_unused]] Values v_;
};

}  // namespace

AdjustmentPtr adj::threshold(const nlohmann::json& vals, const AdjustContext& c) {
  return make_adjustment<Values, Threshold>(vals, c);
}

}  // namespace pb
