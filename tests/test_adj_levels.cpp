// Levels (D33, core/adj_levels.cpp): each set's formula against double precision on the 33³ lattice and 16-bit ramps,
// gammas to both ends of their range; the identities; which set goes where (a channel's object, the top-level set of
// the channel shown, RGB's after the channels', a grey document's RGB alone); the eyedroppers (levels_from_pick());
// the GPU the CPU's to the bit (adjust_check.h); Photoshop's images (D32's levels-*, once B33 has made them).
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include "adjust.h"
#include "adjust_check.h"
#include "check.h"
#include "golden.h"
#include "histogram.h"

namespace pb {
// core/adj_levels.cpp's (D33): declared where it is called.
std::optional<std::string> levels_from_pick(const std::string& vals, int point, const std::array<double, 3>& sample,
                                            const std::array<double, 3>& target, Mode mode);
}  // namespace pb

using namespace pb;

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

// A set, levels 0..255, and what the task's formula makes of v (0..1) in double precision.
struct Ref {
  double in0 = 0, gam = 1, in1 = 255, out0 = 0, out1 = 255;
  double operator()(double v) const {
    const double u = std::clamp((v * 255 - in0) / (in1 - in0), 0., 1.);
    return (out0 + std::pow(u, 1 / gam) * (out1 - out0)) / 255;
  }
};

// Levels' vals, and the sets they hold: RGB's after each channel's own.
struct Case {
  const char* vals;
  Ref rgb, red, green, blue;
};

const Case kCases[] = {
    {R"({"RGB":{"gam":0.5}})", {0, 0.5}},
    {R"({"RGB":{"gam":2}})", {0, 2}},
    {R"({"RGB":{"gam":1.3,"in0":20,"in1":230,"out0":10,"out1":240}})", {20, 1.3, 230, 10, 240}},
    {R"({"RGB":{"out0":255,"out1":0}})", {0, 1, 255, 255, 0}},
    {R"({"Blue":{"in1":180,"out0":30},"Green":{"gam":1.8},"Red":{"in0":40}})", {}, {40}, {0, 1.8}, {0, 1, 180, 30}},
    {R"({"RGB":{"gam":0.6,"in0":15},"Red":{"gam":1.5,"in0":60,"out1":230}})", {15, 0.6}, {60, 1.5, 255, 0, 230}},
    {R"({"RGB":{"gam":9.99}})", {0, 9.99}},
    {R"({"RGB":{"gam":0.01}})", {0, 0.01}},
    {R"({"RGB":{"gam":3},"Red":{"gam":0.1},"Green":{"gam":5},"Blue":{"gam":0.3}})", {0, 3}, {0, 0.1}, {0, 5}, {0, 0.3}},
    {R"({"RGB":{"in0":100,"in1":102}})", {100, 1, 102}},
    {R"({"Green":{"gam":1.37,"in0":12.5,"in1":200.25,"out0":3.5,"out1":250.75}})", {}, {}, {12.5, 1.37, 200.25, 3.5, 250.75}},
    {R"({"RGB":{"gam":2.2,"in0":30,"in1":220,"out0":20,"out1":235},"Red":{"gam":0.45,"in1":240},)"
     R"("Green":{"in0":10,"out0":40},"Blue":{"gam":1.6,"out1":200}})",
     {30, 2.2, 220, 20, 235}, {0, 0.45, 240}, {10, 1, 255, 40}, {0, 1.6, 255, 0, 200}},
    // The top-level set, the channel shown's (the mock's one set).
    {R"({"gam":0.8,"in0":10,"in1":240})", {10, 0.8, 240}},
    {R"({"ch":"Green","gam":2.5,"out1":200})", {}, {}, {0, 2.5, 255, 0, 200}},
    // A channel's object wins over the top-level set; the top-level one is ch's alone.
    {R"({"Red":{"in0":10},"ch":"Red","in0":50})", {}, {10}},
    {R"({"Blue":{"gam":2},"ch":"Red","gam":3})", {}, {0, 3}, {}, {0, 2}},
};

// A channel darkened as far as it goes and RGB brightened back: what the tables drop below 2^-100 (adj_levels.cpp's
// kTinyEntry) comes back as 2^-10 or so (within 1e-3 of the formula).
const Case kFar[] = {
    {R"({"RGB":{"gam":9.99},"Red":{"gam":0.1}})", {0, 9.99}, {0, 0.1}},
    {R"({"Blue":{"gam":0.01},"RGB":{"gam":9.99}})", {0, 9.99}, {}, {}, {0, 0.01}},
};

// The largest distance of adj from the case's sets over the colours.
double worst(const Adjustment& adj, const Case& k, const std::vector<RGB>& px) {
  const Ref* own[3] = {&k.red, &k.green, &k.blue};
  double w = 0;
  for (const RGB& c : px) {
    const RGB got = adj.apply(c);
    for (int i = 0; i < 3; i++) w = std::max(w, std::fabs(got[size_t(i)] - k.rgb((*own[i])(c[size_t(i)]))));
  }
  return w;
}

std::string pick(const std::string& vals, int point, std::array<double, 3> sample, std::array<double, 3> target,
                 Mode mode = Mode::Rgb) {
  const std::optional<std::string> v = levels_from_pick(vals, point, sample, target, mode);
  CHECK(v);
  return *v;
}
// The colour (levels) the Levels of vals make of `sample` (levels), the furthest channel from want.
double off(const std::string& vals, std::array<double, 3> sample, std::array<double, 3> want, Mode mode = Mode::Rgb) {
  const AdjustmentPtr a = adjustment("Levels", vals, {mode, nullptr});
  CHECK(a);
  const RGB got = a->apply({float(sample[0] / 255), float(sample[1] / 255), float(sample[2] / 255)});
  double w = 0;
  for (int c = 0; c < 3; c++) w = std::max(w, std::fabs(got[size_t(c)] * 255 - want[size_t(c)]));
  return w;
}

}  // namespace

// Each set's formula (the task's: u = clamp((v - in0) / (in1 - in0)), u^(1 / gam), out0 + that (out1 - out0)), RGB's
// on each channel's: gammas 0.01 to 9.99, outputs turned over, inputs 2 apart, fractions, every channel at once.
TEST(test_levels_formula) {
  const std::vector<RGB> px = colours();
  double most = 0;
  for (const Case& k : kCases) {
    const AdjustmentPtr a = adjustment("Levels", k.vals);
    CHECK(a && !a->identity() && a->vals() == nlohmann::json::parse(k.vals).dump());
    const double w = worst(*a, k, px);
    if (w > 1e-4) std::printf("test_levels_formula: %s: %.3g from the formula\n", k.vals, w);
    CHECK(w <= 1e-4);
    most = std::max(most, w);
  }
  double far = 0;
  for (const Case& k : kFar) {
    const AdjustmentPtr a = adjustment("Levels", k.vals);
    CHECK(a && a->vals() == nlohmann::json::parse(k.vals).dump());
    far = std::max(far, worst(*a, k, px));
  }
  std::printf("test_levels_formula: %zu settings x %zu colours within %.2g of the formula, %zu darkened and "
              "brightened back within %.2g\n",
              std::size(kCases), px.size(), most, std::size(kFar), far);
  CHECK(far <= 1e-3);
}

// The defaults and sets at theirs are the identity, bit for bit; a channel's object at its defaults hides the
// top-level set; a grey document has RGB's set alone (R = G = B, R the result); in1 short of in0 + 2 is that.
TEST(test_levels_sets) {
  const std::vector<RGB> px = colours();
  for (const char* vals : {"{}", R"({"gam":1})", R"({"RGB":{}})", R"({"Red":{"gam":1,"in0":0}})", R"({"ch":"Red"})",
                           R"({"pre":"Lighter"})", R"({"Red":{},"ch":"Red","in0":10})"}) {
    const AdjustmentPtr a = adjustment("Levels", vals);
    CHECK(a && a->identity() && a->tables().empty());
    for (const RGB& c : px) CHECK(a->apply(c) == c);
  }
  CHECK(adjustment("Levels", R"({"gam":1})")->vals() == "{}");
  // Grey: RGB's alone.
  const AdjustmentPtr g = adjustment("Levels", R"({"RGB":{"gam":2},"Red":{"in0":100}})", {Mode::Gray, nullptr});
  CHECK(g && !g->identity() && adjustment("Levels", R"({"Red":{"in0":100}})", {Mode::Gray, nullptr})->identity());
  const Ref two{0, 2};
  double w = 0;
  for (int v = 0; v < 65536; v++) {
    const float u = float(double(v) / 65535);
    w = std::max(w, std::fabs(g->apply({u, u, u})[0] - two(u)));
  }
  CHECK(w <= 1e-5);
  // in1 at least in0 + 2.
  const AdjustmentPtr gap = adjustment("Levels", R"({"RGB":{"in0":200,"in1":100}})");
  const Case narrow{"", {200, 1, 202}};
  CHECK(gap && worst(*gap, narrow, px) <= 1e-4);
  // What does not read: a set that is not an object, or with a key of the wrong type.
  for (const char* vals : {R"({"Red":3})", R"({"Red":{"in0":"x"}})", R"({"RGB":[1]})", R"({"gam":"2"})",
                           R"({"Blue":{"gam":true}})", R"({"ch":"Alpha"})"})
    CHECK(!adjustment("Levels", vals));
  // Read back as written, numbers clamped inside a set as at the top level.
  CHECK(adjustment("Levels", R"({"Red":{"gam":20,"in0":-5}})")->vals() == R"({"Red":{"gam":20,"in0":-5}})");
  const AdjustmentPtr clamped = adjustment("Levels", R"({"Red":{"gam":20,"in0":-5}})");
  const Case top{"", {}, {0, 9.99}};
  CHECK(worst(*clamped, top, px) <= 1e-4);
}

// The eyedroppers: the sample comes out at the target (black, white: whole levels, as Photoshop keeps them), each
// channel's set where it came from; grey keeps the luminosity; a grey document's black and white set RGB's, its grey
// does nothing.
TEST(test_levels_pick) {
  const std::array<double, 3> black{0, 0, 0}, grey{128, 128, 128}, white{255, 255, 255};
  // Black and white points of each channel, targets by default and not.
  std::string v = pick("{}", 0, {30, 50, 70}, black);
  CHECK(v == R"({"Blue":{"in0":70},"Green":{"in0":50},"Red":{"in0":30}})");
  CHECK(off(v, {30, 50, 70}, black) <= 1e-4 && off(v, {29, 49, 69}, black) <= 1e-4);
  v = pick(v, 2, {200, 220, 240}, white);
  CHECK(v == R"({"Blue":{"in0":70,"in1":240},"Green":{"in0":50,"in1":220},"Red":{"in0":30,"in1":200}})");
  CHECK(off(v, {200, 220, 240}, white) <= 1e-4 && off(v, {30, 50, 70}, black) <= 1e-4);
  v = pick("{}", 0, {40, 60, 80}, {10, 12, 14});
  CHECK(v == R"({"Blue":{"in0":80,"out0":14},"Green":{"in0":60,"out0":12},"Red":{"in0":40,"out0":10}})");
  CHECK(off(v, {40, 60, 80}, {10, 12, 14}) <= 1e-4);
  v = pick("{}", 2, {180, 190, 200}, {240, 245, 250});
  CHECK(off(v, {180, 190, 200}, {240, 245, 250}) <= 1e-4);
  // Through RGB's set after the channels': the target reached to within the whole level the output is kept at.
  v = pick(R"({"RGB":{"gam":1.5,"out0":5}})", 0, {40, 60, 80}, {20, 20, 20});
  CHECK(off(v, {40, 60, 80}, {20, 20, 20}) <= 1);
  // A sample past the white point pushes it on: in1 kept 2 past in0.
  v = pick(R"({"Red":{"in1":100}})", 0, {120, 10, 10}, black);
  CHECK(v == R"({"Blue":{"in0":10},"Green":{"in0":10},"Red":{"in0":120,"in1":122}})");
  // Grey: neutral at the luminosity the sample had, through what the Levels did.
  const std::array<double, 3> s{180, 150, 120};
  const double lum = (kLumPercent[0] * s[0] + kLumPercent[1] * s[1] + kLumPercent[2] * s[2]) / 100.;
  v = pick("{}", 1, s, grey);
  CHECK(off(v, s, {lum, lum, lum}) <= 1e-3);
  const std::string before = R"({"RGB":{"gam":1.2,"in0":10,"in1":245},"Red":{"in0":5}})";
  const AdjustmentPtr b = adjustment("Levels", before);
  const RGB was = b->apply({float(s[0] / 255), float(s[1] / 255), float(s[2] / 255)});
  const double l2 = (kLumPercent[0] * was[0] + kLumPercent[1] * was[1] + kLumPercent[2] * was[2]) / 100. * 255;
  v = pick(before, 1, s, grey);
  CHECK(off(v, s, {l2, l2, l2}) <= 1e-3);
  CHECK(nlohmann::json::parse(v)["RGB"] == nlohmann::json::parse(before)["RGB"]);  // RGB's own set stays
  // A warm grey target: its colour, at the sample's luminosity.
  const std::array<double, 3> warm{140, 128, 110};
  const double tl = (kLumPercent[0] * warm[0] + kLumPercent[1] * warm[1] + kLumPercent[2] * warm[2]) / 100.;
  v = pick("{}", 1, s, warm);
  CHECK(off(v, s, {lum + warm[0] - tl, lum + warm[1] - tl, lum + warm[2] - tl}) <= 1e-3);
  // The top-level set, the channel shown's, stays where it is.
  v = pick(R"({"ch":"Red","gam":1.2,"in0":5})", 0, {30, 40, 50}, black);
  CHECK(v == R"({"Blue":{"in0":50},"Green":{"in0":40},"ch":"Red","gam":1.2,"in0":30})");
  // A grey document: RGB's set from R; no grey point.
  v = pick("{}", 0, {50, 50, 50}, black, Mode::Gray);  // ch RGB, no object: the top-level set
  CHECK(v == R"({"in0":50})" && off(v, {50, 50, 50}, black, Mode::Gray) <= 1e-4);
  CHECK(pick(R"({"RGB":{"in0":50}})", 1, {90, 90, 90}, grey, Mode::Gray) == R"({"RGB":{"in0":50}})");
  CHECK(pick(R"({"in1":250})", 2, {220, 0, 0}, white, Mode::Gray) == R"({"in1":220})");
  // What does not read.
  CHECK(!levels_from_pick("{", 0, black, black, Mode::Rgb) && !levels_from_pick("{}", 3, black, black, Mode::Rgb) &&
        !levels_from_pick(R"({"Red":1})", 0, black, black, Mode::Rgb));
}

// Image › Adjustments › Levels over a 2048 x 2048 RGBA U16 layer (D31's apply_adjustment()): each pixel as apply()
// makes it (samples as composite.cpp's unit() makes floats of them), rounded to its level, alpha kept; and the time.
TEST(test_levels_apply) {
  constexpr int W = 2048;
  std::vector<uint16_t> px(size_t(W) * W * 4);
  uint32_t h = 12345;
  for (uint16_t& s : px) s = uint16_t((h = h * 1664525u + 1013904223u) >> 16);
  TileGrid g(Format{Depth::U16, 4});
  g.write_rect(0, 0, W, W, reinterpret_cast<const uint8_t*>(px.data()), size_t(W) * 8);
  const AdjustmentPtr a = adjustment("Levels", kCases[11].vals);  // every channel's and RGB's
  CHECK(a);
  const auto t0 = std::chrono::steady_clock::now();
  const TileGrid out = apply_adjustment(g, *a);
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  std::vector<uint16_t> got(px.size());
  out.read_rect(0, 0, W, W, reinterpret_cast<uint8_t*>(got.data()), size_t(W) * 8);
  const auto unit = [](uint16_t v) { return float(double(v) * (1.0 / 65535)); };
  for (size_t i = 0; i < px.size(); i += 4 * 997) {
    const RGB o = a->apply({unit(px[i]), unit(px[i + 1]), unit(px[i + 2])});
    for (size_t c = 0; c < 3; c++) CHECK(got[i + c] == from_unit(o[c], Depth::U16));
    CHECK(got[i + 3] == px[i + 3]);
  }
  std::printf("test_levels_apply: 2048 x 2048 RGBA U16 in %.1f ms (%.0f Mpx/s)\n", ms, 4.194304 / ms * 1000);
}

// adjust_pixel() of pack() and tables() on lavapipe, against apply() to the bit: every case above, grey documents,
// and what the eyedroppers make.
TEST(test_gpu_levels) {
  test::AdjustCheck check;
  if (!check) return;
  int n = 0;
  std::vector<std::pair<std::string, Mode>> all;
  for (const Case& k : kCases) all.push_back({k.vals, Mode::Rgb});
  for (const Case& k : kFar) all.push_back({k.vals, Mode::Rgb});
  all.push_back({R"({"RGB":{"gam":2},"Red":{"in0":100}})", Mode::Gray});
  all.push_back({R"({"RGB":{"gam":0.3,"out0":250,"out1":5}})", Mode::Gray});
  all.push_back({pick("{}", 1, {180, 150, 120}, {128, 128, 128}), Mode::Rgb});
  all.push_back({pick(R"({"RGB":{"gam":1.5,"out0":5}})", 0, {40, 60, 80}, {20, 20, 20}), Mode::Rgb});
  for (const auto& [vals, mode] : all) {
    CHECK(check("Levels", vals, {mode, nullptr}));
    n++;
  }
  std::printf("test_gpu_levels: %d settings x %zu colours alike\n", n, check.pixels());
}

// D32's levels-* (tools/ps-golden/p1/adjust.jsx): a Levels layer at Normal 100 % over an input, as Image ›
// Adjustments › Levels of it, against Photoshop's image: within a level of 8 bits (the convention for a public
// formula). D32's sample index (p1/sample/adjust, no images) has its values read.
TEST(test_levels_ps) {
  for (const char* dir : {"p1/adjust", "p1/sample/adjust"}) {
    golden::Set set(dir);
    if (!set.has_index()) {
      if (dir == std::string("p1/adjust")) set.ready("test_levels_ps");
      continue;
    }
    if (!set.ready("test_levels_ps")) continue;
    int n = 0, compared = 0;
    for (const golden::Entry& e : set.entries()) {
      if (e.json.contains("apply")) continue;  // Image › Adjustments steps: D41's
      const std::vector<golden::SceneLayer> layers = set.layers(e);
      if (layers.size() != 2 || layers[0].kind != "pixel" || layers[1].kind != "adj" || layers[0].mask ||
          layers[1].mask)
        continue;
      const std::optional<LayerProps> p = props_from_json(layers[1].props);
      if (!p || p->adj != "Levels" || p->blend != Blend::Normal || p->opacity != 1 || p->fill != 1) continue;
      const AdjustmentPtr adj = adjustment(*p);
      if (!adj) std::printf("test_levels_ps: %s: %s does not read\n", e.name.c_str(), layers[1].props.c_str());
      CHECK(adj);
      n++;
      const golden::Image want = set.image(e);
      if (!want) continue;
      const golden::Image& in = *layers[0].pixels;
      const TileGrid out = apply_adjustment(*golden::grid(in, {Depth::U16, 4}), *adj);
      const golden::Diff d = golden::compare(golden::image(out, in.w, in.h), want, std::string(dir) + "/" + e.name);
      golden::print(e.name, d);
      CHECK(d.levels <= 1);
      compared++;
    }
    std::printf("test_levels_ps: %s: %d Levels cases, %d compared\n", dir, n, compared);
    set.skipped("test_levels_ps");
  }
}
