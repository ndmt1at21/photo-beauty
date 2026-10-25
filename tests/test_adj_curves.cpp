// Curves (D33, core/adj_curves.cpp): the curve through its points (within 1e-4 there) against a natural cubic spline
// written here apart, flat past the ends, clamped; two points a straight line; monotone points a monotone curve; a
// mapping of 256 outputs right at its levels and straight between; which curve goes where (RGB's after the channels',
// a grey document's RGB alone); what does not read; the identities; the GPU the CPU's to the bit (adjust_check.h);
// Photoshop's images (D32's curves-*, once B33 has made them).
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "adjust.h"
#include "adjust_check.h"
#include "check.h"
#include "golden.h"

using namespace pb;
using nlohmann::json;

namespace {

using RGB = std::array<float, 3>;
using Points = std::vector<std::array<double, 2>>;

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

// v (0..1) to 0..1: a channel's curve as the task says it, in double precision.
using Fn = std::function<double(double)>;
const Fn kSame = [](double v) { return v; };

// The natural cubic spline through the points (levels), flat past the ends, clamped to 0..255: its second derivatives
// by Gaussian elimination of the whole system (not adj_curves.cpp's sweep), then the textbook piece.
Fn spline(const Points& p) {
  const size_t n = p.size(), m = n - 2;
  std::vector<double> M(n, 0.0);
  if (n > 2) {
    std::vector<std::vector<double>> a(m, std::vector<double>(m + 1, 0.0));
    for (size_t r = 0; r < m; r++) {
      const size_t i = r + 1;
      const double h0 = p[i][0] - p[i - 1][0], h1 = p[i + 1][0] - p[i][0];
      if (r > 0) a[r][r - 1] = h0 / 6;
      a[r][r] = (h0 + h1) / 3;
      if (r + 1 < m) a[r][r + 1] = h1 / 6;
      a[r][m] = (p[i + 1][1] - p[i][1]) / h1 - (p[i][1] - p[i - 1][1]) / h0;
    }
    for (size_t c = 0; c < m; c++) {
      size_t best = c;
      for (size_t r = c + 1; r < m; r++)
        if (std::fabs(a[r][c]) > std::fabs(a[best][c])) best = r;
      std::swap(a[c], a[best]);
      for (size_t r = 0; r < m; r++) {
        if (r == c) continue;
        const double f = a[r][c] / a[c][c];
        for (size_t k = c; k <= m; k++) a[r][k] -= f * a[c][k];
      }
    }
    for (size_t r = 0; r < m; r++) M[r + 1] = a[r][m] / a[r][r];
  }
  return [p, M](double v) {
    const double x = v * 255;
    double y;
    if (x <= p.front()[0]) {
      y = p.front()[1];
    } else if (x >= p.back()[0]) {
      y = p.back()[1];
    } else {
      size_t i = 0;
      while (x > p[i + 1][0]) i++;
      const double x0 = p[i][0], x1 = p[i + 1][0], h = x1 - x0;
      y = M[i] * std::pow(x1 - x, 3) / (6 * h) + M[i + 1] * std::pow(x - x0, 3) / (6 * h) +
          (p[i][1] / h - M[i] * h / 6) * (x1 - x) + (p[i + 1][1] / h - M[i + 1] * h / 6) * (x - x0);
    }
    return std::clamp(y, 0., 255.) / 255;
  };
}

// A mapping's outputs, straight between them.
Fn mapping(const std::vector<double>& out) {
  return [out](double v) {
    const double x = std::clamp(v, 0., 1.) * 255;
    const int i = std::min(int(x), 254);
    return (out[size_t(i)] + (out[size_t(i) + 1] - out[size_t(i)]) * (x - i)) / 255;
  };
}

json points_json(const Points& p) {
  json a = json::array();
  for (const auto& q : p) a.push_back({q[0], q[1]});
  return a;
}

// Curves' vals, and each channel's curve: RGB's after the channel's own.
struct Case {
  std::string vals;
  Fn rgb = kSame, red = kSame, green = kSame, blue = kSame;
};
Case one(const Points& p) {  // RGB's curve alone
  Case c;
  c.vals = json{{"pts", {{"RGB", points_json(p)}}}}.dump();
  c.rgb = spline(p);
  return c;
}

const Points kS = {{0, 0}, {64, 40}, {192, 215}, {255, 255}};
const Points kEdges = {{0, 0}, {8, 32}, {247, 223}, {255, 255}};
const Points kEnds = {{30, 15}, {128, 140}, {220, 245}};
const Points kMany = {{0, 0}, {32, 20}, {64, 70}, {96, 90}, {128, 140}, {160, 150}, {200, 215}, {255, 255}};
const Points kSteep = {{0, 0}, {10, 200}, {255, 230}};  // past 255, clamped
const Points kClose = {{100, 50}, {103, 120}, {255, 255}};
const Points kFraction = {{0.5, 3.25}, {127.75, 140.5}, {254.5, 251}};
const Points kGentle = {{0, 20}, {70, 60}, {180, 200}, {255, 240}};
Points sixteen() {  // 16 points, the most a curve holds
  Points p;
  for (int i = 0; i < 16; i++) p.push_back({17. * i, std::round(255 * std::pow(17. * i / 255, 0.7))});
  return p;
}

std::vector<Case> cases() {
  std::vector<Case> out;
  for (const Points& p : {kS, kEdges, kEnds, kMany, kSteep, kClose, kFraction, kGentle, sixteen()}) out.push_back(one(p));
  Case c;  // channels alone
  c.vals = R"({"pts":{"Blue":[[0,40],[255,215]],"Red":[[0,0],[128,170],[255,255]]}})";
  c.red = spline({{0, 0}, {128, 170}, {255, 255}}), c.blue = spline({{0, 40}, {255, 215}});
  out.push_back(c);
  c = {};  // RGB after Green
  c.vals = R"({"pts":{"Green":[[0,0],[96,150],[255,255]],"RGB":[[0,0],[128,96],[255,255]]}})";
  c.rgb = spline({{0, 0}, {128, 96}, {255, 255}}), c.green = spline({{0, 0}, {96, 150}, {255, 255}});
  out.push_back(c);
  c = {};  // every channel, a mapping among them
  std::vector<double> m(256);
  for (int k = 0; k < 256; k++) m[size_t(k)] = 79 * (k / 64) + (k % 64) / 4.;  // steps
  json pts = {{"RGB", points_json(kS)}, {"Red", points_json(kEnds)}, {"Green", m}, {"Blue", points_json(kGentle)}};
  c.vals = json{{"pts", pts}, {"ch", "Green"}, {"pre", "Medium Contrast (RGB)"}}.dump();
  c.rgb = spline(kS), c.red = spline(kEnds), c.green = mapping(m), c.blue = spline(kGentle);
  out.push_back(c);
  c = {};  // a straight line, and turned over
  c.vals = R"({"pts":{"Blue":[[0,255],[255,0]],"RGB":[[30,50],[200,180]]}})";
  c.rgb = spline({{30, 50}, {200, 180}}), c.blue = spline({{0, 255}, {255, 0}});
  out.push_back(c);
  return out;
}

// The largest distance of adj from the case's curves over the colours.
double worst(const Adjustment& adj, const Case& k, const std::vector<RGB>& px) {
  const Fn* own[3] = {&k.red, &k.green, &k.blue};
  double w = 0;
  for (const RGB& c : px) {
    const RGB got = adj.apply(c);
    for (int i = 0; i < 3; i++) w = std::max(w, std::fabs(got[size_t(i)] - k.rgb((*own[i])(c[size_t(i)]))));
  }
  return w;
}

AdjustmentPtr rgb_curve(const json& pts, Mode mode = Mode::Rgb) {
  return adjustment("Curves", json{{"pts", {{"RGB", pts}}}}.dump(), {mode, nullptr});
}

}  // namespace

// Each case against the spline (or the mapping) written above, over the lattice and the 16-bit ramps; RGB's curve on
// each channel's.
TEST(test_curves_formula) {
  const std::vector<RGB> px = colours();
  double most = 0;
  for (const Case& k : cases()) {
    const AdjustmentPtr a = adjustment("Curves", k.vals);
    CHECK(a && !a->identity() && a->vals() == json::parse(k.vals).dump());
    const double w = worst(*a, k, px);
    if (w > 1e-4) std::printf("test_curves_formula: %s: %.3g from the formula\n", k.vals.c_str(), w);
    CHECK(w <= 1e-4);
    most = std::max(most, w);
  }
  std::printf("test_curves_formula: %zu settings x %zu colours within %.2g of the formula\n", cases().size(),
              px.size(), most);
}

// Through the points (within 1e-4), flat past the ends; two points a straight line; points rising a curve rising (all
// but kEdges', kSteep's and kClose's, whose splines swing); the mapping right at each level and straight between.
TEST(test_curves_points) {
  double most = 0;
  for (const Points& p : {kS, kEdges, kEnds, kMany, kFraction, kGentle, sixteen()}) {
    const AdjustmentPtr a = rgb_curve(points_json(p));
    for (const auto& q : p) {
      const float v = float(q[0] / 255);
      most = std::max(most, std::fabs(a->apply({v, v, v})[0] * 255. - q[1]) / 255);
    }
    const float below = float(std::max(0., p.front()[0] - 7) / 255), above = float(std::min(255., p.back()[0] + 7) / 255);
    CHECK(std::fabs(a->apply({below, below, below})[0] - p.front()[1] / 255) <= 1e-6);
    CHECK(std::fabs(a->apply({above, above, above})[0] - p.back()[1] / 255) <= 1e-6);
  }
  std::printf("test_curves_points: through the points within %.2g\n", most);
  CHECK(most <= 1e-4);
  // Two points: straight, flat past them.
  const AdjustmentPtr line = rgb_curve(json::parse("[[30,50],[200,180]]"));
  for (int v = 0; v < 65536; v += 7) {
    const float u = float(double(v) / 65535);
    const double x = std::clamp(u * 255., 30., 200.), want = (50 + (x - 30) * 130 / 170) / 255;
    CHECK(std::fabs(line->apply({u, u, u})[0] - want) <= 1e-6);
  }
  // Rising points, rising curve: on every 16-bit level (within the table's rounding, 1e-6).
  int rising = 0;
  for (const Points& p : {kS, kEnds, kMany, kFraction, kGentle, sixteen(), Points{{0, 0}, {128, 170}, {255, 255}},
                          Points{{0, 0}, {96, 150}, {255, 255}}, Points{{0, 10}, {255, 245}}}) {
    const AdjustmentPtr a = rgb_curve(points_json(p));
    float last = -1;
    for (int v = 0; v < 65536; v++) {
      const float u = float(double(v) / 65535), y = a->apply({u, u, u})[0];
      CHECK(y >= last - 1e-6f);
      last = std::max(last, y);
    }
    rising++;
  }
  // kEdges' spline swings, as the natural spline does (the hypothesis: Photoshop's images say).
  const AdjustmentPtr edges = rgb_curve(points_json(kEdges));
  const float at64 = edges->apply({64 / 255.f, 0, 0})[0] * 255, at192 = edges->apply({192 / 255.f, 0, 0})[0] * 255;
  std::printf("test_curves_points: %d rising curves rising; kEdges at 64 %.1f, at 192 %.1f\n", rising, at64, at192);
  CHECK(at64 > 140 && at192 < 115);
  // A mapping: at each level its output, straight between (a steep step, the levels turned over).
  std::vector<double> step(256), back(256);
  for (int k = 0; k < 256; k++) step[size_t(k)] = k < 128 ? 20 : 235, back[size_t(k)] = 255 - k;
  for (const std::vector<double>& m : {step, back}) {
    const AdjustmentPtr a = rgb_curve(m);
    CHECK(a && !a->identity());
    const Fn want = mapping(m);
    double w = 0;
    for (int v = 0; v < 65536; v++) {
      const float u = float(double(v) / 65535);
      w = std::max(w, std::fabs(a->apply({u, u, u})[0] - want(u)));
    }
    for (int k = 0; k < 256; k++) w = std::max(w, std::fabs(a->apply({k / 255.f, 0, 0})[0] - m[size_t(k)] / 255));
    std::printf("test_curves_points: a mapping within %.2g\n", w);
    CHECK(w <= 1e-5);
  }
}

// Which curve goes where, what reads, what is the identity.
TEST(test_curves_sets) {
  const std::vector<RGB> px = colours();
  // The identities: none, the diagonal by points or by mapping, a preset's name alone, a channel null.
  std::vector<double> same(256);
  for (int k = 0; k < 256; k++) same[size_t(k)] = k;
  for (const std::string& vals : {std::string("{}"), std::string(R"({"pts":{"RGB":[[0,0],[255,255]]}})"),
                                  std::string(R"({"pts":{"Red":[[0,0],[64,64],[255,255]]}})"),
                                  json{{"pts", {{"Green", same}}}}.dump(), std::string(R"j({"pre":"Lighter (RGB)"})j"),
                                  std::string(R"({"pts":{"Blue":null,"Alpha":[1]},"ch":"Blue"})"),
                                  std::string(R"({"pts":{"RGB":[[-10,-5],[300,400]]}})")}) {
    const AdjustmentPtr a = adjustment("Curves", vals);
    if (!a || !a->identity()) std::printf("test_curves_sets: %s is no identity\n", vals.c_str());
    CHECK(a && a->identity() && a->tables().empty());
    for (const RGB& c : px) CHECK(a->apply(c) == c);
  }
  // Points on the diagonal not reaching both ends: flat past them, no identity.
  const AdjustmentPtr mid = rgb_curve(json::parse("[[10,10],[200,200]]"));
  CHECK(mid && !mid->identity() && std::fabs(mid->apply({5 / 255.f, 0, 0})[0] - 10 / 255.f) <= 1e-6);
  // A grey document: RGB's curve alone.
  const std::string both = R"({"pts":{"RGB":[[0,0],[128,170],[255,255]],"Red":[[0,40],[255,215]]}})";
  const AdjustmentPtr g = adjustment("Curves", both, {Mode::Gray, nullptr});
  const Fn lighter = spline({{0, 0}, {128, 170}, {255, 255}});
  CHECK(g && !g->identity() && adjustment("Curves", R"({"pts":{"Red":[[0,40],[255,215]]}})", {Mode::Gray, nullptr})
                                   ->identity());
  double w = 0;
  for (int v = 0; v < 65536; v++) {
    const float u = float(double(v) / 65535);
    const RGB o = g->apply({u, u, u});
    CHECK(o[0] == o[1] && o[1] == o[2]);
    w = std::max(w, std::fabs(o[0] - lighter(u)));
  }
  CHECK(w <= 1e-5);
  // What does not read: more than 16 points, inputs not rising, points that are not pairs of numbers, one point, a
  // mapping not of 256 numbers, pts not an object.
  Points seventeen;
  for (int i = 0; i < 17; i++) seventeen.push_back({15. * i, 15. * i});
  CHECK(rgb_curve(points_json(sixteen())) && !rgb_curve(points_json(seventeen)));
  for (const char* pts : {"[[0,0],[128,140],[100,150],[255,255]]", "[[0,0],[128,140],[128,150],[255,255]]",
                          "[[0,0,1],[255,255]]", R"([[0,"a"],[255,255]])", "[0,0]", "[[128,128]]", "[]", "7",
                          R"("x")", "[[-10,0],[-5,10],[255,255]]"})
    CHECK(!rgb_curve(json::parse(pts)));
  CHECK(!rgb_curve(std::vector<double>(255, 1.0)) && !rgb_curve(std::vector<double>(257, 1.0)) &&
        rgb_curve(std::vector<double>(256, 1.0)));
  for (const char* vals : {R"({"pts":[1]})", R"({"pts":"x"})", R"({"pts":{"Green":{"a":1}}})", R"({"ch":"Gray"})"})
    CHECK(!adjustment("Curves", vals));
  // Read back as written; the numbers clamped where they work.
  CHECK(adjustment("Curves", R"({"pts":{"RGB":[[0,-20],[255,300]]}})")->vals() ==
        R"({"pts":{"RGB":[[0,-20],[255,300]]}})");
}

// Image › Adjustments › Curves over a 2048 x 2048 RGBA U16 layer (D31's apply_adjustment()): each pixel as apply()
// makes it (samples as composite.cpp's unit() makes floats of them), rounded to its level, alpha kept; and the time.
TEST(test_curves_apply) {
  constexpr int W = 2048;
  std::vector<uint16_t> px(size_t(W) * W * 4);
  uint32_t h = 12345;
  for (uint16_t& s : px) s = uint16_t((h = h * 1664525u + 1013904223u) >> 16);
  TileGrid g(Format{Depth::U16, 4});
  g.write_rect(0, 0, W, W, reinterpret_cast<const uint8_t*>(px.data()), size_t(W) * 8);
  const AdjustmentPtr a = adjustment("Curves", cases()[11].vals);  // every channel's and RGB's
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
  std::printf("test_curves_apply: 2048 x 2048 RGBA U16 in %.1f ms (%.0f Mpx/s)\n", ms, 4.194304 / ms * 1000);
}

// adjust_pixel() of pack() and tables() on lavapipe, against apply() to the bit: every case above and grey documents.
TEST(test_gpu_curves) {
  test::AdjustCheck check;
  if (!check) return;
  int n = 0;
  for (const Case& k : cases()) {
    CHECK(check("Curves", k.vals));
    n++;
  }
  for (const char* vals : {R"({"pts":{"RGB":[[0,0],[128,170],[255,255]],"Red":[[0,40],[255,215]]}})",
                           R"({"pts":{"RGB":[[20,240],[90,30],[255,200]]}})"}) {
    CHECK(check("Curves", vals, {Mode::Gray, nullptr}));
    n++;
  }
  std::printf("test_gpu_curves: %d settings x %zu colours alike\n", n, check.pixels());
}

// D32's curves-* (tools/ps-golden/p1/adjust.jsx): a Curves layer at Normal 100 % over an input, as Image ›
// Adjustments › Curves of it, against Photoshop's image: within a level of 8 bits. D32's sample index
// (p1/sample/adjust, no images) has its values read.
TEST(test_curves_ps) {
  for (const char* dir : {"p1/adjust", "p1/sample/adjust"}) {
    golden::Set set(dir);
    if (!set.has_index()) {
      if (dir == std::string("p1/adjust")) set.ready("test_curves_ps");
      continue;
    }
    if (!set.ready("test_curves_ps")) continue;
    int n = 0, compared = 0;
    for (const golden::Entry& e : set.entries()) {
      if (e.json.contains("apply")) continue;  // Image › Adjustments steps: D41's
      const std::vector<golden::SceneLayer> layers = set.layers(e);
      if (layers.size() != 2 || layers[0].kind != "pixel" || layers[1].kind != "adj" || layers[0].mask ||
          layers[1].mask)
        continue;
      const std::optional<LayerProps> p = props_from_json(layers[1].props);
      if (!p || p->adj != "Curves" || p->blend != Blend::Normal || p->opacity != 1 || p->fill != 1) continue;
      const AdjustmentPtr adj = adjustment(*p);
      if (!adj) std::printf("test_curves_ps: %s: %s does not read\n", e.name.c_str(), layers[1].props.c_str());
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
    std::printf("test_curves_ps: %s: %d Curves cases, %d compared\n", dir, n, compared);
    set.skipped("test_curves_ps");
  }
}
