// Invert, Posterize, Threshold, Gradient Map (core/adj_invert.cpp, adj_posterize.cpp, adj_threshold.cpp,
// adj_gradient_map.cpp; D38) against their formulas: Invert 1 - v and twice the identity on every 8- and 16-bit sample,
// of layers and masks; Posterize n levels exactly, where floor(v n) puts them, at every 16-bit level and every n;
// Threshold white from t on and black below, to the level, ties and fractions; Gradient Map black to white its
// luminosity, every method and Reverse gradient.h's table at it to the bit, the opacity stops left out, grey documents,
// profiles, the dither (black and white kept, means kept, within half an 8-bit level), the default gradient that of
// ui/qml/AdjustSpec.js; the GPU the CPU's to the bit (adjust_check.h); D32's Photoshop images when they are there, and
// how far each hypothesis's alternative is.
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "adjust.h"
#include "adjust_check.h"
#include "check.h"
#include "color.h"
#include "golden.h"
#include "gradient.h"
#include "histogram.h"

using namespace pb;
using nlohmann::json;

namespace {

constexpr int S = kTileSize;

std::string dump(const json& j) { return j.dump(); }

// {"key": v}, a whole number without a fraction (as the UI writes it).
std::string one(const char* key, double v) {
  json j = json::object();
  if (v == std::floor(v))
    j[key] = int64_t(v);
  else
    j[key] = v;
  return dump(j);
}

// A 16-bit sample as apply_adjustment() hands it to apply(): v / 65535 rounded to the nearest float.
float unit16(uint32_t s) { return float(double(s) * (1.0 / 65535)); }

// The colours: the 33³ lattice, every 16-bit grey, and R, G, B ramps apart (adjust_check.h's), alpha anything.
std::vector<Float4> colours() {
  std::vector<Float4> px;
  for (int b = 0; b <= 32; b++)
    for (int g = 0; g <= 32; g++)
      for (int r = 0; r <= 32; r++) px.push_back({r / 32.f, g / 32.f, b / 32.f, 0.5f});
  for (uint32_t v = 0; v < 65536; v++) {
    px.push_back({unit16(v), unit16(v), unit16(v), 0.25f});
    px.push_back({unit16(v), unit16(65535 - v), unit16((v * 40503u + 12345u) & 0xffff), 0.75f});
  }
  return px;
}

// px adjusted by a, in rows of 512 from pixel (x0, y0) as adjust_check.h lays them out.
std::vector<Float4> adjusted(const Adjustment& a, std::vector<Float4> px, int x0 = -300, int y0 = -7) {
  constexpr size_t kRow = 512;
  for (size_t at = 0; at < px.size(); at += kRow)
    a.apply(px[at].data(), int(std::min(kRow, px.size() - at)), x0, y0 + int(at / kRow));
  return px;
}

// Pixel i (x + 256 y) of a tile holding every level of the depth: R i, G its reverse, B a shuffle, alpha another.
Pixel every_level(Format f, uint32_t i) {
  const uint32_t top = uint32_t(max_value(f.depth)), n = top + 1;
  const uint16_t r = uint16_t(i % n), g = uint16_t(top - i % n), b = uint16_t((i * 40503u + 12345u) % n);
  const uint16_t a = uint16_t((i * 7919u) % n);
  if (f.channels == 4) return {r, g, b, a};
  if (f.channels == 2) return {r, a, 0, 0};
  return {r, 0, 0, 0};
}

// A grid of format f (a mask's fill white): tile (0, 0) every level, (1, 0) every level shuffled, (-1, 0) solid, (0, 1)
// solid black, (-1, 1) and (1, 1) empty. Opaque solid tiles: a transparent one is no colour to adjust.
TileGrid every_sample(Format f) {
  const uint16_t top = uint16_t(max_value(f.depth));
  TileGrid g(f, f.channels == 1 ? Pixel{top} : Pixel{});
  for (int k = 0; k < 2; k++) {
    const std::shared_ptr<Tile> t = Tile::make(f);
    {
      const WritePin w = t->write();
      for (uint32_t i = 0; i < uint32_t(S * S); i++) {
        const Pixel p = every_level(f, k ? i * 2654435761u >> 16 : i);
        for (int c = 0; c < f.channels; c++) {
          const size_t at = size_t(i) * size_t(f.channels) + size_t(c);
          if (f.depth == Depth::U16)
            reinterpret_cast<uint16_t*>(w.data())[at] = p[size_t(c)];
          else
            w.data()[at] = uint8_t(p[size_t(c)]);
        }
      }
    }
    g.set(k, 0, t);
  }
  const Pixel solid = f.channels == 4 ? Pixel{uint16_t(top / 3), uint16_t(top / 2), 7, top}
                      : f.channels == 2 ? Pixel{uint16_t(top / 3), top}
                                        : Pixel{uint16_t(top / 3)};
  const Pixel black = f.channels == 4 ? Pixel{0, 0, 0, top} : f.channels == 2 ? Pixel{0, top} : Pixel{0};
  g.set(-1, 0, Tile::solid(f, solid));
  g.set(0, 1, Tile::solid(f, black));
  return g;
}
const TileRect kRect{-1, 0, 2, 2};  // every tile of every_sample()

// Every sample of tiles r of g, row by row.
std::vector<uint16_t> samples(const TileGrid& g, TileRect r) {
  const Format f = g.format();
  const int w = (r.x1 - r.x0) * S, h = (r.y1 - r.y0) * S;
  std::vector<uint8_t> raw(size_t(w) * size_t(h) * size_t(f.bytes()));
  g.read_rect(r.x0 * S, r.y0 * S, w, h, raw.data(), size_t(w) * size_t(f.bytes()));
  std::vector<uint16_t> out(size_t(w) * size_t(h) * size_t(f.channels));
  for (size_t i = 0; i < out.size(); i++)
    out[i] = f.depth == Depth::U16 ? reinterpret_cast<const uint16_t*>(raw.data())[i] : raw[i];
  return out;
}

// Lum of 16-bit levels times 255 against t, in integers: Lum = (30 R + 59 G + 11 B) / (100 x 65535) of kLumPercent.
bool lum_reaches(uint32_t r, uint32_t g, uint32_t b, double t) {
  const uint64_t l = uint64_t(kLumPercent[0]) * r + uint64_t(kLumPercent[1]) * g + uint64_t(kLumPercent[2]) * b;
  return double(l) * 255 >= t * 100 * 65535;  // both exact: integers below 2^53
}

uint32_t level16(float v) { return uint32_t(std::lround(std::clamp(double(v), 0., 1.) * 65535)); }

// The gradient AdjustSpec.js shows for a Gradient Map: its GRAD row's CSS colours, evenly spaced as CSS spaces them.
json spec_gradient() {
  std::ifstream f(PB_SOURCE_DIR "/ui/qml/AdjustSpec.js");
  std::stringstream ss;
  ss << f.rdbuf();
  const std::string js = ss.str();
  const size_t at = js.find("\"Gradient Map\": {");
  CHECK(at != std::string::npos);
  const size_t line = js.find('\n', at), g = js.find("GRAD~linear-gradient(", at), end = js.find(')', g);
  CHECK(g < line && end < line);
  const std::string css = js.substr(g, end - g);
  const std::regex hex("#([0-9a-fA-F]{6})");
  std::vector<std::array<int, 3>> stops;
  for (auto it = std::sregex_iterator(css.begin(), css.end(), hex); it != std::sregex_iterator(); ++it) {
    const int v = std::stoi((*it)[1].str(), nullptr, 16);
    stops.push_back({v >> 16, (v >> 8) & 255, v & 255});
  }
  CHECK(stops.size() >= 2);
  json colors = json::array();
  for (size_t i = 0; i < stops.size(); i++)
    colors.push_back({{"p", double(i) / double(stops.size() - 1)}, {"c", {stops[i][0], stops[i][1], stops[i][2]}}});
  return {{"colors", colors}};
}

// AdjustSpec's three stops (D32's A.MAP).
const char* const kMap = R"({"colors":[{"c":[27,16,32],"p":0},{"c":[158,47,69],"p":0.5},{"c":[233,199,179],"p":1}]})";
// Black to white without Smoothness: Classic is then linear in the encoded values (gradient.cpp).
const char* const kBlackWhite = R"({"colors":[{"c":[0,0,0],"p":0},{"c":[255,255,255],"p":1}],"smoothness":0})";
constexpr const char* kMethods[] = {"Perceptual", "Linear", "Classic", "Smooth", "Stripes"};

std::string map_vals(const std::string& gradient, const char* method, bool rev = false, bool dither = false) {
  json j = {{"gradient", json::parse(gradient)}, {"method", method}};
  if (rev) j["rev"] = true;
  if (dither) j["dither"] = true;
  return dump(j);
}

bool same_table(const std::vector<Float4>& t, const GradientLut& lut) {
  return t.size() == lut.rgba.size() && std::memcmp(t.data(), lut.rgba.data(), sizeof lut.rgba) == 0;
}

}  // namespace

// Invert: 1 - v exactly, alpha alone; twice the identity on every sample of 8 and 16 bits, RGBA, grey and masks (a
// mask's white fill turning black and back).
TEST(test_adj_invert) {
  const AdjustmentPtr inv = adjustment("Invert");
  CHECK(inv && inv->type() == AdjType::Invert && !inv->identity() && !inv->dithered() && inv->tables().empty());
  CHECK(inv->vals() == "{}" && adjustment("Invert", R"({"x":1})")->vals() == "{}");
  const std::vector<Float4> px = colours(), out = adjusted(*inv, px);
  for (size_t i = 0; i < px.size(); i++) {
    for (size_t c = 0; c < 3; c++) CHECK(out[i][c] == 1 - px[i][c]);
    CHECK(out[i][3] == px[i][3]);
  }
  for (Format f : {Format{Depth::U16, 4}, Format{Depth::U8, 4}, Format{Depth::U16, 2}, Format{Depth::U8, 2},
                   Format{Depth::U16, 1}, Format{Depth::U8, 1}}) {
    const uint16_t top = uint16_t(max_value(f.depth));
    const int colors = f.channels == 4 ? 3 : 1;
    const TileGrid in = every_sample(f);
    const TileGrid once = apply_adjustment(in, *inv), twice = apply_adjustment(once, *inv);
    const std::vector<uint16_t> a = samples(in, kRect), b = samples(once, kRect);
    const int w = (kRect.x1 - kRect.x0) * S;
    bool all = true;
    for (size_t i = 0; i < a.size(); i++) {
      const size_t p = i / size_t(f.channels);
      const int c = int(i % size_t(f.channels));
      const int tx = int(p % size_t(w)) / S + kRect.x0, ty = int(p / size_t(w)) / S + kRect.y0;
      const bool empty = !in.get(tx, ty);  // a layer's stays empty; a mask's is its fill, inverted
      const uint16_t want = c < colors && !(empty && f.alpha()) ? uint16_t(top - a[i]) : a[i];
      all &= b[i] == want;
    }
    CHECK(all);
    CHECK(once.fill()[0] == (f.channels == 1 ? 0 : in.fill()[0]));
    CHECK(samples(twice, kRect) == a && twice.fill() == in.fill());
  }
}

// Posterize: at every 16-bit level and every n, floor(v n) (the top band taking 1) at k / (n - 1), so n levels exactly;
// 8 bits at 4 levels in Photoshop's bands; the range, a fraction of a level rounding.
TEST(test_adj_posterize) {
  CHECK(adjustment("Posterize")->vals() == "{}" && !adjustment("Posterize")->identity());
  CHECK(adjustment("Posterize", R"({"lv":1})")->vals() == R"({"lv":2})");
  CHECK(adjustment("Posterize", R"({"lv":900})")->vals() == R"({"lv":255})");
  CHECK(!adjustment("Posterize", R"({"lv":"4"})"));
  std::vector<Float4> ramp(65536);
  for (uint32_t s = 0; s < 65536; s++) ramp[s] = {unit16(s), unit16(65535 - s), unit16(s), 0};
  for (int n = 2; n <= 255; n++) {
    const AdjustmentPtr a = adjustment("Posterize", one("lv", n));
    CHECK(a && a->vals() == (n == 4 ? "{}" : one("lv", n)));
    const std::vector<Float4> out = adjusted(*a, ramp, 0, 0);
    const auto level = [&](uint32_t s) {
      return float(std::min(std::floor(double(s) * n / 65535), double(n - 1))) / float(n - 1);
    };
    std::set<float> levels;
    bool all = true;
    for (uint32_t s = 0; s < 65536; s++) {
      all &= out[s][0] == level(s) && out[s][1] == level(65535 - s) && out[s][2] == out[s][0];
      levels.insert(out[s][0]);
    }
    CHECK(all && int(levels.size()) == n && *levels.begin() == 0 && *levels.rbegin() == 1);
  }
  // 4 levels of 8 bits: 0-63, 64-127, 128-191, 192-255 become 0, 85, 170, 255.
  const AdjustmentPtr four = adjustment("Posterize");
  for (int k = 0; k <= 255; k++) {
    const float v = to_unit(k, Depth::U8);
    const uint16_t got = from_unit(four->apply({v, v, v})[0], Depth::U8);
    CHECK(got == (k < 64 ? 0 : k < 128 ? 85 : k < 192 ? 170 : 255));
  }
  // A fraction of a level: the nearest whole number of them.
  CHECK(four->apply({0.3f, 0.3f, 0.3f}) == adjustment("Posterize", R"({"lv":4.4})")->apply({0.3f, 0.3f, 0.3f}));
  CHECK(adjustment("Posterize", R"({"lv":4.6})")->apply({0.3f, 0.3f, 0.3f})[0] == 0.25f);  // 5 levels: 1 / 4
}

// Threshold: every 8-bit grey white from t on, black below, for every t; 16-bit greys at 257 t and one below; colours
// by Lum x 255 >= t (kLumPercent, of the 16-bit levels, in integers) on the lattice and the ramps, ties included; a
// fraction of t.
TEST(test_adj_threshold) {
  CHECK(adjustment("Threshold")->vals() == "{}" && !adjustment("Threshold")->identity());
  CHECK(adjustment("Threshold", R"({"t":0})")->vals() == R"({"t":1})");
  CHECK(adjustment("Threshold", R"({"t":256})")->vals() == R"({"t":255})");
  const auto grey = [](const Adjustment& a, float v) { return a.apply({v, v, v}); };
  const auto is = [](const std::array<float, 3>& c, float v) { return c[0] == v && c[1] == v && c[2] == v; };
  for (int t = 1; t <= 255; t++) {
    const AdjustmentPtr a = adjustment("Threshold", one("t", t));
    bool all = true;
    for (int k = 0; k <= 255; k++) all &= is(grey(*a, to_unit(k, Depth::U8)), k >= t ? 1.f : 0.f);
    CHECK(all);
    CHECK(is(grey(*a, unit16(257 * uint32_t(t))), 1) && is(grey(*a, unit16(257 * uint32_t(t) - 1)), 0));
  }
  const std::vector<Float4> px = colours();
  for (double t : {1., 64., 128., 200., 255., 127.5}) {
    const AdjustmentPtr a = adjustment("Threshold", one("t", t));
    const std::vector<Float4> out = adjusted(*a, px);
    bool all = true;
    size_t white = 0;
    for (size_t i = 0; i < px.size(); i++) {
      const bool w = lum_reaches(level16(px[i][0]), level16(px[i][1]), level16(px[i][2]), t);
      all &= is({out[i][0], out[i][1], out[i][2]}, w ? 1.f : 0.f) && out[i][3] == px[i][3];
      white += w;
    }
    CHECK(all && white > 0 && white < px.size());
  }
  // Colours of 8 bits right at t = 128 (30 R + 59 G + 11 B = 12800): white; a level of blue less: black.
  const AdjustmentPtr mid = adjustment("Threshold");
  int ties = 0;
  for (int r = 0; r <= 255; r += 5)
    for (int g = 0; g <= 255; g++) {
      const int rest = 12800 - 30 * r - 59 * g;
      if (rest < 11 || rest % 11 || rest / 11 > 255) continue;
      const int b = rest / 11;
      const auto c = [](int v) { return to_unit(v, Depth::U8); };
      CHECK(is(mid->apply({c(r), c(g), c(b)}), 1) && is(mid->apply({c(r), c(g), c(b - 1)}), 0));
      ties++;
    }
  CHECK(ties > 20);
  // t = 127.5: 8-bit grey 128 white, 127 black; 16-bit 32768 (Lum x 255 = 127.502) white, 32767 (127.498) black.
  const AdjustmentPtr half = adjustment("Threshold", R"({"t":127.5})");
  CHECK(is(grey(*half, to_unit(128, Depth::U8)), 1) && is(grey(*half, to_unit(127, Depth::U8)), 0));
  CHECK(is(grey(*half, unit16(32768)), 1) && is(grey(*half, unit16(32767)), 0));
}

// Gradient Map: the default (Perceptual, AdjustSpec.js's GRAD); black to white its luminosity, Reverse 1 minus it;
// every method gradient.h's table at Lum, to the bit; the opacity left out; grey documents grey; the document's profile
// in Linear; the dither within half an 8-bit level, black and white kept at either depth, means kept, tiles of one
// colour coming out of data.
TEST(test_adj_gradient_map) {
  const AdjustmentPtr def = adjustment("Gradient Map");
  CHECK(def && def->vals() == "{}" && !def->identity() && !def->dithered());
  const std::optional<Gradient> spec = gradient_from(spec_gradient());
  CHECK(spec && same_table(def->tables(), *gradient_lut(*spec, GradientMethod::Perceptual)));
  CHECK(same_table(def->tables(), *gradient_lut(*gradient_from_json(kMap), GradientMethod::Perceptual)));
  CHECK(!adjustment("Gradient Map", R"({"method":"Noise"})") && !adjustment("Gradient Map", R"({"gradient":[0,0]})"));

  // Black to white in Classic: Lum = 0.30 R + 0.59 G + 0.11 B itself, in each channel; Reverse: 1 - Lum.
  const std::vector<Float4> px = colours();
  for (bool rev : {false, true}) {
    const AdjustmentPtr a = adjustment("Gradient Map", map_vals(kBlackWhite, "Classic", rev));
    const std::vector<Float4> out = adjusted(*a, px);
    double worst = 0;
    for (size_t i = 0; i < px.size(); i++) {
      const double lum = 0.30 * px[i][0] + 0.59 * px[i][1] + 0.11 * px[i][2], want = rev ? 1 - lum : lum;
      for (size_t c = 0; c < 3; c++) worst = std::max(worst, std::fabs(out[i][c] - want));
      CHECK(out[i][3] == px[i][3]);
    }
    std::printf("test_adj_gradient_map: black to white%s within %.3g of %s\n", rev ? ", reversed," : "", worst,
                rev ? "1 - Lum" : "Lum");
    CHECK(worst < 1e-6);
  }

  // Every method, either way: gradient.h's table at t (Lum, or 1 - Lum), to the bit; the methods differ.
  std::set<std::vector<float>> middles;
  for (const char* method : kMethods)
    for (bool rev : {false, true}) {
      const AdjustmentPtr a = adjustment("Gradient Map", map_vals(kMap, method, rev));
      const std::shared_ptr<const GradientLut> lut = gradient_lut(*gradient_from_json(kMap), *gradient_method(method));
      CHECK(a && same_table(a->tables(), *lut));
      const std::vector<Float4> out = adjusted(*a, px);
      bool all = true;
      for (size_t i = 0; i < px.size(); i++) {
        const float lum = (30.f * px[i][0] + 59.f * px[i][1] + 11.f * px[i][2]) / 100;
        const std::array<float, 4> c = lut->at(rev ? 1 - lum : lum);
        all &= out[i][0] == c[0] && out[i][1] == c[1] && out[i][2] == c[2];
      }
      CHECK(all);
      const std::array<float, 3> m = a->apply({0.2f, 0.2f, 0.2f});  // 0.4 of the first segment, 0.6 of the second
      middles.insert({m[0], m[1], m[2]});
    }
  CHECK(middles.size() == 2 * std::size(kMethods));

  // The gradient's opacity counts for nothing: fading to transparent maps as opaque.
  const std::string fading =
      R"({"alphas":[{"a":1,"p":0},{"a":0,"p":1}],"colors":[{"c":[27,16,32],"p":0},{"c":[158,47,69],"p":0.5},)"
      R"({"c":[233,199,179],"p":1}]})";
  CHECK(adjusted(*adjustment("Gradient Map", map_vals(fading, "Linear")), px) ==
        adjusted(*adjustment("Gradient Map", map_vals(kMap, "Linear")), px));

  // A grey document: the stops grey, R = G = B. The document's profile: Linear's light by its curve (Adobe RGB's 2.2
  // against sRGB's); one it cannot read is sRGB's.
  const AdjustmentPtr g = adjustment("Gradient Map", map_vals(kMap, "Linear"), {Mode::Gray, nullptr});
  const auto grey = [](const Float4& e) { return e[0] == e[1] && e[1] == e[2]; };
  CHECK(std::all_of(g->tables().begin(), g->tables().end(), grey));
  const auto adobe = std::make_shared<const std::vector<uint8_t>>(icc_profile(ColorSpace::AdobeRgb));
  const auto junk = std::make_shared<const std::vector<uint8_t>>(std::vector<uint8_t>(200, 7));
  const AdjustmentPtr srgb = adjustment("Gradient Map", map_vals(kMap, "Linear"));
  const AdjustmentPtr in_adobe = adjustment("Gradient Map", map_vals(kMap, "Linear"), {Mode::Rgb, adobe});
  CHECK(!same_table(in_adobe->tables(), *gradient_lut(*gradient_from_json(kMap), GradientMethod::Linear)));
  CHECK(same_table(srgb->tables(), *gradient_lut(*gradient_from_json(kMap), GradientMethod::Linear)));
  CHECK(adjustment("Gradient Map", map_vals(kMap, "Linear"), {Mode::Rgb, junk})->tables() == srgb->tables());

  // The dither: within half an 8-bit level of the undithered colour, a hash of the pixel (gradient.h's), none at 0 and
  // 1.
  const AdjustmentPtr plain = adjustment("Gradient Map", map_vals(kBlackWhite, "Classic"));
  const AdjustmentPtr d = adjustment("Gradient Map", map_vals(kBlackWhite, "Classic", false, true));
  CHECK(d->dithered() && d->vals() == R"({"dither":true,"gradient":)" + dump(json::parse(kBlackWhite)) +
                                          R"(,"method":"Classic"})");
  const std::vector<Float4> want = adjusted(*plain, px), got = adjusted(*d, px);
  double far = 0;
  size_t moved = 0;
  for (size_t i = 0; i < px.size(); i++)
    for (size_t c = 0; c < 3; c++) {
      far = std::max(far, std::fabs(double(got[i][c]) - want[i][c]));
      moved += got[i][c] != want[i][c];
      if (want[i][c] == 0 || want[i][c] == 1) CHECK(got[i][c] == want[i][c]);
    }
  std::printf("test_adj_gradient_map: dithered up to %.4f of an 8-bit level, %zu of %zu samples moved\n", far * 255,
              moved, 3 * px.size());
  CHECK(far <= 0.5 / 255 + 1e-7 && moved > px.size());
  CHECK(d->apply({0.4f, 0.5f, 0.6f}, 3, 4) != d->apply({0.4f, 0.5f, 0.6f}, 4, 3));
  // Means over a tile of one grey, rounded at 8 and 16 bits, near the ends too (where the noise narrows): within 0.05
  // of an 8-bit level of the undithered value (statistically some 0.002), the ends exact; at 8 bits within half a level
  // of either end, where the noise narrows below a level, within 0.3 (0.25 at worst: adj_gradient_map.cpp).
  for (Depth depth : {Depth::U8, Depth::U16}) {
    const double top = max_value(depth);
    for (double v : {0., 3 / 65535., 0.25 / 255, 0.4 / 255, 1 / 255., 0.1234567, 0.5, 0.75, 254.7 / 255,
                     1 - 2 / 65535., 1.}) {
      const float u = float(v), base = plain->apply({u, u, u})[0];
      std::vector<Float4> tile(size_t(S) * S, Float4{u, u, u, 1});
      for (int y = 0; y < S; y++) d->apply(tile[size_t(y) * S].data(), S, 0, y);
      double sum = 0;
      for (const Float4& p : tile) sum += from_unit(p[0], depth);
      const double mean = sum / double(tile.size()), off = (mean - double(base) * top) / top * 255;
      const double allowed = depth == Depth::U8 && std::min(v, 1 - v) < 0.5 / 255 ? 0.3 : 0.05;
      if (std::fabs(off) > 0.05) std::printf("test_adj_gradient_map: %g at %d bits: mean %.4f, %.4f levels off\n", v,
                                             depth == Depth::U16 ? 16 : 8, mean, off);
      CHECK(std::fabs(off) <= allowed);
      if (v == 0 || v == 1) CHECK(mean == v * top);
    }
  }
  // Through apply_adjustment(): a tile of one colour comes out of data, black stays the tile it was.
  TileGrid layer(Format{Depth::U16, 4});
  layer.set(0, 0, Tile::solid(layer.format(), {30000, 20000, 10000, 65535}));
  layer.set(1, 0, Tile::solid(layer.format(), {0, 0, 0, 65535}));
  const TileGrid out = apply_adjustment(layer, *d);
  CHECK(!out.get(0, 0)->is_solid() && out.get(1, 0) == layer.get(1, 0));
  CHECK(apply_adjustment(layer, *plain).get(0, 0)->is_solid());

  // The time its table takes (the first time: the cache has it after), and a layer of 2048 x 2048 RGBA U16, every
  // pixel, Perceptual, dithered.
  const auto ms = [](std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  };
  json timed = json::parse(map_vals(kMap, "Perceptual", false, true));
  timed["gradient"]["name"] = "test_adj_gradient_map's own";
  auto t0 = std::chrono::steady_clock::now();
  const AdjustmentPtr perceptual = adjustment("Gradient Map", dump(timed));
  const double table = ms(t0);
  const TileGrid levels = every_sample(Format{Depth::U16, 4});
  TileGrid big(Format{Depth::U16, 4});
  for (int i = 0; i < 64; i++) big.set(i % 8, i / 8, levels.tiles().at(TileXY{i % 2, 0}));
  t0 = std::chrono::steady_clock::now();
  const TileGrid done = apply_adjustment(big, *perceptual);
  const double layer_ms = ms(t0);
  CHECK(done.tiles().size() == 64);
  std::printf("test_adj_gradient_map: its table in %.2f ms; 2048 x 2048 RGBA U16, Perceptual, dithered, in %.1f ms "
              "(%.0f Mpx/s)\n", table, layer_ms, 4.194304 / layer_ms * 1000);
}

// The GPU (adjust_apply.slang) against the CPU, to the bit, on lavapipe: the four with their values off the defaults,
// Gradient Map in every method, Reverse, Dither, in grey and in another profile.
TEST(test_gpu_adj_simple) {
  test::AdjustCheck check;
  if (!check) return;
  int n = 0;
  const auto ok = [&](const std::string& adj, const std::string& vals, const AdjustContext& c = {}) {
    CHECK(check(adj, vals, c));
    n++;
  };
  ok("Invert", "{}");
  for (double lv : {2., 3., 4., 7., 16., 100., 254., 255., 4.6}) ok("Posterize", one("lv", lv));
  for (double t : {1., 64., 127.5, 128., 200., 255.}) ok("Threshold", one("t", t));
  ok("Gradient Map", "{}");
  for (const char* method : kMethods)
    for (int k = 0; k < 4; k++) ok("Gradient Map", map_vals(kMap, method, k & 1, k & 2));
  ok("Gradient Map", map_vals(kBlackWhite, "Classic", false, true));
  ok("Gradient Map", map_vals(kMap, "Perceptual", true, true), {Mode::Gray, nullptr});
  const auto adobe = std::make_shared<const std::vector<uint8_t>>(icc_profile(ColorSpace::AdobeRgb));
  ok("Gradient Map", map_vals(kMap, "Linear", false, true), {Mode::Rgb, adobe});
  std::printf("test_gpu_adj_simple: %d adjustments x %zu colours alike\n", n, check.pixels());
}

namespace {

// Photoshop's 8 bits: ours, 16 bits, rounded as Photoshop saved its 16-bit composite (golden.h's write_png() too).
golden::Image as8(golden::Image im) {
  for (uint16_t& v : im.px) v = uint16_t((v + 128) / 257 * 257);
  im.bits = 8;
  return im;
}

// The input image with f applied to each pixel's R, G, B (0..1), alpha kept: the alternatives of a hypothesis.
template <class F>
golden::Image each(const golden::Image& in, const F& f) {
  golden::Image out = in;
  for (size_t p = 0; p < out.px.size(); p += 4) {
    double c[3] = {in.px[p] / 65535., in.px[p + 1] / 65535., in.px[p + 2] / 65535.};
    f(c);
    for (size_t k = 0; k < 3; k++) out.px[p + k] = uint16_t(std::lround(std::clamp(c[k], 0., 1.) * 65535));
  }
  return out;
}

}  // namespace

// D32's images of the four (tests/golden/ps/p1/adjust, Photoshop's at B33; until then the sample's index, without
// images, whose vals must still read): each case's input adjusted by its vals, rounded to 8 bits, against Photoshop's;
// Invert, Posterize and Threshold within a level of 8 bits, Gradient Map printed (D30's methods are hypotheses). Beside
// each, how far the other hypotheses are: Posterize at the nearest level (GIMP's), Threshold and Gradient Map with
// Rec. 601's weights unrounded, Threshold with Lum rounded to a level first (as the histogram) or cut among Photoshop's
// 15-bit levels, Gradient Map with the gradient's opacity.
TEST(test_adj_simple_ps) {
  golden::Set ps("p1/adjust");
  if (!ps.has_index()) {
    ps.ready("test_adj_simple_ps");
    ps = golden::Set("p1/sample/adjust");
    if (!ps.has_index()) return;
  }
  const char* const kOurs[] = {"Invert", "Posterize", "Threshold", "Gradient Map"};
  int cases = 0, compared = 0;
  for (const golden::Entry& e : ps.entries()) {
    const std::vector<golden::SceneLayer> layers = ps.layers(e);
    if (layers.size() != 2 || layers[1].kind != "adj" || !layers[0].pixels || layers[1].mask) continue;
    const std::optional<LayerProps> props = props_from_json(layers[1].props);
    CHECK(props);
    if (std::none_of(std::begin(kOurs), std::end(kOurs), [&](const char* n) { return props->adj == n; })) continue;
    const AdjustmentPtr a = adjustment(*props);
    if (!a) std::printf("test_adj_simple_ps: %s: %s does not read\n", e.name.c_str(), layers[1].props.c_str());
    CHECK(a);
    cases++;
    const golden::Image want = ps.image(e);
    if (!want) continue;
    compared++;
    const golden::Image& in = *layers[0].pixels;
    const TileGrid done = apply_adjustment(*golden::grid(in, Format{Depth::U16, 4}), *a);
    const auto fit = [&](const golden::Image& im) { return want.bits == 8 ? as8(im) : im; };
    const golden::Diff d = golden::compare(fit(golden::image(done, in.w, in.h)), want, "p1/adjust/" + e.name);
    golden::print(e.name, d);
    const json vals = props->vals ? json::parse(*props->vals) : json::object();
    const auto alternative = [&](const char* what, const golden::Image& im) {
      golden::print(e.name + " (" + what + ")", golden::compare(fit(im), want));
    };
    if (props->adj == "Posterize") {
      const double n = std::round(std::clamp(vals.value("lv", 4.), 2., 255.));
      alternative("nearest level", each(in, [&](double* c) {
                    for (int k = 0; k < 3; k++) c[k] = std::round(c[k] * (n - 1)) / (n - 1);
                  }));
    } else if (props->adj == "Threshold") {
      const double t = std::clamp(vals.value("t", 128.), 1., 255.);
      alternative("Rec. 601", each(in, [&](double* c) {
                    c[0] = c[1] = c[2] = (0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2]) * 255 >= t ? 1 : 0;
                  }));
      alternative("Lum rounded", each(in, [&](double* c) {
                    c[0] = c[1] = c[2] = std::floor((0.30 * c[0] + 0.59 * c[1] + 0.11 * c[2]) * 255 + 0.5) >= t ? 1 : 0;
                  }));
      alternative("15-bit levels", each(in, [&](double* c) {  // Photoshop's 16 bits, t of 255 among 32768
                    double l = 0;
                    for (int k = 0; k < 3; k++)
                      l += kLumPercent[k] / 100. * golden::ps15(uint16_t(std::lround(c[k] * 65535)));
                    c[0] = c[1] = c[2] = l >= t * 32768 / 255 ? 1 : 0;
                  }));
    } else if (props->adj == "Gradient Map") {
      const Float4* lut = a->tables().data();
      const bool rev = vals.value("rev", false);
      const auto at = [&](double lum) { return adjust_curve(lut, float(rev ? 1 - lum : lum)); };
      alternative("Rec. 601", each(in, [&](double* c) {
                    const Float4 g = at(0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2]);
                    for (int k = 0; k < 3; k++) c[k] = g[size_t(k)];
                  }));
      alternative("opacity", each(in, [&](double* c) {
                    const Float4 g = at(0.30 * c[0] + 0.59 * c[1] + 0.11 * c[2]);
                    for (int k = 0; k < 3; k++) c[k] += (g[size_t(k)] - c[k]) * g[3];
                  }));
    }
    if (props->adj != "Gradient Map") CHECK(d.levels <= 1);
  }
  std::printf("test_adj_simple_ps: %d cases of %s, %d compared\n", cases, ps.name().c_str(), compared);
  ps.skipped("test_adj_simple_ps");
}
