#pragma once
// S39's inputs: the selections tools/ps-golden/select.jsx loads in Photoshop before Select > Modify, Inverse and the
// four ways of combining two selections (select_in:: below says what each holds), as tests/golden/ps/select/in/ of the
// repo has them: hard.png, aa.png, soft.png, ramp-x.png, ramp-y.png. A selection as an input is grey (R = G = B, the
// value selected 0..1) in an RGB file at 16 bits, every sample a level Photoshop reads back exactly, no colour chunk,
// as P1's inputs (tests/golden/ps/p1/in/, B30's, which the script's Magic Wand and Color Range cases pick from): the
// script opens it as one of them and loads its red channel as the selection. Header only, as golden.h: pb-bench
// ps-select (bench/ps_select.cpp) writes the files with it, test_ps_select_inputs checks the repo's against it.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <vector>

#include "golden.h"

namespace pb::golden::select_in {

inline constexpr int kSize = 256;  // every input of the set is kSize x kSize
inline constexpr int kFiles = 5;   // all()'s
// The folder of the inputs under tests/golden/ps (golden::kRoot, pb-bench ps-select's <dir>): select.jsx's options()
// find them there as select/in/<name>, beside P1's as p1/in/<name>.
inline std::filesystem::path dir(const std::filesystem::path& root) { return root / "select" / "in"; }

namespace detail {

// An input with nothing selected: grey 0, opaque.
inline Image blank() {
  Image im(kSize, kSize, 3);
  for (size_t p = 3; p < im.px.size(); p += 4) im.px[p] = 65535;
  return im;
}
inline void put(Image& im, int x, int y, double v) {
  uint16_t* p = im.at(x, y);
  p[0] = p[1] = p[2] = ps_level(v);
}
inline void fill(Image& im, int x0, int y0, int x1, int y1, double v) {  // [x0, x1) x [y0, y1)
  for (int y = y0; y < y1; y++)
    for (int x = x0; x < x1; x++) put(im, x, y, v);
}

// A polygon whose corners are multiples of 1/64, so that the sign of every edge's cross product with a point on a grid
// of 1/32 is exact: the same pixels in on every platform.
struct Polygon {
  std::vector<double> x, y;
};
inline double snap(double v) { return std::round(v * 64) / 64; }
// The even-odd rule (a ray to +x): the polygons here do not cross themselves, where it is the non-zero one too.
inline bool inside(const Polygon& p, double px, double py) {
  bool in = false;
  for (size_t i = 0, j = p.x.size() - 1; i < p.x.size(); j = i++) {
    const double xi = p.x[i], yi = p.y[i], xj = p.x[j], yj = p.y[j];
    if ((yi > py) == (yj > py)) continue;
    // where the edge crosses the row, compared without dividing: px < xi + (py - yi) (xj - xi) / (yj - yi)
    const double lhs = (px - xi) * (yj - yi), rhs = (py - yi) * (xj - xi);
    if (yj > yi ? lhs < rhs : lhs > rhs) in = !in;
  }
  return in;
}
// A star of `points` points about (cx, cy): its points `outer` out, its notches `inner`, the first point up.
inline Polygon star(double cx, double cy, int points, double outer, double inner) {
  Polygon p;
  const double pi = std::acos(-1.);
  for (int k = 0; k < 2 * points; k++) {
    const double a = -pi / 2 + k * pi / points, r = k % 2 ? inner : outer;
    p.x.push_back(snap(cx + r * std::cos(a)));
    p.y.push_back(snap(cy + r * std::sin(a)));
  }
  return p;
}
// A w x h rectangle about (cx, cy), turned by `degrees` (clockwise on screen, y down).
inline Polygon turned(double cx, double cy, double w, double h, double degrees) {
  Polygon p;
  const double a = degrees * std::acos(-1.) / 180, c = std::cos(a), s = std::sin(a);
  const double u[4] = {-w / 2, w / 2, w / 2, -w / 2}, v[4] = {-h / 2, -h / 2, h / 2, h / 2};
  for (int i = 0; i < 4; i++) {
    p.x.push_back(snap(cx + u[i] * c - v[i] * s));
    p.y.push_back(snap(cy + u[i] * s + v[i] * c));
  }
  return p;
}

}  // namespace detail

// hard.png: whole pixels in or out (pixel centres in the shape), for Feather, Expand, Contract, Border and Smooth: a
// rectangle in the top left corner (two canvas edges: Apply effect at canvas bounds) x 0-89, y 0-69; one 20 px wide on
// the right edge, x 236-255, y 20-89; a large one with holes of 3 x 3, 1 x 1 and 2 x 2, x 120-249, y 112-247 (what
// Contract 50 leaves); two 40 x 16 bars 4 px apart, x 130-169 and 174-213, y 80-95 (Expand bridges them); lines 1 and
// 2 px high, x 10-99 on rows 230 and 240-241; one 1 px wide, column 110 from row 120 to the bottom edge; a diagonal of
// single pixels (100 + i, 20 + i), i 0-29; dots of 1, 2, 3 and 5 px at (150, 30), (170, 30), (190, 30), (205, 28)
// (their top left); a five-pointed star about (60, 170), points 40 px out, notches 16 (corners of both signs).
inline Image hard() {
  using namespace detail;
  Image im = blank();
  fill(im, 0, 0, 90, 70, 1);
  fill(im, 236, 20, 256, 90, 1);
  fill(im, 120, 112, 250, 248, 1);
  fill(im, 160, 150, 163, 153, 0);
  fill(im, 200, 200, 201, 201, 0);
  fill(im, 180, 220, 182, 222, 0);
  fill(im, 130, 80, 170, 96, 1);
  fill(im, 174, 80, 214, 96, 1);
  fill(im, 10, 230, 100, 231, 1);
  fill(im, 10, 240, 100, 242, 1);
  fill(im, 110, 120, 111, 256, 1);
  for (int i = 0; i < 30; i++) put(im, 100 + i, 20 + i, 1);
  fill(im, 150, 30, 151, 31, 1);
  fill(im, 170, 30, 172, 32, 1);
  fill(im, 190, 30, 193, 33, 1);
  fill(im, 205, 28, 210, 33, 1);
  const Polygon s = star(60, 170, 5, 40, 16);
  for (int y = 120; y < 220; y++)
    for (int x = 10; x < 110; x++)
      if (inside(s, x + .5, y + .5)) put(im, x, y, 1);
  return im;
}

// aa.png: shapes anti-aliased (the share of 16 x 16 samples a pixel in them), for what Expand, Contract, Border and
// Smooth make of a soft edge: a disc about (64, 64) r 40; a triangle (150.25, 20.5), (245.75, 50.125), (170.5,
// 100.875); a rectangle 80 x 40 about (180, 180) turned 30 degrees; a line 1.5 px wide from (16, 150) to (110, 240),
// round ends; a disc about (8, 200) r 24, cut by the left edge of the canvas.
inline Image aa() {
  using namespace detail;
  Image im = blank();
  Polygon tri;
  tri.x = {150.25, 245.75, 170.5}, tri.y = {20.5, 50.125, 100.875};
  const Polygon box = turned(180, 180, 80, 40, 30);
  const auto line = [](double x, double y) {  // within 0.75 of the segment, squares of multiples of 1/32: exact
    const double ax = 16, ay = 150, bx = 110, by = 240, dx = bx - ax, dy = by - ay;
    const double t = (x - ax) * dx + (y - ay) * dy, len2 = dx * dx + dy * dy;
    if (t <= 0) return (x - ax) * (x - ax) + (y - ay) * (y - ay) <= .5625;
    if (t >= len2) return (x - bx) * (x - bx) + (y - by) * (y - by) <= .5625;
    const double cross = (x - ax) * dy - (y - ay) * dx;
    return cross * cross <= .5625 * len2;
  };
  const auto in = [&](double x, double y) {
    return (x - 64) * (x - 64) + (y - 64) * (y - 64) <= 40 * 40 || inside(tri, x, y) || inside(box, x, y) || line(x, y) ||
           (x - 8) * (x - 8) + (y - 200) * (y - 200) <= 24 * 24;
  };
  for (int y = 0; y < kSize; y++)
    for (int x = 0; x < kSize; x++) {
      int n = 0;
      for (int j = 0; j < 16; j++)
        for (int i = 0; i < 16; i++) n += in(x + (i + .5) / 16, y + (j + .5) / 16);
      if (n) put(im, x, y, n / 256.);
    }
  return im;
}

// soft.png: a soft selection, for Feather and Expand of one and Select > Inverse: a Gaussian of sigma 12 about (64,
// 64), about 1 at its peak, to 5 sigma; a disc about (190, 66), 1 to r 30 falling straight to 0 at r 50; a ramp across, x /
// 255, rows 150-179; plateaus of 0.6, 0.5 (exactly half: level 16384 of Photoshop's 32768) and 0.4, x 30-79, 80-129,
// 130-179, rows 200-239 (which of them Expand and Contract take as selected). Values at pixel centres.
inline Image soft() {
  using namespace detail;
  Image im = blank();
  for (int y = 0; y < kSize; y++)
    for (int x = 0; x < kSize; x++) {
      const double cx = x + .5, cy = y + .5;
      const double g2 = (cx - 64) * (cx - 64) + (cy - 64) * (cy - 64);
      const double d = std::sqrt((cx - 190) * (cx - 190) + (cy - 66) * (cy - 66));
      double v = g2 <= 60 * 60 ? std::exp(-g2 / (2 * 12 * 12)) : 0;
      v = std::max(v, std::clamp((50 - d) / 20, 0., 1.));
      if (y >= 150 && y < 180) v = x / 255.;
      if (y >= 200 && y < 240 && x >= 30 && x < 180) v = x < 80 ? .6 : x < 130 ? .5 : .4;
      put(im, x, y, v);
    }
  return im;
}

// ramp-x.png, ramp-y.png: x / 255 across, y / 255 down: loaded one after the other with Add, Subtract, Intersect, the
// image is the combination of every pair of 256 levels, as alpha.png on itself turned is for blending (B30).
inline Image ramp(bool down) {
  Image im = detail::blank();
  for (int y = 0; y < kSize; y++)
    for (int x = 0; x < kSize; x++) detail::put(im, x, y, (down ? y : x) / 255.);
  return im;
}

// Every file of select/in/.
inline std::vector<inputs::File> all() {
  return {{"hard.png", hard()}, {"aa.png", aa()}, {"soft.png", soft()}, {"ramp-x.png", ramp(false)},
          {"ramp-y.png", ramp(true)}};
}

}  // namespace pb::golden::select_in
