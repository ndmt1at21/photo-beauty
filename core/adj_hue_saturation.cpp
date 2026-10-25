// Hue/Saturation (D35), on the CPU: Image › Adjustments › Hue/Saturation and its adjustment layer, and its range
// eyedropper (span_from_pick()). GIMP's model of Photoshop's dialog (gimp_operation_hue_saturation_process()) in
// Photoshop's ranges, a hypothesis throughout (below): each pixel in HSL (L = (max + min) / 2, S its chroma max - min
// over the most there is at that L, hue in sextants of 60°); Master's hue, saturation and lightness plus each range's
// weighted by where the pixel's hue sits in its span; the hue turned (max and min kept, the middle channel moved),
// the chroma scaled about L, then mixed with white or black. Colorize: one colour's hue and saturation at the pixel's
// lightness. All in float with a division where HSL has one, no table: shaders/lib/adj_hue_saturation.slang does the
// same line by line, so both give the same bits (adjust.h says how a type goes).
#include "adjust.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace pb {

// The range eyedropper of Hue/Saturation (N37): the span [a, b, c, d] (degrees 0..359, as `span` keeps it) Photoshop's
// default widths make around `hue` (a 30° range between 30° falloffs, AdjustSpec.SPANS'), in whole degrees. hue: the
// HSL hue of the pixel picked, degrees (QColor::hslHueF() × 360; a grey pixel has none: nothing to pick). range_of_hue():
// the range whose span centres nearest hue, 1 Reds ... 6 Magentas (AdjustSpec.LISTS.hueRange's index), where the
// eyedropper goes from Master. Declared here: core/adjust.h is D31's; callers declare them as they are (api/d35.md).
std::array<double, 4> span_from_pick(double hue);
int range_of_hue(double hue);

namespace {

using nlohmann::json;

// ---- Hypotheses -----------------------------------------------------------------------------------------------------
// Adobe documents none of Hue/Saturation's formulas: D32's hue-* cases (B33) settle these, B38. GIMP's model has
// Master's values and the range's add up (hue, saturation, lightness alike), a grey pixel no hue (here: in no range;
// GIMP's undefined hue falls in Reds), and the steps hue, saturation, lightness in that order.

// Positive saturation: S to S / (1 - s), 1 at most (what circulates as Photoshop's: +100 saturates every colour
// fully), or GIMP 2.10's S (1 + s), 1 at most (Linear). Negative: S (1 + s) either way, -100 grey at L (Desaturate).
enum class SaturationUp { Reciprocal, Linear };
constexpr SaturationUp kSaturationUp = SaturationUp::Reciprocal;
// Lightness l: each channel mixed with white, v + (1 - v) l, or black, v (1 + l), which takes saturation away as it
// goes (what Photoshop's slider is known for); false: GIMP's, L moved the same way with S kept.
constexpr bool kLightnessMixes = true;
// Colorize: the colour's hue and saturation (0..100) at the pixel's lightness L, this mix of its max, min, R, G and B:
// HSL's (max + min) / 2 (Rec. 601's luma would be {0, 0, 0.299, 0.587, 0.114}); then Lightness as above.
constexpr double kColorizeLum[5] = {0.5, 0.5, 0, 0, 0};

// ---- Values ---------------------------------------------------------------------------------------------------------

constexpr const char* kSets[] = {"Master", "Reds", "Yellows", "Greens", "Cyans", "Blues", "Magentas"};
// AdjustSpec.SPANS: each range's span by default, degrees.
constexpr double kSpans[6][4] = {{315, 345, 15, 45},   {15, 45, 75, 105},    {75, 105, 135, 165},
                                 {135, 165, 195, 225}, {195, 225, 255, 285}, {255, 285, 315, 345}};

// A span is null (the range's own: AdjustSpec's SPANS) or four numbers, degrees.
bool span_valid(const json& j) {
  if (!j.is_array() || j.size() != 4) return false;
  for (const json& v : j)
    if (!v.is_number()) return false;
  return true;
}

// A range's set (Master's has no span): Hue -180..180 (Master's with Colorize the colour's, 0..360), Saturation
// (Colorize: 0..100), Lightness -100..100, span [a, b, c, d] (null: SPANS').
struct Set {
  double h = 0, s = 0, l = 0;
  json span;
  void fields(AdjFields& f, double hue_max) {
    f.number("h", h, 0, -180, hue_max);
    f.number("s", s, 0, -100, 100);
    f.number("l", l, 0, -100, 100);
    f.value("span", span, nullptr, span_valid);
  }
  bool none() const { return std::fmod(h, 360) == 0 && s == 0 && l == 0; }
};

// A set by a range's name (README-psd.md: what E33, D32 and M43 write), an object of a set's keys whose wrong ones make
// the whole fail.
bool valid_set(const json& j) {
  if (!j.is_object()) return false;
  Set s;
  AdjFields in(&j);
  s.fields(in, 360);
  return in.ok();
}

// AdjustSpec's: the preset's name, the range shown (Master, Reds...), one set at the top level (the mock's: h, s, l,
// span), the shown range's, and Colorize; and each range's set by its name. The preset's name changes nothing: the UI
// writes a preset's values (N37).
struct Values {
  std::string pre = "Default", rng = "Master";
  Set shown;
  bool colorize = false;
  json sets[7];  // by kSets, null for none
  void fields(AdjFields& f) {
    f.text("pre", pre, "Default");
    f.choice("rng", rng, {"Master", "Reds", "Yellows", "Greens", "Cyans", "Blues", "Magentas"});
    shown.fields(f, 180);
    f.flag("colorize", colorize, false);
    for (int i = 0; i < 7; i++) f.value(kSets[i], sets[i], nullptr, valid_set);
  }
};

// kSets[i]'s set: its object; else, for the range shown (rng), the top level's; else none.
Set set_of(const Values& v, int i) {
  Set s;
  if (!v.sets[i].is_null()) {
    AdjFields in(&v.sets[i]);
    s.fields(in, 360);
  } else if (v.rng == kSets[i]) {
    s = v.shown;
  }
  return s;
}

// Degrees as a turn of -180..180: what a hue shift of h does.
double shift(double h) {
  h = std::fmod(h, 360);
  return h > 180 ? h - 360 : h <= -180 ? h + 360 : h;
}
double turn(double h) {  // 0..360
  h = std::fmod(h, 360);
  return h < 0 ? h + 360 : h;
}

// ---- Pixels ---------------------------------------------------------------------------------------------------------

// Below this a channel is 0 (and NaN, and below 0; past 1 it is 1): differences and quotients then stay clear of the
// subnormals the GPU may flush (blend.slang). 2^-64, adj_hue_saturation.slang's kHueSaturationTiny.
constexpr float kTiny = 0x1p-64f;
inline float in(float v) { return v >= kTiny ? (v < 1 ? v : 1) : 0; }

// The hue of c, max M, chroma C > 0, in sextants: 0 red, 2 green, 4 blue, to 6 (red again).
inline float hue6(const float* c, float M, float C) {
  if (M == c[0]) {
    const float h = (c[1] - c[2]) / C;
    return h < 0 ? h + 6 : h;
  }
  if (M == c[1]) return (c[2] - c[0]) / C + 2;
  return (c[0] - c[1]) / C + 4;
}

// How much of a span hue h (sextants) is in: s its a, then b, c, d from a (sextants), rb = 1 / b, rd = 1 / (d - c):
// rising from a to b, all of it from b to c, falling to d, going round past 6.
inline float weight(const Float4& s, float rb, float rd, float h) {
  float t = h - s[0];
  if (t < 0) t = t + 6;
  if (t < s[1]) return t * rb;
  if (t <= s[2]) return 1;
  if (t < s[3]) return (s[3] - t) * rd;
  return 0;
}

// A channel's value where its part of the chroma is k (hue y's: |y - 3| - 1 for R, 2 - |y - 2| G, 2 - |y - 4| B; 1 is
// the max, 0 the min, kept to the bit).
inline float part(float k, float M, float m, float C) { return k >= 1 ? M : k <= 0 ? m : m + C * k; }

// A span (degrees) as weight() reads it: a in 0..6, b, c and d from a, each the one before at least (once round).
void span6(const double* d, Float4& s, float& rb, float& rd) {
  const double a = turn(d[0]), b = turn(d[1] - d[0]), c = b + turn(d[2] - d[1]), e = c + turn(d[3] - d[2]);
  s = {float(a / 60), float(b / 60), float(c / 60), float(e / 60)};
  rb = s[1] > 0 ? float(1 / double(s[1])) : 0;
  rd = s[3] > s[2] ? float(1 / (double(s[3]) - double(s[2]))) : 0;
}

// Words (adj_hue_saturation.slang's): w[2] bit 0 Colorize, bit 1 kLightnessMixes, bit 2 kSaturationUp Linear; w[3]
// the ranges in the tables, n. Without Colorize: w[4] Master's hue (sextants), w[5] saturation, w[6] lightness (-1..1);
// tables 3 n entries, range j's at 3 j: its span (weight()'s a, b, c, d), then (rb, rd, hue, saturation), then
// (lightness, 0, 0, 0). With Colorize: w[4] saturation 0..1, w[5] lightness, w[6..10] kColorizeLum, w[11..13] each
// channel's part of the chroma less 1/2 at the colour's hue, w[14] the saturation times the lightness' chroma factor.
class HueSaturation final : public Adjustment {
 public:
  HueSaturation(const Values& v, const AdjustContext& c, std::string vals)
      : Adjustment(AdjType::HueSaturation, c, std::move(vals)) {
    colorize_ = v.colorize;
    if (colorize_) {  // Master's set: its object, else the one shown
      const Set s = !v.sets[0].is_null() ? set_of(v, 0) : v.shown;
      const double y = turn(s.h) / 60, k[3] = {std::fabs(y - 3) - 1, 2 - std::fabs(y - 2), 2 - std::fabs(y - 4)};
      for (int i = 0; i < 3; i++) part_[i] = float(std::clamp(k[i], 0., 1.) - 0.5);
      sat_ = float(std::clamp(s.s, 0., 100.) / 100);
      light_ = float(s.l / 100);
      const double g = light_ > 0 ? 1 - double(light_) : 1 + double(light_);
      sg_ = float(double(sat_) * g);
      for (int i = 0; i < 5; i++) lum_[i] = float(kColorizeLum[i]);
      return;
    }
    const Set master = set_of(v, 0);
    hue_ = float(shift(master.h) / 60), sat_ = float(master.s / 100), light_ = float(master.l / 100);
    for (int i = 1; i <= 6; i++) {
      const Set s = set_of(v, i);
      if (s.none()) continue;
      double d[4];
      for (int k = 0; k < 4; k++) d[k] = s.span.is_null() ? kSpans[i - 1][k] : s.span[size_t(k)].get<double>();
      Float4 sp;
      float rb, rd;
      span6(d, sp, rb, rd);
      tables_.push_back(sp);
      tables_.push_back({rb, rd, float(shift(s.h) / 60), float(s.s / 100)});
      tables_.push_back({float(s.l / 100), 0, 0, 0});
      ranges_++;
    }
  }
  bool identity() const override { return !colorize_ && !ranges_ && hue_ == 0 && sat_ == 0 && light_ == 0; }

 private:
  // Colorize, c coming in (0..1), M, m its max and min: adj_hue_saturation.slang's hue_saturation_colorize().
  void colorize(float* c, float M, float m) const {
    float L = lum_[0] * M + lum_[1] * m + lum_[2] * c[0] + lum_[3] * c[1] + lum_[4] * c[2];
    L = L > 0 ? (L < 1 ? L : 1) : 0;
    const float s2 = L + L, den = s2 <= 1 ? s2 : 2 - s2;
    float L2 = L;
    if (light_ > 0)
      L2 = L + (1 - L) * light_;
    else if (light_ < 0)
      L2 = L * (1 + light_);
    float q;
    if (kLightnessMixes) {
      q = den * sg_;
    } else {
      const float s3 = L2 + L2, den2 = s3 <= 1 ? s3 : 2 - s3;
      q = sat_ * den2;
    }
    for (int i = 0; i < 3; i++) c[i] = L2 + q * part_[i];
  }

  void adjust_row(float* px, int n, int, int) const override {
    const Float4* t = tables_.data();
    for (int i = 0; i < n; i++, px += 4) {
      float c[3] = {in(px[0]), in(px[1]), in(px[2])};
      const float M = std::max(std::max(c[0], c[1]), c[2]), m = std::min(std::min(c[0], c[1]), c[2]);
      if (colorize_) {
        colorize(c, M, m);
        px[0] = c[0], px[1] = c[1], px[2] = c[2];
        continue;
      }
      const float C = M - m, sum = M + m, L = sum * 0.5f, den = sum <= 1 ? sum : 2 - sum;
      // Master's and the ranges' the pixel's hue is in.
      float H = hue_, St = sat_, Lt = light_, h = -1;
      if (ranges_ && C > 0) {
        h = hue6(c, M, C);
        for (int j = 0; j < ranges_; j++) {
          const Float4 *s = t + 3 * j, &a = s[1];
          const float w = weight(s[0], a[0], a[1], h);
          H = H + w * a[2];
          St = St + w * a[3];
          Lt = Lt + w * s[2][0];
        }
        St = St > -1 ? (St < 1 ? St : 1) : -1;
        Lt = Lt > -1 ? (Lt < 1 ? Lt : 1) : -1;
      }
      // The hue turned: max and min stay, the middle channel moves.
      if (C > 0 && H != 0) {
        if (h < 0) h = hue6(c, M, C);
        float y = h + H;
        for (int k = 0; k < 8 && y >= 6; k++) y = y - 6;
        for (int k = 0; k < 8 && y < 0; k++) y = y + 6;
        c[0] = part(std::fabs(y - 3) - 1, M, m, C);
        c[1] = part(2 - std::fabs(y - 2), M, m, C);
        c[2] = part(2 - std::fabs(y - 4), M, m, C);
      }
      // The chroma about L by k (saturation) and g (lightness), L to L2.
      float k = 1;
      if (St < 0) {
        k = 1 + St;
      } else if (St > 0 && C > 0) {
        const float top = den / C;  // to S = 1
        k = St >= 1 ? top : std::min(kSaturationUp == SaturationUp::Linear ? 1 + St : 1 / (1 - St), top);
      }
      float L2 = L, g = 1;
      if (Lt > 0) {
        L2 = L + (1 - L) * Lt;
        g = 1 - Lt;
      } else if (Lt < 0) {
        g = 1 + Lt;
        L2 = L * g;
      }
      if (!kLightnessMixes && Lt != 0 && C > 0) {
        const float s2 = L2 + L2, den2 = s2 <= 1 ? s2 : 2 - s2;
        g = den2 / den;
      }
      const float q = k * g;
      if (q != 1 || L2 != L)
        for (int j = 0; j < 3; j++) c[j] = L2 + (c[j] - L) * q;
      px[0] = c[0], px[1] = c[1], px[2] = c[2];
    }
  }

  void pack_words(AdjustParams& p) const override {
    p.w[2] = (colorize_ ? 1u : 0u) | (kLightnessMixes ? 2u : 0u) | (kSaturationUp == SaturationUp::Linear ? 4u : 0u);
    p.w[3] = uint32_t(ranges_);
    if (colorize_) {
      p.w[4] = word(sat_), p.w[5] = word(light_);
      for (int i = 0; i < 5; i++) p.w[size_t(6 + i)] = word(lum_[i]);
      for (int i = 0; i < 3; i++) p.w[size_t(11 + i)] = word(part_[i]);
      p.w[14] = word(sg_);
    } else {
      p.w[4] = word(hue_), p.w[5] = word(sat_), p.w[6] = word(light_);
    }
  }

  bool colorize_ = false;
  int ranges_ = 0;
  float hue_ = 0, sat_ = 0, light_ = 0;  // Master's (Colorize: saturation 0..1, lightness)
  float part_[3] = {}, lum_[5] = {}, sg_ = 0;  // Colorize's
};

}  // namespace

AdjustmentPtr adj::hue_saturation(const json& vals, const AdjustContext& c) {
  return make_adjustment<Values, HueSaturation>(vals, c);
}

// ---- The eyedropper -------------------------------------------------------------------------------------------------

std::array<double, 4> span_from_pick(double hue) {
  const double h = std::round(turn(hue));
  std::array<double, 4> s = {h - 45, h - 15, h + 15, h + 45};
  for (double& v : s) v = turn(v);
  return s;
}

int range_of_hue(double hue) { return int(std::lround(turn(hue) / 60)) % 6 + 1; }

}  // namespace pb
