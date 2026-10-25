// Channel Mixer (D37), on the CPU: Image › Adjustments › Channel Mixer and its adjustment layer. D31's frame: its
// values read and written as AdjustSpec keys them, the identity until D37 puts the formula in, and
// shaders/lib/adj_channel_mixer.slang the same (adjust.h says how a type goes).
#include "adjust.h"

#include <nlohmann/json.hpp>

#include <string>

namespace pb {
namespace {

// AdjustSpec's: the output channel shown, Monochrome, Red 100, Green 0, Blue 0, Constant 0 (%, -200..200): the set
// the mock shows; and each output channel's set by its name (Gray: Monochrome's), kept for D37 (README-psd.md:
// what E33, D32 and M43 write).
struct Values {
  std::string out = "Red";
  bool mono = false;
  double r = 100, g = 0, b = 0, k = 0;
  static constexpr const char* kSets[] = {"Red", "Green", "Blue", "Gray"};
  nlohmann::json sets[4];
  void fields(AdjFields& f) {
    f.choice("out", out, {"Red", "Green", "Blue"});
    f.flag("mono", mono, false);
    f.number("r", r, 100, -200, 200);
    f.number("g", g, 0, -200, 200);
    f.number("b", b, 0, -200, 200);
    f.number("k", k, 0, -200, 200);
    for (int i = 0; i < 4; i++) f.object(kSets[i], sets[i]);
  }
};

class ChannelMixer final : public Adjustment {
 public:
  ChannelMixer(const Values& v, const AdjustContext& c, std::string vals)
      : Adjustment(AdjType::ChannelMixer, c, std::move(vals)), v_(v) {}
  bool identity() const override { return true; }  // the frame's: D37 says when

 private:
  void adjust_row(float*, int, int, int) const override {}  // the frame's: the identity (D37)
  void pack_words(AdjustParams&) const override {}         // the frame's: no words yet (D37: w[2..31], tables)
  [[maybe_unused]] Values v_;
};

}  // namespace

AdjustmentPtr adj::channel_mixer(const nlohmann::json& vals, const AdjustContext& c) {
  return make_adjustment<Values, ChannelMixer>(vals, c);
}

}  // namespace pb
