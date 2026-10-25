// Hue/Saturation and Vibrance (D35): the hypotheses adj_hue_saturation.cpp and adj_vibrance.cpp state, against a
// reference written here in double through textbook HSL and back; what the task asks: the defaults the identity,
// Master's hue +120 taking pure red to pure green, saturation -100 grey at (max + min) / 2 to the bit, Reds leaving
// pure blue alone, a range across 0°, Colorize's hue, Vibrance monotonic in its slider; the GPU the CPU's to the bit
// (adjust_check.h); vals as the mock keeps them (one set, the range shown) and as README-psd.md's sets by range, the
// default spans AdjustSpec.SPANS' (read from ui/qml/AdjustSpec.js); the range eyedropper; Photoshop's images (D32)
// where B33 has made them, the deviation printed (hypotheses: no thresholds until B38).
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "adjust.h"
#include "adjust_check.h"
#include "check.h"
#include "golden.h"

namespace pb {  // adj_hue_saturation.cpp's
std::array<double, 4> span_from_pick(double hue);
int range_of_hue(double hue);
}  // namespace pb

using namespace pb;
using nlohmann::json;

namespace {

// The 33³ lattice, a 16-bit grey ramp and R, G, B ramps apart (as adjust_check.h's, every 7th sample).
std::vector<Float4> colours() {
  std::vector<Float4> px;
  for (int b = 0; b <= 32; b++)
    for (int g = 0; g <= 32; g++)
      for (int r = 0; r <= 32; r++) px.push_back({r / 32.f, g / 32.f, b / 32.f, 0.5f});
  const auto u = [](uint32_t s) { return float(double(s & 0xffff) * (1.0 / 65535)); };
  for (uint32_t v = 0; v < 65536; v += 7) {
    px.push_back({u(v), u(v), u(v), 0.5f});
    px.push_back({u(v), u(65535 - v), u(v * 40503u + 12345u), 0.5f});
  }
  return px;
}

AdjustmentPtr make(const char* name, const std::string& vals) {
  const AdjustmentPtr a = adjustment(name, vals);
  if (!a) std::printf("test_adj_hue: %s %s does not read\n", name, vals.c_str());
  CHECK(a);
  return a;
}
AdjustmentPtr hue(const std::string& vals) { return make("Hue/Saturation", vals); }
AdjustmentPtr vibrance(const std::string& vals) { return make("Vibrance", vals); }

std::vector<Float4> applied(const Adjustment& a, std::vector<Float4> px) {
  a.apply(px[0].data(), int(px.size()), -3, 5);
  return px;
}

bool same(const std::vector<Float4>& a, const std::vector<Float4>& b) {
  return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(Float4)) == 0;
}

// ---- HSL in double, as textbooks have it ----------------------------------------------------------------------------

double mod360(double v) {
  v = std::fmod(v, 360);
  return v < 0 ? v + 360 : v;
}

struct Hsl {
  double h = 0, s = 0, l = 0;  // degrees (0 for a grey), 0..1, 0..1
  double chroma = 0;
};
Hsl hsl(const double* c) {
  const double M = std::max({c[0], c[1], c[2]}), m = std::min({c[0], c[1], c[2]}), C = M - m;
  Hsl x;
  x.l = (M + m) / 2, x.chroma = C;
  if (C == 0) return x;
  x.s = C / (1 - std::fabs(2 * x.l - 1));
  if (M == c[0])
    x.h = 60 * std::fmod((c[1] - c[2]) / C + 6, 6);
  else if (M == c[1])
    x.h = 60 * ((c[2] - c[0]) / C + 2);
  else
    x.h = 60 * ((c[0] - c[1]) / C + 4);
  return x;
}
void rgb(double h, double s, double l, double* c) {
  const double C = (1 - std::fabs(2 * l - 1)) * s, hp = mod360(h) / 60, X = C * (1 - std::fabs(std::fmod(hp, 2) - 1));
  const double m = l - C / 2;
  const int k = std::min(int(hp), 5);
  const double r[6] = {C, X, 0, 0, X, C}, g[6] = {X, C, C, X, 0, 0}, b[6] = {0, 0, X, C, C, X};
  c[0] = r[k] + m, c[1] = g[k] + m, c[2] = b[k] + m;
}
double hue_of(const float* c) {
  const double d[3] = {c[0], c[1], c[2]};
  return hsl(d).h;
}

// How much of span [a, b, c, d] (degrees) hue h is in: 0 at a, rising to 1 at b, 1 to c, falling to 0 at d.
double span_weight(const std::array<double, 4>& s, double h) {
  const double b = mod360(s[1] - s[0]), c = b + mod360(s[2] - s[1]), d = c + mod360(s[3] - s[2]), t = mod360(h - s[0]);
  if (t < b) return t / b;
  if (t <= c) return 1;
  if (t < d) return (d - t) / (d - c);
  return 0;
}

// ---- Hue/Saturation's hypothesis (adj_hue_saturation.cpp), in double -------------------------------------------------

const char* const kRanges[] = {"Reds", "Yellows", "Greens", "Cyans", "Blues", "Magentas"};
const std::array<double, 4> kSpans[6] = {{315, 345, 15, 45},   {15, 45, 75, 105},    {75, 105, 135, 165},
                                         {135, 165, 195, 225}, {195, 225, 255, 285}, {255, 285, 315, 345}};

struct RefSet {
  double h = 0, s = 0, l = 0;  // degrees, -1..1, -1..1
  std::array<double, 4> span{};
};
struct Ref {
  bool colorize = false;
  RefSet master, ranges[6];  // Colorize: master's h the colour's hue, s 0..1
};

RefSet ref_set(const json& j, const std::array<double, 4>& span) {
  RefSet s;
  s.h = std::clamp(j.value("h", 0.), -180., 360.), s.s = std::clamp(j.value("s", 0.), -100., 100.) / 100;
  s.l = std::clamp(j.value("l", 0.), -100., 100.) / 100, s.span = span;
  if (j.contains("span")) s.span = j["span"].get<std::array<double, 4>>();
  return s;
}
// vals as the task reads them: each range's object; else the top-level set for the range shown (rng); Colorize's
// colour Master's object, else the top-level set.
Ref ref_of(const std::string& vals) {
  const json j = json::parse(vals);
  Ref r;
  r.colorize = j.value("colorize", false);
  const std::string shown = j.value("rng", std::string("Master"));
  json top = json::object();
  for (const char* k : {"h", "s", "l", "span"})
    if (j.contains(k)) top[k] = j[k];
  r.master = ref_set(j.contains("Master") ? j["Master"] : shown == "Master" || r.colorize ? top : json::object(), {});
  for (int i = 0; i < 6; i++)
    r.ranges[i] = ref_set(j.contains(kRanges[i]) ? j[kRanges[i]] : shown == kRanges[i] ? top : json::object(), kSpans[i]);
  return r;
}

double mix(double v, double l) { return l > 0 ? v + (1 - v) * l : v * (1 + l); }
double shift(double h) {  // a hue shift as a turn of -180..180
  h = std::fmod(h, 360);
  return h > 180 ? h - 360 : h <= -180 ? h + 360 : h;
}

void ref_apply(const Ref& r, const float* in, double* out) {
  const double c[3] = {in[0], in[1], in[2]};
  const Hsl x = hsl(c);
  if (r.colorize) {
    rgb(r.master.h, std::clamp(r.master.s, 0., 1.), x.l, out);
    for (int i = 0; i < 3; i++) out[i] = mix(out[i], r.master.l);
  } else {
    double H = r.master.h, St = r.master.s, Lt = r.master.l;
    if (x.chroma > 0)
      for (const RefSet& s : r.ranges) {
        const double w = span_weight(s.span, x.h);
        H += w * shift(s.h), St += w * s.s, Lt += w * s.l;
      }
    St = std::clamp(St, -1., 1.), Lt = std::clamp(Lt, -1., 1.);
    const double S = St < 0 ? x.s * (1 + St) : St >= 1 ? (x.s > 0 ? 1 : 0) : std::min(1., x.s / (1 - St));
    rgb(x.h + H, S, x.l, out);
    for (int i = 0; i < 3; i++) out[i] = mix(out[i], Lt);
  }
  for (int i = 0; i < 3; i++) out[i] = std::clamp(out[i], 0., 1.);
}

// The largest difference of apply() from the reference over colours().
double ref_error(const std::string& vals) {
  const std::vector<Float4> px = colours(), got = applied(*hue(vals), px);
  const Ref r = ref_of(vals);
  double worst = 0;
  size_t at = 0;
  for (size_t i = 0; i < px.size(); i++) {
    double want[3];
    ref_apply(r, px[i].data(), want);
    for (int c = 0; c < 3; c++)
      if (std::fabs(got[i][size_t(c)] - want[c]) > worst) worst = std::fabs(got[i][size_t(c)] - want[c]), at = i;
  }
  if (worst > 1e-5) {
    double want[3];
    ref_apply(r, px[at].data(), want);
    std::printf("test_adj_hue: %s: %.3g off at %.9g %.9g %.9g: %.9g %.9g %.9g, want %.9g %.9g %.9g\n", vals.c_str(),
                worst, px[at][0], px[at][1], px[at][2], got[at][0], got[at][1], got[at][2], want[0], want[1], want[2]);
  }
  return worst;
}

// ---- Vibrance's hypothesis (adj_vibrance.cpp: its kSkinSpan, kSkinProtect here too, changed with them) -------------

const std::array<double, 4> kSkinSpan = {0, 15, 35, 50};
constexpr double kSkinProtect = 0.5;

void ref_vibrance(double v, double s, const float* in, double* out) {
  const double c[3] = {in[0], in[1], in[2]};
  const Hsl x = hsl(c);
  const double a = (1 - std::min(x.s, 1.)) * (1 - kSkinProtect * span_weight(kSkinSpan, x.h));
  const double q = x.chroma > 0 ? (1 + v / 100 * a) * (1 + s / 100) : 1;
  for (int i = 0; i < 3; i++) out[i] = std::clamp(x.l + (c[i] - x.l) * q, 0., 1.);
}

// ---- AdjustSpec.SPANS -----------------------------------------------------------------------------------------------

std::map<std::string, std::array<double, 4>> spec_spans() {
  std::ifstream f(PB_SOURCE_DIR "/ui/qml/AdjustSpec.js", std::ios::binary);
  CHECK(f);
  std::ostringstream s;
  s << f.rdbuf();
  const std::string js = s.str();
  const size_t at = js.find("var SPANS = {");
  CHECK(at != std::string::npos);
  const std::string block = js.substr(at, js.find("};", at) - at);
  std::map<std::string, std::array<double, 4>> out;
  const std::regex one(R"((\w+): \[(-?[0-9.]+), (-?[0-9.]+), (-?[0-9.]+), (-?[0-9.]+)\])");
  for (auto it = std::sregex_iterator(block.begin(), block.end(), one); it != std::sregex_iterator(); ++it)
    out[(*it)[1]] = {std::stod((*it)[2]), std::stod((*it)[3]), std::stod((*it)[4]), std::stod((*it)[5])};
  return out;
}

std::string span_json(const std::array<double, 4>& s) {
  json j = json::array();
  for (double v : s) j.push_back(v);
  return j.dump();
}

}  // namespace

// The defaults and whatever changes nothing are the identity; vals as the mock keeps them (the set of the range shown)
// and as README-psd.md's sets by range read alike, a range's own object first; spans by default AdjustSpec.SPANS';
// sets of the wrong type do not read.
TEST(test_adj_hue_vals) {
  const std::vector<Float4> px = colours();
  for (const char* v : {"{}", R"({"pre":"Sepia"})", R"({"rng":"Reds"})", R"({"rng":"Reds","span":[300,330,30,60]})",
                        R"({"Greens":{"span":[60,90,150,200]}})", R"({"Master":{"h":360,"l":0,"s":0}})",
                        R"({"Blues":{"h":360}})"}) {
    const AdjustmentPtr a = hue(v);
    CHECK(a->identity() && same(applied(*a, px), px));
  }
  CHECK(vibrance("{}")->identity() && same(applied(*vibrance("{}"), px), px));
  CHECK(!hue(R"({"colorize":true})")->identity() && !hue(R"({"h":1})")->identity() &&
        !hue(R"({"Magentas":{"l":-1}})")->identity() && !vibrance(R"({"v":1})")->identity() &&
        !vibrance(R"({"s":-1})")->identity());

  // Two vals alike, pixel for pixel.
  const auto alike = [&](const std::string& a, const std::string& b) {
    const bool ok = same(applied(*hue(a), px), applied(*hue(b), px));
    if (!ok) std::printf("test_adj_hue_vals: %s and %s differ\n", a.c_str(), b.c_str());
    return ok;
  };
  CHECK(alike(R"({"h":30,"l":-10,"s":40})", R"({"Master":{"h":30,"l":-10,"s":40}})"));
  CHECK(alike(R"({"h":30,"rng":"Reds","s":40})", R"({"Reds":{"h":30,"s":40}})"));
  CHECK(alike(R"({"h":30,"rng":"Blues"})", R"({"Blues":{"h":30}})"));
  CHECK(alike(R"({"h":60,"rng":"Greens","span":[60,90,150,200]})", R"({"Greens":{"h":60,"span":[60,90,150,200]}})"));
  CHECK(alike(R"({"Reds":{"s":-50},"rng":"Reds","s":50})", R"({"Reds":{"s":-50}})"));  // the object first
  CHECK(alike(R"({"Master":{"s":20},"h":90})", R"({"s":20})"));
  CHECK(alike(R"({"Reds":{"s":500}})", R"({"Reds":{"s":100}})"));  // clamped
  CHECK(alike(R"({"Master":{"h":200}})", R"({"h":-160})"));         // a turn is a turn
  CHECK(alike(R"({"Master":{"h":-500}})", R"({"h":180})"));
  // Colorize: Master's object, else the set shown whatever range it is; the ranges unused; its hue 0..360.
  CHECK(alike(R"({"colorize":true,"h":-160,"s":25})", R"({"Master":{"h":200,"s":25},"colorize":true})"));
  CHECK(alike(R"({"colorize":true,"h":30,"rng":"Reds","s":50})", R"({"Master":{"h":30,"s":50},"colorize":true})"));
  CHECK(alike(R"({"Master":{"h":30,"s":50},"Reds":{"h":90},"colorize":true,"l":40})",
              R"({"Master":{"h":30,"s":50},"colorize":true})"));
  CHECK(alike(R"({"Master":{"h":30,"s":-50},"colorize":true})", R"({"Master":{"h":30},"colorize":true})"));
  // Each range's span by default is AdjustSpec.SPANS's (ui/qml/AdjustSpec.js).
  const auto spans = spec_spans();
  CHECK(spans.size() == 6);
  for (const char* r : kRanges) {
    const std::string set = std::string(R"({")") + r + R"(":{"h":40,"l":-20,"s":30)";
    CHECK(spans.count(r) && alike(set + "}}", set + R"(,"span":)" + span_json(spans.at(r)) + "}}"));
  }
  // A set of the wrong type, or with a key of the wrong type, does not read; vals come back as written.
  for (const char* bad : {R"({"Reds":{"h":"x"}})", R"({"Greens":{"span":[1,2,3]}})", R"({"Master":5})",
                          R"({"Blues":{"l":true}})", R"({"span":[1,2,3,"4"]})"})
    CHECK(!adjustment("Hue/Saturation", bad));
  const char* const sets = R"({"Cyans":{"h":40,"s":-50},"Master":{"h":200,"s":40},"Yellows":{"h":-20,"s":30},)"
                           R"("colorize":true})";
  CHECK(hue(sets)->vals() == sets);
}

// What D35 asks of Hue/Saturation, then the hypothesis against the reference: Master, one range, several ranges
// overlapping, spans of their own across 0°, Colorize.
TEST(test_adj_hue) {
  const std::vector<Float4> px = colours();
  // Master's hue: pure colours to pure colours, exactly.
  const auto turned = [](const char* vals, std::array<float, 3> in) { return hue(vals)->apply(in); };
  using C3 = std::array<float, 3>;
  CHECK(turned(R"({"h":120})", {1, 0, 0}) == (C3{0, 1, 0}) && turned(R"({"h":120})", {0, 1, 0}) == (C3{0, 0, 1}) &&
        turned(R"({"h":120})", {0, 0, 1}) == (C3{1, 0, 0}));
  CHECK(turned(R"({"h":-120})", {1, 0, 0}) == (C3{0, 0, 1}) && turned(R"({"h":60})", {1, 0, 0}) == (C3{1, 1, 0}) &&
        turned(R"({"h":180})", {1, 0, 0}) == (C3{0, 1, 1}) && turned(R"({"h":-60})", {0, 1, 0}) == (C3{1, 1, 0}));
  CHECK(turned(R"({"h":120})", {0.8f, 0.2f, 0.2f}) == (C3{0.2f, 0.8f, 0.2f}));

  // Saturation -100: grey at (max + min) / 2, to the bit, everywhere (Desaturate, D41).
  {
    const std::vector<Float4> out = applied(*hue(R"({"s":-100})"), px);
    size_t bad = 0;
    for (size_t i = 0; i < px.size(); i++) {
      const float* c = px[i].data();
      const float L = float((double(std::max({c[0], c[1], c[2]})) + double(std::min({c[0], c[1], c[2]}))) / 2);
      bad += !(out[i][0] == L && out[i][1] == L && out[i][2] == L && out[i][3] == c[3]);
    }
    CHECK(bad == 0);
  }

  // Reds leave every colour outside their span alone, to the bit: pure blue, greys, hues 45..315.
  {
    const AdjustmentPtr reds = hue(R"({"Reds":{"h":30,"l":-10,"s":40}})");
    CHECK(reds->apply({0, 0, 1}) == (C3{0, 0, 1}) && reds->apply({0.3f, 0.3f, 0.3f}) == (C3{0.3f, 0.3f, 0.3f}));
    const std::vector<Float4> out = applied(*reds, px);
    size_t outside = 0, inside = 0;
    for (size_t i = 0; i < px.size(); i++) {
      const double h = hue_of(px[i].data());
      const bool grey = px[i][0] == px[i][1] && px[i][1] == px[i][2];
      if (grey || (h >= 45 + 1e-3 && h <= 315 - 1e-3)) {  // past float's hue there
        CHECK(std::memcmp(&out[i], &px[i], sizeof(Float4)) == 0);
        outside++;
      } else if (h <= 15 || h >= 345) {
        inside += std::memcmp(&out[i], &px[i], sizeof(Float4)) != 0;
      }
    }
    CHECK(outside > 10000 && inside > 1000);
  }

  // A range across 0°: Reds' hue +30 turns a hue h by 30 times its weight in 315/345 15\45.
  {
    const AdjustmentPtr reds = hue(R"({"Reds":{"h":30}})");
    for (double h : {300., 315., 320., 330., 340., 345., 350., 0., 10., 15., 20., 30., 40., 45., 50., 90.}) {
      double c[3];
      rgb(h, 1, 0.5, c);
      const std::array<float, 3> in = {float(c[0]), float(c[1]), float(c[2])}, out = reds->apply(in);
      const double from = hue_of(in.data()), want = mod360(from + 30 * span_weight(kSpans[0], from));
      const double got = hue_of(out.data()), d = std::fabs(std::remainder(got - want, 360));
      if (d > 1e-3) std::printf("test_adj_hue: Reds +30 at %g: %.6f, want %.6f\n", h, got, want);
      CHECK(d < 1e-3);
    }
  }

  // Colorize: every colour the colour's hue and saturation at its own lightness.
  {
    const std::vector<Float4> out = applied(*hue(R"({"Master":{"h":200,"s":40},"colorize":true})"), px);
    double worst = 0;
    for (size_t i = 0; i < px.size(); i++) {
      const double in[3] = {px[i][0], px[i][1], px[i][2]}, o[3] = {out[i][0], out[i][1], out[i][2]};
      const Hsl a = hsl(in), b = hsl(o);
      worst = std::max(worst, std::fabs(a.l - b.l));
      if (b.chroma > 1e-3) {
        worst = std::max(worst, std::fabs(std::remainder(b.h - 200, 360)) / 360);
        worst = std::max(worst, std::fabs(b.s - 0.4));
      }
    }
    std::printf("test_adj_hue: Colorize within %.3g of its hue, saturation and the lightness kept\n", worst);
    CHECK(worst < 1e-4);
  }

  // The hypothesis, everywhere.
  double worst = 0;
  for (const char* v : {R"({"h":120})", R"({"h":-45})", R"({"h":180})", R"({"s":50})", R"({"s":-30})",
                        R"({"s":100})", R"({"s":-100})", R"({"l":40})", R"({"l":-40})", R"({"l":100})",
                        R"({"l":-100})", R"({"h":30,"l":-15,"s":25})", R"({"Reds":{"h":30,"l":-10,"s":40}})",
                        R"({"Blues":{"h":-20,"s":30},"Cyans":{"h":40,"s":-50},"Yellows":{"l":30}})",
                        R"({"Greens":{"h":60,"span":[60,90,150,200]}})",
                        R"({"Reds":{"h":50,"span":[350,350,10,10]}})",
                        R"({"Magentas":{"s":50,"span":[200,300,300,40]}})",
                        R"({"Master":{"h":10,"l":5,"s":-20},"Reds":{"h":-30,"s":60},"Yellows":{"l":20,"s":-40},)"
                        R"("Greens":{"h":90},"Cyans":{"l":-60},"Blues":{"h":-170,"s":100},"Magentas":{"l":100,"s":-100}})",
                        R"({"Master":{"s":60},"Reds":{"s":60}})", R"({"Master":{"l":-70},"Blues":{"l":-70}})",
                        R"({"Master":{"h":200,"s":40},"colorize":true})", R"({"Master":{"h":30,"l":-20,"s":70},"colorize":true})",
                        R"({"colorize":true,"h":-160,"l":60,"s":100})", R"({"colorize":true})"})
    worst = std::max(worst, ref_error(v));
  std::printf("test_adj_hue: within %.3g of the hypothesis in double\n", worst);
  CHECK(worst < 1e-5);

  // Image › Adjustments over 2048 x 2048 RGBA U16 of noise: the time, three ranges and Vibrance (printed).
  constexpr int W = 2048;
  TileGrid g(Format{Depth::U16, 4});
  std::vector<uint16_t> noise(size_t(W) * W * 4);
  uint32_t seed = 1;
  for (uint16_t& v : noise) v = uint16_t((seed = seed * 1664525u + 1013904223u) >> 16);
  g.write_rect(0, 0, W, W, reinterpret_cast<const uint8_t*>(noise.data()), size_t(W) * 8);
  for (const AdjustmentPtr& a : {hue(R"({"Blues":{"l":30},"Cyans":{"h":40,"s":-50},"Yellows":{"h":-20,"s":30}})"),
                                 vibrance(R"({"s":-30,"v":60})")}) {
    const auto t0 = std::chrono::steady_clock::now();
    const TileGrid out = apply_adjustment(g, *a);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("test_adj_hue: %s %s over 2048 x 2048 RGBA U16 in %.1f ms\n", kAdjNames[size_t(a->type())],
                a->vals().c_str(), ms);
    CHECK(out.tiles().size() == g.tiles().size());
  }
}

// Vibrance: the identity at its defaults, more of it never less chroma (each channel away from L), full colours and
// greys left alone, its Saturation -100 grey, skin tones less vibrant; the hypothesis against the reference.
TEST(test_adj_vibrance) {
  const std::vector<Float4> px = colours();
  // Monotonic in vibrance: each channel above L rises (or stays), each below falls, from -100 to 100.
  std::vector<Float4> before;
  for (int v = -100; v <= 100; v += 10) {
    const std::vector<Float4> out = applied(*vibrance("{\"v\":" + std::to_string(v) + "}"), px);
    if (!before.empty())
      for (size_t i = 0; i < px.size(); i++) {
        const float* c = px[i].data();
        const float L = (std::max({c[0], c[1], c[2]}) + std::min({c[0], c[1], c[2]})) * 0.5f;
        for (int k = 0; k < 3; k++)
          CHECK(c[k] > L ? out[i][size_t(k)] >= before[i][size_t(k)]
                         : c[k] < L ? out[i][size_t(k)] <= before[i][size_t(k)] : out[i][size_t(k)] == c[k]);
      }
    if (v == 0) CHECK(same(out, px));
    before = out;
  }
  // Full colours and greys stay; Saturation -100 is grey at L.
  const AdjustmentPtr full = vibrance(R"({"v":100})");
  using C3 = std::array<float, 3>;
  for (const C3& c : {C3{1, 0, 0}, C3{0.5f, 0, 0.25f}, C3{1, 1, 0.5f}, C3{0.4f, 0.4f, 0.4f}, C3{0, 0, 0}, C3{1, 1, 1}})
    CHECK(full->apply(c) == c);
  CHECK(vibrance(R"({"s":-100})")->apply({0.8f, 0.2f, 0.4f}) == (C3{0.5f, 0.5f, 0.5f}));
  // Skin (hue 25) gets less than a sky blue (205) of the same S and L.
  double skin[3], sky[3];
  rgb(25, 0.4, 0.6, skin);
  rgb(205, 0.4, 0.6, sky);
  const auto chroma = [&](const double* c) {
    const C3 o = full->apply({float(c[0]), float(c[1]), float(c[2])});
    return *std::max_element(o.begin(), o.end()) - *std::min_element(o.begin(), o.end());
  };
  CHECK(chroma(skin) < chroma(sky) && chroma(sky) > 0.32 * 1.5);  // S 0.4 at L 0.6: chroma 0.32

  double worst = 0;
  for (const auto& [v, s] : std::vector<std::pair<int, int>>{
           {50, 0}, {-50, 0}, {100, 0}, {-100, 0}, {0, 50}, {0, -50}, {0, 100}, {60, -30}, {100, 100}, {-100, -100}}) {
    const std::string vals = json{{"s", s}, {"v", v}}.dump();
    const std::vector<Float4> out = applied(*vibrance(vals), px);
    for (size_t i = 0; i < px.size(); i++) {
      double want[3];
      ref_vibrance(v, s, px[i].data(), want);
      for (int c = 0; c < 3; c++) worst = std::max(worst, std::fabs(out[i][size_t(c)] - want[c]));
    }
  }
  std::printf("test_adj_vibrance: within %.3g of the hypothesis in double\n", worst);
  CHECK(worst < 1e-5);
}

// adjust_pixel() on lavapipe against apply() to the bit (adjust_check.h): Hue/Saturation's paths (hue turned or not,
// saturation down, up, capped, lightness either way, ranges with their spans and across 0°, degenerate spans,
// Colorize) and Vibrance's.
TEST(test_gpu_adj_hue) {
  test::AdjustCheck check;
  if (!check) return;
  const auto t0 = std::chrono::steady_clock::now();
  int n = 0;
  for (const char* v : {"{}", R"({"h":120})", R"({"h":-45})", R"({"h":180})", R"({"s":50})", R"({"s":-100})",
                        R"({"s":100})", R"({"s":-30})", R"({"l":40})", R"({"l":-40})", R"({"l":100})", R"({"l":-100})",
                        R"({"h":30,"l":-15,"s":25})", R"({"Reds":{"h":30,"l":-10,"s":40}})",
                        R"({"Blues":{"l":30},"Cyans":{"h":40,"s":-50},"Yellows":{"h":-20,"s":30}})",
                        R"({"Greens":{"h":60,"span":[60,90,150,200]}})", R"({"Reds":{"h":50,"span":[350,350,10,10]}})",
                        R"({"Magentas":{"s":50,"span":[200,300,300,40]}})", R"({"Blues":{"l":50,"span":[240,240,240,240]}})",
                        R"({"Master":{"h":10,"l":5,"s":-20},"Reds":{"h":-30,"s":60},"Yellows":{"l":20,"s":-40},)"
                        R"("Greens":{"h":90},"Cyans":{"l":-60},"Blues":{"h":-170,"s":100},"Magentas":{"l":100,"s":-100}})",
                        R"({"Master":{"s":60},"Reds":{"s":60}})", R"({"h":30,"rng":"Reds","s":40})",
                        R"({"l":-50,"rng":"Blues","span":[180,200,260,300]})",
                        R"({"Master":{"h":200,"s":40},"colorize":true})", R"({"Master":{"h":30,"l":-20,"s":70},"colorize":true})",
                        R"({"colorize":true,"h":-160,"s":25})", R"({"colorize":true,"l":60,"s":100})", R"({"colorize":true})"}) {
    CHECK(check("Hue/Saturation", v));
    n++;
  }
  for (const char* v : {R"({"v":50})", R"({"v":-50})", R"({"v":100})", R"({"v":-100})", R"({"s":50})", R"({"s":-50})",
                        R"({"s":100})", R"({"s":-100})", R"({"s":-30,"v":60})", R"({"s":100,"v":100})"}) {
    CHECK(check("Vibrance", v));
    n++;
  }
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  std::printf("test_gpu_adj_hue: %d adjustments x %zu colours alike, %.0f ms\n", n, check.pixels(), ms);
}

// The range eyedropper: Photoshop's default widths around the hue picked, whole degrees, going round; the range of a
// hue; a picked span weighs the hue picked fully, 45° off it nothing.
TEST(test_adj_hue_pick) {
  using S4 = std::array<double, 4>;
  CHECK(span_from_pick(0) == (S4{315, 345, 15, 45}) && span_from_pick(60) == (S4{15, 45, 75, 105}));
  CHECK(span_from_pick(12.4) == (S4{327, 357, 27, 57}) && span_from_pick(359.6) == (S4{315, 345, 15, 45}));
  CHECK(span_from_pick(-90) == (S4{225, 255, 285, 315}) && span_from_pick(200) == (S4{155, 185, 215, 245}));
  CHECK(range_of_hue(0) == 1 && range_of_hue(29) == 1 && range_of_hue(31) == 2 && range_of_hue(359) == 1 &&
        range_of_hue(120) == 3 && range_of_hue(200) == 4 && range_of_hue(250) == 5 && range_of_hue(-50) == 6);
  for (double h : {7., 100., 222., 350.}) {
    const S4 s = span_from_pick(h);
    CHECK(span_weight(s, std::round(h)) == 1 && span_weight(s, std::round(h) + 15) == 1 &&
          span_weight(s, std::round(h) - 45) == 0 && span_weight(s, std::round(h) + 45) == 0);
    // As a range's span: the hue picked turned by its hue, one 60° away not at all.
    const std::string vals = R"({"rng":"Greens","h":30,"span":)" + span_json(s) + "}";
    double c[3], far[3];
    rgb(std::round(h), 0.8, 0.5, c);
    rgb(std::round(h) + 60, 0.8, 0.5, far);
    const std::array<float, 3> in = {float(c[0]), float(c[1]), float(c[2])}, out = hue(vals)->apply(in);
    CHECK(std::fabs(std::remainder(hue_of(out.data()) - hue_of(in.data()) - 30, 360)) < 1e-3);
    const std::array<float, 3> in2 = {float(far[0]), float(far[1]), float(far[2])};
    CHECK(hue(vals)->apply(in2) == in2);
  }
}

// Photoshop's Hue/Saturation and Vibrance layers (D32's hue-* and vibrance-* cases, B33): each on its input, Normal at
// 100 % over opaque pixels as apply_adjustment() of the layer's pixels, printed against Photoshop's image. Before B33
// D32's sample index: every case's vals read, the images not there.
TEST(test_adj_hue_ps) {
  golden::Set real("p1/adjust"), sample("p1/sample/adjust");
  golden::Set& set = real.has_index() ? real : sample;
  if (!set.ready("test_adj_hue_ps")) return;
  int cases = 0, compared = 0;
  for (const golden::Entry& e : set.entries()) {
    if (e.name.rfind("hue-", 0) != 0 && e.name.rfind("vibrance-", 0) != 0) continue;
    const std::vector<golden::SceneLayer> layers = set.layers(e);
    CHECK(layers.size() == 2 && layers[0].pixels && layers[1].kind == "adj");
    const std::optional<LayerProps> props = props_from_json(layers[1].props);
    CHECK(props && (props->adj == "Hue/Saturation" || props->adj == "Vibrance"));
    const AdjustmentPtr a = adjustment(*props);
    if (!a) std::printf("test_adj_hue_ps: %s: %s does not read\n", e.name.c_str(), layers[1].props.c_str());
    CHECK(a);
    cases++;
    const golden::Image want = set.image(e);
    if (!want) continue;
    if (props->blend != Blend::Normal || props->opacity != 1 || props->fill != 1) {
      std::printf("test_adj_hue_ps: %s: not Normal at 100 %% (D40's)\n", e.name.c_str());
      continue;
    }
    const golden::Image& in = *layers[0].pixels;
    const TileGrid out = apply_adjustment(*golden::grid(in, Format{Depth::U16, 4}), *a);
    golden::print(e.name, golden::compare(golden::image(out, in.w, in.h), want, "p1/adjust/" + e.name));
    compared++;
  }
  std::printf("test_adj_hue_ps: %d cases in %s, %d compared\n", cases, set.name().c_str(), compared);
  CHECK(cases > 0);
  set.skipped("test_adj_hue_ps");
}
