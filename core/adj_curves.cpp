// Curves (D33), on the CPU: Image › Adjustments › Curves and its adjustment layer. pts holds each channel's curve by
// its name (RGB, Red, Green, Blue): points [[in, out], ...], levels 0..255, inputs increasing, 2 to kMaxPoints of
// them, a natural cubic spline through them (spline()), flat past the first and the last, clamped to 0..255; or 256
// outputs, one for each input level (a curve drawn with the pencil: PSD's mapping), straight between them. Each
// channel's curve on its own channel, RGB's on all three after them, as Levels' sets go (kChannelsFirst). The curves
// are a table made here, each along its own span of inputs; shaders/lib/adj_curves.slang does the rest line by line,
// so both give the same bits (adjust.h says how a type goes).
#include "adjust.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace pb {
namespace {

using nlohmann::json;

// ---- Hypotheses -----------------------------------------------------------------------------------------------------

// A curve through its points is the natural cubic spline (second derivative 0 at the first and the last point:
// spline()), the curve said to be Photoshop's (moving a point moves the whole curve), and the one Krita's and
// Paint.NET's curves draw for Photoshop's points. It need not be monotone: points steep by an end swing it
// (curves-edges' (0, 0), (8, 32), (247, 223), (255, 255) rises to 145 at 64, falls to 110 at 192, rises again). D32's
// curves-* (B33) say. A mapping (256 outputs) is straight between its outputs, at the levels' values k / 255.

// At most this many points a curve: what Photoshop's dialog takes (Adobe's .acv and PSD's curves hold 2 to 19); a curve
// of more does not read.
constexpr size_t kMaxPoints = 16;
// Each channel's curve first, RGB's on what it gives: adj_levels.cpp's kChannelsFirst, the same order.
constexpr bool kChannelsFirst = true;

// ---- Values ---------------------------------------------------------------------------------------------------------

constexpr const char* kChannels[] = {"RGB", "Red", "Green", "Blue"};  // AdjustSpec.LISTS.chan, pts' names

// A channel's curve, levels 0..255: its points and the spline's second derivatives at them; or a mapping's 256
// outputs (x empty).
struct Curve {
  std::vector<double> x, y, m;

  bool identity() const {
    for (size_t i = 0; i < y.size(); i++)
      if (y[i] != (x.empty() ? double(i) : x[i])) return false;
    return x.empty() || (x.front() == 0 && x.back() == 255);
  }
  // At v (levels), not clamped: a mapping straight between its outputs; points flat past the ends, the spline between.
  double at(double v) const {
    if (x.empty()) {
      v = std::clamp(v, 0., 255.);
      const int i = std::min(int(v), 254);
      return y[size_t(i)] + (y[size_t(i) + 1] - y[size_t(i)]) * (v - i);
    }
    if (v <= x.front()) return y.front();
    if (v >= x.back()) return y.back();
    const size_t i = size_t(std::upper_bound(x.begin(), x.end(), v) - x.begin()) - 1;  // x[i] <= v < x[i + 1]
    const double h = x[i + 1] - x[i], a = (x[i + 1] - v) / h, b = 1 - a;
    return a * y[i] + b * y[i + 1] + ((a * a * a - a) * m[i] + (b * b * b - b) * m[i + 1]) * (h * h / 6);
  }
};

// The second derivatives of the natural spline through (x, y), 0 at the ends (the hypothesis above): the tridiagonal
// system solved by elimination (diagonally dominant: no pivoting).
std::vector<double> spline(const std::vector<double>& x, const std::vector<double>& y) {
  const size_t n = x.size();
  std::vector<double> m(n), c(n), d(n);  // c, d: the eliminated system's upper diagonal and right side
  for (size_t i = 1; i + 1 < n; i++) {
    const double h0 = x[i] - x[i - 1], h1 = x[i + 1] - x[i];
    const double b = 2 * (h0 + h1) - h0 * c[i - 1];
    c[i] = h1 / b;
    d[i] = (6 * ((y[i + 1] - y[i]) / h1 - (y[i] - y[i - 1]) / h0) - h0 * d[i - 1]) / b;
  }
  for (size_t i = n - 1; i-- > 1;) m[i] = d[i] - c[i] * m[i + 1];
  return m;
}

// pts[name] as a curve: 2 to kMaxPoints points [in, out], each number clamped to 0..255, the inputs then increasing;
// or 256 outputs. False for anything else.
bool read_curve(const json& j, Curve& c) {
  if (!j.is_array()) return false;
  if (j.size() == 256 && std::all_of(j.begin(), j.end(), [](const json& e) { return e.is_number(); })) {
    for (const json& e : j) c.y.push_back(std::clamp(e.get<double>(), 0., 255.));
    return true;
  }
  if (j.size() < 2 || j.size() > kMaxPoints) return false;
  for (const json& p : j) {
    if (!p.is_array() || p.size() != 2 || !p[0].is_number() || !p[1].is_number()) return false;
    const double x = std::clamp(p[0].get<double>(), 0., 255.);
    if (!c.x.empty() && !(x > c.x.back())) return false;
    c.x.push_back(x);
    c.y.push_back(std::clamp(p[1].get<double>(), 0., 255.));
  }
  c.m = spline(c.x, c.y);
  return true;
}

// pts: an object whose channels (kChannels; other names are skipped, null is none) read as curves.
bool valid_pts(const json& j) {
  if (!j.is_object()) return false;
  for (const char* name : kChannels) {
    const auto it = j.find(name);
    Curve c;
    if (it != j.end() && !it->is_null() && !read_curve(*it, c)) return false;
  }
  return true;
}

// AdjustSpec's: the preset's name, the channel shown, each channel's points by its name. A channel without a curve
// of its own is the identity: the UI writes a preset's points (AdjustSpec's curvePoints() draws the mock's preset for
// RGB without them: N37), the preset's name changing nothing.
struct Values {
  std::string pre = "Default", ch = "RGB";
  json pts = json::object();
  void fields(AdjFields& f) {
    f.text("pre", pre, "Default");
    f.choice("ch", ch, {"RGB", "Red", "Green", "Blue"});
    f.value("pts", pts, json::object(), valid_pts);
  }
};

// ---- The adjustment -------------------------------------------------------------------------------------------------

// Subnormals, which the GPU may flush (blend.slang), never come of the table: an entry below kTinyEntry is 0, and a
// curve's t below kTiny is 0, so the interpolation's products stay normal on both sides (adj_levels.cpp's).
constexpr double kTinyEntry = 0x1p-100;
constexpr float kTiny = 0x1p-30f;  // adj_curves.slang's kCurvesTiny

// A curve as it runs on a channel: its table at t = (v - a) k. Points: their span as 0..1, so the table's ends are the
// first and the last point, the curve's corners there on entries; the spline unclamped in between, its corners where
// it leaves 0..255 made exactly by the clamp after it (apply()'s, adjust_pixel()'s, and the next stage's, whose t
// clamps as that of 0 or 1 would: past 0..1 a value goes no further than 0 or 1 does). A mapping: t = 255 v / 256,
// so that output k sits on entry 16 k, its corners all on entries (entries past 4,080 repeat the last).
constexpr int kMappingStep = kCurveSteps / 256;  // entries a level of a mapping
struct Stage {
  bool on = false;
  float a = 0, k = 1;
};

// Words (adj_curves.slang's): w[2 + 2s], w[3 + 2s] stage s's a and k (s: 0 Red, 1 Green, 2 Blue, 3 RGB); w[10] bit s
// stage s on; w[11] 1 when RGB's stage goes first (kChannelsFirst false). One table, 4,098 entries: stage s's curve in
// component s (R, G, B, RGB in x, y, z, w), levels / 255.
class Curves final : public Adjustment {
 public:
  Curves(const Values& v, const AdjustContext& c, std::string vals) : Adjustment(AdjType::Curves, c, std::move(vals)) {
    Curve curves[4];  // by stage
    bool any = false;
    for (int s = 0; s < 4; s++) {
      if (s < 3 && c.mode == Mode::Gray) continue;  // a grey document has RGB's curve alone
      const auto it = v.pts.find(kChannels[s < 3 ? s + 1 : 0]);
      if (it == v.pts.end() || it->is_null() || !read_curve(*it, curves[s]) || curves[s].identity()) continue;
      Stage& t = stage_[s];
      t.on = any = true;
      if (curves[s].x.empty()) {
        t.k = 255.f / 256;  // exactly
      } else {
        t.a = curves[s].x.front() / 255 < kTinyEntry ? 0.f : float(curves[s].x.front() / 255);
        t.k = float(255 / (curves[s].x.back() - curves[s].x.front()));
      }
    }
    if (!any) return;
    tables_.assign(size_t(kCurveSteps) + 2, Float4{});
    for (int i = 0; i <= kCurveSteps; i++) {
      const double t = double(i) / kCurveSteps;
      for (int s = 0; s < 4; s++) {
        double y = t;  // a stage that is not on: unread
        if (stage_[s].on) {
          const Curve& cv = curves[s];
          const double x = cv.x.empty()      ? double(std::min(i, 255 * kMappingStep)) / kMappingStep
                           : i == kCurveSteps ? cv.x.back()
                                              : cv.x.front() + (cv.x.back() - cv.x.front()) * t;
          y = cv.at(x) / 255;
        }
        tables_[size_t(i)][size_t(s)] = std::fabs(y) < kTinyEntry ? 0.f : float(y);
      }
    }
    tables_[size_t(kCurveSteps) + 1] = tables_[size_t(kCurveSteps)];
  }
  bool identity() const override { return tables_.empty(); }

 private:
  // Stage s of value v: adj_curves.slang's curves_stage().
  float stage(int s, float v) const {
    const Stage& t = stage_[s];
    if (!t.on) return v;
    const float d = v - t.a;
    const float e = d * t.k;
    const float u = e >= kTiny ? (e < 1 ? e : 1) : 0;  // and NaN, below 0
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
      flags |= (stage_[s].on ? 1u : 0u) << s;
    }
    p.w[10] = flags;
    p.w[11] = kChannelsFirst ? 0 : 1;
  }

  Stage stage_[4];
};

}  // namespace

AdjustmentPtr adj::curves(const json& vals, const AdjustContext& c) { return make_adjustment<Values, Curves>(vals, c); }

}  // namespace pb
