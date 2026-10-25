// Levels (D33), on the CPU: Image › Adjustments › Levels and its adjustment layer, and its eyedroppers
// (levels_from_pick()). A set {in0, gam, in1, out0, out1}, levels 0..255, maps a value v as Photoshop's dialog shows:
// u = clamp((v - in0) / (in1 - in0), 0, 1), then u^(1 / gam), then out0 + that × (out1 - out0); Red's, Green's and
// Blue's each on its own channel, RGB's on all three after them (kChannelsFirst). The power is a table made here, a
// curve a set; shaders/lib/adj_levels.slang does the rest line by line, so both give the same bits (adjust.h says how a
// type goes).
#include "adjust.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string>

#include "histogram.h"

namespace pb {

// Levels' eyedroppers (N37) and D41's Auto: vals (Levels' JSON) with the black point (point 0: Set Black Point), the
// grey one (1: Set Gray Point) or the white one (2: Set White Point) set from `sample`, the pixel picked before the
// adjustment, R, G, B levels 0..255 (a 16-bit sample's v × 255 / 65535), so that it comes out as `target` (levels;
// Photoshop's targets by default: 0, 128 and 255 grey). Black and white: each channel's input point at the sample's
// value through what goes before that channel's set, its output point at the target through what goes after it
// (kChannelsFirst: RGB's set, undone), both rounded to whole levels as Photoshop keeps them. Grey: each channel's gamma
// such that the sample comes out neutral, at its own luminosity (kGrayKeepsLuminosity), unrounded; a channel it cannot
// get there (the sample at its set's ends) keeps its gamma. RGB's own set stays as it is. On a grey document (mode
// Gray) only RGB's set, from the sample's R: black and white as above, grey not at all (Photoshop has none there). Each
// set changed is written where it came from (its object, or the top-level set of the channel shown, else a new object):
// the vals vals() would write. nullopt when vals do not read or point is not 0, 1 or 2. Any thread.
// Declared here: core/adjust.h is D31's; callers declare it as it is above (api/d33.md).
std::optional<std::string> levels_from_pick(const std::string& vals, int point, const std::array<double, 3>& sample,
                                            const std::array<double, 3>& target, Mode mode);

namespace {

using nlohmann::json;

// ---- Hypotheses -----------------------------------------------------------------------------------------------------

// The channels' own sets (Red, Green, Blue) first, RGB's on what they give, as GIMP's Levels (each channel, then
// Value: gimp_operation_levels_process()) and Curves (the colour curve over the channel's: gimp_curve_map_pixels())
// do. Adobe documents none; D32's levels-order-cube and curves-order-ramp (B33) say. adj_curves.cpp takes the same.
constexpr bool kChannelsFirst = true;
// Set Gray Point neutralises without changing the pixel's brightness: the sample comes out at the target's colour at
// the luminosity it had (kLumPercent's mix of what the Levels gave it), the target's own brightness unused (grey 128
// moves a light grey no darker). Photoshop greys the eyedropper out on Grayscale documents, where an eyedropper that
// only removed colour would do nothing; Adobe gives no formula. To check against Photoshop (B33, N37).
constexpr bool kGrayKeepsLuminosity = true;

// ---- Values ---------------------------------------------------------------------------------------------------------

constexpr const char* kSets[] = {"RGB", "Red", "Green", "Blue"};  // AdjustSpec.LISTS.chan, README-psd.md's sets
constexpr double kGap = 2;  // AdjustSpec's limits(): the dialog keeps in1 at least 2 levels past in0

// A channel's set, levels 0..255: AdjustSpec's FIELDS, in0 0..253, gam 0.01..9.99, in1 2..255, out0 and out1 0..255.
struct Set {
  double in0 = 0, gam = 1, in1 = 255, out0 = 0, out1 = 255;
  void fields(AdjFields& f) {
    f.number("in0", in0, 0, 0, 253);
    f.number("gam", gam, 1, 0.01, 9.99);
    f.number("in1", in1, 255, 2, 255);
    f.number("out0", out0, 0, 0, 255);
    f.number("out1", out1, 255, 0, 255);
  }
  bool identity() const { return in0 == 0 && gam == 1 && in1 == 255 && out0 == 0 && out1 == 255; }
  // in1 as it works: kGap past in0 at least (a file's in1 below in0 + 2 is as the dialog would have it).
  double white() const { return std::max(in1, in0 + kGap); }
  // v (levels) through the set, in double: what the table holds.
  double map(double v) const {
    const double u = std::clamp((v - in0) / (white() - in0), 0., 1.);
    return out0 + std::pow(u, 1 / gam) * (out1 - out0);
  }
  // The input the set maps to y, or the nearest of its ends when its outputs do not reach y.
  double unmap(double y) const {
    const double q = out1 != out0 ? std::clamp((y - out0) / (out1 - out0), 0., 1.) : 0;
    return in0 + std::pow(q, gam) * (white() - in0);
  }
};

bool valid_set(const json& j) {
  if (!j.is_object()) return false;
  Set s;
  AdjFields in(&j);
  s.fields(in);
  return in.ok();
}

// AdjustSpec's: the preset's name, the channel shown, and one set at the top level (the mock's: in0, gam, in1, out0,
// out1, FIELDS'), the shown channel's; and each channel's set by its name (README-psd.md: what E33, D32 and M43
// write), an object of the same keys whose wrong ones make the whole fail. The preset's name changes nothing: the UI
// writes a preset's values (N37).
struct Values {
  std::string pre = "Default", ch = "RGB";
  Set shown;
  json sets[4];  // by kSets, null for none
  void fields(AdjFields& f) {
    f.text("pre", pre, "Default");
    f.choice("ch", ch, {"RGB", "Red", "Green", "Blue"});
    shown.fields(f);
    for (int i = 0; i < 4; i++) f.value(kSets[i], sets[i], nullptr, valid_set);
  }
};

// kSets[i]'s set: its object; else, for the channel shown (ch), the top-level set; else the identity. `from` says
// which: 0 the object, 1 the top level, 2 none.
Set set_of(const Values& v, int i, int* from = nullptr) {
  Set s;
  int f = 2;
  if (!v.sets[i].is_null()) {
    AdjFields in(&v.sets[i]);
    s.fields(in);
    f = 0;
  } else if (v.ch == kSets[i]) {
    s = v.shown, f = 1;
  }
  if (from) *from = f;
  return s;
}

std::string written(Values v) {
  AdjFields out(nullptr);
  v.fields(out);
  return out.written();
}

// ---- The adjustment -------------------------------------------------------------------------------------------------

// Subnormals, which the GPU may flush (blend.slang), never come of the tables: an entry below kTinyEntry is 0, and a
// set's u below kTiny (kTinyRoot for one along roots) is 0, so the interpolation's products stay normal on both sides.
// What that drops is below a 16-bit level but after a set with a gamma near 9.99 (2^-100 is 2^-10 there): a channel
// darkened to 0.01 and RGB brightened back come out within 1e-3 of the formula, where the others are within 1e-4.
constexpr double kTinyEntry = 0x1p-100;
constexpr float kTiny = 0x1p-30f, kTinyRoot = 0x1p-118f;  // adj_levels.slang's kLevelsTiny, kLevelsTinyRoot

// A set as it runs on a channel: u = clamp((v - a) k, 0, 1), its root of degree 2^roots, its curve there in the table.
// A gamma past 1 makes u^(1 / gam) steep at 0, its slope infinite: along s = u^(1 / 2^r) the curve is s^(2^r / gam),
// whose slope stays finite once 2^r >= gam (up to 4 square roots at 9.99), gentle enough for 4,096 steps. A gamma up
// to 1 runs along u (its worst, 0.01, within 7.3e-5 of the formula by 1).
struct Stage {
  bool on = false;
  int roots = 0;
  float a = 0, k = 1;
};

// Words (adj_levels.slang's): w[2 + 2s], w[3 + 2s] stage s's a and k (s: 0 Red, 1 Green, 2 Blue, 3 RGB); w[10] bit s
// stage s on; w[11] 1 when RGB's stage goes first (kChannelsFirst false); w[12 + s] stage s's roots. One table, 4,098
// entries: stage s's curve in component s (R, G, B, RGB in x, y, z, w), levels / 255.
class Levels final : public Adjustment {
 public:
  Levels(const Values& v, const AdjustContext& c, std::string vals) : Adjustment(AdjType::Levels, c, std::move(vals)) {
    Set sets[4];  // by stage
    bool any = false;
    for (int s = 0; s < 4; s++) {
      if (s < 3 && c.mode == Mode::Gray) continue;  // a grey document has RGB's set alone
      sets[s] = set_of(v, s < 3 ? s + 1 : 0);
      if (sets[s].identity()) continue;
      Stage& t = stage_[s];
      t.on = any = true;
      while (std::ldexp(1.0, t.roots) < sets[s].gam) t.roots++;
      t.a = sets[s].in0 / 255 < kTinyEntry ? 0.f : float(sets[s].in0 / 255);
      t.k = float(255 / (sets[s].white() - sets[s].in0));
    }
    if (!any) return;
    tables_.assign(size_t(kCurveSteps) + 2, Float4{});
    for (int i = 0; i <= kCurveSteps; i++) {
      const double t = double(i) / kCurveSteps;
      for (int s = 0; s < 4; s++) {
        double y = t;  // a stage that is not on: unread
        if (stage_[s].on) {
          const Set& set = sets[s];  // at u = t^(2^roots)
          y = (set.out0 + std::pow(t, std::ldexp(1.0, stage_[s].roots) / set.gam) * (set.out1 - set.out0)) / 255;
        }
        tables_[size_t(i)][size_t(s)] = std::fabs(y) < kTinyEntry ? 0.f : float(y);
      }
    }
    tables_[size_t(kCurveSteps) + 1] = tables_[size_t(kCurveSteps)];
  }
  bool identity() const override { return tables_.empty(); }

 private:
  // Stage s of value v: adj_levels.slang's levels_stage().
  float stage(int s, float v) const {
    const Stage& t = stage_[s];
    if (!t.on) return v;
    const float d = v - t.a;
    const float e = d * t.k;
    float u = e >= (t.roots ? kTinyRoot : kTiny) ? (e < 1 ? e : 1) : 0;  // and NaN, below 0
    for (int r = 0; r < t.roots; r++) u = std::sqrt(u);
    return adjust_curve(tables_.data(), u)[size_t(s)];
  }
  void adjust_row(float* px, int n, int, int) const override {
    for (int i = 0; i < n; i++, px += 4)
      for (int c = 0; c < 3; c++) px[c] = kChannelsFirst ? stage(3, stage(c, px[c])) : stage(c, stage(3, px[c]));
  }
  void pack_words(AdjustParams& p) const override {
    uint32_t flags = 0;
    for (int s = 0; s < 4; s++) {
      p.w[size_t(2 + 2 * s)] = word(stage_[s].a);
      p.w[size_t(3 + 2 * s)] = word(stage_[s].k);
      p.w[size_t(12 + s)] = uint32_t(stage_[s].roots);
      flags |= (stage_[s].on ? 1u : 0u) << s;
    }
    p.w[10] = flags;
    p.w[11] = kChannelsFirst ? 0 : 1;
  }

  Stage stage_[4];
};

}  // namespace

AdjustmentPtr adj::levels(const json& vals, const AdjustContext& c) { return make_adjustment<Values, Levels>(vals, c); }

// ---- The eyedroppers ------------------------------------------------------------------------------------------------

std::optional<std::string> levels_from_pick(const std::string& vals, int point, const std::array<double, 3>& sample,
                                            const std::array<double, 3>& target, Mode mode) {
  const json j = json::parse(vals, nullptr, false);
  if (!j.is_object() || point < 0 || point > 2) return std::nullopt;
  Values v;
  {
    AdjFields in(&j);
    v.fields(in);
    if (!in.ok()) return std::nullopt;
  }
  int from[4];
  Set sets[4];  // by kSets
  for (int i = 0; i < 4; i++) sets[i] = set_of(v, i, &from[i]);
  const bool grey = mode == Mode::Gray;
  if (grey && point == 1) return written(v);

  // Channel c's value x (sample) through the stages before its set, and the output y it is to give (target) back
  // through those after it; grey: RGB's set alone, from R.
  const Set& rgb = sets[0];
  const auto before = [&](double x) { return grey || kChannelsFirst ? x : rgb.map(x); };
  const auto after = [&](double y) { return grey || !kChannelsFirst ? y : rgb.map(y); };
  const auto unafter = [&](double y) { return grey || !kChannelsFirst ? y : rgb.unmap(y); };
  const int n = grey ? 1 : 3;
  double want[3] = {target[0], target[1], target[2]};
  if (point == 1) {  // the sample's luminosity as the Levels leave it, the target's colour
    double lum = 0, tlum = 0;
    for (int c = 0; c < 3; c++) {
      const double w = kLumPercent[c] / 100.;
      lum += w * after(sets[c + 1].map(before(sample[size_t(c)])));
      tlum += w * target[size_t(c)];
    }
    for (int c = 0; c < 3; c++)
      want[c] = std::clamp(kGrayKeepsLuminosity ? lum + target[size_t(c)] - tlum : target[size_t(c)], 0., 255.);
  }
  for (int c = 0; c < n; c++) {
    const int i = grey ? 0 : c + 1;
    Set& s = sets[i];
    const double x = before(sample[size_t(c)]), y = unafter(want[c]);
    if (point == 0) {
      s.in0 = std::clamp(std::round(x), 0., 253.);
      s.out0 = std::clamp(std::round(y), 0., 255.);
      s.in1 = std::max(s.in1, s.in0 + kGap);
    } else if (point == 2) {
      s.in1 = std::clamp(std::round(x), 2., 255.);
      s.out1 = std::clamp(std::round(y), 0., 255.);
      s.in0 = std::min(s.in0, s.in1 - kGap);
    } else {
      const double u = (x - s.in0) / (s.white() - s.in0), q = s.out1 != s.out0 ? (y - s.out0) / (s.out1 - s.out0) : 0;
      if (u > 0 && u < 1 && q > 0 && q < 1) s.gam = std::clamp(std::log(u) / std::log(q), 0.01, 9.99);
    }
    if (from[i] == 1) {
      v.shown = s;
    } else {
      AdjFields out(nullptr);
      s.fields(out);
      v.sets[i] = s.identity() ? json() : json::parse(out.written());
    }
  }
  return written(v);
}

}  // namespace pb
