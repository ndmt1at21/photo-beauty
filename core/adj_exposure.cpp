// Exposure (D34), on the CPU: Image › Adjustments › Exposure and its adjustment layer, and its eyedroppers
// (exposure_from_pick()). Adobe: it "works by performing calculations in a linear color space (gamma 1.0) rather than
// the image's current color space". Each channel's value v goes to linear light through the document's curve
// (decode: its profile's, color.h's source_profile(); sRGB's without one), x = decode(v) 2^exposure + offset, clamped
// to 0..1, then x^(1 / gamma) ("a simple power function"), and back through the curve (encode). That is a table per
// channel made here, its steps closer where the curve is steep; shaders/lib/adj_exposure.slang does the rest line by
// line, so both give the same bits (adjust.h says how a type goes).
#include "adjust.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <memory>
#include <optional>
#include <string>

#include "color.h"
#include "histogram.h"

namespace pb {

// Exposure's eyedroppers (N37): vals (Exposure's JSON) with Offset set by Set Black Point (point 0), or Exposure by
// Set Gray Point (1) or Set White Point (2), from `sample`, the pixel picked before the adjustment, R, G, B levels
// 0..255 (a 16-bit sample's v × 255 / 65535) as levels_from_pick() (D33) takes it; context the document's (its curves
// to linear light; a grey one's R). Adobe: "The Set Black Point eyedropper sets the Offset, shifting the pixel you
// click to zero. The Set White Point eyedropper sets the Exposure, making the point you click white. The Set Gray
// Point eyedropper sets the Exposure, making the value you click middle gray", by the pixel's luminance. So, L the
// sample's luminance in linear light (kPickGrey's note): black o = -L 2^e; white e = log2((1 - o) / L); grey e =
// log2((G^g - o) / L), G = kPickGrey, so that (L 2^e + o)^(1 / g) is G; each clamped to its range, unrounded
// (Photoshop keeps a double), the other values as they were. A black sample leaves Exposure as it was (no exposure
// makes it white or grey). Written as vals() would write them: nullopt when vals do not read or point is not 0, 1 or
// 2. Any thread.
// Declared here: core/adjust.h is D31's; callers declare it as it is above (api/d34.md).
std::optional<std::string> exposure_from_pick(const std::string& vals, int point, const std::array<double, 3>& sample,
                                              const AdjustContext& context);

namespace {

using nlohmann::json;

// ---- Hypotheses -----------------------------------------------------------------------------------------------------

// Linear light is the document's: its profile's curves (Adobe's "linear color space (gamma 1.0)" of "the image's
// current color space"). Not 0: a plain gamma of this instead, for every profile (2.2: the task's other guess). D32's
// exposure-* (B33) say which; test_tone_ps prints both.
constexpr double kLinearGamma = 0;
// The eyedroppers' luminance: kLumPercent's 30/59/11 of the channels in linear light. Their middle grey: 18 % in
// linear light (photography's, HDR's), not the encoded 50 %. To check in Photoshop (N37, B33).
constexpr double kPickGrey = 0.18;

// ---- Values ---------------------------------------------------------------------------------------------------------

// AdjustSpec's: Exposure -20..20, Offset -0.5..0.5, Gamma Correction 0.01..9.99.
struct Values {
  double e = 0, o = 0, g = 1;
  void fields(AdjFields& f) {
    f.number("e", e, 0, -20, 20);
    f.number("o", o, 0, -0.5, 0.5);
    f.number("g", g, 1, 0.01, 9.99);
  }
};

bool read(const json& j, Values& v) {
  AdjFields in(&j);
  v.fields(in);
  return in.ok();
}

std::string written(Values v) {
  AdjFields out(nullptr);
  v.fields(out);
  return out.written();
}

// ---- Linear light ---------------------------------------------------------------------------------------------------

// The document's curves: its profile's (null, empty or not one lcms reads: sRGB's), held while it is used.
std::shared_ptr<const Source> source_of(const AdjustContext& c) {
  static const std::shared_ptr<const Source> srgb = source_profile(icc_profile(ColorSpace::Srgb));
  if (!c.icc || c.icc->empty()) return srgb;
  try {
    return source_profile(*c.icc);
  } catch (const std::exception&) {
    return srgb;  // a document's profile that does not read shows as sRGB too
  }
}

// Encoded e (0..1) of channel ch in linear light: the profile's table, linear between its entries (gradient.cpp's
// to_linear()), or kLinearGamma's power.
double decode(const Source& s, int ch, double e) {
  e = std::clamp(e, 0., 1.);
  if (kLinearGamma != 0) return std::pow(e, kLinearGamma);
  const double x = e * (Source::kLut - 1);
  const int i = std::min(int(x), Source::kLut - 2);
  return s.linear[ch][i] + (double(s.linear[ch][i + 1]) - s.linear[ch][i]) * (x - i);
}
// Back: the encoded value of light y through the same table (it rises: gradient.cpp's to_encoded()).
double encode(const Source& s, int ch, double y) {
  if (kLinearGamma != 0) return std::pow(std::clamp(y, 0., 1.), 1 / kLinearGamma);
  const float* l = s.linear[ch];
  if (!(y > l[0])) return 0;
  if (y >= l[Source::kLut - 1]) return 1;
  int lo = 0, hi = Source::kLut - 1;  // l[lo] <= y < l[hi]
  while (hi - lo > 1) {
    const int mid = (lo + hi) / 2;
    (l[mid] <= y ? lo : hi) = mid;
  }
  return (lo + (y - l[lo]) / (double(l[hi]) - l[lo])) / (Source::kLut - 1);
}

// ---- The adjustment -------------------------------------------------------------------------------------------------

// Below this a value is 0, in the table and coming in (and NaN, below 0): products of the table's interpolation then
// never fall to subnormals, which the GPU may flush (blend.slang). 2^-64, adj_exposure.slang's kExposureTiny.
constexpr float kTiny = 0x1p-64f;

// The largest float at or below d, the smallest at or above it.
float below(double d) {
  const float f = float(d);
  return double(f) > d ? std::nextafter(f, -1.f) : f;
}
float above(double d) {
  const float f = float(d);
  return double(f) < d ? std::nextafter(f, 2.f) : f;
}

// Each channel's table spans where its curve moves: from v0, where x leaves 0 (0 unless the offset is below 0: the
// curve's foot, 0 below it), to v1, where x reaches 1 (1 unless the exposure or the offset gets there first: the top,
// 1 past it), so that both kinks are its ends and its 4,096 steps lie where it changes. Along u = (v - v0) k, k = 1 /
// (v1 - v0), it runs by t = sqrt(u / 2) up to the middle and 1 - sqrt((1 - u) / 2) past it, its steps closer at both
// ends: where the curve leaves its foot with an infinite slope (x^(1 / gamma) past gamma 1; at an offset below 0 any
// gamma on a profile whose curve has no linear foot, a plain gamma's encode being steep at 0), the steep part lies
// within 6e-8 of v0, under a 16-bit level; where gammas under 1 climb to the top (x^100 at 0.01), 4,096 steps follow
// them.
//
// Words (adj_exposure.slang's): w[2] 1 (0: the identity); w[3 + c] channel c's v0, w[6 + c] its k. One table, 4,098
// entries: channel c's curve in component c (R, G, B in x, y, z).
class Exposure final : public Adjustment {
 public:
  Exposure(const Values& v, const AdjustContext& c, std::string vals)
      : Adjustment(AdjType::Exposure, c, std::move(vals)) {
    if (v.e == 0 && v.o == 0 && v.g == 1) return;
    on_ = true;
    const std::shared_ptr<const Source> src = source_of(c);
    const double scale = std::exp2(v.e), top = (1 - v.o) / scale;  // decode(v1) = top
    const auto curve = [&](int ch, double e) {  // encoded e through the formula, in double
      const double x = std::clamp(decode(*src, ch, e) * scale + v.o, 0., 1.);
      return encode(*src, ch, v.g == 1 ? x : std::pow(x, 1 / v.g));
    };
    tables_.assign(size_t(kCurveSteps) + 2, Float4{});
    for (int ch = 0; ch < 3; ch++) {
      // The ends as floats just inside where x is 0 and 1, so that the foot's entry is 0 and the top's 1.
      float v0 = v.o < 0 ? below(encode(*src, ch, -v.o / scale)) : 0;
      if (v0 < kTiny) v0 = 0;
      float v1 = top < 1 ? above(encode(*src, ch, top)) : 1;
      if (!(v1 > v0)) v1 = std::nextafter(v0, 2.f);  // x 0 everywhere (v0 1), or a curve flat there
      v0_[ch] = v0;
      k_[ch] = float(1 / (double(v1) - double(v0)));
      for (int i = 0; i <= kCurveSteps; i++) {
        const double t = double(i) / kCurveSteps, u = t <= 0.5 ? 2 * t * t : 1 - 2 * (1 - t) * (1 - t);
        // The ends exactly: rounding could leave x a hair above 0 at the foot, which a high gamma would lift far.
        double y;
        if (i == 0 && v.o < 0)
          y = 0;
        else if (i == kCurveSteps && v1 < 1)
          y = 1;
        else
          y = curve(ch, std::min(1., double(v0) + u / double(k_[ch])));
        tables_[size_t(i)][size_t(ch)] = std::fabs(y) < double(kTiny) ? 0.f : float(y);
      }
    }
    tables_[size_t(kCurveSteps) + 1] = tables_[size_t(kCurveSteps)];
  }
  bool identity() const override { return !on_; }

 private:
  // Channel ch's place along its table: adj_exposure.slang's exposure_along().
  float along(int ch, float v) const {
    if (!(v >= kTiny)) v = 0;  // and NaN, below 0
    const float d = v - v0_[ch];
    const float e = d * k_[ch];
    const float u = e > 0 ? (e < 1 ? e : 1) : 0;
    return u <= 0.5f ? std::sqrt(u * 0.5f) : 1 - std::sqrt((1 - u) * 0.5f);
  }
  void adjust_row(float* px, int n, int, int) const override {
    if (!on_) return;
    for (int i = 0; i < n; i++, px += 4)
      for (int ch = 0; ch < 3; ch++) px[ch] = adjust_curve(tables_.data(), along(ch, px[ch]))[size_t(ch)];
  }
  void pack_words(AdjustParams& p) const override {
    p.w[2] = on_ ? 1 : 0;
    for (int ch = 0; ch < 3; ch++) p.w[size_t(3 + ch)] = word(v0_[ch]), p.w[size_t(6 + ch)] = word(k_[ch]);
  }

  bool on_ = false;
  float v0_[3] = {}, k_[3] = {1, 1, 1};
};

}  // namespace

AdjustmentPtr adj::exposure(const json& vals, const AdjustContext& c) {
  return make_adjustment<Values, Exposure>(vals, c);
}

// ---- The eyedroppers ------------------------------------------------------------------------------------------------

std::optional<std::string> exposure_from_pick(const std::string& vals, int point, const std::array<double, 3>& sample,
                                              const AdjustContext& context) {
  const json j = json::parse(vals, nullptr, false);
  Values v;
  if (!j.is_object() || point < 0 || point > 2 || !read(j, v)) return std::nullopt;
  const std::shared_ptr<const Source> src = source_of(context);
  double lum = 0;
  if (context.mode == Mode::Gray) {
    lum = decode(*src, 0, sample[0] / 255);
  } else {
    for (int c = 0; c < 3; c++) lum += kLumPercent[c] / 100. * decode(*src, c, sample[size_t(c)] / 255);
  }
  const double scale = std::exp2(v.e);
  if (point == 0) {
    v.o = std::clamp(-lum * scale, -0.5, 0.5);
  } else if (lum > 0) {
    const double want = point == 2 ? 1 - v.o : std::pow(kPickGrey, v.g) - v.o;  // what L 2^e is to be
    if (want > 0) v.e = std::clamp(std::log2(want / lum), -20., 20.);
  }
  return written(v);
}

}  // namespace pb
