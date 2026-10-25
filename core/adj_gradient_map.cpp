// Gradient Map (D38), on the CPU: Image › Adjustments › Gradient Map and its adjustment layer. Each pixel takes the
// gradient's colour at its luminosity (Adobe: "maps the equivalent grayscale range of an image to the colors of a
// specified gradient fill"): t = Lum, histogram.h's kLumPercent mix, 1 - t with Reverse, the colour gradient.h's table
// of the gradient in the Method at t, dithered with Dither as gradient.h dithers (its hash, its seed). The table is
// made here, once, of the document's colours (its profile for Linear's light and Perceptual's Oklab; grey stops on a
// grey document); shaders/lib/adj_gradient_map.slang looks it up and does the rest line by line (adjust.h says how a
// type goes).
#include "adjust.h"

#include <nlohmann/json.hpp>

#include <stdexcept>
#include <string>

#include "color.h"
#include "gradient.h"
#include "histogram.h"

namespace pb {
namespace {

// ---- Hypotheses -----------------------------------------------------------------------------------------------------

// Whether the gradient's opacity stops count: false, as Photoshop is said to ignore them in a Gradient Map (only the
// colours map; transparency is the layer's opacity or mask); true would mix the pixel with the colour by the
// gradient's opacity there, v + (c - v) a. D32's gmap-alpha (opacity 1 to 0.3) settles it (B38); test_adj_simple_ps
// prints how far the other way is.
constexpr bool kUseOpacity = false;

// The dither, in values 0..1 since an adjustment does not know the depth its result is rounded to: noise (gradient.h's
// dither_noise(), kGradientSeed) times kDitherWide, ±0.5 of an 8-bit level (gradient.cpp's kDitherLevels8 and
// kDitherLevels16 alike), narrowed within it of 0 and 1 to twice the way there plus kDitherFloor, one 16-bit level, as
// gradient.h's to_level() narrows at 16 bits, so that a mean keeps (at 8 bits, within half a level of either end, a
// little less of it); 0 and 1 themselves stay, at either depth (a float rounding at the edge of a level could take
// white one 16-bit level down).
constexpr float kDitherWide = 1.f / 255, kDitherFloor = 1.f / 65535;

// The gradient of a Gradient Map whose vals have none: AdjustSpec's GRAD row, linear-gradient(90deg, #1b1020, #9e2f45,
// #e9c7b3), which the UI shows for one (and D32's adjust.jsx puts in for one without); test_adj_simple reads it there.
// Photoshop's new Gradient Map takes the foreground to background colours instead: the UI writes "gradient" for that.
Gradient default_gradient() {
  Gradient g;
  g.colors = {{0, {27, 16, 32}}, {0.5, {158, 47, 69}}, {1, {233, 199, 179}}};
  return g;
}

// ---- Values ---------------------------------------------------------------------------------------------------------

// AdjustSpec's: Dither, Reverse, Method (gradient.h's, as the UI spells them); the gradient (README-psd.md:
// gradient.h's JSON), null by default (default_gradient()).
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

// v dithered by noise n (-0.5..0.5): kDitherWide, narrowed near 0 and 1, none at them (and past them).
inline float dither(float v, float n) {
  const float back = 1 - v;
  const float end = v < back ? v : back;
  const float room = end + end + kDitherFloor;
  const float w = end > 0 ? (room < kDitherWide ? room : kDitherWide) : 0;
  return v + n * w;
}

class GradientMap final : public Adjustment {
 public:
  GradientMap(const Values& v, const AdjustContext& c, std::string vals)
      : Adjustment(AdjType::GradientMap, c, std::move(vals)), reverse_(v.rev), dither_(v.dither) {
    GradientSpace space;
    space.grey = c.mode == Mode::Gray;
    if (c.icc && !c.icc->empty()) {
      try {
        space.profile = source_profile(*c.icc);
      } catch (const std::runtime_error&) {  // a profile it cannot read: sRGB's, as for none
      }
    }
    const Gradient g = v.gradient.is_null() ? default_gradient() : *gradient_from(v.gradient);
    const std::shared_ptr<const GradientLut> lut = gradient_lut(g, *gradient_method(v.method), space);
    tables_.assign(lut->rgba.begin(), lut->rgba.end());  // GradientLut's 4098 entries: a curve's layout
  }
  bool identity() const override { return false; }  // a colour of its luminosity: never every colour as it was
  bool dithered() const override { return dither_; }

 private:
  void adjust_row(float* px, int n, int x, int y) const override {
    const float wr = float(kLumPercent[0]), wg = float(kLumPercent[1]), wb = float(kLumPercent[2]);
    for (int i = 0; i < n; i++, px += 4) {
      const float lum = (wr * px[0] + wg * px[1] + wb * px[2]) / 100;
      const float t = reverse_ ? 1 - lum : lum;
      const Float4 c = adjust_curve(tables_.data(), t);
      float o[3] = {c[0], c[1], c[2]};
      if (kUseOpacity)
        for (int k = 0; k < 3; k++) o[k] = px[k] + (c[k] - px[k]) * c[3];
      if (dither_) {
        const std::array<float, 4> d = dither_noise(x + i, y, kGradientSeed);
        for (int k = 0; k < 3; k++) o[k] = dither(o[k], d[size_t(k)]);
      }
      px[0] = o[0], px[1] = o[1], px[2] = o[2];
    }
  }
  // w[2] Reverse, w[3] Dither, w[4] the opacity (kUseOpacity), 0 or 1; w[5..7] the weights (kLumPercent, floats),
  // w[8] the dither's seed, w[9] kDitherWide, w[10] kDitherFloor. The table from tables[w[1]].
  void pack_words(AdjustParams& p) const override {
    p.w[2] = reverse_ ? 1 : 0;
    p.w[3] = dither_ ? 1 : 0;
    p.w[4] = kUseOpacity ? 1 : 0;
    for (int k = 0; k < 3; k++) p.w[size_t(5 + k)] = word(float(kLumPercent[k]));
    p.w[8] = kGradientSeed;
    p.w[9] = word(kDitherWide);
    p.w[10] = word(kDitherFloor);
  }

  bool reverse_, dither_;
};

}  // namespace

AdjustmentPtr adj::gradient_map(const nlohmann::json& vals, const AdjustContext& c) {
  return make_adjustment<Values, GradientMap>(vals, c);
}

}  // namespace pb
