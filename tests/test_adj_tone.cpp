// Brightness/Contrast and Exposure (D34: core/adj_brightness_contrast.cpp, core/adj_exposure.cpp). Use Legacy against
// its formula in double precision on the 33³ lattice and 16-bit ramps and against Adobe's own numbers (Contrast +100
// and -100, Brightness +100 over -100), its ranges; the default's curve as its hypothesis shapes it (black and white
// kept, monotone, Brightness lifting or lowering, Contrast an S about 0.5, each channel alike); Exposure against its
// formula through the profiles' curves (sRGB's, Adobe RGB's, ProPhoto's, a grey one's): +1 doubling linear light,
// Offset adding to it, Gamma's power; the eyedroppers (exposure_from_pick()); the GPU the CPU's to the bit
// (adjust_check.h); Photoshop's images (D32's bc-*, exposure-*, once B33 has made them), with the default's deviation
// and the other hypotheses' printed for B38.
#include <lcms2.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "adjust.h"
#include "adjust_check.h"
#include "check.h"
#include "color.h"
#include "golden.h"

namespace pb {
// core/adj_exposure.cpp's (D34): declared where it is called.
std::optional<std::string> exposure_from_pick(const std::string& vals, int point, const std::array<double, 3>& sample,
                                              const AdjustContext& context);
}  // namespace pb

using namespace pb;
using nlohmann::json;

namespace {

using RGB = std::array<float, 3>;

// The 33³ lattice, the grey 16-bit ramp and one with R, G, B apart, as samples become floats (v / max).
std::vector<RGB> colours() {
  std::vector<RGB> px;
  for (int b = 0; b <= 32; b++)
    for (int g = 0; g <= 32; g++)
      for (int r = 0; r <= 32; r++) px.push_back({r / 32.f, g / 32.f, b / 32.f});
  const auto u = [](uint32_t s) { return float(double(s & 0xffff) / 65535); };
  for (uint32_t v = 0; v < 65536; v++) {
    px.push_back({u(v), u(v), u(v)});
    px.push_back({u(v), u(65535 - v), u(v * 40503u + 12345u)});
  }
  return px;
}

// The grey 16-bit ramp alone.
std::vector<RGB> grey_ramp() {
  std::vector<RGB> px(65536);
  for (size_t v = 0; v < px.size(); v++) {
    const float u = float(double(v) / 65535);
    px[v] = {u, u, u};
  }
  return px;
}

// px through a, alpha set apart (apply() leaves it).
std::vector<RGB> applied(const Adjustment& a, const std::vector<RGB>& px) {
  std::vector<float> buf(px.size() * 4);
  for (size_t i = 0; i < px.size(); i++)
    buf[4 * i] = px[i][0], buf[4 * i + 1] = px[i][1], buf[4 * i + 2] = px[i][2], buf[4 * i + 3] = 0.25f;
  a.apply(buf.data(), int(px.size()), 0, 0);
  std::vector<RGB> out(px.size());
  for (size_t i = 0; i < px.size(); i++) {
    out[i] = {buf[4 * i], buf[4 * i + 1], buf[4 * i + 2]};
    CHECK(buf[4 * i + 3] == 0.25f);
  }
  return out;
}

AdjustmentPtr bc(const json& vals, const AdjustContext& c = {}) {
  const AdjustmentPtr a = adjustment("Brightness/Contrast", vals.dump(), c);
  CHECK(a);
  return a;
}
AdjustmentPtr exposure(const json& vals, const AdjustContext& c = {}) {
  const AdjustmentPtr a = adjustment("Exposure", vals.dump(), c);
  CHECK(a);
  return a;
}

// ---- Use Legacy -----------------------------------------------------------------------------------------------------

// Use Legacy in double precision (adj_brightness_contrast.cpp's hypotheses): about 0.5 by k of c, b / 255 before a
// scaling up and after a scaling down, a step at c 100.
double legacy(double v, double b, double c) {
  const double p = 0.5, s = b / 255;
  if (c >= 100) return v + s >= p ? 1 : 0;
  const double k = c > 0 ? 100 / (100 - c) : (100 + c) / 100;
  return std::clamp(c < 0 ? (v - p) * k + p + s : (v + s - p) * k + p, 0., 1.);
}

}  // namespace

// Use Legacy against its formula on every colour, Adobe's numbers for it, and its ranges (its dialog's -100..100).
TEST(test_bc_legacy) {
  const std::vector<RGB> px = colours();
  double worst = 0;
  int n = 0;
  // Whole brightnesses: the step's edge, (127.5 - b) / 255, is no sample's, so float and double agree on it.
  for (double b : {-100., -50., -1., 0., 30., 100.})
    for (double c : {-100., -60., -1., 0., 25., 50., 99., 100.}) {
      if (b == 0 && c == 0) continue;
      const AdjustmentPtr a = bc({{"b", b}, {"c", c}, {"legacy", true}});
      CHECK(!a->identity() && a->tables().empty());
      const std::vector<RGB> out = applied(*a, px);
      for (size_t i = 0; i < px.size(); i++)
        for (size_t ch = 0; ch < 3; ch++) {
          const double want = legacy(px[i][ch], b, c);
          worst = std::max(worst, std::fabs(out[i][ch] - want));
          if (c == 100) CHECK(out[i][ch] == float(want));  // a step: 0 or 1 exactly
        }
      n++;
    }
  std::printf("test_bc_legacy: %d settings within %.3g of the formula\n", n, worst);
  CHECK(worst < 1e-5);

  // Adobe's: Contrast +100 makes every value 0 or 255 (127 down, 128 up), -100 makes them 127 (127.5, truncated),
  // -100 with Brightness +100 227 (227.5); Brightness alone adds, clamping.
  const auto at = [](const AdjustmentPtr& a, double level) { return double(a->apply({float(level / 255), 0, 1})[0]); };
  const AdjustmentPtr step = bc({{"c", 100}, {"legacy", true}});
  for (int v = 0; v < 256; v++) CHECK(at(step, v) == (v >= 128 ? 1 : 0));
  const AdjustmentPtr flat = bc({{"c", -100}, {"legacy", true}});
  const AdjustmentPtr lifted = bc({{"b", 100}, {"c", -100}, {"legacy", true}});
  for (int v = 0; v < 256; v++) {
    CHECK(at(flat, v) == 0.5);
    CHECK(std::fabs(at(lifted, v) - 227.5 / 255) < 1e-6);
  }
  const AdjustmentPtr plus = bc({{"b", 50}, {"legacy", true}});
  for (int v = 0; v < 256; v++) CHECK(std::fabs(at(plus, v) - std::min(v + 50, 255) / 255.) < 1e-6);

  // Ranges: Use Legacy's dialog stops at -100..100 both, the default's at -150..150 and -50..100.
  CHECK(bc({{"b", 150}, {"legacy", true}})->vals() == R"({"b":100,"legacy":true})");
  CHECK(bc({{"b", -150}, {"legacy", true}})->vals() == R"({"b":-100,"legacy":true})");
  CHECK(bc({{"c", -100}, {"legacy", true}})->vals() == R"({"c":-100,"legacy":true})");
  CHECK(bc({{"c", -100}})->vals() == R"({"c":-50})" && bc({{"b", -150}})->vals() == R"({"b":-150})");
  CHECK(bc({{"c", 150}, {"legacy", true}})->vals() == R"({"c":100,"legacy":true})");
  CHECK(bc({{"legacy", true}})->identity() && bc({{"legacy", true}})->vals() == R"({"legacy":true})");
  CHECK(bc(json::object())->identity() && bc({{"b", 0}, {"c", 0}})->vals() == "{}");
}

// The default: one curve for every channel, keeping black and white, monotone; Brightness lifts (lowers) every value
// between, the more the higher it is; Contrast pulls values below 0.5 down and above it up (or the other way), 0.5
// staying, steeper at 0.5 the higher it is.
TEST(test_bc_curve) {
  const std::vector<RGB> ramp = grey_ramp();
  const auto curve = [&](double b, double c) {
    const AdjustmentPtr a = bc({{"b", b}, {"c", c}});
    CHECK(!a->identity() && a->tables().size() == size_t(kCurveSteps) + 2);
    const std::vector<RGB> out = applied(*a, ramp);
    std::vector<double> f(out.size());
    for (size_t i = 0; i < out.size(); i++) {
      CHECK(out[i][0] == out[i][1] && out[i][1] == out[i][2]);
      f[i] = out[i][0];
    }
    // Each channel alike, whatever the others hold.
    const RGB mix = a->apply({0.2f, 0.5f, 0.9f});
    CHECK(mix[0] == a->apply({0.2f, 0.2f, 0.2f})[0] && mix[1] == a->apply({0.5f, 0.5f, 0.5f})[0] &&
          mix[2] == a->apply({0.9f, 0.9f, 0.9f})[0]);
    return f;
  };
  int n = 0;
  double last_mid = 0, last_slope = 0;
  for (double b : {-150., -50., 0., 50., 150.})
    for (double c : {-50., 0., 50., 100.}) {
      if (b == 0 && c == 0) continue;
      const std::vector<double> f = curve(b, c);
      CHECK(f.front() == 0 && f.back() == 1);
      for (size_t i = 1; i < f.size(); i++) CHECK(f[i] >= f[i - 1]);
      for (size_t i = 1; i + 1 < f.size(); i++) {
        const double v = double(ramp[i][0]);
        if (c == 0) CHECK(b > 0 ? f[i] > v : f[i] < v);
        if (b == 0 && v != 0.5) CHECK((v < 0.5) == (c > 0 ? f[i] < v : f[i] > v));
      }
      n++;
    }
  // Monotone in its values: the midpoint up with Brightness, the slope there up with Contrast.
  for (double b : {-150., -100., -50., -10., 10., 50., 100., 150.}) {
    const double mid = curve(b, 0)[32768];
    CHECK(b == -150 || mid > last_mid);
    last_mid = mid;
  }
  for (double c : {-50., -25., 25., 50., 75., 100.}) {
    const std::vector<double> f = curve(0, c);
    const double slope = (f[33768] - f[31768]) / (2000 / 65535.);
    CHECK(c == -50 || slope > last_slope);
    CHECK((slope > 1) == (c > 0));
    last_slope = slope;
    CHECK(std::fabs(bc({{"c", c}})->apply({0.5f, 0.5f, 0.5f})[0] - 0.5f) == 0);
  }
  std::printf("test_bc_curve: %d curves keep black and white, monotone, lifting, lowering and bending as they should\n",
              n);
}

// ---- Exposure -------------------------------------------------------------------------------------------------------

namespace {

double srgb_encode(double y) { return y <= 0.0031308 ? 12.92 * y : 1.055 * std::pow(y, 1 / 2.4) - 0.055; }

// A document's curve in double precision: its profile's as color.h's source_profile() samples it, linear between the
// entries, and that line's inverse; or a plain gamma (src null), as test_tone_ps tries for B38. An sRGB v2 profile
// keeps its curve as a table of 16-bit steps: near black it lies up to 6 % under IEC 61966-2-1's formula (2.3e-5 at
// most in linear light), as the canvas and D30's Linear gradients take it too.
struct Curve {
  std::shared_ptr<const Source> src;
  double gamma = 0;
  double decode(double e, int ch = 0) const {
    e = std::clamp(e, 0., 1.);
    if (!src) return std::pow(e, gamma);
    const float* l = src->linear[ch];
    const double x = e * (Source::kLut - 1);
    const int i = std::min(int(x), Source::kLut - 2);
    return l[i] + (double(l[i + 1]) - l[i]) * (x - i);
  }
  double encode(double y, int ch = 0) const {
    if (!src) return std::pow(std::clamp(y, 0., 1.), 1 / gamma);
    const float* l = src->linear[ch];
    if (!(y > l[0])) return 0;
    if (y >= l[Source::kLut - 1]) return 1;
    const int i = int(std::upper_bound(l, l + Source::kLut, y, [](double a, float b) { return a < b; }) - l) - 1;
    return (i + (y - l[i]) / (double(l[i + 1]) - l[i])) / (Source::kLut - 1);
  }
};
Curve curve_of(const AdjustContext& c) {
  return {source_profile(c.icc && !c.icc->empty() ? *c.icc : icc_profile(ColorSpace::Srgb))};
}

// Exposure in double precision through curve k, channel ch.
double exposure_ref(double v, double e, double o, double g, const Curve& k, int ch = 0) {
  const double x = std::clamp(k.decode(v, ch) * std::exp2(e) + o, 0., 1.);
  return k.encode(std::pow(x, 1 / g), ch);
}

std::vector<uint8_t> grey_profile() {  // gamma 2.2
  cmsToneCurve* g = cmsBuildGamma(nullptr, 2.2);
  cmsHPROFILE p = cmsCreateGrayProfile(cmsD50_xyY(), g);
  cmsUInt32Number n = 0;
  CHECK(cmsSaveProfileToMem(p, nullptr, &n));
  std::vector<uint8_t> out(n);
  CHECK(cmsSaveProfileToMem(p, out.data(), &n));
  cmsCloseProfile(p);
  cmsFreeToneCurve(g);
  return out;
}

// Documents' colours; stepped: the curve a table of 16-bit steps (sRGB's v2 profile, and Display P3's, its curve).
struct Profile {
  const char* name;
  AdjustContext context;
  bool stepped;
};
std::vector<Profile> profiles() {
  const auto icc = [](ColorSpace s) { return std::make_shared<const std::vector<uint8_t>>(icc_profile(s)); };
  return {{"sRGB", {Mode::Rgb, nullptr}, true},
          {"Adobe RGB", {Mode::Rgb, icc(ColorSpace::AdobeRgb)}, false},
          {"ProPhoto", {Mode::Rgb, icc(ColorSpace::ProPhoto)}, false},
          {"Display P3", {Mode::Rgb, icc(ColorSpace::DisplayP3)}, true},
          {"grey 2.2", {Mode::Gray, std::make_shared<const std::vector<uint8_t>>(grey_profile())}, false},
          {"grey", {Mode::Gray, nullptr}, true}};
}

struct Setting {
  double e, o, g;
};
// Each value alone both ways (D32's exposure-*), all three, and the ends of each range.
constexpr Setting kSettings[] = {
    {1, 0, 1},         {-1.5, 0, 1},    {0, 0.1, 1},    {0, -0.05, 1}, {0, 0, 0.5},     {0, 0, 2},
    {0.7, -0.02, 1.3}, {3, 0, 1},       {-20, 0, 1},    {20, 0, 1},    {0, 0.5, 1},     {0, -0.5, 1},
    {2, -0.3, 9.99},   {-3, 0.2, 0.01}, {0, 0, 9.99},   {0, 0, 0.01},  {0.5, -0.1, 4},  {-1, 0.05, 0.3},
};

json exposure_vals(const Setting& s) { return {{"e", s.e}, {"o", s.o}, {"g", s.g}}; }

}  // namespace

// Exposure against its formula in double precision through each profile's curve as color.h samples it (4,096 entries,
// linear between), on every 16-bit value and the lattice's (each channel on its own: R = G = B covers them): within
// 5e-4 (an eighth of an 8-bit level) where that curve kinks under a second table of 4,096 steps across another span
// (an sRGB v2 profile's curve is a table of 16-bit steps, its slope jumping by 15 % between entries in the shadows; a
// plain gamma's first entries bend by 1.2 / k from the k-th to the next; a high gamma's foot is steep), within 1e-5 on
// a plain gamma's profile at gammas up to 1 from 1/16 up; +1 doubles linear light, Offset adds to it, Gamma takes its
// power; against the curves as their formulas write them, sRGB's within its table's steps.
TEST(test_exposure_formula) {
  std::vector<RGB> px = grey_ramp();
  for (int i = 0; i <= 32; i++) px.push_back({i / 32.f, i / 32.f, i / 32.f});
  const auto t0 = std::chrono::steady_clock::now();
  double worst = 0, smooth = 0;
  std::string where, where_smooth;
  int n = 0;
  for (const Profile& p : profiles()) {
    const Curve k = curve_of(p.context);
    const int channels = p.context.mode == Mode::Gray ? 1 : 3;
    for (const Setting& s : kSettings) {
      const AdjustmentPtr a = exposure(exposure_vals(s), p.context);
      CHECK(!a->identity() && a->tables().size() == size_t(kCurveSteps) + 2);
      const std::vector<RGB> out = applied(*a, px);
      for (size_t i = 0; i < px.size(); i++)
        for (int ch = 0; ch < channels; ch++) {
          const double v = px[i][size_t(ch)];
          const double err = std::fabs(out[i][size_t(ch)] - exposure_ref(v, s.e, s.o, s.g, k, ch));
          const auto note = [&](double& w, std::string& at) {
            if (err <= w) return;
            char t[160];
            std::snprintf(t, sizeof t, "%s e %g o %g g %g at %.9g", p.name, s.e, s.o, s.g, v);
            w = err, at = t;
          };
          note(worst, where);
          if (!p.stepped && s.g <= 1 && v >= 1 / 16.) note(smooth, where_smooth);
        }
      n++;
    }
  }
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  std::printf("test_exposure_formula: %d settings within %.3g of the formula (worst: %s); plain gammas at gamma up to "
              "1 within %.3g (%s); %.0f ms\n", n, worst, where.c_str(), smooth, where_smooth.c_str(), ms);
  CHECK(worst < 5e-4 && smooth < 1e-5);

  // In linear light (sRGB's curve): +1 doubles, Offset adds, Gamma takes its power, each value within 2e-4 of it and
  // 2e-5 (the encoded 5e-4 above, and the curve's 16-bit steps, in light).
  const std::vector<RGB> ramp = grey_ramp();
  const Curve srgb = curve_of({});
  const auto linear = [&](const json& vals, const std::function<double(double)>& want) {
    const std::vector<RGB> out = applied(*exposure(vals), ramp);
    double bad = 0;  // the error in light over what is allowed
    for (size_t i = 0; i < ramp.size(); i++) {
      const double w = std::clamp(want(srgb.decode(ramp[i][0])), 0., 1.);
      bad = std::max(bad, std::fabs(srgb.decode(out[i][0]) - w) / (2e-4 * w + 2e-5));
    }
    return bad;
  };
  const double twice = linear({{"e", 1}}, [](double x) { return 2 * x; });
  const double plus = linear({{"o", 0.1}}, [](double x) { return x + 0.1; });
  const double minus = linear({{"o", -0.05}}, [](double x) { return x - 0.05; });
  const double power = linear({{"g", 2}}, [](double x) { return std::sqrt(x); });
  std::printf("test_exposure_formula: in linear light, +1 doubles, offsets +0.1 and -0.05 add, gamma 2 takes the root "
              "at %.2g, %.2g, %.2g and %.2g of what is allowed\n", twice, plus, minus, power);
  CHECK(twice < 1 && plus < 1 && minus < 1 && power < 1);

  // The curves as written down, values in and out from 1/16 up: Adobe RGB's gamma 563 / 256 within 2e-5, sRGB's
  // formula (IEC 61966-2-1) within 1e-3: its table's 16-bit steps, which an offset's subtraction near the foot
  // magnifies. Below 1/16, the profiles' tables take their first entries as lines (the sRGB table in its 16-bit steps),
  // which a gamma past 1 lifts: up to 0.005 off the formulas at gamma 2, as the canvas and D30's gradients take them.
  const auto against = [&](const AdjustContext& c, const std::function<double(double)>& dec,
                           const std::function<double(double)>& enc) {
    double bad = 0;
    for (const Setting& s : {Setting{1, 0, 1}, Setting{0, -0.05, 1}, Setting{0, 0, 2}, Setting{0.7, -0.02, 1.3}}) {
      const std::vector<RGB> out = applied(*exposure(exposure_vals(s), c), ramp);
      for (size_t i = 4096; i < ramp.size(); i++) {
        const double x = std::clamp(dec(ramp[i][0]) * std::exp2(s.e) + s.o, 0., 1.), want = enc(std::pow(x, 1 / s.g));
        if (want >= 1 / 16.) bad = std::max(bad, std::fabs(out[i][0] - want));
      }
    }
    return bad;
  };
  const double iec = against({}, golden::srgb_linear, srgb_encode);
  const double adobe = against(profiles()[1].context, [](double e) { return std::pow(e, 563 / 256.); },
                               [](double y) { return std::pow(y, 256 / 563.); });
  std::printf("test_exposure_formula: against sRGB's formula within %.2g (its profile's 16-bit steps), Adobe RGB's "
              "gamma within %.2g\n", iec, adobe);
  CHECK(iec < 1e-3 && adobe < 2e-5);
  // What a new value costs (a slider's step): its table, and on a document with a profile lcms reading that.
  for (const Profile& p : {profiles()[0], profiles()[1]}) {
    const auto t1 = std::chrono::steady_clock::now();
    for (int i = 0; i < 20; i++) exposure({{"e", 0.5 + i / 1000.}, {"o", -0.01}, {"g", 1.5}}, p.context);
    std::printf("test_exposure_formula: a new Exposure on %s in %.2f ms\n", p.name,
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count() / 20);
  }
  CHECK(exposure(json::object())->identity() && exposure({{"e", 0}, {"o", 0}, {"g", 1}})->vals() == "{}");
  CHECK(exposure({{"e", 0.7}, {"o", -0.02}, {"g", 1.3}})->vals() == R"({"e":0.7,"g":1.3,"o":-0.02})");
}

// The eyedroppers: black sets Offset so the pixel (its luminance) goes to 0, white sets Exposure so it goes to 1, grey
// so it goes to 18 % in linear light; the other values stay; on a grey document R alone.
TEST(test_exposure_pick) {
  const Curve srgb = curve_of({});
  const auto pick = [](const std::string& vals, int point, std::array<double, 3> s, const AdjustContext& c = {}) {
    const std::optional<std::string> r = exposure_from_pick(vals, point, s, c);
    CHECK(r);
    CHECK(adjustment("Exposure", *r, c)->vals() == *r);  // as vals() writes them
    return json::parse(*r);
  };
  const auto value = [](const json& j, const char* k, double def) { return j.contains(k) ? j[k].get<double>() : def; };
  // A sample's luminance in linear light: 30/59/11 of its channels (each its own curve: lcms's sRGB channels differ
  // by 1e-7).
  const auto lum = [](const Curve& k, std::array<double, 3> s) {
    return 0.3 * k.decode(s[0] / 255, 0) + 0.59 * k.decode(s[1] / 255, 1) + 0.11 * k.decode(s[2] / 255, 2);
  };
  // Black: a mid-dark grey to 0.
  {
    const json j = pick("{}", 0, {64, 64, 64});
    CHECK(std::fabs(value(j, "o", 0) + lum(srgb, {64, 64, 64})) < 1e-12);
    CHECK(exposure(j)->apply({64 / 255.f, 64 / 255.f, 64 / 255.f})[0] < 1e-5);
    // Gamma 2 climbs from the foot as a root: the pixel's float, 1.5e-8 past 64 / 255, comes out within a level.
    const json k = pick(R"({"e":1.5,"g":2})", 0, {64, 64, 64});
    CHECK(std::fabs(value(k, "o", 0) + lum(srgb, {64, 64, 64}) * std::exp2(1.5)) < 1e-12 && value(k, "g", 1) == 2);
    CHECK(exposure(k)->apply({64 / 255.f, 64 / 255.f, 64 / 255.f})[0] < 0.5 / 255);
    CHECK(value(pick(R"({"e":3})", 0, {255, 255, 255}), "o", 0) == -0.5);  // past the range: its end
  }
  // White: a light grey to 1, whatever the offset and the gamma; a colour by its luminance.
  {
    const json j = pick(R"({"o":0.1,"g":1.5})", 2, {200, 200, 200});
    CHECK(std::fabs(value(j, "e", 0) - std::log2(0.9 / lum(srgb, {200, 200, 200}))) < 1e-12);
    CHECK(value(j, "o", 0) == 0.1 && value(j, "g", 1) == 1.5);
    CHECK(exposure(j)->apply({200 / 255.f, 200 / 255.f, 200 / 255.f})[0] > 1 - 1e-5);
    CHECK(std::fabs(value(pick("{}", 2, {200, 100, 50}), "e", 0) - std::log2(1 / lum(srgb, {200, 100, 50}))) < 1e-12);
    CHECK(pick(R"({"g":2})", 2, {0, 0, 0}) == json::parse(R"({"g":2})"));  // black: no exposure makes it white
    CHECK(value(pick("{}", 2, {0, 0, 1e-9}), "e", 0) == 20);
  }
  // Grey: to 18 % in linear light.
  {
    const json j = pick(R"({"o":0.01,"g":1.2})", 1, {90, 90, 90});
    CHECK(std::fabs(value(j, "e", 0) - std::log2((std::pow(0.18, 1.2) - 0.01) / lum(srgb, {90, 90, 90}))) < 1e-12);
    const double out = exposure(j)->apply({90 / 255.f, 90 / 255.f, 90 / 255.f})[0];
    CHECK(std::fabs(srgb.decode(out) - 0.18) < 1e-4);
  }
  // A grey document: R alone; another profile's curve.
  {
    const json j = pick("{}", 0, {64, 200, 200}, {Mode::Gray, nullptr});
    CHECK(std::fabs(value(j, "o", 0) + srgb.decode(64 / 255.)) < 1e-12);
    const AdjustContext adobe = profiles()[1].context;
    const json k = pick("{}", 2, {128, 128, 128}, adobe);
    CHECK(std::fabs(value(k, "e", 0) - std::log2(1 / lum(curve_of(adobe), {128, 128, 128}))) < 1e-12);
    CHECK(std::fabs(value(k, "e", 0) - std::log2(1 / std::pow(128 / 255., 563 / 256.))) < 1e-5);  // its gamma
  }
  // Not read: bad vals, no such eyedropper.
  CHECK(!exposure_from_pick("{", 0, {1, 2, 3}, {}) && !exposure_from_pick("[1]", 0, {1, 2, 3}, {}) &&
        !exposure_from_pick(R"({"e":"x"})", 0, {1, 2, 3}, {}) && !exposure_from_pick("{}", 3, {1, 2, 3}, {}) &&
        !exposure_from_pick("{}", -1, {1, 2, 3}, {}));
  std::printf("test_exposure_pick: black, grey and white eyedroppers put the pixel where they should\n");
}

// adjust_pixel() of pack() and tables() on lavapipe, against apply() to the bit: Use Legacy's lines and step, the
// default's curves, Exposure's settings on every profile, grey documents.
TEST(test_gpu_tone) {
  test::AdjustCheck check;
  if (!check) return;
  int n = 0;
  for (const char* vals :
       {"{}", R"({"b":30,"legacy":true})", R"({"c":-60,"legacy":true})", R"({"b":-50,"c":50,"legacy":true})",
        R"({"b":100,"c":99,"legacy":true})", R"({"c":100,"legacy":true})", R"({"b":-1,"c":100,"legacy":true})",
        R"({"b":100,"c":-100,"legacy":true})", R"({"b":150})", R"({"b":-150})", R"({"c":100})", R"({"c":-50})",
        R"({"b":50,"c":50})", R"({"b":-75.5,"c":33})"}) {
    CHECK(check("Brightness/Contrast", vals));
    n++;
  }
  CHECK(check("Brightness/Contrast", R"({"b":50,"c":50})", {Mode::Gray, nullptr}));
  CHECK(check("Brightness/Contrast", R"({"b":50,"c":-40,"legacy":true})", {Mode::Gray, nullptr}));
  n += 2;
  for (const Profile& p : profiles())
    for (const Setting& s : kSettings) {
      CHECK(check("Exposure", exposure_vals(s).dump(), p.context));
      n++;
    }
  CHECK(check("Exposure", "{}"));
  std::printf("test_gpu_tone: %d adjustments x %zu colours alike\n", n + 1, check.pixels());
}

// ---- Photoshop's images ---------------------------------------------------------------------------------------------

namespace {

// An image of the formula f on each pixel of `in` (16-bit samples to 0..1 and back), alpha kept.
golden::Image formula_image(const golden::Image& in, const std::function<double(double)>& f) {
  golden::Image out = in;
  for (size_t i = 0; i < out.px.size(); i += 4)
    for (size_t c = 0; c < 3; c++) out.px[i + c] = from_unit(float(f(in.px[i + c] / 65535.)), Depth::U16);
  return out;
}

}  // namespace

// D32's bc-* and exposure-* (tools/ps-golden/p1/adjust.jsx): an adjustment layer at Normal 100 % over an input, as
// Image › Adjustments of it, against Photoshop's image: Use Legacy and Exposure within ΔE2000 1 on average (the
// convention for a formula with guesses in it), the default's deviation printed for B38, and the other hypotheses'
// (Use Legacy about 127 of 255, Exposure's linear light a plain gamma 2.2) beside them. D32's sample index
// (p1/sample/adjust, no images) has its values read: Use Legacy's ranges are its dialog's.
TEST(test_tone_ps) {
  for (const char* dir : {"p1/adjust", "p1/sample/adjust"}) {
    golden::Set set(dir);
    if (!set.has_index()) {
      if (dir == std::string("p1/adjust")) set.ready("test_tone_ps");
      continue;
    }
    if (!set.ready("test_tone_ps")) continue;
    int n = 0, compared = 0;
    for (const golden::Entry& e : set.entries()) {
      if (e.json.contains("apply")) continue;  // Image › Adjustments steps: D41's
      const std::vector<golden::SceneLayer> layers = set.layers(e);
      if (layers.size() != 2 || layers[0].kind != "pixel" || layers[1].kind != "adj" || layers[0].mask ||
          layers[1].mask)
        continue;
      const std::optional<LayerProps> p = props_from_json(layers[1].props);
      if (!p || (p->adj != "Brightness/Contrast" && p->adj != "Exposure") || p->blend != Blend::Normal ||
          p->opacity != 1 || p->fill != 1)
        continue;
      const AdjustmentPtr adj = adjustment(*p);
      if (!adj) std::printf("test_tone_ps: %s: %s does not read\n", e.name.c_str(), layers[1].props.c_str());
      CHECK(adj);
      const json vals = json::parse(p->vals ? *p->vals : std::string("{}"));
      if (adj->vals() != vals.dump())
        std::printf("test_tone_ps: %s: %s read as %s\n", e.name.c_str(), vals.dump().c_str(), adj->vals().c_str());
      CHECK(adj->vals() == vals.dump());  // as Photoshop took them: nothing clamped
      n++;
      const golden::Image want = set.image(e);
      if (!want) continue;
      const golden::Image& in = *layers[0].pixels;
      const TileGrid out = apply_adjustment(*golden::grid(in, {Depth::U16, 4}), *adj);
      const std::string name = std::string(dir) + "/" + e.name;
      const golden::Diff d = golden::compare(golden::image(out, in.w, in.h), want, name);
      golden::print(e.name, d);
      compared++;
      const double b = vals.value("b", 0.), c = vals.value("c", 0.);
      if (p->adj == "Exposure") {
        const double ex = vals.value("e", 0.), o = vals.value("o", 0.), g = vals.value("g", 1.);
        const Curve gamma{nullptr, 2.2};
        const golden::Diff alt = golden::compare(
            formula_image(in, [&](double v) { return exposure_ref(v, ex, o, g, gamma); }), want, name + "-gamma22");
        std::printf("  %s with linear light a plain gamma 2.2: %s\n", e.name.c_str(), golden::str(alt).c_str());
        CHECK(d.mean < 1);
      } else if (vals.value("legacy", false)) {
        const auto about = [&](double v) {  // about 127 of 255
          const double q = 127 / 255., s = b / 255;
          if (c >= 100) return v + s > q ? 1. : 0.;
          const double k = c > 0 ? 100 / (100 - c) : (100 + c) / 100;
          return std::clamp(c < 0 ? (v - q) * k + q + s : (v + s - q) * k + q, 0., 1.);
        };
        const golden::Diff alt = golden::compare(formula_image(in, about), want, name + "-pivot127");
        std::printf("  %s about 127 of 255: %s\n", e.name.c_str(), golden::str(alt).c_str());
        CHECK(d.mean < 1);
      } else {
        // B38's curve: the grey band of ramp.png (row 16, x the level in), Photoshop's and ours, in levels.
        std::string curve;
        const golden::Image ours = golden::image(out, in.w, in.h);
        if ((*layers[0].json).value("src", std::string()) == "ramp.png")
          for (int x : {0, 32, 64, 96, 128, 160, 192, 224, 255}) {
            char s[48];
            std::snprintf(s, sizeof s, " %d: %.1f %.1f", x, want.at(x, 16)[0] / 257., ours.at(x, 16)[0] / 257.);
            curve += s;
          }
        std::printf("  %s: the default's hypothesis (B38 fits it)%s%s\n", e.name.c_str(),
                    curve.empty() ? "" : "; level in: Photoshop's, ours:", curve.c_str());
      }
    }
    std::printf("test_tone_ps: %s: %d Brightness/Contrast and Exposure cases, %d compared\n", dir, n, compared);
    set.skipped("test_tone_ps");
  }
}
