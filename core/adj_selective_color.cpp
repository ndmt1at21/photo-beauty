// Selective Color (D36), on the CPU: Image › Adjustments › Selective Color and its adjustment layer. D31's frame: its
// values read and written as AdjustSpec keys them, the identity until D36 puts the formula in, and
// shaders/lib/adj_selective_color.slang the same (adjust.h says how a type goes).
#include "adjust.h"

#include <nlohmann/json.hpp>

#include <string>

namespace pb {
namespace {

// AdjustSpec's: the colours shown (Reds... Blacks), Cyan, Magenta, Yellow, Black -100..100 %, Relative (0) or
// Absolute (1): the set the mock shows; and each colour's set by its name, kept for D36 (README-psd.md: what E33,
// D32 and M43 write).
struct Values {
  std::string col = "Reds";
  double c = 0, m = 0, y = 0, k = 0;
  int rel = 0;
  static constexpr const char* kSets[] = {"Reds", "Yellows", "Greens", "Cyans", "Blues", "Magentas", "Whites",
                                          "Neutrals", "Blacks"};
  nlohmann::json sets[9];
  void fields(AdjFields& f) {
    f.choice("col", col,
             {"Reds", "Yellows", "Greens", "Cyans", "Blues", "Magentas", "Whites", "Neutrals", "Blacks"});
    f.number("c", c, 0, -100, 100);
    f.number("m", m, 0, -100, 100);
    f.number("y", y, 0, -100, 100);
    f.number("k", k, 0, -100, 100);
    f.index("rel", rel, 0, 2);
    for (int i = 0; i < 9; i++) f.object(kSets[i], sets[i]);
  }
};

class SelectiveColor final : public Adjustment {
 public:
  SelectiveColor(const Values& v, const AdjustContext& c, std::string vals)
      : Adjustment(AdjType::SelectiveColor, c, std::move(vals)), v_(v) {}
  bool identity() const override { return true; }  // the frame's: D36 says when

 private:
  void adjust_row(float*, int, int, int) const override {}  // the frame's: the identity (D36)
  void pack_words(AdjustParams&) const override {}         // the frame's: no words yet (D36: w[2..31], tables)
  [[maybe_unused]] Values v_;
};

}  // namespace

AdjustmentPtr adj::selective_color(const nlohmann::json& vals, const AdjustContext& c) {
  return make_adjustment<Values, SelectiveColor>(vals, c);
}

}  // namespace pb
