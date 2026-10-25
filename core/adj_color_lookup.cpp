// Color Lookup (D39), on the CPU: Image › Adjustments › Color Lookup and its adjustment layer. D31's frame: its values
// read and written as AdjustSpec keys them, the identity until D39 puts the formula in, and
// shaders/lib/adj_color_lookup.slang the same (adjust.h says how a type goes).
#include "adjust.h"

#include <nlohmann/json.hpp>

#include <string>

namespace pb {
namespace {

// AdjustSpec's: which source (0 3DLUT File, 1 Abstract, 2 Device Link), each one's file name, Dither; and the file's
// content (README-psd.md's lut: a .cube's text), kept for D39. The names' defaults are the mock's (D39 and N37
// replace them).
struct Values {
  int lf = 0;
  std::string file = "Phim ấm.cube", abstract = "Chọn…", device_link = "Chọn…", lut;
  bool dither = false;
  void fields(AdjFields& f) {
    f.index("lf", lf, 0, 3);
    f.text("lf0", file, "Phim ấm.cube");
    f.text("lf1", abstract, "Chọn…");
    f.text("lf2", device_link, "Chọn…");
    f.flag("dither", dither, false);
    f.text("lut", lut, "");
  }
};

class ColorLookup final : public Adjustment {
 public:
  ColorLookup(const Values& v, const AdjustContext& c, std::string vals)
      : Adjustment(AdjType::ColorLookup, c, std::move(vals)), v_(v) {}
  bool identity() const override { return true; }  // the frame's: D39 says when

 private:
  void adjust_row(float*, int, int, int) const override {}  // the frame's: the identity (D39)
  void pack_words(AdjustParams&) const override {}         // the frame's: no words yet (D39: w[2..31], tables)
  [[maybe_unused]] Values v_;
};

}  // namespace

AdjustmentPtr adj::color_lookup(const nlohmann::json& vals, const AdjustContext& c) {
  return make_adjustment<Values, ColorLookup>(vals, c);
}

}  // namespace pb
