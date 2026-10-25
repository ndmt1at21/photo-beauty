// Hue/Saturation (D35), on the CPU: Image › Adjustments › Hue/Saturation and its adjustment layer. D31's frame: its
// values read and written as AdjustSpec keys them, the identity until D35 puts the formula in, and
// shaders/lib/adj_hue_saturation.slang the same (adjust.h says how a type goes).
#include "adjust.h"

#include <nlohmann/json.hpp>

#include <string>

namespace pb {
namespace {

// A span is null (the range's own: AdjustSpec's SPANS) or four numbers, degrees.
bool span_valid(const nlohmann::json& j) {
  if (!j.is_array() || j.size() != 4) return false;
  for (const nlohmann::json& v : j)
    if (!v.is_number()) return false;
  return true;
}

// AdjustSpec's: the preset's name, the range shown (Master, Reds...), Hue -180..180, Saturation, Lightness
// -100..100, Colorize, the range's span [a, b, c, d] (null: SPANS'): the set the mock shows; and each range's set by
// its name, kept for D35 (README-psd.md: what E33, D32 and M43 write).
struct Values {
  std::string pre = "Default", rng = "Master";
  double h = 0, s = 0, l = 0;
  bool colorize = false;
  nlohmann::json span;  // null
  static constexpr const char* kSets[] = {"Master", "Reds", "Yellows", "Greens", "Cyans", "Blues", "Magentas"};
  nlohmann::json sets[7];
  void fields(AdjFields& f) {
    f.text("pre", pre, "Default");
    f.choice("rng", rng, {"Master", "Reds", "Yellows", "Greens", "Cyans", "Blues", "Magentas"});
    f.number("h", h, 0, -180, 180);
    f.number("s", s, 0, -100, 100);
    f.number("l", l, 0, -100, 100);
    f.flag("colorize", colorize, false);
    f.value("span", span, nullptr, span_valid);
    for (int i = 0; i < 7; i++) f.object(kSets[i], sets[i]);
  }
};

class HueSaturation final : public Adjustment {
 public:
  HueSaturation(const Values& v, const AdjustContext& c, std::string vals)
      : Adjustment(AdjType::HueSaturation, c, std::move(vals)), v_(v) {}
  bool identity() const override { return true; }  // the frame's: D35 says when

 private:
  void adjust_row(float*, int, int, int) const override {}  // the frame's: the identity (D35)
  void pack_words(AdjustParams&) const override {}         // the frame's: no words yet (D35: w[2..31], tables)
  [[maybe_unused]] Values v_;
};

}  // namespace

AdjustmentPtr adj::hue_saturation(const nlohmann::json& vals, const AdjustContext& c) {
  return make_adjustment<Values, HueSaturation>(vals, c);
}

}  // namespace pb
