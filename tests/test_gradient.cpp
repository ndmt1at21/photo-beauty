// Gradients (gradient.h, D30) and the hash under their dither (hash.h): the JSON the UI keeps; the table each method
// mixes (stops exact, Classic linear, midpoints, Linear's light, Perceptual's Oklab, Smooth, Stripes' edges, opacity
// apart from colour, grey documents); t of the five shapes at known points, Reverse, a fill's angle, scale and offset;
// render_gradient() in every format; the dither's reach and mean; pcg4d's values and statistics; then
// shaders/gradient_check.slang against all of it to the bit on lavapipe, and Photoshop's images (D32) once there are
// some.
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "check.h"
#include "golden.h"
#include "gradient.h"
#include "gradient_check.spv.h"
#include "hash.h"
#include "render.h"

using namespace pb;

namespace {

constexpr int kSteps = GradientLut::kSteps;
constexpr int S = kTileSize;
constexpr double kPi = 3.14159265358979323846;
using Rgb = std::array<double, 3>;

bool near(double a, double b, double eps) { return std::fabs(a - b) <= eps; }
uint32_t bits(float f) {
  uint32_t u;
  std::memcpy(&u, &f, 4);
  return u;
}
double srgb_linear(double e) { return e <= 0.04045 ? e / 12.92 : std::pow((e + 0.055) / 1.055, 2.4); }

Gradient two(const Rgb& a, const Rgb& b, double smoothness = 0, double m = 0.5) {
  Gradient g;
  g.colors = {{0, a, m}, {1, b, 0.5}};
  g.smoothness = smoothness;
  return g;
}
float t_at(GradientStyle s, GradientLine l, int x, int y, bool reverse = false) {
  return gradient_t(plane({s, l, reverse}), x, y);
}

// A gradient with everything: five colour stops (two at one place, an edge), midpoints off centre, opacity stops apart.
Gradient busy() {
  Gradient g;
  g.colors = {{0, {10, 200, 30}, 0.5},           {1229. / 4096, {250, 5, 128}, 0.3}, {0.5, {0, 0, 0}, 0.7},
              {0.5, {255, 255, 255}, 0.5},       {1, {77, 88, 99}, 0.5}};
  g.alphas = {{0, 1, 0.5}, {0.25, 0.5, 0.2}, {1, 0, 0.5}};
  g.smoothness = 60;
  return g;
}

}  // namespace

TEST(test_gradient_json) {
  // The UI's GradientBar value (Color.js gradient()): a midpoint off centre, a background stop, opacity to nothing.
  const auto g = gradient_from_json(
      R"({"colors": [{"p": 0, "c": [233, 199, 179], "m": 0.5}, {"p": 0.6, "c": [158, 47, 69], "m": 0.3},
                     {"p": 1, "type": "bg"}],
          "alphas": [{"p": 0, "a": 1, "m": 0.5}, {"p": 1, "a": 0, "m": 0.5}], "extra": 1})");
  CHECK(g && g->colors.size() == 3 && g->alphas.size() == 2);
  CHECK(g->colors[1].p == 0.6 && g->colors[1].m == 0.3 && g->colors[1].c == (Rgb{158, 47, 69}));
  CHECK(g->colors[2].type == ColorStop::Type::Background && g->colors[2].c == (Rgb{255, 255, 255}));
  CHECK(g->colors[0].type == ColorStop::Type::User && g->alphas[1].a == 0);
  CHECK(g->smoothness == 100 && g->name.empty());
  // Written back: members sorted, no spaces; read again, the same bytes.
  const std::string j = to_json(*g);
  CHECK(j.find(' ') == std::string::npos);
  CHECK(j.find("\"alphas\"") < j.find("\"colors\"") && j.find("\"colors\"") < j.find("\"smoothness\""));
  CHECK(j.find("\"type\":\"bg\"") != std::string::npos && j.find("extra") == std::string::npos);
  const auto back = gradient_from_json(j);
  CHECK(back && to_json(*back) == j && back->colors[1].c == g->colors[1].c && back->colors[2].type == g->colors[2].type);
  // Defaults and clamping: m 0.05..0.95, p 0..1, c 0..255, opaque, Smoothness at most 100; a foreground stop's colour
  // is black until with_colors().
  const auto d = gradient_from_json(R"({"colors": [{"p": -1, "c": [300, -5, 12.5], "m": 0}, {"p": 2, "c": [0, 0, 0],
      "m": 1}, {"p": 0.5, "type": "fg"}], "smoothness": 250, "name": "Ramp"})");
  CHECK(d && d->colors[0].p == 0 && d->colors[1].p == 1 && d->colors[0].c == (Rgb{255, 0, 12.5}));
  CHECK(d->colors[0].m == 0.05 && d->colors[1].m == 0.95 && d->smoothness == 100 && d->name == "Ramp");
  CHECK(d->colors[2].type == ColorStop::Type::Foreground && d->colors[2].c == (Rgb{0, 0, 0}));
  CHECK(d->alphas.size() == 2 && d->alphas[0].a == 1 && d->alphas[1].a == 1 && d->alphas[1].p == 1);
  CHECK(to_json(*gradient_from_json(to_json(*d))) == to_json(*d));
  for (const char* bad : {"", "[]", "{}", "{\"colors\": []}", "{\"colors\": {}}", "{\"colors\": [{\"c\": [0, 0, 0]}]}",
                          "{\"colors\": [{\"p\": 0}]}", "{\"colors\": [{\"p\": 0, \"c\": [0, 0]}]}",
                          "{\"colors\": [{\"p\": \"0\", \"c\": [0, 0, 0]}]}",
                          "{\"colors\": [{\"p\": 0, \"c\": [0, 0, \"0\"]}]}",
                          "{\"colors\": [{\"p\": 0, \"c\": [0, 0, 0], \"type\": \"fore\"}]}",
                          "{\"colors\": [{\"p\": 0, \"c\": [0, 0, 0], \"m\": null}]}",
                          "{\"colors\": [{\"p\": 0, \"c\": [0, 0, 0]}], \"alphas\": [{\"p\": 0}]}",
                          "{\"colors\": [{\"p\": 0, \"c\": [0, 0, 0]}], \"alphas\": {}}",
                          "{\"colors\": [{\"p\": 0, \"c\": [0, 0, 0]}], \"smoothness\": true}",
                          "{\"colors\": [{\"p\": 0, \"c\": [0, 0, 0]}], \"name\": 3}"})
    CHECK(!gradient_from_json(bad));

  // The dynamic stops take the colours given and become user stops; user stops keep theirs.
  Gradient fb;  // Foreground to Background
  fb.colors.push_back({0.5, {1, 2, 3}, 0.5});
  const Gradient fc = with_colors(fb, {10, 20, 30}, {40, 50, 60});
  CHECK(fc.colors[0].c == (Rgb{10, 20, 30}) && fc.colors[1].c == (Rgb{40, 50, 60}) && fc.colors[2].c == (Rgb{1, 2, 3}));
  for (const ColorStop& s : fc.colors) CHECK(s.type == ColorStop::Type::User);

  // Names, as the UI's lists have them (ToolSpec's and AdjustSpec's "method", LayerStyleDialog's and GTYPES' styles).
  for (size_t i = 0; i < std::size(kGradientMethodNames); i++)
    CHECK(gradient_method(kGradientMethodNames[i]) == GradientMethod(i));
  for (size_t i = 0; i < std::size(kGradientStyleNames); i++)
    CHECK(gradient_style(kGradientStyleNames[i]) == GradientStyle(i));
  CHECK(gradient_method("Perceptual") == GradientMethod::Perceptual && gradient_method("Stripes") == GradientMethod::Stripes);
  CHECK(!gradient_method("Noise") && !gradient_method("") && gradient_style("Diamond") == GradientStyle::Diamond);
  CHECK(!gradient_style("conic"));

  // A fill's or an overlay's keys (LayerStyleDialog's gradientOverlay), the caller's own skipped.
  const auto f = gradient_fill_from(nlohmann::json::parse(
      R"({"on": true, "blend": "multiply", "opacity": 80, "gradient": {"colors": [{"p": 0, "c": [1, 2, 3]}]},
          "reverse": true, "dither": true, "style": "radial", "align": false, "angle": 30, "scale": 50,
          "offset": [10, -20], "method": "Linear"})"));
  CHECK(f && f->reverse && f->dither && !f->align && f->style == GradientStyle::Radial);
  CHECK(f->method == GradientMethod::Linear && f->angle == 30 && f->scale == 50 && f->offset[0] == 10 && f->offset[1] == -20);
  CHECK(f->gradient.colors.size() == 1 && f->gradient.colors[0].c == (Rgb{1, 2, 3}));
  const auto def = gradient_fill_from(nlohmann::json::object());
  CHECK(def && def->style == GradientStyle::Linear && def->method == GradientMethod::Perceptual && def->angle == 90);
  CHECK(def->scale == 100 && def->align && !def->reverse && !def->dither && def->offset[0] == 0);
  CHECK(def->gradient.colors.size() == 2 && def->gradient.colors[0].type == ColorStop::Type::Foreground);
  const std::string fj = to_json(*f);
  const auto f2 = gradient_fill_from(nlohmann::json::parse(fj));
  CHECK(f2 && to_json(*f2) == fj && fj.find("\"method\":\"linear\"") != std::string::npos);
  CHECK(gradient_fill_from(nlohmann::json::parse(R"({"scale": 0})"))->scale == 1);
  for (const char* bad : {R"({"style": "conic"})", R"({"method": 3})", R"({"scale": "x"})", R"({"offset": [1]})",
                          R"({"reverse": 1})", R"({"gradient": [0, 0]})", R"({"angle": null})", "[]"})
    CHECK(!gradient_fill_from(nlohmann::json::parse(bad)));
}

TEST(test_gradient_lut) {
  // Classic, two stops, no Smoothness: linear in t, at every entry and between them.
  {
    const auto lut = gradient_lut(two({0, 64, 255}, {255, 128, 0}), GradientMethod::Classic);
    for (int i = 0; i <= kSteps; i++) {
      const double t = double(i) / kSteps;
      const std::array<float, 4>& c = lut->rgba[size_t(i)];
      CHECK(near(c[0], t, 1e-6) && near(c[1], (64 + 64 * t) / 255, 1e-6) && near(c[2], 1 - t, 1e-6) && c[3] == 1);
    }
    CHECK(lut->rgba[kSteps + 1] == lut->rgba[kSteps]);
    std::mt19937 rng(1);
    for (int n = 0; n < 1000; n++) {
      const float t = std::uniform_real_distribution<float>(0, 1)(rng);
      CHECK(near(lut->at(t)[0], t, 1e-6) && near(lut->at(t)[2], 1 - t, 1e-6));
    }
    // t out of range: the ends; NaN: the start.
    CHECK(lut->at(-1) == lut->rgba[0] && lut->at(2) == lut->rgba[kSteps] && lut->at(1) == lut->rgba[kSteps]);
    CHECK(lut->at(std::numeric_limits<float>::quiet_NaN()) == lut->rgba[0]);
    CHECK(lut->at(std::numeric_limits<float>::infinity()) == lut->rgba[kSteps]);
  }
  // At a stop its colour, exactly, whatever the method: stops at k / 4096 land on entries, and at(k / 4096) is the
  // entry. Two stops at one place: the later one from there on.
  for (GradientMethod m : {GradientMethod::Perceptual, GradientMethod::Linear, GradientMethod::Classic,
                           GradientMethod::Smooth, GradientMethod::Stripes}) {
    const Gradient g = busy();
    const auto lut = gradient_lut(g, m);
    for (size_t s = 0; s < g.colors.size(); s++) {
      if (s == 2) continue;  // hidden by the stop after it, at the same place
      const int e = int(std::lround(g.colors[s].p * kSteps));
      for (size_t k = 0; k < 3; k++) {
        CHECK(lut->rgba[size_t(e)][k] == float(g.colors[s].c[k] / 255));
        CHECK(lut->at(float(e) / kSteps)[k] == float(g.colors[s].c[k] / 255));
      }
    }
    for (const AlphaStop& a : g.alphas) CHECK(lut->rgba[size_t(std::lround(a.p * kSteps))][3] == float(a.a));
    // Before the edge, the black stop's side; at it, white.
    CHECK(lut->rgba[2048][0] == 1 && lut->rgba[2047][0] < 0.05f);
  }
  // A midpoint m: half and half at m of the way, each method in its own terms; a quarter of the way at m / 2.
  {
    const auto classic = gradient_lut(two({0, 0, 0}, {255, 255, 255}, 0, 0.25), GradientMethod::Classic);
    CHECK(classic->rgba[1024][0] == 0.5f && classic->rgba[512][0] == 0.25f && classic->rgba[2560][0] == 0.75f);
    const auto linear = gradient_lut(two({0, 0, 0}, {255, 255, 255}, 0, 0.25), GradientMethod::Linear);
    CHECK(near(srgb_linear(linear->rgba[1024][0]), 0.5, 2e-5));
    const auto perceptual = gradient_lut(two({0, 0, 0}, {255, 255, 255}, 0, 0.25), GradientMethod::Perceptual);
    CHECK(near(perceptual->rgba[1024][0], 0.388572, 1e-4));  // Oklab L 0.5: luminance 0.125
    const auto smooth = gradient_lut(two({0, 0, 0}, {255, 255, 255}, 0, 0.25), GradientMethod::Smooth);
    CHECK(near(smooth->rgba[1024][0], 0.388572, 1e-4));  // smoothstep(0.5) = 0.5
    Gradient a = two({0, 0, 0}, {0, 0, 0});
    a.alphas = {{0, 1, 0.75}, {1, 0, 0.5}};
    CHECK(gradient_lut(a, GradientMethod::Perceptual)->rgba[3072][3] == 0.5f);
    CHECK(gradient_lut(a, GradientMethod::Classic)->rgba[3072][3] == 0.5f);
  }
  // Linear, black to white: halfway is half the light, in the document's own curve (sRGB's; Adobe RGB's gamma
  // 563/256). Classic halfway: half the encoded value. Perceptual: Oklab's L halfway, neutral stays neutral.
  {
    const Gradient bw = two({0, 0, 0}, {255, 255, 255});
    const auto srgb = gradient_lut(bw, GradientMethod::Linear);
    CHECK(near(srgb_linear(srgb->rgba[2048][0]), 0.5, 2e-5) && near(srgb->rgba[2048][0], 0.735357, 1e-4));
    for (int i = 0; i <= kSteps; i += 64) CHECK(near(srgb_linear(srgb->rgba[size_t(i)][1]), double(i) / kSteps, 3e-5));
    GradientSpace adobe;
    adobe.profile = source_profile(icc_profile(ColorSpace::AdobeRgb));
    const auto argb = gradient_lut(bw, GradientMethod::Linear, adobe);
    CHECK(near(std::pow(double(argb->rgba[2048][0]), 563. / 256), 0.5, 2e-5));
    CHECK(gradient_lut(bw, GradientMethod::Classic)->rgba[2048][0] == 0.5f);
    const auto p = gradient_lut(bw, GradientMethod::Perceptual);
    CHECK(near(p->rgba[2048][0], 0.388572, 1e-4) && near(p->rgba[2048][1], p->rgba[2048][0], 1e-5));
    CHECK(near(p->rgba[2048][2], p->rgba[2048][0], 1e-5));
    // Red to blue halfway, as Ottosson's sRGB matrices put it (worked out apart, in Python): not Classic's dark purple.
    const auto rb = gradient_lut(two({255, 0, 0}, {0, 0, 255}), GradientMethod::Perceptual);
    CHECK(near(rb->rgba[2048][0], 0.5504, 3e-3) && near(rb->rgba[2048][1], 0.3256, 3e-3));
    CHECK(near(rb->rgba[2048][2], 0.6365, 3e-3));
  }
  // Smoothness (Classic only, hypothesis: eased to smoothstep at 100 %): 0 linear, 100 smoothstep, flat at the stops.
  {
    const auto s100 = gradient_lut(two({0, 0, 0}, {255, 255, 255}, 100), GradientMethod::Classic);
    const auto s50 = gradient_lut(two({0, 0, 0}, {255, 255, 255}, 50), GradientMethod::Classic);
    const auto lin = gradient_lut(two({0, 0, 0}, {255, 255, 255}, 100), GradientMethod::Linear);
    CHECK(near(s100->rgba[1024][0], 0.15625, 1e-6) && near(s50->rgba[1024][0], (0.25 + 0.15625) / 2, 1e-6));
    CHECK(s100->rgba[2048][0] == 0.5f && s100->rgba[1][0] < 1e-6f);
    CHECK(near(srgb_linear(lin->rgba[1024][0]), 0.25, 3e-5));  // Smoothness leaves Linear alone
    const auto sm = gradient_lut(two({0, 0, 0}, {255, 255, 255}, 0), GradientMethod::Smooth);
    CHECK(sm->rgba[1][0] < 1e-3f && sm->rgba[kSteps - 1][0] > 0.999f);
  }
  // Stripes: no mixing, the next stop's colour from the midpoint on (hypothesis), opacity likewise.
  {
    Gradient g = two({0, 0, 0}, {255, 255, 255});
    g.alphas = {{0, 1, 0.5}, {1, 0.2, 0.5}};
    const auto s = gradient_lut(g, GradientMethod::Stripes);
    for (int i = 0; i <= kSteps; i++) {
      CHECK(s->rgba[size_t(i)][0] == (i < 2048 ? 0.f : 1.f));
      CHECK(s->rgba[size_t(i)][3] == (i < 2048 ? 1.f : 0.2f));
    }
    const auto s3 = gradient_lut(two({0, 0, 0}, {255, 255, 255}, 0, 0.3), GradientMethod::Stripes);
    CHECK(s3->rgba[1228][0] == 0 && s3->rgba[1229][0] == 1);  // 0.3 lies between entries 1228 and 1229
  }
  // A grey document: each stop its grey (hypothesis: Rec. 601 weights), R = G = B everywhere.
  {
    GradientSpace grey;
    grey.grey = true;
    for (GradientMethod m : {GradientMethod::Perceptual, GradientMethod::Linear, GradientMethod::Classic}) {
      const auto lut = gradient_lut(two({255, 0, 0}, {0, 0, 255}), m, grey);
      CHECK(near(lut->rgba[0][0], 0.299, 1e-6) && near(lut->rgba[kSteps][0], 0.114, 1e-6));
      for (int i = 0; i <= kSteps; i += 37)
        CHECK(lut->rgba[size_t(i)][0] == lut->rgba[size_t(i)][1] && lut->rgba[size_t(i)][1] == lut->rgba[size_t(i)][2]);
    }
  }
  // One stop: its colour throughout.
  {
    Gradient one;
    one.colors = {{0.3, {51, 102, 153}, 0.5}};
    const auto lut = gradient_lut(one, GradientMethod::Perceptual);
    for (int i = 0; i <= kSteps; i += 512) CHECK(lut->rgba[size_t(i)][1] == float(102. / 255));
  }
  bool threw = false;
  try {
    Gradient none;
    none.colors.clear();
    gradient_lut(none, GradientMethod::Classic);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  CHECK(threw);
}

TEST(test_gradient_shapes) {
  // Pixel (x, y) counts at its centre: a line from (0.5, 0.5) to (128.5, 0.5) puts pixel x at t = x / 128.
  const GradientLine h{0.5, 0.5, 128.5, 0.5};
  const GradientStyle L = GradientStyle::Linear, R = GradientStyle::Radial, A = GradientStyle::Angle,
                      F = GradientStyle::Reflected, D = GradientStyle::Diamond;
  CHECK(t_at(L, h, 0, 0) == 0 && t_at(L, h, 128, 0) == 1 && t_at(L, h, 64, 0) == 0.5f && t_at(L, h, 32, 37) == 0.25f);
  CHECK(t_at(L, h, -10, 5) == 0 && t_at(L, h, 150, -3) == 1 && t_at(L, h, 33, 0) == 33.f / 128);
  CHECK(t_at(L, h, 32, 0, true) == 0.75f && t_at(L, h, -10, 0, true) == 1 && t_at(L, h, 150, 0, true) == 0);
  // Any direction: diagonal down and right, up and left.
  CHECK(t_at(L, {0.5, 0.5, 64.5, 64.5}, 32, 32) == 0.5f && t_at(L, {0.5, 0.5, 64.5, 64.5}, 64, 0) == 0.5f);
  CHECK(t_at(L, {100.5, 100.5, 0.5, 0.5}, 25, 25) == 0.75f);
  // Radial: the distance from the start over the line's length, the same every way.
  const GradientLine c{50.5, 50.5, 90.5, 50.5};
  CHECK(t_at(R, c, 50, 50) == 0 && t_at(R, c, 90, 50) == 1 && t_at(R, c, 50, 70) == 0.5f && t_at(R, c, 30, 50) == 0.5f);
  CHECK(t_at(R, c, 74, 82) == 1 && t_at(R, c, 74, 18) == 1 && near(t_at(R, c, 62, 34), 0.5, 1e-6));  // 12, 16: 20
  CHECK(t_at(R, c, 100, 100) == 1 && t_at(R, c, 50, 50, true) == 1);
  // Angle: from the line, counterclockwise on screen (up from a line to the right), a turn = 1.
  CHECK(t_at(A, c, 70, 50) == 0 && near(t_at(A, c, 50, 30), 0.25, 1e-6) && near(t_at(A, c, 30, 50), 0.5, 1e-6));
  CHECK(near(t_at(A, c, 50, 70), 0.75, 1e-6) && near(t_at(A, c, 70, 30), 0.125, 1e-6) && near(t_at(A, c, 30, 30), 0.375, 1e-6));
  CHECK(near(t_at(A, c, 30, 70), 0.625, 1e-6) && near(t_at(A, c, 70, 70), 0.875, 1e-6));
  CHECK(near(t_at(A, c, 50 + 40, 50 - 23), std::atan2(23., 40.) / (2 * kPi), 1e-7));  // off the octants' edges
  CHECK(near(t_at(A, c, 50 - 7, 50 + 31), 1 - std::atan2(31., -7.) / (2 * kPi), 1e-7));
  CHECK(t_at(A, c, 90, 51) > 0.99f && near(t_at(A, c, 50, 30, true), 0.75, 1e-6));  // just past a turn; reversed
  const GradientLine up{50.5, 50.5, 50.5, 10.5};  // a line straight up: left is a quarter turn on
  CHECK(t_at(A, up, 50, 30) == 0 && near(t_at(A, up, 30, 50), 0.25, 1e-6) && near(t_at(A, up, 70, 50), 0.75, 1e-6));
  CHECK(t_at(A, c, 50, 50) == 0);  // the start itself
  // Reflected: Linear both ways from the start.
  const GradientLine r{50.5, 0.5, 60.5, 0.5};
  CHECK(t_at(F, r, 55, 0) == 0.5f && t_at(F, r, 45, 7) == 0.5f && t_at(F, r, 50, 0) == 0 && t_at(F, r, 40, 0) == 1);
  CHECK(t_at(F, r, 70, 0) == 1 && t_at(F, r, 45, 0, true) == 0.5f && t_at(F, r, 50, 0, true) == 1);
  // Diamond: |along| + |across| over the length, a square about the start with a corner at the end.
  const GradientLine d{50.5, 50.5, 70.5, 50.5};
  CHECK(t_at(D, d, 60, 50) == 0.5f && t_at(D, d, 50, 60) == 0.5f && t_at(D, d, 55, 55) == 0.5f && t_at(D, d, 45, 45) == 0.5f);
  CHECK(t_at(D, d, 65, 55) == 1 && t_at(D, d, 70, 50) == 1 && t_at(D, d, 50, 50) == 0 && t_at(D, d, 40, 55) == 0.75f);
  const GradientLine d45{50.5, 50.5, 60.5, 60.5};  // turned 45°: a square with sides along the axes, 10 out
  for (int y = 35; y < 66; y += 3)
    for (int x = 36; x < 66; x += 2)
      CHECK(near(t_at(D, d45, x, y), std::min(10, std::max(std::abs(x - 50), std::abs(y - 50))) / 10., 1e-6));
  // A line of no length: t = 0, everywhere.
  for (GradientStyle s : {L, R, A, F, D}) {
    CHECK(t_at(s, {5, 5, 5, 5}, 0, 0) == 0 && t_at(s, {5, 5, 5, 5}, 99, -7) == 0);
    CHECK(t_at(s, {5, 5, 5, 5}, 99, -7, true) == 1);
  }

  // At mip level 2, a pixel is 4 across: t at its centre is t of the full-size point there.
  const GradientShape big{R, {13.25, -40.5, 513.75, 260}, false};
  const GradientPlane p0 = plane(big), p2 = plane(at_level(big, 2));
  for (int y = -20; y < 80; y += 7)
    for (int x = -10; x < 140; x += 11) {
      const double dx = (x + 0.5) * 4 - 13.25, dy = (y + 0.5) * 4 + 40.5;
      CHECK(near(gradient_t(p2, x, y), std::min(1., std::hypot(dx, dy) / std::hypot(500.5, 300.5)), 2e-6));
    }
  CHECK(gradient_t(p0, 13, -41) < 1e-3f);

  // A fill's line over box (10, 20, 200, 100), centre (110, 70).
  const auto line = [](GradientStyle s, double angle, double scale = 100, std::array<double, 2> off = {0, 0}) {
    GradientFill f;
    f.style = s, f.angle = angle, f.scale = scale, f.offset = off;
    return fill_line(f, 10, 20, 200, 100);
  };
  const auto same = [](GradientLine a, GradientLine b) {
    return near(a.x0, b.x0, 1e-9) && near(a.y0, b.y0, 1e-9) && near(a.x1, b.x1, 1e-9) && near(a.y1, b.y1, 1e-9);
  };
  CHECK(same(line(L, 0), {10, 70, 210, 70}) && same(line(L, 90), {110, 120, 110, 20}));  // 90°: bottom to top
  CHECK(same(line(L, 180), {210, 70, 10, 70}) && same(line(L, -90), {110, 20, 110, 120}));
  CHECK(same(line(L, 0, 50), {60, 70, 160, 70}) && same(line(L, 90, 150), {110, 145, 110, -5}));
  CHECK(same(line(L, 0, 100, {10, -20}), {30, 50, 230, 50}));  // the centre to (130, 50)
  CHECK(same(line(R, 90), {110, 70, 110, 20}) && same(line(R, 0), {110, 70, 210, 70}));  // the centre out, half
  CHECK(same(line(A, 0, 50), {110, 70, 160, 70}) && same(line(F, 90), {110, 70, 110, 20}));
  CHECK(same(line(D, 180), {110, 70, 10, 70}));
  {  // 45° (hypothesis: CSS's corners): the box's corners at t = 0 and 1
    const GradientLine l = line(L, 45);
    const auto t = [&](double x, double y) {
      return ((x - l.x0) * (l.x1 - l.x0) + (y - l.y0) * (l.y1 - l.y0)) /
             ((l.x1 - l.x0) * (l.x1 - l.x0) + (l.y1 - l.y0) * (l.y1 - l.y0));
    };
    CHECK(near(t(10, 120), 0, 1e-9) && near(t(210, 20), 1, 1e-9) && near(t(110, 70), 0.5, 1e-9));
    CHECK(near(t(10, 20), 1. / 3, 1e-9));  // the top left corner: (-100, -50) along (1, -1) / sqrt 2, over 300 / sqrt 2
  }
  GradientFill rf;
  rf.reverse = true, rf.style = R;
  const GradientShape fs = fill_shape(rf, 0, 0, 100, 100);
  CHECK(fs.reverse && fs.style == R && same(fs.line, {50, 50, 50, 0}));
}

TEST(test_gradient_render) {
  // A ramp over two tiles in every format: render_gradient() is gradient_pixels() tile by tile; tiles all alike are
  // solid; the ramp is linear; a stop's colour lands exactly on its pixel.
  const auto ramp = gradient_lut(two({0, 0, 0}, {255, 255, 255}), GradientMethod::Classic);
  const GradientRender r{ramp, {GradientStyle::Linear, {0.5, 0.5, 2 * S + 0.5, 0.5}, false}, false, kGradientSeed};
  for (Format f : {Format{Depth::U8, 4}, Format{Depth::U16, 4}, Format{Depth::U8, 2}, Format{Depth::U16, 2},
                   Format{Depth::U8, 1}, Format{Depth::U16, 1}}) {
    TileGrid grid(f);
    render_gradient(r, grid, {-1, -1, 3, 2});
    CHECK(grid.tiles().size() == (f.channels == 1 ? 9u : 12u));  // one channel: black is the fill, so empty
    const int m = max_value(f.depth);
    for (int ty = -1; ty < 2; ty++)
      for (int tx = -1; tx < 3; tx++) {
        const Tile* t = grid.get(tx, ty);
        CHECK(f.channels == 1 && tx == -1 ? !t : (t && t->is_solid() == (tx == -1 || tx == 2)));
        std::vector<uint8_t> want(size_t(S) * S * size_t(f.bytes())), got(want.size());
        gradient_pixels(r, f, tx * S, ty * S, S, S, want.data(), size_t(S) * size_t(f.bytes()));
        grid.read_rect(tx * S, ty * S, S, S, got.data(), size_t(S) * size_t(f.bytes()));
        CHECK(got == want);
      }
    for (int x = -5; x < 2 * S + 5; x++) {  // linear: within half a level of t
      uint8_t px[8];
      grid.read_rect(x, 3, 1, 1, px, 8);
      int v = px[0];
      if (f.depth == Depth::U16) {
        uint16_t s;
        std::memcpy(&s, px, 2);
        v = s;
      }
      const double t = std::clamp(double(x) / (2 * S), 0., 1.);
      CHECK(std::fabs(v - t * m) <= 0.5 + 1. / 128);  // from_unit() rounds v x max to a float first: ties go even
    }
    if (f.channels > 1) {  // opaque
      uint8_t px[8];
      grid.read_rect(100, 0, 1, 1, px, 8);
      CHECK(f.depth == Depth::U8 ? px[f.channels - 1] == 255 : (px[2 * f.channels - 1] == 255 && px[2 * f.channels - 2] == 255));
    }
  }
  // Two and one channels: the grey and the alpha of four (a grey table, R = G = B).
  {
    GradientSpace grey;
    grey.grey = true;
    Gradient g = busy();
    const GradientRender gr{gradient_lut(g, GradientMethod::Perceptual, grey),
                            {GradientStyle::Angle, {100.3, 80.7, 140, 20}, true}, true, 77};
    for (Depth dep : {Depth::U8, Depth::U16}) {
      const int b = dep == Depth::U16 ? 2 : 1;
      std::vector<uint8_t> four(size_t(S) * S * 4 * size_t(b)), twoc(size_t(S) * S * 2 * size_t(b)),
          one(size_t(S) * S * size_t(b));
      gradient_pixels(gr, {dep, 4}, 0, 0, S, S, four.data(), size_t(S) * 4 * size_t(b));
      gradient_pixels(gr, {dep, 2}, 0, 0, S, S, twoc.data(), size_t(S) * 2 * size_t(b));
      gradient_pixels(gr, {dep, 1}, 0, 0, S, S, one.data(), size_t(S) * size_t(b));
      for (size_t i = 0; i < size_t(S) * S; i++) {
        CHECK(std::memcmp(&twoc[i * 2 * size_t(b)], &four[i * 4 * size_t(b)], size_t(b)) == 0);
        CHECK(std::memcmp(&twoc[(i * 2 + 1) * size_t(b)], &four[(i * 4 + 3) * size_t(b)], size_t(b)) == 0);
        CHECK(std::memcmp(&one[i * size_t(b)], &four[i * 4 * size_t(b)], size_t(b)) == 0);
      }
    }
  }
  // A stop's colour on its pixel: a line 4096 pixels long puts pixel x at t = x / 4096.
  {
    const GradientRender b{gradient_lut(busy(), GradientMethod::Perceptual),
                           {GradientStyle::Linear, {0.5, 0.5, 4096.5, 0.5}, false}, false, kGradientSeed};
    uint8_t p8[4];
    uint16_t p16[4];
    gradient_pixels(b, {Depth::U8, 4}, 1229, 9, 1, 1, p8, 4);
    gradient_pixels(b, {Depth::U16, 4}, 1229, 9, 1, 1, reinterpret_cast<uint8_t*>(p16), 8);
    CHECK(p8[0] == 250 && p8[1] == 5 && p8[2] == 128 && p16[0] == 64250 && p16[1] == 1285 && p16[2] == 32896);
    gradient_pixels(b, {Depth::U8, 4}, 1024, 0, 1, 1, p8, 4);
    CHECK(p8[3] == 128);  // the opacity stop at 0.25: 0.5
  }
  // Transparent where the gradient is: empty tiles of a layer.
  {
    Gradient fade = two({255, 0, 0}, {255, 0, 0});
    fade.alphas = {{0, 1, 0.5}, {0.5, 0, 0.5}};
    TileGrid grid({Depth::U8, 4});
    render_gradient({gradient_lut(fade, GradientMethod::Classic), {GradientStyle::Linear, {0, 0, 2 * S, 0}, false}},
                    grid, {0, 0, 3, 1});
    CHECK(grid.get(0, 0) && !grid.get(0, 0)->is_solid() && !grid.get(1, 0) && !grid.get(2, 0));
  }
  {  // How long, for the record: the table of busy() in Oklab, and 2048 x 2048 pixels of Angle, dithered, 16 bits.
    const auto t0 = std::chrono::steady_clock::now();
    const auto lut = gradient_lut(busy(), GradientMethod::Perceptual);
    const auto t1 = std::chrono::steady_clock::now();
    TileGrid grid({Depth::U16, 4});
    render_gradient({lut, {GradientStyle::Angle, {1000.5, 900.25, 1800, 300}, false}, true}, grid, {0, 0, 8, 8});
    const auto t2 = std::chrono::steady_clock::now();
    const auto ms = [](auto d) { return std::chrono::duration<double, std::milli>(d).count(); };
    std::printf("test_gradient_render: table %.2f ms; 2048 x 2048 Angle, dithered, U16: %.1f ms\n", ms(t1 - t0),
                ms(t2 - t1));
  }
  bool threw = false;
  try {
    TileGrid grid({Depth::U8, 4});
    render_gradient({}, grid, {0, 0, 1, 1});
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  CHECK(threw);
}

TEST(test_gradient_dither) {
  // The noise: uniform in [-0.5, 0.5), each channel its own; the same pixel and seed give the same, another seed others.
  double sum[4] = {}, cross = 0;
  const int n = 256 * 256;
  for (int y = 0; y < 256; y++)
    for (int x = 0; x < 256; x++) {
      const std::array<float, 4> o = dither_noise(x - 128, y - 128);
      for (size_t c = 0; c < 4; c++) {
        CHECK(o[c] >= -0.5f && o[c] < 0.5f);
        sum[c] += o[c];
      }
      cross += double(o[0]) * o[1];
    }
  for (double s : sum) CHECK(std::fabs(s / n) < 0.005);
  CHECK(std::fabs(cross / n) / (1. / 12) < 0.02);  // R and G unrelated
  CHECK(dither_noise(7, -9) == dither_noise(7, -9) && dither_noise(7, -9, 1) != dither_noise(7, -9, 2));

  // to_level() without an offset is tile.h's from_unit(), edges and all.
  std::mt19937 rng(5);
  std::vector<float> vs = {0, -0.f, 1, -1, 2, 1e-30f, std::nextafter(1.f, 0.f), 0.5f / 255, 0.5f / 65535,
                           std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()};
  for (int i = 0; i < 100000; i++) vs.push_back(std::uniform_real_distribution<float>(-0.01f, 1.01f)(rng));
  for (float v : vs)
    for (Depth d : {Depth::U8, Depth::U16}) CHECK(to_level(v, d) == from_unit(v, d));

  // A flat value (8-bit levels below), dithered. 8 bits: each pixel one of the two levels about it, their mean the
  // value (within 0.01 of a level over 65,536 pixels). 16 bits: within ±128.5 levels of it, the noise that wide in the
  // middle, narrower near 0 and the maximum, so that the mean holds there too.
  for (double level : {100.3, 0.25, 254.9, 17.5, 0.002, 254.999, 0.6}) {
    const float v = float(level / 255);
    double mean = 0, mean16 = 0, lo16 = 0, hi16 = 0;
    for (int y = 0; y < 256; y++)
      for (int x = 0; x < 256; x++) {
        const std::array<float, 4> o = dither_noise(x, y);
        const int q = to_level(v, Depth::U8, o[1]);
        CHECK(q == int(std::floor(level)) || q == int(std::floor(level)) + 1);
        mean += q;
        const double d16 = to_level(v, Depth::U16, o[2]) - level * 257;
        CHECK(std::fabs(d16) <= 128.5 + 1);
        mean16 += d16, lo16 = std::min(lo16, d16), hi16 = std::max(hi16, d16);
      }
    CHECK(std::fabs(mean / n - level) < 0.01 && std::fabs(mean16 / n) < 1.5);
    const double room = std::min(128.5, std::min(level, 255 - level) * 257 + 0.5);  // how far the noise reaches
    CHECK(lo16 > -room - 1 && lo16 < -room + 2 && hi16 < room + 1 && hi16 > room - 2);
  }
  // Black and white stay, at both depths.
  for (int y = 0; y < 64; y++)
    for (int x = 0; x < 64; x++)
      for (Depth d : {Depth::U8, Depth::U16}) {
        const std::array<float, 4> o = dither_noise(x, y, 3);
        CHECK(to_level(0, d, o[0]) == 0 && to_level(1, d, o[1]) == max_value(d));
      }
  // Exact levels stay, but where the float of level / 255 lies an ulp below it and the noise is within that of -0.5
  // (about 1e-5 of the pixels): then a level below.
  int moved = 0;
  for (int level = 0; level <= 255; level++)
    for (int x = 0; x < 64; x++) {
      const int q = to_level(float(level / 255.), Depth::U8, dither_noise(x, level)[0]);
      CHECK(q == level || q == level - 1);
      moved += q != level;
    }
  CHECK(moved <= 2);

  // A dark ramp in 8 bits: undithered it steps by whole levels; dithered, each column's mean over 512 rows follows the
  // exact ramp (no bands), within the noise's error (at most 0.022 a column: 5 deviations).
  const auto lut = gradient_lut(two({0, 0, 0}, {16, 16, 16}), GradientMethod::Classic);
  const GradientRender plain{lut, {GradientStyle::Linear, {0, 0, 1024, 0}, false}, false, kGradientSeed};
  GradientRender dith = plain;
  dith.dither = true;
  std::vector<uint8_t> a(size_t(1024) * 512 * 4), b(a.size());
  gradient_pixels(plain, {Depth::U8, 4}, 0, 0, 1024, 512, a.data(), 1024 * 4);
  gradient_pixels(dith, {Depth::U8, 4}, 0, 0, 1024, 512, b.data(), 1024 * 4);
  double worst_plain = 0, worst_dith = 0;
  for (int x = 0; x < 1024; x++) {
    double ma = 0, mb = 0;
    for (int y = 0; y < 512; y++) ma += a[(size_t(y) * 1024 + size_t(x)) * 4], mb += b[(size_t(y) * 1024 + size_t(x)) * 4];
    const double exact = (x + 0.5) / 1024 * 16;
    worst_plain = std::max(worst_plain, std::fabs(ma / 512 - exact));
    worst_dith = std::max(worst_dith, std::fabs(mb / 512 - exact));
    CHECK(b[size_t(x) * 4 + 3] == 255);  // opaque stays opaque
  }
  CHECK(worst_plain > 0.45 && worst_dith < 0.12);
}

TEST(test_hash) {
  // pcg4d's values, worked out apart from this code (Python, from the paper's listing): they are the format's.
  const struct {
    uint32_t in[4], out[4];
  } kKnown[] = {
      {{0, 0, 0, 0}, {0x0f02f829, 0x2d568769, 0x32b0c43b, 0xd32548ea}},
      {{1, 0, 0, 0}, {0x0b3cefc3, 0xa31dc1d5, 0x5ffbe5a5, 0x623d099e}},
      {{0, 1, 0, 0}, {0x2a9d71a8, 0xa13cdd97, 0xe9c89c0e, 0xc4b7884c}},
      {{0, 0, 1, 0}, {0x2eda6c9d, 0x681d2eed, 0x306c117d, 0xb0607dce}},
      {{0, 0, 0, 1}, {0x8d9effe2, 0xa833f3dc, 0x8246b1b2, 0x58ab6650}},
      {{uint32_t(-1), uint32_t(-1), 7, 3}, {0x7befcc1f, 0x9590eb18, 0x306c9947, 0x28efd47f}},
      {{255, 255, 0x9e3779b9, 0}, {0x8f8bdbfc, 0xd43a2d1d, 0x6f69aa16, 0xbc62b12d}},
      {{123456, uint32_t(-7890), 42, 2}, {0x92e65d3c, 0x1a7bf644, 0x522e54ac, 0x75ebbe4d}},
  };
  for (const auto& k : kKnown) {
    const std::array<uint32_t, 4> h = hash4(k.in[0], k.in[1], k.in[2], k.in[3]);
    for (size_t c = 0; c < 4; c++) CHECK(h[c] == k.out[c]);
    CHECK(hash3(k.in[0], k.in[1], k.in[2]) == hash4(k.in[0], k.in[1], k.in[2], 0)[0]);
  }
  CHECK(hash_unit(0) == 0 && hash_unit(0xffffffff) == 1 - 1.f / 16777216 && hash_unit(0x80000000) == 0.5f);
  CHECK(hash_unit(0x12345678) * 16777216 == float(0x123456));

  // Over a tile, each output and seed: uniform (χ² of the top 8 bits, 255 degrees of freedom: 255 ± 22.6, so within
  // 5 deviations), neighbours unrelated across and down, outputs and seeds unrelated; Dissolve's share at 30 %.
  for (uint32_t seed : {0u, 1u, kGradientSeed}) {
    std::vector<std::array<float, 4>> u(65536);
    std::vector<std::array<float, 4>> other(65536);
    for (int y = 0; y < 256; y++)
      for (int x = 0; x < 256; x++) {
        const std::array<uint32_t, 4> h = hash4(uint32_t(x), uint32_t(y), seed, 0);
        const std::array<uint32_t, 4> o = hash4(uint32_t(x), uint32_t(y), seed + 1, 0);
        for (size_t c = 0; c < 4; c++) u[size_t(y * 256 + x)][c] = hash_unit(h[c]), other[size_t(y * 256 + x)][c] = hash_unit(o[c]);
      }
    const auto corr = [&](auto a, auto b, int count) {
      double s = 0;
      for (int i = 0; i < count; i++) s += (a(i) - 0.5) * (b(i) - 0.5);
      return s / count * 12;  // over the variance, 1/12
    };
    for (size_t c = 0; c < 4; c++) {
      std::vector<int> bins(256);
      double mean = 0;
      int below = 0;
      for (const auto& v : u) bins[size_t(v[c] * 256)]++, mean += v[c], below += v[c] < 0.3f;
      double chi = 0;
      for (int b : bins) chi += (b - 256.) * (b - 256.) / 256;
      CHECK(chi > 255 - 5 * 22.6 && chi < 255 + 5 * 22.6);
      CHECK(std::fabs(mean / 65536 - 0.5) < 0.005 && std::fabs(below / 65536. - 0.3) < 0.01);
      const auto at = [&](int x, int y) { return double(u[size_t(y * 256 + x)][c]); };
      CHECK(std::fabs(corr([&](int i) { return at(i % 255, i / 255); }, [&](int i) { return at(i % 255 + 1, i / 255); },
                           255 * 255)) < 0.03);
      CHECK(std::fabs(corr([&](int i) { return at(i % 256, i / 256); }, [&](int i) { return at(i % 256, i / 256 + 1); },
                           256 * 255)) < 0.03);
      CHECK(std::fabs(corr([&](int i) { return double(u[size_t(i)][c]); }, [&](int i) { return double(u[size_t(i)][(c + 1) % 4]); },
                           65536)) < 0.03);
      CHECK(std::fabs(corr([&](int i) { return double(u[size_t(i)][c]); }, [&](int i) { return double(other[size_t(i)][c]); },
                           65536)) < 0.03);
    }
  }
}

TEST(test_gpu_gradient) {
  auto dev = pb::test::gpu_device();
  if (!dev) return;
  struct Consts {
    uint32_t mode;
    int32_t x0, y0;
    uint32_t width, count, u16, dither, seed, w, style, reverse;
    float sx, sy, ax, ay, inv_len;
  };
  static_assert(sizeof(Consts) == 64, "gradient_check.slang's");
  const auto kernel = dev->kernel("gradient_check", gradient_check_spv, gradient_check_spv_sizeInBytes, 3, sizeof(Consts));
  constexpr int W = 128, H = 64;  // a rectangle of pixels a dispatch, across a tile's edge
  const uint32_t n = uint32_t(W) * H;
  auto lut_buf = dev->buffer(sizeof(GradientLut::rgba)), ts = dev->buffer(n * 4), out = dev->buffer(n * 16);
  std::vector<uint32_t> got(size_t(n) * 4);
  const auto run = [&](const Consts& k, size_t words) {
    dev->fill(*out, 0xdeadbeef);
    dev->dispatch(*kernel, {lut_buf.get(), ts.get(), out.get()}, &k, sizeof k, (k.count + 63) / 64, 1);
    dev->download(*out, got.data(), words * 4);
    dev->flush();
  };
  const auto consts = [&](uint32_t mode, const GradientRender& r, int x0, int y0, Depth d) {
    const GradientPlane p = plane(r.shape);
    return Consts{mode,   x0,       y0,       uint32_t(W), n,   d == Depth::U16, r.dither, r.seed, 0, p.style,
                  p.reverse, p.sx, p.sy, p.ax, p.ay, p.inv_len};
  };
  // The CPU's samples of a rectangle as the kernel packs them.
  const auto cpu = [&](const GradientRender& r, int x0, int y0, Depth d) {
    std::vector<uint16_t> s(size_t(n) * 4);
    if (d == Depth::U16) {
      gradient_pixels(r, {d, 4}, x0, y0, W, H, reinterpret_cast<uint8_t*>(s.data()), size_t(W) * 8);
    } else {
      std::vector<uint8_t> b(size_t(n) * 4);
      gradient_pixels(r, {d, 4}, x0, y0, W, H, b.data(), size_t(W) * 4);
      std::copy(b.begin(), b.end(), s.begin());
    }
    std::vector<uint32_t> w(size_t(n) * 2);
    for (size_t i = 0; i < n; i++)
      w[2 * i] = s[4 * i] | uint32_t(s[4 * i + 1]) << 16, w[2 * i + 1] = s[4 * i + 2] | uint32_t(s[4 * i + 3]) << 16;
    return w;
  };
  const auto differ = [&](const std::vector<uint32_t>& want, size_t words, const char* what) {
    size_t bad = 0, first = 0;
    for (size_t i = 0; i < words; i++)
      if (got[i] != want[i] && bad++ == 0) first = i;
    if (bad)
      std::printf("test_gpu_gradient: %s: %zu words differ, first %zu: GPU %08x, CPU %08x\n", what, bad, first,
                  got[first], want[first]);
    return bad;
  };

  std::mt19937 rng(30);
  std::uniform_real_distribution<double> coord(-300, 600);
  struct Case {
    Gradient g;
    GradientMethod m;
  };
  const std::vector<Case> cases = {{busy(), GradientMethod::Perceptual},
                                   {two({0, 0, 0}, {255, 255, 255}), GradientMethod::Classic},
                                   {busy(), GradientMethod::Stripes}};
  size_t bad = 0;
  int runs = 0;
  for (const Case& c : cases) {
    const auto lut = gradient_lut(c.g, c.m);
    dev->upload(*lut_buf, lut->rgba.data(), sizeof(lut->rgba));
    std::vector<GradientLine> lines = {{0.5, 0.5, 256.5, 0.5}, {128, 128, 128, 128}, {-5000.25, 3, 9000.75, 77.125},
                                       {40.5, 200.25, 41, 30}};
    for (int i = 0; i < 3; i++) lines.push_back({coord(rng), coord(rng), coord(rng), coord(rng)});
    for (const GradientLine& l : lines)
      for (GradientStyle st : {GradientStyle::Linear, GradientStyle::Radial, GradientStyle::Angle,
                               GradientStyle::Reflected, GradientStyle::Diamond})
        for (bool rev : {false, true}) {
          const int x0 = int(rng() % 512) - 256 - W / 2, y0 = int(rng() % 512) - 256 - H / 2;
          GradientRender r{lut, {st, l, rev}, false, uint32_t(rng())};
          // t, to the bit
          run(consts(1, r, x0, y0, Depth::U8), n);
          std::vector<uint32_t> tw(n);
          const GradientPlane p = plane(r.shape);
          for (uint32_t i = 0; i < n; i++) tw[i] = bits(gradient_t(p, x0 + int(i % W), y0 + int(i / W)));
          bad += differ(tw, n, "t");
          // samples, both depths, dithered and not
          for (Depth d : {Depth::U8, Depth::U16})
            for (bool dith : {false, true}) {
              r.dither = dith;
              run(consts(0, r, x0, y0, d), size_t(n) * 2);
              bad += differ(cpu(r, x0, y0, d), size_t(n) * 2, "samples");
              runs++;
            }
        }
    // Gradient Map's way: t given, edges and all (NaN, infinities, a subnormal, ulps around entries and 1).
    std::vector<float> t(n);
    const float special[] = {0, -0.f, 1, -1, 2, std::numeric_limits<float>::quiet_NaN(),
                             std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
                             1e-40f, 1e-30f, std::nextafter(1.f, 0.f), 0.5f, std::nextafter(0.5f, 1.f),
                             1229.f / 4096, std::nextafter(1229.f / 4096, 0.f)};
    for (uint32_t i = 0; i < n; i++)
      t[i] = i < std::size(special) ? special[i] : std::uniform_real_distribution<float>(-0.1f, 1.1f)(rng);
    dev->upload(*ts, t.data(), n * 4);
    for (Depth d : {Depth::U8, Depth::U16})
      for (bool dith : {false, true}) {
        const GradientRender r{lut, {}, dith, 12345};
        run(consts(3, r, -37, 11, d), size_t(n) * 2);
        std::vector<uint32_t> want(size_t(n) * 2);
        for (uint32_t i = 0; i < n; i++) {
          const int x = -37 + int(i % W), y = 11 + int(i / W);
          const std::array<float, 4> v = lut->at(t[i]);
          const std::array<float, 4> o = dith ? dither_noise(x, y, 12345) : std::array<float, 4>{};
          uint32_t q[4];
          for (size_t k = 0; k < 4; k++) q[k] = to_level(v[k], d, o[k]);
          want[2 * i] = q[0] | q[1] << 16, want[2 * i + 1] = q[2] | q[3] << 16;
        }
        bad += differ(want, size_t(n) * 2, "lookups");
      }
  }
  // The hash, negative pixels too.
  for (uint32_t seed : {0u, 7u, kGradientSeed})
    for (uint32_t w : {0u, 3u}) {
      Consts k = consts(2, GradientRender{}, -128, -77, Depth::U8);
      k.seed = seed, k.w = w;
      run(k, size_t(n) * 4);
      std::vector<uint32_t> want(size_t(n) * 4);
      for (uint32_t i = 0; i < n; i++) {
        const std::array<uint32_t, 4> h = hash4(uint32_t(-128 + int(i % W)), uint32_t(-77 + int(i / W)), seed, w);
        std::copy(h.begin(), h.end(), want.begin() + 4 * i);
      }
      bad += differ(want, size_t(n) * 4, "hash");
    }
  std::printf("test_gpu_gradient: %d rectangles of samples, %s\n", runs, dev->name().c_str());
  CHECK(bad == 0);
}

// Photoshop's Gradient fill layers (D32's tools/ps-golden/p1/gradient.jsx, run at B33): each scene of pixel layers and
// fill layers ("props": {"content": GradientFill's JSON}), the fills drawn by render_gradient() over the canvas,
// composited, printed against Photoshop's image. Hypotheses, so no thresholds yet: B38 sets them.
TEST(test_gradient_ps) {
  golden::Set ps("p1/gradient");
  if (!ps.ready("test_gradient_ps")) return;
  const Format f{Depth::U16, 4};
  for (const golden::Entry& e : ps.entries()) {
    const golden::Image want = ps.image(e);
    if (!want) continue;
    auto root = std::make_shared<Layer>();
    root->kind = Kind::Group;
    std::string skip;
    try {
      for (const golden::SceneLayer& l : ps.layers(e)) {
        auto layer = std::make_shared<Layer>();
        layer->id = LayerId(root->children.size() + 1);
        layer->props = props_from_json(l.props).value();
        if (l.mask || !(l.kind == "pixel" || l.kind == "fill")) skip = l.kind + (l.mask ? " with a mask" : "");
        if (l.kind == "pixel" && l.pixels) layer->pixels = golden::grid(*l.pixels, f);
        if (l.kind == "fill") {
          const nlohmann::json content = l.json->value("props", nlohmann::json::object()).value("content", nlohmann::json());
          const std::optional<GradientFill> fill = gradient_fill_from(content);
          if (!fill) {
            skip = "a fill that is not a gradient: " + content.dump();
            break;
          }
          auto grid = std::make_shared<TileGrid>(f);
          const GradientRender r{gradient_lut(fill->gradient, fill->method),
                                 fill_shape(*fill, 0, 0, want.w, want.h), fill->dither, kGradientSeed};
          render_gradient(r, *grid, {0, 0, (want.w + S - 1) / S, (want.h + S - 1) / S});
          layer->pixels = grid;
        }
        root->children.push_back(layer);
      }
    } catch (const std::exception& x) {
      skip = x.what();
    }
    if (!skip.empty()) {
      std::printf("test_gradient_ps: %s not drawn: %s\n", e.name.c_str(), skip.c_str());
      continue;
    }
    auto doc = std::make_shared<DocState>();
    doc->width = want.w, doc->height = want.h, doc->mode = Mode::Rgb, doc->depth = Depth::U16;
    doc->root = root;
    doc->next_id = LayerId(root->children.size() + 1);
    TileGrid out(f);
    RenderInput in;
    in.doc = doc;
    for (int ty = 0; ty * S < want.h; ty++)
      for (int tx = 0; tx * S < want.w; tx++) out.set(tx, ty, composite_tile(in, 0, tx, ty));
    golden::print(e.name, golden::compare(golden::image(out, want.w, want.h), want, "p1/gradient/" + e.name));
  }
  ps.skipped("test_gradient_ps");
}
