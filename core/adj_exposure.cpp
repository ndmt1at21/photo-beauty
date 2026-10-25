// Exposure (D34), on the CPU: Image › Adjustments › Exposure and its adjustment layer. D31's frame: its values read and
// written as AdjustSpec keys them, the identity until D34 puts the formula in, and shaders/lib/adj_exposure.slang the
// same (adjust.h says how a type goes).
#include "adjust.h"

#include <nlohmann/json.hpp>

namespace pb {
namespace {

// AdjustSpec's: Exposure -20..20, Offset -0.5..0.5, Gamma Correction 0.01..9.99.
struct Values {
  double e = 0, o = 0, g = 1;
  void fields(AdjFields& f) {
    f.number("e", e, 0, -20, 20);
    f.number("o", o, 0, -0.5, 0.5);
    f.number("g", g, 1, 0.01, 9.99);
  }
};

class Exposure final : public Adjustment {
 public:
  Exposure(const Values& v, const AdjustContext& c, std::string vals)
      : Adjustment(AdjType::Exposure, c, std::move(vals)), v_(v) {}
  bool identity() const override { return true; }  // the frame's: D34 says when

 private:
  void adjust_row(float*, int, int, int) const override {}  // the frame's: the identity (D34)
  void pack_words(AdjustParams&) const override {}         // the frame's: no words yet (D34: w[2..31], tables)
  [[maybe_unused]] Values v_;
};

}  // namespace

AdjustmentPtr adj::exposure(const nlohmann::json& vals, const AdjustContext& c) {
  return make_adjustment<Values, Exposure>(vals, c);
}

}  // namespace pb
