#pragma once
// C41's inputs: what tools/ps-golden/strokes.jsx paints on in Photoshop and the paths its strokes follow (strokes_in::
// below says what each holds), as tests/golden/ps/strokes/in/ of the repo has them: white.png, patches.png, tones.png,
// skin.png and paths.json. Images RGB or RGBA at 16 bits, every sample a level Photoshop reads back exactly, no colour
// chunk, as P1's inputs (tests/golden/ps/p1/in/, B30's). Header only, as golden.h: pb-bench ps-strokes
// (bench/ps_strokes.cpp) writes the files with it, test_ps_strokes_inputs checks the repo's against it, and tests feed
// a path to photo-beauty's tools as pen events (events()).
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "golden.h"
#include "hash.h"
#include "stroke.h"

namespace pb::golden::strokes_in {

inline constexpr int kW = 256, kH = 160;        // white.png, patches.png
inline constexpr int kPatch = 32;               // patches.png: 8 x 5 cells of kPatch
inline constexpr int kTonesW = 256, kBand = 16;  // tones.png: 8 bands of kBand rows
inline constexpr int kTonesH = 8 * kBand;
inline constexpr int kSkin = 128;  // skin.png: kSkin x kSkin
inline constexpr int kFiles = 4;   // all()'s images; paths.json besides
// The folder of the inputs under tests/golden/ps (golden::kRoot, pb-bench ps-strokes' <dir>): strokes.jsx's options()
// find them there as strokes/in/<name>, beside P1's as p1/in/<name>.
inline std::filesystem::path dir(const std::filesystem::path& root) { return root / "strokes" / "in"; }

namespace detail {

inline void put(Image& im, int x, int y, double r, double g, double b, double a = 1) {
  uint16_t* p = im.at(x, y);
  p[0] = ps_level(r), p[1] = ps_level(g), p[2] = ps_level(b), p[3] = ps_level(a);
}
inline double mix(double a, double b, double t) { return a + (b - a) * t; }
inline double smooth(double t) { return t * t * (3 - 2 * t); }

// Value noise in [0, 1): hash3() of the lattice corners (core/hash.h, exact everywhere), smoothstepped between them.
inline double noise(double x, double y, double cell, uint32_t seed) {
  const double u = x / cell, v = y / cell, fi = std::floor(u), fj = std::floor(v);
  const int i = int(fi), j = int(fj);
  const auto at = [&](int a, int b) { return double(hash_unit(hash3(uint32_t(a), uint32_t(b), seed))); };
  const double su = smooth(u - fi), sv = smooth(v - fj);
  const double top = mix(at(i, j), at(i + 1, j), su), bottom = mix(at(i, j + 1), at(i + 1, j + 1), su);
  return mix(top, bottom, sv);
}

}  // namespace detail

// white.png, RGB: kW x kH of white, what the brush, the pencil and the gradients paint on.
inline Image white() {
  Image im(kW, kH, 3);
  std::fill(im.px.begin(), im.px.end(), uint16_t(65535));
  return im;
}

// patches.png's colours (sRGB, 0..1), row by row of its cells, 8 a row: greys, primaries, secondaries, skin tones,
// darks, pastels.
inline constexpr double kPalette[24][3] = {
    {1, 1, 1},       {.75, .75, .75}, {.5, .5, .5},   {.25, .25, .25}, {0, 0, 0},     {1, 0, 0},       {0, 1, 0},    {0, 0, 1},
    {0, 1, 1},       {1, 0, 1},       {1, 1, 0},      {1, .5, 0},      {.96, .8, .69}, {.82, .6, .46},  {.55, .36, .24}, {.4, .25, .1},
    {1, .75, .8},    {0, .5, .5},     {.5, 0, .5},    {.5, .5, 0},     {0, 0, .5},    {.5, 0, 0},      {.75, 1, 0},  {.53, .81, .92}};

// patches.png, RGBA, kW x kH, cells of kPatch: rows 0-2 (y 0-95) opaque, kPalette's 24 colours, for the modes the
// brush paints in (Multiply, Screen, Clear) and the eraser; row 3 (y 96-127) the first row's colours with alpha x / 255
// across, for Behind and for erasing what is partly transparent; row 4 (y 128-159) transparent (black, alpha 0).
inline Image patches() {
  Image im(kW, kH, 4);
  for (int y = 0; y < kH; y++)
    for (int x = 0; x < kW; x++) {
      const int row = y / kPatch, col = x / kPatch;
      if (row == 4) continue;  // all 0
      const double* c = kPalette[(row == 3 ? 0 : row) * 8 + col];
      detail::put(im, x, y, c[0], c[1], c[2], row == 3 ? x / 255. : 1);
    }
  return im;
}

// tones.png, RGB, kTonesW x kTonesH: 8 bands of kBand rows, each a ramp across, t = x / 255, as B30's ramp.png in
// bands half as high (golden::inputs::ramp()): 0 grey t; 1, 2, 3 red, green, blue t (others 0); then hue 360 x / 256
// degrees at (saturation, value) 4 (1, 1), 5 (1, 0.5), 6 (0.5, 1), 7 (0.25, 0.75). Dodge, Burn and Sponge stroke
// along each band, a level of every value under them; the paint bucket fills from them.
inline Image tones() {
  const double sv[4][2] = {{1, 1}, {1, .5}, {.5, 1}, {.25, .75}};
  Image im(kTonesW, kTonesH, 3);
  for (int y = 0; y < kTonesH; y++)
    for (int x = 0; x < kTonesW; x++) {
      const int band = y / kBand;
      const double t = x / 255.;
      double rgb[3] = {t, t, t};
      if (band >= 1 && band <= 3)
        rgb[0] = rgb[1] = rgb[2] = 0, rgb[band - 1] = t;
      else if (band >= 4)
        inputs::hsv(360. * x / 256, sv[band - 4][0], sv[band - 4][1], rgb);
      detail::put(im, x, y, rgb[0], rgb[1], rgb[2]);
    }
  return im;
}

// A blemish of skin.png: an ellipse about (x, y) of radii rx, ry, its colour mixed in by strength (1 - d^2)^2 at d
// of the ellipse's 1.
struct Spot {
  double x, y, rx, ry, r, g, b, strength;
};
// skin.png's four, all left of x 52 (the strokes over them take their source 64 px right, on clean skin): a dark mole
// (a), a dark streak (b), a small dark dot (c), a red blemish (d).
inline const std::vector<Spot>& spots() {
  static const std::vector<Spot> all = {{20, 24, 6, 6, .48, .32, .24, .85},
                                        {36, 60, 10, 5, .52, .34, .27, .8},
                                        {18, 98, 4, 4, .36, .23, .18, .9},
                                        {40, 104, 7, 7, .86, .47, .44, .7}};
  return all;
}

// skin.png, RGB, kSkin square: skin of a light tone, darker to the bottom right, a texture of value noise (cells of 9
// and 3.5 px, about 4% either way), and spots(); values at pixel centres. For the Healing Brush, the Spot Healing
// Brush and the Clone Stamp.
inline Image skin() {
  Image im(kSkin, kSkin, 3);
  const double top[3] = {.87, .68, .56}, bottom[3] = {.8, .59, .48}, grain[3] = {.8, .7, .6};
  for (int y = 0; y < kSkin; y++)
    for (int x = 0; x < kSkin; x++) {
      const double cx = x + .5, cy = y + .5, t = (cx + cy) / (2 * kSkin);
      const double tex = (detail::noise(cx, cy, 9, 1) - .5) * .06 + (detail::noise(cx, cy, 3.5, 2) - .5) * .03;
      double c[3];
      for (int k = 0; k < 3; k++) c[k] = detail::mix(top[k], bottom[k], t) + tex * grain[k];
      for (const Spot& s : spots()) {
        const double u = (cx - s.x) / s.rx, v = (cy - s.y) / s.ry, d2 = u * u + v * v;
        if (d2 >= 1) continue;
        const double f = s.strength * (1 - d2) * (1 - d2), sc[3] = {s.r, s.g, s.b};
        for (int k = 0; k < 3; k++) c[k] = detail::mix(c[k], sc[k], f);
      }
      detail::put(im, x, y, c[0], c[1], c[2]);
    }
  return im;
}

// Every image of strokes/in/.
inline std::vector<inputs::File> all() {
  return {{"white.png", white()}, {"patches.png", patches()}, {"tones.png", tones()}, {"skin.png", skin()}};
}

// ---- Paths: paths.json ---------------------------------------------------------------------------------------------

// A path point as Photoshop's PathPointInfo: its anchor, the handle into it (leftDirection) and out of it
// (rightDirection); a corner point has both at the anchor. Document pixels, pixel (i, j) covering [i, i + 1) x
// [j, j + 1) (PenEvent's), multiples of 1/8.
struct Point {
  double x, y, ix, iy, ox, oy;
  bool corner() const { return ix == x && iy == y && ox == x && oy == y; }
};
inline Point corner(double x, double y) { return {x, y, x, y, x, y}; }
// An open subpath: from a point to the next a cubic Bezier through the first's out handle and the second's in handle
// (a straight line between corners); one point is a single dab. strokes.jsx strokes each as a path of its own.
struct Path {
  std::string name;
  std::vector<Point> points;
};

// The paths of strokes.jsx's cases: on white.png and patches.png (kW x kH) line, cross (down across line near its
// end), dot (a quarter pixel off the grid: where Photoshop centres a dab), diagonal (45 degrees), curve (an S), loop (a
// stroke crossing itself), shallow (a line of slope 22 / 144, for the pencil); h16, h48, h80, h112, h144 across a row of
// patches.png's cells each, past the edges; on tones.png band0-band7 along each band's middle, past the edges; on
// skin.png spot-a to spot-d over spots()' a to d.
inline const std::vector<Path>& paths() {
  static const std::vector<Path> all = [] {
    std::vector<Path> p = {
        {"line", {corner(16, 20), corner(240, 20)}},
        {"cross", {corner(224, 4), corner(224, 44)}},
        {"dot", {corner(28.25, 64.75)}},
        {"diagonal", {corner(60, 48), corner(116, 104)}},
        {"curve", {{136, 48, 136, 48, 200, 48}, {240, 104, 176, 104, 240, 104}}},
        {"loop", {corner(16, 112), corner(88, 156), corner(88, 112), corner(16, 156)}},
        {"shallow", {corner(104, 128), corner(248, 150)}}};
    for (int y = 16; y < kH; y += kPatch) p.push_back({"h" + std::to_string(y), {corner(-8, y), corner(kW + 8, y)}});
    for (int b = 0; b < 8; b++)
      p.push_back({"band" + std::to_string(b), {corner(-8, b * kBand + kBand / 2), corner(kTonesW + 8, b * kBand + kBand / 2)}});
    p.push_back({"spot-a", {corner(20, 24)}});
    p.push_back({"spot-b", {corner(28, 60), corner(44, 60)}});
    p.push_back({"spot-c", {corner(18, 98)}});
    p.push_back({"spot-d", {corner(40, 104)}});
    return p;
  }();
  return all;
}
inline const Path& path(const std::string& name) {
  for (const Path& p : paths())
    if (p.name == name) return p;
  throw std::out_of_range("golden: no path " + name + " in strokes_in::paths()");
}

namespace detail {
inline std::string number(double v) {  // a multiple of 1/8 as its shortest decimals
  char s[32];
  std::snprintf(s, sizeof s, "%.3f", v);
  std::string out = s;
  while (out.back() == '0') out.pop_back();
  if (out.back() == '.') out.pop_back();
  return out == "-0" ? "0" : out;
}
}  // namespace detail

// paths.json as pb-bench ps-strokes writes it, one path a line: {"paths": {"line": [[16, 20], [240, 20]], ...}}; a
// point [x, y] (a corner) or [x, y, in x, in y, out x, out y]. strokes.jsx reads it (eval: ExtendScript has no JSON).
inline std::string paths_json() {
  std::string s = "{\"paths\": {\n";
  for (size_t i = 0; i < paths().size(); i++) {
    const Path& p = paths()[i];
    s += "\"" + p.name + "\": [";
    for (size_t k = 0; k < p.points.size(); k++) {
      const Point& q = p.points[k];
      const double v[6] = {q.x, q.y, q.ix, q.iy, q.ox, q.oy};
      s += k ? ", [" : "[";
      for (int c = 0; c < (q.corner() ? 2 : 6); c++) s += (c ? ", " : "") + detail::number(v[c]);
      s += "]";
    }
    s += i + 1 < paths().size() ? "],\n" : "]\n";
  }
  return s + "}}\n";
}

// The path as pen events for a Stroke (stroke.h): its first anchor, then each segment sampled at least every `step` px
// (of its control polygon, uniformly in its parameter; a straight one evenly), pressure 1, 200 a second. DabPlacer
// lays dabs by arc length along a curve through them, as Photoshop strokes a path; a single point is a single dab.
inline std::vector<PenEvent> events(const Path& p, double step = .5) {
  std::vector<PenEvent> out;
  const auto add = [&](double x, double y) {
    PenEvent e;
    e.t = double(out.size()) / 200, e.x = float(x), e.y = float(y);
    out.push_back(e);
  };
  if (p.points.empty()) return out;
  add(p.points[0].x, p.points[0].y);
  for (size_t k = 1; k < p.points.size(); k++) {
    const Point &a = p.points[k - 1], &b = p.points[k];
    const double px[4] = {a.x, a.ox, b.ix, b.x}, py[4] = {a.y, a.oy, b.iy, b.y};
    double polygon = 0;
    for (int i = 0; i < 3; i++) polygon += std::hypot(px[i + 1] - px[i], py[i + 1] - py[i]);
    const int n = std::max(1, int(std::ceil(polygon / step)));
    const bool straight = a.ox == a.x && a.oy == a.y && b.ix == b.x && b.iy == b.y;
    for (int i = 1; i <= n; i++) {
      const double t = double(i) / n, u = 1 - t;
      if (straight)
        add(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
      else
        add(u * u * u * px[0] + 3 * u * u * t * px[1] + 3 * u * t * t * px[2] + t * t * t * px[3],
            u * u * u * py[0] + 3 * u * u * t * py[1] + 3 * u * t * t * py[2] + t * t * t * py[3]);
    }
  }
  return out;
}

// Where strokes of a brush of diameter `size` along the paths can paint: the bounding box of their points (handles
// included: a Bezier stays inside its control points) grown by size / 2 and a pixel, in whole pixels, clipped to w x h:
// [x, y, w, h]. index.json gives the Healing Brushes' and the Clone Stamp's (their "areas") for statistics.
inline std::array<int, 4> area(const Path& p, double size, int w, int h) {
  double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
  for (const Point& q : p.points)
    for (int c = 0; c < 3; c++) {
      const double x = c == 0 ? q.x : c == 1 ? q.ix : q.ox, y = c == 0 ? q.y : c == 1 ? q.iy : q.oy;
      x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
    }
  const int l = std::max(0, int(std::floor(x0 - size / 2 - 1))), t = std::max(0, int(std::floor(y0 - size / 2 - 1)));
  const int r = std::min(w, int(std::ceil(x1 + size / 2 + 1))), b = std::min(h, int(std::ceil(y1 + size / 2 + 1)));
  return {l, t, std::max(0, r - l), std::max(0, b - t)};
}

}  // namespace pb::golden::strokes_in
