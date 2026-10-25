// Selections (selection.h, S31). The shapes against references worked out pixel by pixel: the Rectangular Marquee's
// pixel centres (whole and fractional edges, cut at the canvas); the ellipse's exact area against its chords integrated
// down each pixel, summing to pi a b within 0.5%, its centres without anti-aliasing; the Lasso's pentagram at known
// points by either fill rule, its anti-aliased shares against the polygons it is made of (clipped to each pixel),
// simple polygons against their own; tiles inside one shared solid tile, outside none. The four modes on two hard and
// two soft shapes against their formulas, both ways of combining, the tiles they share; Select All the size of D1 (and
// D1 itself) without a pixel allocated; Inverse twice; Deselect and Reselect; Move Selection against a shift by hand,
// cut at the canvas, none of what an edge tile holds past it coming in; bounds and the 50% where the edges show; each
// command a step of Photoshop's name, undo giving back the state before. And S39's images of Photoshop's
// (tests/golden/ps/select/, skipped until B33): how far each hypothesis is (the fill rule, how soft selections combine,
// the ellipse's anti-aliasing, the rectangle's fractional edges), the comparison itself checked on stand-ins.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "check.h"
#include "doc.h"
#include "gauss.h"
#include "golden.h"
#include "history.h"
#include "pbd.h"
#include "selection.h"
#include "tilecache.h"

using namespace pb;
namespace fs = std::filesystem;
using nlohmann::json;

namespace {

constexpr int S = kTileSize;
constexpr double kPi = 3.14159265358979323846;
constexpr Depth kDepths[] = {Depth::U8, Depth::U16};
constexpr SelectMode kModes[] = {SelectMode::New, SelectMode::Add, SelectMode::Subtract, SelectMode::Intersect};

bool throws(const std::function<void()>& f) {
  try {
    f();
  } catch (const std::invalid_argument&) {
    return true;
  }
  return false;
}

// The pixels of g on a w x h canvas, row by row; null: none selected.
std::vector<int> pixels(const TileGrid& g, int w, int h) {
  const Format f = g.format();
  std::vector<uint8_t> buf(size_t(w) * size_t(h) * size_t(f.bytes()));
  g.read_rect(0, 0, w, h, buf.data(), size_t(w) * size_t(f.bytes()));
  std::vector<int> v(size_t(w) * size_t(h));
  for (size_t i = 0; i < v.size(); i++) {
    if (f.depth == Depth::U16) {
      uint16_t s;
      std::memcpy(&s, buf.data() + 2 * i, 2);
      v[i] = s;
    } else {
      v[i] = buf[i];
    }
  }
  return v;
}
std::vector<int> pixels(const std::shared_ptr<const TileGrid>& g, int w, int h) {
  return g ? pixels(*g, w, h) : std::vector<int>(size_t(w) * size_t(h));
}

// A selection's grid holding v (w x h, row by row) from (0, 0).
std::shared_ptr<const TileGrid> grid_of(const std::vector<int>& v, int w, int h, Depth d) {
  const Format f{d, 1};
  std::vector<uint8_t> buf(v.size() * size_t(f.bytes()));
  for (size_t i = 0; i < v.size(); i++) {
    if (d == Depth::U16) {
      const uint16_t s = uint16_t(v[i]);
      std::memcpy(buf.data() + 2 * i, &s, 2);
    } else {
      buf[i] = uint8_t(v[i]);
    }
  }
  auto g = std::make_shared<TileGrid>(f);
  g->write_rect(0, 0, w, h, buf.data(), size_t(w) * size_t(f.bytes()));
  return g;
}

// Coverage 0..1 as the level the functions round it to.
int level(double c, Depth d) {
  const int m = max_value(d);
  return c <= 0 ? 0 : c >= 1 ? m : int(c * m + .5);
}

// The tiles of g: solid ones (and how many distinct tiles they are), data ones, any past the canvas.
struct Kinds {
  int solid = 0, data = 0, past = 0;
  std::set<const Tile*> solids;
};
Kinds kinds(const TileGrid& g, int w, int h) {
  Kinds k;
  for (const auto& [xy, t] : g.tiles()) {
    if (xy.x < 0 || xy.y < 0 || xy.x * S >= w || xy.y * S >= h) k.past++;
    if (t->is_solid())
      k.solid++, k.solids.insert(t.get());
    else
      k.data++;
  }
  return k;
}

// ---- References -----------------------------------------------------------------------------------------------------

// The share of pixel (i, j) inside the ellipse about (cx, cy) of semi-axes a, b: the overlap of [i, i + 1] with the
// ellipse's chord integrated down the pixel (midpoints of 4,000 rows), whole and none decided by the pixel's corners.
double ellipse_share(double cx, double cy, double a, double b, int i, int j) {
  const double u0 = (i - cx) / a, u1 = (i + 1 - cx) / a, v0 = (j - cy) / b, v1 = (j + 1 - cy) / b;
  const double nu = std::clamp(0., u0, u1), nv = std::clamp(0., v0, v1);
  if (nu * nu + nv * nv >= 1) return 0;  // the nearest point of the pixel outside
  const double fu = std::max(u0 * u0, u1 * u1), fv = std::max(v0 * v0, v1 * v1);
  if (fu + fv <= 1) return 1;  // the farthest corner inside
  constexpr int kN = 4000;
  double sum = 0;
  for (int k = 0; k < kN; k++) {
    const double y = j + (k + .5) / kN, t = (y - cy) / b;
    if (t * t >= 1) continue;
    const double half = a * std::sqrt(1 - t * t);
    sum += std::max(0., std::min(double(i + 1), cx + half) - std::max(double(i), cx - half));
  }
  return sum / kN;
}

// The signed area of polygon p within the rectangle [x0, x1] x [y0, y1] (Sutherland-Hodgman, then the shoelace): the
// integral of its winding there, for a simple polygon the share it covers.
double clipped_area(const std::vector<SelectionPoint>& p, double x0, double y0, double x1, double y1) {
  std::vector<SelectionPoint> poly = p, next;
  const auto clip = [&](int axis, double v, bool keep_above) {
    next.clear();
    for (size_t k = 0; k < poly.size(); k++) {
      const SelectionPoint a = poly[k], b = poly[(k + 1) % poly.size()];
      const bool ia = keep_above ? a[size_t(axis)] >= v : a[size_t(axis)] <= v;
      const bool ib = keep_above ? b[size_t(axis)] >= v : b[size_t(axis)] <= v;
      if (ia) next.push_back(a);
      if (ia != ib) {
        const double t = (v - a[size_t(axis)]) / (b[size_t(axis)] - a[size_t(axis)]);
        next.push_back({a[0] + t * (b[0] - a[0]), a[1] + t * (b[1] - a[1])});
      }
    }
    poly.swap(next);
  };
  clip(0, x0, true), clip(0, x1, false), clip(1, y0, true), clip(1, y1, false);
  double sum = 0;
  for (size_t k = 0; k < poly.size(); k++) {
    const SelectionPoint a = poly[k], b = poly[(k + 1) % poly.size()];
    sum += a[0] * b[1] - b[0] * a[1];
  }
  return sum / 2;
}

// The winding number of polygon p about (x, y) (crossings of a ray to +x, half-open in y).
int winding(const std::vector<SelectionPoint>& p, double x, double y) {
  int w = 0;
  for (size_t k = 0; k < p.size(); k++) {
    const SelectionPoint a = p[k], b = p[(k + 1) % p.size()];
    if ((a[1] <= y) == (b[1] <= y)) continue;
    const double xc = a[0] + (y - a[1]) * (b[0] - a[0]) / (b[1] - a[1]);
    if (xc > x) w += b[1] > a[1] ? 1 : -1;
  }
  return w;
}
bool inside(int w, FillRule r) { return r == FillRule::EvenOdd ? (w & 1) != 0 : w != 0; }

// S39's pentagram (select.jsx's S.STAR), its points joined point to point: winding 2 in the middle.
const std::vector<SelectionPoint> kStar = {
    {128, 28}, {186.75, 208.875}, {32.875, 97.125}, {223.125, 97.125}, {69.25, 208.875}};
// Where its edges cross, and its outline (points and crossings by angle about its centre, 128, 128).
std::vector<SelectionPoint> star_crossings() {
  std::vector<SelectionPoint> out;
  for (size_t i = 0; i < 5; i++)
    for (size_t j = i + 2; j < 5; j++) {
      if (i == 0 && j == 4) continue;  // adjacent
      const SelectionPoint a = kStar[i], b = kStar[(i + 1) % 5], c = kStar[j], d = kStar[(j + 1) % 5];
      const double den = (b[0] - a[0]) * (d[1] - c[1]) - (b[1] - a[1]) * (d[0] - c[0]);
      const double t = ((c[0] - a[0]) * (d[1] - c[1]) - (c[1] - a[1]) * (d[0] - c[0])) / den;
      out.push_back({a[0] + t * (b[0] - a[0]), a[1] + t * (b[1] - a[1])});
    }
  CHECK(out.size() == 5);
  return out;
}
std::vector<SelectionPoint> by_angle(std::vector<SelectionPoint> p) {
  std::sort(p.begin(), p.end(), [](const SelectionPoint& a, const SelectionPoint& b) {
    return std::atan2(a[1] - 128, a[0] - 128) < std::atan2(b[1] - 128, b[0] - 128);
  });
  return p;
}

// ---- Shapes ---------------------------------------------------------------------------------------------------------

// Rectangles: the pixels whose centres lie in them, whole and fractional edges, either order of corners, cut at the
// canvas; tiles within one shared solid tile; the whole canvas as Select All.
void check_rect() {
  const int W = 600, H = 520;
  const SelectionBox boxes[] = {
      {10, 20, 300, 270},         {10.25, 10.5, 40.5, 30.75}, {300, 270, 10, 20}, {-50.3, -10, 700, 100.5},
      {255.5, 3.25, 256.5, 3.75}, {700, 0, 800, 50},          {5, 5, 5, 40},      {0, 0, W, H},
      {256, 0, 512, 256}};
  for (const Depth d : kDepths)
    for (const SelectionBox& b : boxes) {
      const TileGrid g = rect_selection(b, W, H, d);
      const std::vector<int> got = pixels(g, W, H);
      const double x0 = std::min(b.x0, b.x1), x1 = std::max(b.x0, b.x1), y0 = std::min(b.y0, b.y1),
                   y1 = std::max(b.y0, b.y1);
      int n = 0;
      for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
          const bool in = x0 <= x + .5 && x + .5 < x1 && y0 <= y + .5 && y + .5 < y1;
          CHECK(got[size_t(y) * W + size_t(x)] == (in ? max_value(d) : 0));
          n += in;
        }
      const Kinds k = kinds(g, W, H);
      CHECK(k.past == 0 && k.solids.size() <= 1);
      if (n == 0) CHECK(g.tiles().empty());
    }
  // fractional edges: 10.25 takes pixel 10, 10.5 too (its centre), 40.5 leaves pixel 40, 30.75 takes 30
  const std::vector<int> f = pixels(rect_selection({10.25, 10.5, 40.5, 30.75}, W, H, Depth::U8), W, H);
  CHECK(f[10 * W + 10] == 255 && f[9 * W + 10] == 0 && f[10 * W + 9] == 0 && f[30 * W + 39] == 255 &&
        f[30 * W + 40] == 0 && f[31 * W + 39] == 0);
  // the canvas: every tile the one solid tile, no pixel; a tile's worth exactly that tile
  const size_t raw = tile_stats().raw;
  const TileGrid all = rect_selection({0, 0, W, H}, W, H, Depth::U16);
  const Kinds k = kinds(all, W, H);
  CHECK(k.solid == 9 && k.data == 0 && k.solids.size() == 1 && tile_stats().raw == raw);
  const TileGrid one = rect_selection({256, 0, 512, 256}, W, H, Depth::U16);
  CHECK(one.tiles().size() == 1 && one.get(1, 0) && one.get(1, 0)->is_solid() && one.get(1, 0)->value()[0] == 65535);
  CHECK(throws([] { rect_selection({0, 0, std::numeric_limits<double>::quiet_NaN(), 5}, 10, 10, Depth::U8); }));
  CHECK(throws([] { rect_selection({0, 0, 1e30, 5}, 10, 10, Depth::U8); }));
  CHECK(throws([] { rect_selection({0, 0, 5, 5}, 0, 10, Depth::U8); }));
}

// Ellipses: anti-aliased, the exact share of each pixel (against the chords integrated), summing to pi a b within 0.5%
// (with a wider box too), cut at the canvas; not, the pixels whose centre is inside; tiles within shared solid.
void check_ellipse() {
  const int W = 1100, H = 700;
  const SelectionBox boxes[] = {{20, 30, 200, 130},        {30, 150, 90, 210},          {210, 150, 217, 155},
                                {220, 200, 223, 203},      {37.3, 41.7, 1037.9, 641.2}, {-40.5, 600.25, 99.75, 800},
                                {1090.1, 3.3, 1110.7, 9.9}};
  double worst = 0;
  for (const SelectionBox& b : boxes) {
    const double cx = (b.x0 + b.x1) / 2, cy = (b.y0 + b.y1) / 2, a = (b.x1 - b.x0) / 2, bb = (b.y1 - b.y0) / 2;
    const PixelRect around{std::max(0, int(std::floor(cx - a)) - 2), std::max(0, int(std::floor(cy - bb)) - 2),
                           std::min(W, int(std::ceil(cx + a)) + 2), std::min(H, int(std::ceil(cy + bb)) + 2)};
    std::vector<double> share;  // the reference, over `around`
    for (int y = around.y0; y < around.y1; y++)
      for (int x = around.x0; x < around.x1; x++) share.push_back(ellipse_share(cx, cy, a, bb, x, y));
    for (const Depth d : kDepths) {
      const TileGrid g = ellipse_selection(b, true, W, H, d);
      const std::vector<int> got = pixels(g, W, H);
      const int m = max_value(d);
      double sum = 0, want = 0, off = 0;
      for (int y = around.y0, k = 0; y < around.y1; y++)
        for (int x = around.x0; x < around.x1; x++, k++) {
          const double s = share[size_t(k)];
          const int v = got[size_t(y) * W + size_t(x)];
          sum += v / double(m), want += s;
          off = std::max(off, std::abs(v / double(m) - s));
        }
      double total = 0;
      for (const int v : got) total += v / double(m);
      CHECK(std::abs(total - sum) < 1e-9);  // nothing past the ellipse's box
      CHECK(off <= 2e-3 + .5 / m);          // each pixel its exact share
      const bool whole = b.x0 >= 0 && b.y0 >= 0 && b.x1 <= W && b.y1 <= H;
      if (whole) {
        const double rel = std::abs(sum - kPi * a * bb) / (kPi * a * bb);
        CHECK(rel <= .005);
        worst = std::max(worst, rel);
      } else {
        CHECK(std::abs(sum - want) <= 1e-3 * want + 1);  // cut at the canvas: what lies on it
      }
      CHECK(kinds(g, W, H).past == 0 && kinds(g, W, H).solids.size() <= 1);
      // without anti-aliasing: the centres inside or on the ellipse
      const std::vector<int> hard = pixels(ellipse_selection(b, false, W, H, d), W, H);
      for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
          const double du = (x + .5 - cx) / a, dv = (y + .5 - cy) / bb;
          CHECK(hard[size_t(y) * W + size_t(x)] == (du * du + dv * dv <= 1 ? m : 0));
        }
    }
  }
  std::printf("test_selection_ellipse: anti-aliased sums within %.2e of pi a b\n", worst);
  // a wider anti-aliasing box keeps the area, softens the edge
  const SelectionBox b{100.5, 100.5, 400.5, 300.5};
  const std::vector<int> one = pixels(ellipse_selection(b, true, W, H, Depth::U16), W, H);
  const std::vector<int> two = pixels(ellipse_selection(b, true, W, H, Depth::U16, 2), W, H);
  double s1 = 0, s2 = 0;
  int soft1 = 0, soft2 = 0;
  for (size_t i = 0; i < one.size(); i++) {
    s1 += one[i] / 65535., s2 += two[i] / 65535.;
    soft1 += one[i] > 0 && one[i] < 65535, soft2 += two[i] > 0 && two[i] < 65535;
  }
  CHECK(std::abs(s2 - s1) < 1e-3 * s1 && soft2 > soft1 * 3 / 2);
  // the big one: its middle tiles one solid tile, its corners' empty
  const TileGrid big = ellipse_selection({37.3, 41.7, 1037.9, 641.2}, true, W, H, Depth::U16);
  const Kinds k = kinds(big, W, H);
  CHECK(k.solid >= 2 && k.solids.size() == 1 && big.get(2, 1) && big.get(2, 1)->is_solid());
  CHECK(big.get(0, 0) && !big.get(0, 0)->is_solid() && !big.get(4, 2));  // across its edge; past it
  CHECK(ellipse_selection({5, 5, 5, 50}, true, W, H, Depth::U8).tiles().empty());
  CHECK(throws([] { ellipse_selection({0, 0, 5, 5}, true, 10, 10, Depth::U8, 0); }));
  CHECK(throws([] { ellipse_selection({0, 0, std::numeric_limits<double>::infinity(), 5}, true, 10, 10, Depth::U8); }));
}

// Polygons: the pentagram at known points by either rule (with and without anti-aliasing), its pixel centres against
// the winding, its shares against the outline and the pentagon it is made of; simple polygons' shares against their
// own clipped areas; one of whole pixels as the rectangle; too few points, a line, nothing.
void check_polygon() {
  const int W = 256, H = 256;
  const std::vector<SelectionPoint> cross = star_crossings();
  std::vector<SelectionPoint> outline = cross;
  outline.insert(outline.end(), kStar.begin(), kStar.end());
  outline = by_angle(outline);
  const std::vector<SelectionPoint> pentagon = by_angle(cross);
  // the shares: the outline's (non-zero), less the pentagon's (even-odd); none where three windings meet in a pixel
  std::vector<double> whole(size_t(W) * H), hole(whole.size());
  std::vector<char> near(whole.size());
  std::vector<int> wind(whole.size());
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      const size_t i = size_t(y) * W + size_t(x);
      whole[i] = std::abs(clipped_area(outline, x, y, x + 1, y + 1));
      hole[i] = std::abs(clipped_area(pentagon, x, y, x + 1, y + 1));
      near[i] = std::any_of(cross.begin(), cross.end(), [&](const SelectionPoint& c) {
        return std::abs(c[0] - (x + .5)) < 1.5 && std::abs(c[1] - (y + .5)) < 1.5;
      });
      wind[i] = winding(kStar, x + .5, y + .5);
    }
  for (const Depth d : kDepths)
    for (const FillRule rule : {FillRule::EvenOdd, FillRule::NonZero})
      for (const bool aa : {false, true}) {
        const int m = max_value(d);
        const std::vector<int> got = pixels(polygon_selection(kStar, aa, W, H, d, rule), W, H);
        const auto at = [&](int x, int y) { return got[size_t(y) * W + size_t(x)]; };
        // known points: the middle (winding 2) by the rule, a point (the top) and an arm (the left) in, outside out
        CHECK(at(128, 128) == (rule == FillRule::EvenOdd ? 0 : m) && at(120, 135) == at(128, 128));
        CHECK(at(128, 50) == m && at(60, 100) == m);
        CHECK(at(10, 10) == 0 && at(128, 200) == 0 && at(128, 230) == 0 && at(250, 250) == 0);
        for (size_t i = 0; i < got.size(); i++) {
          if (!aa)
            CHECK(got[i] == (inside(wind[i], rule) ? m : 0));
          else if (!near[i])
            CHECK(std::abs(got[i] - level(rule == FillRule::EvenOdd ? whole[i] - hole[i] : whole[i], d)) <= 1);
        }
      }
  // simple polygons: a triangle, a concave arrow across tiles and the canvas's edge; either rule alike
  const int W2 = 700, H2 = 600;
  const std::vector<std::vector<SelectionPoint>> simple = {{{150.25, 20.5}, {245.75, 50.125}, {170.5, 100.875}},
                                                           {{-30.5, 100.25},
                                                            {400.75, 80.5},
                                                            {390.125, 20.5},
                                                            {690.5, 300.25},
                                                            {380.5, 590.75},
                                                            {395.25, 500.5},
                                                            {10.5, 480.5}}};
  for (const auto& p : simple) {
    std::vector<double> share(size_t(W2) * H2);
    std::vector<char> centre(share.size());
    for (int y = 0; y < H2; y++)
      for (int x = 0; x < W2; x++) {
        share[size_t(y) * W2 + size_t(x)] = std::abs(clipped_area(p, x, y, x + 1, y + 1));
        centre[size_t(y) * W2 + size_t(x)] = winding(p, x + .5, y + .5) != 0;
      }
    for (const Depth d : kDepths)
      for (const FillRule rule : {FillRule::EvenOdd, FillRule::NonZero}) {
        const TileGrid g = polygon_selection(p, true, W2, H2, d, rule);
        const std::vector<int> got = pixels(g, W2, H2);
        for (size_t i = 0; i < got.size(); i++) CHECK(std::abs(got[i] - level(share[i], d)) <= 1);
        CHECK(kinds(g, W2, H2).past == 0 && kinds(g, W2, H2).solids.size() <= 1);
        const std::vector<int> hard = pixels(polygon_selection(p, false, W2, H2, d, rule), W2, H2);
        for (size_t i = 0; i < hard.size(); i++) CHECK(hard[i] == (centre[i] ? max_value(d) : 0));
      }
  }
  // a polygon of whole pixels: the rectangle, anti-aliased or not
  const std::vector<SelectionPoint> square = {{20, 30}, {300, 30}, {300, 280}, {20, 280}};
  const std::vector<int> r = pixels(rect_selection({20, 30, 300, 280}, W2, H2, Depth::U16), W2, H2);
  CHECK(pixels(polygon_selection(square, true, W2, H2, Depth::U16), W2, H2) == r);
  CHECK(pixels(polygon_selection(square, false, W2, H2, Depth::U16), W2, H2) == r);
  CHECK(polygon_selection({{1, 1}, {50, 50}}, true, W2, H2, Depth::U8).tiles().empty());
  CHECK(polygon_selection({{1, 1}, {50, 50}, {100, 100}}, true, W2, H2, Depth::U8).tiles().empty());
  CHECK(polygon_selection({}, false, W2, H2, Depth::U8).tiles().empty());
  CHECK(throws([] {
    polygon_selection({{1, 1}, {std::numeric_limits<double>::quiet_NaN(), 5}, {9, 9}}, true, 10, 10, Depth::U8);
  }));
}

// ---- Combining ------------------------------------------------------------------------------------------------------

int combined(int a, int b, SelectMode mode, SoftCombine how, int m) {  // the formulas, in reals, to the nearest level
  if (mode == SelectMode::New) return b;
  if (how == SoftCombine::MinMax)
    return mode == SelectMode::Add        ? std::max(a, b)
           : mode == SelectMode::Subtract ? std::min(a, m - b)
                                          : std::min(a, b);
  const double x = a / double(m), y = b / double(m);
  const double v = mode == SelectMode::Add ? x + y - x * y : mode == SelectMode::Subtract ? x * (1 - y) : x * y;
  return int(std::lround(v * m));
}

// The four modes on two hard shapes and on two soft ones (ramps across and down: every pair of levels), both ways of
// combining, against their formulas; tiles shared where one side decides; no selection on either side; nothing left.
void check_modes() {
  const int W = 520, H = 400;
  for (const Depth d : kDepths) {
    const int m = max_value(d);
    const auto a = std::make_shared<const TileGrid>(rect_selection({10, 10, 300, 200}, W, H, d));
    const auto b = std::make_shared<const TileGrid>(ellipse_selection({150, 100, 400, 350}, false, W, H, d));
    std::vector<int> ramp_x(size_t(W) * H), ramp_y(ramp_x.size());
    for (int y = 0; y < H; y++)
      for (int x = 0; x < W; x++) {
        ramp_x[size_t(y) * W + size_t(x)] = int(std::lround(double(x % 256) / 255 * m));
        ramp_y[size_t(y) * W + size_t(x)] = int(std::lround(double(y % 256) / 255 * m));
      }
    const auto sa = grid_of(ramp_x, W, H, d), sb = grid_of(ramp_y, W, H, d);
    for (const SoftCombine how : {SoftCombine::MinMax, SoftCombine::Product})
      for (const SelectMode mode : kModes)
        for (const auto& pair : {std::make_pair(a, b), std::make_pair(sa, sb)}) {
          const std::vector<int> va = pixels(*pair.first, W, H), vb = pixels(*pair.second, W, H);
          const auto out = combine_selection(pair.first, pair.second, mode, W, H, how);
          const std::vector<int> got = pixels(out, W, H);
          for (size_t i = 0; i < got.size(); i++) {
            const int want = combined(va[i], vb[i], mode, how, m);
            CHECK(std::abs(got[i] - want) <= (how == SoftCombine::Product && mode != SelectMode::New ? 1 : 0));
          }
          if (out) CHECK(kinds(*out, W, H).past == 0);
        }
    // hard shapes alike either way
    for (const SelectMode mode : kModes)
      CHECK(pixels(combine_selection(a, b, mode, W, H, SoftCombine::MinMax), W, H) ==
            pixels(combine_selection(a, b, mode, W, H, SoftCombine::Product), W, H));
    // one side decides a tile: the other's tile is shared
    const auto all = all_selected(W, H, d);
    const auto add = combine_selection(a, all, SelectMode::Add, W, H);
    for (const auto& [xy, t] : add->tiles()) CHECK(t == all->tiles().at(xy));
    const auto far =
        std::make_shared<const TileGrid>(rect_selection({400, 300, 500, 390}, W, H, d));  // tiles of its own
    const auto both = combine_selection(a, far, SelectMode::Add, W, H);
    for (const auto& [xy, t] : a->tiles()) CHECK(both->tiles().at(xy) == t);
    CHECK(combine_selection(a, far, SelectMode::Subtract, W, H) == a);         // nothing of a goes: a itself
    CHECK(combine_selection(a, far, SelectMode::Intersect, W, H) == nullptr);  // nothing is in both
    CHECK(combine_selection(a, a, SelectMode::Subtract, W, H) == nullptr);
    CHECK(combine_selection(a, a, SelectMode::Intersect, W, H) == a &&
          combine_selection(a, a, SelectMode::Add, W, H) == a);
    // nothing selected on a side
    CHECK(combine_selection(nullptr, b, SelectMode::New, W, H) == b &&
          combine_selection(nullptr, b, SelectMode::Add, W, H) == b);
    CHECK(!combine_selection(nullptr, b, SelectMode::Subtract, W, H) &&
          !combine_selection(nullptr, b, SelectMode::Intersect, W, H));
    CHECK(combine_selection(a, nullptr, SelectMode::Add, W, H) == a &&
          combine_selection(a, nullptr, SelectMode::Subtract, W, H) == a);
    CHECK(!combine_selection(a, nullptr, SelectMode::Intersect, W, H) &&
          !combine_selection(a, nullptr, SelectMode::New, W, H));
    const auto none = std::make_shared<const TileGrid>(Format{d, 1});
    CHECK(!combine_selection(a, none, SelectMode::New, W, H) && combine_selection(a, none, SelectMode::Add, W, H) == a);
    // a shape's tiles past the canvas left out, and what an edge tile holds past it does not count
    auto past = std::make_shared<TileGrid>(Format{d, 1});
    past->set(-1, 0, Tile::solid(Format{d, 1}, {uint16_t(m)}));
    past->set(2, 3, Tile::solid(Format{d, 1}, {uint16_t(m)}));  // x 512-767 of 520, y 768: past the bottom
    const size_t bytes = size_t(Format{d, 1}.bytes());
    auto edge = Tile::make(Format{d, 1});
    {
      WritePin p = edge->write();
      for (int y = 0; y < S; y++) std::memset(p.data() + (size_t(y) * S + 10) * bytes, 0xff, bytes);
    }
    past->set(2, 0, edge);  // column 512 + 10 = 522: past the canvas's 520
    CHECK(!selects_anything(*past, W, H) && !combine_selection(nullptr, past, SelectMode::New, W, H));
    const auto cut = combine_selection(a, past, SelectMode::Add, W, H);
    CHECK(cut && kinds(*cut, W, H).past == 0 && pixels(cut, W, H) == pixels(*a, W, H));
  }
  const auto u8 = std::make_shared<const TileGrid>(Format{Depth::U8, 1}),
             u16 = std::make_shared<const TileGrid>(Format{Depth::U16, 1});
  const auto rgba = std::make_shared<const TileGrid>(Format{Depth::U8, 4});
  CHECK(throws([&] { combine_selection(u8, u16, SelectMode::Add, 10, 10); }));
  CHECK(throws([&] { combine_selection(u8, rgba, SelectMode::Add, 10, 10); }));
  CHECK(throws([&] { combine_selection(u8, u8, SelectMode::Add, 0, 10); }));
}

// ---- Commands -------------------------------------------------------------------------------------------------------

// A document's state with `sel` selected.
DocPtr with(const DocPtr& d, std::shared_ptr<const TileGrid> sel) {
  auto n = std::make_shared<DocState>(*d);
  n->selection = std::move(sel);
  return n;
}

// Inverse: max - v, twice the selection back; all selected gives nothing, as Deselect.
void check_inverse() {
  const int W = 600, H = 300;
  for (const Depth d : kDepths) {
    const int m = max_value(d);
    auto soft = combine_selection(std::make_shared<const TileGrid>(rect_selection({0, 0, 260, 120}, W, H, d)),
                                  std::make_shared<const TileGrid>(feather_selection(
                                      ellipse_selection({200, 50, 500, 290}, true, W, H, d), 6, W, H, false)),
                                  SelectMode::Add, W, H);
    const auto inv = inverse_selection(*soft, W, H);
    const std::vector<int> p = pixels(*soft, W, H), q = pixels(inv, W, H);
    for (size_t i = 0; i < p.size(); i++) CHECK(q[i] == m - p[i]);
    CHECK(pixels(inverse_selection(*inv, W, H), W, H) == p);
    CHECK(kinds(*inv, W, H).past == 0);
    const auto all = all_selected(W, H, d);
    CHECK(!inverse_selection(*all, W, H));
    const DocPtr doc = new_document(W, H, Mode::Rgb, d, nullptr);
    CHECK(!select_inverse(*doc));
    const auto step = select_inverse(*with(doc, all));
    CHECK(step && !step->command.selection && step->name == "Select Inverse");
    const DocPtr after = run(*with(doc, all), step->command);
    CHECK(!after->selection && after->reselect == all);  // Reselect gives it back
  }
}

// Select All: a solid tile over every tile of the canvas, one tile, no pixel allocated: D1's size, and D1 itself.
void check_select_all() {
  const auto count = [](const DocState& d, const char* what) {
    const size_t raw = tile_stats().raw;
    const auto t0 = std::chrono::steady_clock::now();
    const SelectionStep s = select_all(d);
    const DocPtr after = run(d, s.command);  // validate()'s checks pass
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    const TileGrid& g = *after->selection;
    const Kinds k = kinds(g, d.width, d.height);
    const int across = (d.width + S - 1) / S, down = (d.height + S - 1) / S;
    CHECK(s.name == "Select All" && k.solid == across * down && k.data == 0 && k.past == 0 && k.solids.size() == 1);
    CHECK((*k.solids.begin())->value()[0] == max_value(d.depth));
    CHECK(tile_stats().raw == raw);  // no pixel
    const PixelRect r = selection_bounds(*after);
    CHECK(r == (PixelRect{0, 0, d.width, d.height}) && selection_visible(*after));
    std::printf("test_selection_all: Select All on %s, %d x %d: %d solid tiles, one of them, no pixel, %.3f ms\n", what,
                d.width, d.height, k.solid, ms);
  };
  count(*new_document(8256, 5504, Mode::Rgb, Depth::U16, nullptr), "D1's canvas");
  const fs::path d1 = "/home/ndmt1at21/photo-beauty/build/docs/d1.pbd";  // pb-bench make d1, read only
  if (!fs::exists(d1)) {
    std::printf("test_selection_all: no %s, D1 itself skipped\n", d1.string().c_str());
    return;
  }
  const DocPtr doc = open_pbd(d1).doc;
  CHECK(doc->width == 8256 && doc->height == 5504 && doc->depth == Depth::U16);
  count(*doc, "D1");
}

// The commands at D1's size (8256 x 5504, 16 bits), timed once (printed): what a mouse up costs. Their tiles: the
// shapes' insides solid, so that only their edges hold pixels.
void check_d1_size() {
  const int W = 8256, H = 5504;
  const DocPtr doc = new_document(W, H, Mode::Rgb, Depth::U16, nullptr);
  const auto ms = [](const std::function<void()>& f) {
    const auto t0 = std::chrono::steady_clock::now();
    f();
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  };
  std::optional<SelectionStep> rect, ellipse, star, add, inv, move;
  std::vector<SelectionPoint> lasso_pts;  // a freehand-like star of 2,000 points across most of the canvas
  for (int k = 0; k < 2000; k++) {
    const double t = 2 * kPi * k / 2000, r = 2600 + 900 * std::sin(7 * t) + 40 * std::sin(173 * t);
    lasso_pts.push_back({4128 + 1.4 * r * std::cos(t), 2752 + r * std::sin(t)});
  }
  const double t_rect = ms([&] { rect = rectangular_marquee(*doc, {100.5, 200.25, 8000.75, 5300.5}); });
  const DocPtr with_rect = run(*doc, rect->command);
  const double t_ellipse =
      ms([&] { ellipse = elliptical_marquee(*with_rect, {50.3, 40.7, 8200.1, 5450.9}, {SelectMode::Add, 0, true}); });
  const double t_star = ms([&] { star = lasso(*doc, lasso_pts); });
  const double t_add = ms(
      [&] { add = select_shape(*run(*doc, star->command), ellipse->command.selection, SelectMode::Intersect, "x"); });
  const DocPtr with_add = run(*doc, add->command);
  const double t_inv = ms([&] { inv = select_inverse(*with_add); });
  const double t_move = ms([&] { move = move_selection(*with_add, 13, -7); });
  for (const auto* s : {&rect, &ellipse, &star, &add, &inv, &move}) {
    CHECK(*s && (*s)->command.selection);
    const Kinds k = kinds(*(*s)->command.selection, W, H);
    CHECK(k.past == 0 && k.data < 600);  // edges only, of 726 tiles
  }
  const Kinds ke = kinds(*ellipse->command.selection, W, H), ks = kinds(*star->command.selection, W, H);
  std::printf(
      "test_selection_d1: %d x %d, 16 bits: Rectangular Marquee %.1f ms, Elliptical Marquee (Add) %.1f ms (%d data "
      "tiles, %d solid), Lasso of 2,000 points %.1f ms (%d data, %d solid), Intersect %.1f ms, Select Inverse %.1f ms, "
      "Move Selection %.1f ms\n",
      W, H, t_rect, t_ellipse, ke.data, ke.solid, t_star, ks.data, ks.solid, t_add, t_inv, t_move);
}

// Deselect, Reselect through a document's history; a marquee's click deselects in New, does nothing in the others.
void check_deselect() {
  const int W = 500, H = 400;
  Document doc(new_document(W, H, Mode::Rgb, Depth::U8, nullptr), "New");
  CHECK(!deselect(*doc.state()) && !reselect(*doc.state()));
  const auto run_step = [&](const std::optional<SelectionStep>& s) {
    CHECK(s);
    doc.run(s->command, s->name);
  };
  run_step(rectangular_marquee(*doc.state(), {10, 10, 200, 100}));
  const auto first = doc.state()->selection;
  CHECK(first);
  run_step(deselect(*doc.state()));
  CHECK(!doc.state()->selection && doc.state()->reselect == first && doc.step(doc.steps() - 1).name == "Deselect");
  run_step(reselect(*doc.state()));
  CHECK(doc.state()->selection == first && doc.step(doc.steps() - 1).name == "Reselect");
  CHECK(!reselect(*doc.state()));  // it is the selection already
  // a click: Deselect in New, nothing in the other modes, nothing with nothing selected
  for (const SelectMode mode : {SelectMode::Add, SelectMode::Subtract, SelectMode::Intersect}) {
    CHECK(!rectangular_marquee(*doc.state(), {50, 50, 50, 50}, {mode, 0, true}));
    CHECK(!lasso(*doc.state(), {{3, 3}, {4, 4}}, {mode, 0, true}));
  }
  const auto click = elliptical_marquee(*doc.state(), {50, 50, 50.2, 50.2}, {SelectMode::New, 0, false});
  CHECK(click && !click->command.selection && click->name == "Deselect");
  run_step(click);
  CHECK(!rectangular_marquee(*doc.state(), {50, 50, 50, 50}));
  CHECK(!polygonal_lasso(*doc.state(), {{3, 3}}));
  run_step(reselect(*doc.state()));
  CHECK(doc.state()->selection == first);
}

// Move Selection: by hand a shift, cut at the canvas, nothing coming in from past it (Select All's solid tiles); by
// whole tiles the same tiles; all of it off the canvas nothing.
void check_move() {
  const int W = 700, H = 500;
  for (const Depth d : kDepths) {
    const int m = max_value(d);
    const auto soft = std::make_shared<const TileGrid>(
        feather_selection(ellipse_selection({100, 80, 520, 430}, true, W, H, d), 4, W, H, false));
    const std::vector<int> p = pixels(*soft, W, H);
    for (const auto& dv : {std::array<int, 2>{13, -7}, std::array<int, 2>{-301, 45}, std::array<int, 2>{1, 0},
                           std::array<int, 2>{0, -10}}) {
      const auto out = moved_selection(*soft, dv[0], dv[1], W, H);
      CHECK(out && kinds(*out, W, H).past == 0);
      const std::vector<int> q = pixels(*out, W, H);
      for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
          const int sx = x - dv[0], sy = y - dv[1];
          const bool from = sx >= 0 && sy >= 0 && sx < W && sy < H;
          CHECK(q[size_t(y) * W + size_t(x)] == (from ? p[size_t(sy) * W + size_t(sx)] : 0));
        }
    }
    // Select All moved: the strip it uncovers not selected (its edge tiles' solid pixels past the canvas stay out)
    const auto all = all_selected(W, H, d);
    for (const int dx : {-10, 10}) {
      const std::vector<int> q = pixels(moved_selection(*all, dx, 3, W, H), W, H);
      for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
          const bool in = (dx < 0 ? x < W + dx : x >= dx) && y >= 3;
          CHECK(q[size_t(y) * W + size_t(x)] == (in ? m : 0));
        }
    }
    // by whole tiles: the tiles that stay inside are the same tiles
    const auto r = std::make_shared<const TileGrid>(rect_selection({0, 0, 600, 256}, W, H, d));
    const auto by = moved_selection(*r, S, 0, W, H);
    CHECK(by->get(1, 0) == r->get(0, 0) && by->get(2, 0) != nullptr && !by->get(0, 0));
    CHECK(!moved_selection(*soft, W, 0, W, H) && !moved_selection(*soft, 0, -H, W, H) &&
          !moved_selection(*soft, -2000000000, 0, W, H));
    const DocPtr doc = with(new_document(W, H, Mode::Rgb, d, nullptr), soft);
    CHECK(!move_selection(*doc, 0, 0) && !move_selection(*new_document(W, H, Mode::Rgb, d, nullptr), 1, 1));
    const auto s = move_selection(*doc, 10, 1);
    CHECK(s && s->name == "Move Selection" &&
          pixels(s->command.selection, W, H) == pixels(moved_selection(*soft, 10, 1, W, H), W, H));
  }
}

// Bounds of what is selected at all, on the canvas; the 50% where the edges show: 128 of 255, 32768 of 65535; a
// feathered speck selects but shows nothing.
void check_bounds() {
  const int W = 600, H = 400;
  for (const Depth d : kDepths) {
    const int m = max_value(d);
    const TileGrid r = rect_selection({10.5, 20.5, 300.25, 270.75}, W, H, d);  // centres 10.5..299.5, 20.5..270.5
    CHECK(selection_bounds(r, W, H) == (PixelRect{10, 20, 300, 271}) && selection_visible(r, W, H));
    const TileGrid e = ellipse_selection({-50, 100, 120, 500}, true, W, H, d);
    const PixelRect be = selection_bounds(e, W, H);
    CHECK(be.x0 == 0 && be.y0 == 100 && be.x1 == 120 && be.y1 == H);
    for (const int v : {m / 2, m / 2 + 1}) {
      std::vector<int> px(size_t(W) * H, 0);
      px[size_t(250) * W + 333] = v;
      const auto g = grid_of(px, W, H, d);
      CHECK(selection_visible(*g, W, H) == (2 * v >= m) && selects_anything(*g, W, H));
      CHECK(selection_bounds(*g, W, H) == (PixelRect{333, 250, 334, 251}));
    }
    const auto speck = std::make_shared<const TileGrid>(
        feather_selection(rect_selection({300, 200, 303, 203}, W, H, d), 10, W, H, false));
    CHECK(selects_anything(*speck, W, H) && !selection_visible(*speck, W, H));
    const TileGrid none(Format{d, 1});
    CHECK(selection_bounds(none, W, H).empty() && !selection_visible(none, W, H) && !selects_anything(none, W, H));
    const DocPtr doc = new_document(W, H, Mode::Rgb, d, nullptr);
    CHECK(selection_bounds(*doc).empty() && !selection_visible(*doc));
    CHECK(selection_visible(*with(doc, std::make_shared<const TileGrid>(r))));
  }
}

// Each command a step of Photoshop's name, the History its JSON; undo gives the state before back, redo the step's;
// the options bar's Feather blurs the shape before it meets the selection; what the commands refuse.
void check_steps() {
  const int W = 640, H = 480;
  Document doc(new_document(W, H, Mode::Rgb, Depth::U16, nullptr), "New");
  struct Made {
    std::string name;
    DocPtr before, after;
  };
  std::vector<Made> made;
  const auto step = [&](const std::optional<SelectionStep>& s, const char* name) {
    CHECK(s && s->name == name);
    const DocPtr before = doc.state();
    const size_t steps = doc.steps();
    doc.run(s->command, s->name);
    CHECK(doc.steps() == steps + 1 && doc.step(doc.current()).name == name &&
          doc.state()->selection == s->command.selection);
    CHECK(doc.step(doc.current()).json ==
          std::string("{\"op\":\"setSelection\",\"selection\":") + (s->command.selection ? "true}" : "false}"));
    made.push_back({name, before, doc.state()});
  };
  step(rectangular_marquee(*doc.state(), {20, 20, 300, 200}), "Rectangular Marquee");
  step(elliptical_marquee(*doc.state(), {200, 100, 500, 400}, {SelectMode::Add, 0, true}), "Elliptical Marquee");
  step(lasso(*doc.state(), {{50, 50}, {250, 60}, {150, 150.5}}, {SelectMode::Subtract, 0, true}), "Lasso");
  step(polygonal_lasso(*doc.state(), {{0, 0}, {600, 0}, {600, 300}, {0, 300}}, {SelectMode::Intersect, 0, false}),
       "Polygonal Lasso");
  step(select_inverse(*doc.state()), "Select Inverse");
  step(move_selection(*doc.state(), 5, -3), "Move Selection");
  step(modify_feather(*doc.state(), 7.5, true), "Feather");
  step(deselect(*doc.state()), "Deselect");
  step(reselect(*doc.state()), "Reselect");
  step(select_all(*doc.state()), "Select All");
  for (size_t i = made.size(); i-- > 0;) {
    CHECK(doc.state() == made[i].after);
    CHECK(doc.undo() && doc.state() == made[i].before);
  }
  for (const Made& m : made) CHECK(doc.redo() && doc.state() == m.after && doc.step(doc.current()).name == m.name);
  // each step's selection against the functions it is made of
  const auto at = [&](const char* name) {
    for (const Made& m : made)
      if (m.name == name) return m;
    CHECK(false);
    return made[0];
  };
  const auto lasso_step = at("Lasso");
  CHECK(pixels(lasso_step.after->selection, W, H) ==
        pixels(combine_selection(lasso_step.before->selection,
                                 std::make_shared<const TileGrid>(
                                     polygon_selection({{50, 50}, {250, 60}, {150, 150.5}}, true, W, H, Depth::U16)),
                                 SelectMode::Subtract, W, H),
               W, H));
  const auto f = at("Feather");
  CHECK(pixels(f.after->selection, W, H) == pixels(feather_selection(*f.before->selection, 7.5, W, H, true), W, H));
  // the options bar's Feather: the shape blurred, then added; the selection elsewhere as it was
  const DocPtr base = with(new_document(W, H, Mode::Rgb, Depth::U16, nullptr),
                           std::make_shared<const TileGrid>(rect_selection({0, 0, 100, 100}, W, H, Depth::U16)));
  const auto soft = rectangular_marquee(*base, {300, 200, 400, 300}, {SelectMode::Add, 12, true});
  const auto shape = std::make_shared<const TileGrid>(
      feather_selection(rect_selection({300, 200, 400, 300}, W, H, Depth::U16), 12, W, H, false));
  CHECK(soft && soft->name == "Rectangular Marquee" &&
        pixels(soft->command.selection, W, H) ==
            pixels(combine_selection(base->selection, shape, SelectMode::Add, W, H), W, H));
  const std::vector<int> got = pixels(soft->command.selection, W, H);
  CHECK(got[size_t(50) * W + 50] == 65535 && got[size_t(250) * W + 350] > 65000 && got[size_t(250) * W + 300] > 20000 &&
        got[size_t(250) * W + 300] < 45000 && got[size_t(150) * W + 150] == 0);
  // refusals
  const DocState& d = *base;
  CHECK(throws([&] { rectangular_marquee(d, {0, 0, 10, 10}, {SelectMode::New, -1, true}); }));
  CHECK(throws([&] { lasso(d, {{0, 0}, {10, 0}, {0, 10}}, {SelectMode::New, kFeatherMax + 1, true}); }));
  CHECK(throws([&] { modify_feather(d, std::numeric_limits<double>::quiet_NaN(), false); }));
  CHECK(!modify_feather(d, 0, false) && !modify_feather(*new_document(W, H, Mode::Rgb, Depth::U16, nullptr), 5, false));
  CHECK(throws([&] { select_shape(d, std::make_shared<const TileGrid>(Format{Depth::U8, 1}), SelectMode::Add, "x"); }));
  const auto magic = select_shape(d, base->selection, SelectMode::Add, "Magic Wand");
  CHECK(!magic);  // the selection as it was: no step
}

// ---- Photoshop's images (S39) ---------------------------------------------------------------------------------------

// The hypotheses a run of an entry's steps takes.
struct Hypotheses {
  FillRule rule = kLassoFillRule;
  SoftCombine how = kSoftCombine;
  double aa = kEllipseAntiAliasWidth;
};

SelectMode mode_of(const json& s) {
  const std::string m = s.value("mode", std::string("new"));
  for (int i = 0; i < 4; i++)
    if (m == kSelectModeNames[i]) return SelectMode(i);
  CHECK(false);
  return SelectMode::New;
}

// Whether an entry's steps are S31's: loads, marquees, lassos, Inverse, and not one plain load (S39's way in and out).
bool is_s31(const golden::Entry& e) {
  const json& steps = e.json["select"];
  static const std::set<std::string> ops = {"load", "rect", "ellipse", "lasso", "inverse"};
  for (const json& s : steps)
    if (!ops.count(s["op"].get<std::string>())) return false;
  return !(steps.size() == 1 && steps[0]["op"] == "load" && mode_of(steps[0]) == SelectMode::New);
}

// The selection after an entry's steps, photo-beauty's, by h (16 bits); null for nothing selected.
std::shared_ptr<const TileGrid> run_steps(golden::Set& set, const golden::Entry& e, const Hypotheses& h) {
  const int w = e.json["canvas"][0], ht = e.json["canvas"][1];
  std::shared_ptr<const TileGrid> sel;
  for (const json& s : e.json["select"]) {
    const std::string op = s["op"];
    if (op == "inverse") {
      sel = sel ? inverse_selection(*sel, w, ht) : nullptr;
      continue;
    }
    std::shared_ptr<const TileGrid> shape;
    if (op == "load") {
      shape = golden::grid(set.input(s["src"].get<std::string>()), Format{Depth::U16, 1});  // its red
    } else if (op == "lasso") {
      std::vector<SelectionPoint> pts;
      for (const json& p : s["points"]) pts.push_back({p[0].get<double>(), p[1].get<double>()});
      shape = std::make_shared<const TileGrid>(
          polygon_selection(pts, s.value("antiAlias", true), w, ht, Depth::U16, h.rule));
    } else {
      const json& r = s["rect"];
      const SelectionBox b{r[0].get<double>(), r[1].get<double>(), r[0].get<double>() + r[2].get<double>(),
                           r[1].get<double>() + r[3].get<double>()};
      shape = std::make_shared<const TileGrid>(
          op == "rect" ? rect_selection(b, w, ht, Depth::U16)
                       : ellipse_selection(b, s.value("antiAlias", true), w, ht, Depth::U16, h.aa));
    }
    sel = combine_selection(sel, shape, mode_of(s), w, ht, h.how);
  }
  return sel;
}

golden::Image image_of(const std::shared_ptr<const TileGrid>& sel, int w, int h) {
  if (sel) return golden::image(*sel, w, h);
  golden::Image im(w, h, 1, 16);
  for (size_t p = 3; p < im.px.size(); p += 4) im.px[p] = 65535;
  return im;
}

// Ours (0..65535) against Photoshop's (levels 0..32768 written to 16 bits), in levels of 8 bits: the largest, where,
// the mean, the root mean square.
struct Apart {
  double max = 0, mean = 0, rms = 0;
  int x = 0, y = 0;
};
Apart apart(const golden::Image& ours, const golden::Image& theirs) {
  Apart a;
  double sum = 0, sq = 0;
  for (int y = 0; y < ours.h; y++)
    for (int x = 0; x < ours.w; x++) {
      const double d = std::abs(ours.at(x, y)[0] / 65535. - golden::ps15(theirs.at(x, y)[0]) / 32768.) * 255;
      sum += d, sq += d * d;
      if (d > a.max) a.max = d, a.x = x, a.y = y;
    }
  const double n = double(ours.w) * double(ours.h);
  a.mean = sum / n, a.rms = std::sqrt(sq / n);
  return a;
}
std::string str(const Apart& a) {
  char s[128];
  std::snprintf(s, sizeof s, "%.3f levels of 8 bits at most (%d, %d), mean %.4f, rms %.4f", a.max, a.x, a.y, a.mean,
                a.rms);
  return s;
}

// Photoshop's PNG of a selection of ours, as a stand-in for it: levels of 32768 written back to 16 bits.
golden::Image as_photoshop(const golden::Image& ours) {
  golden::Image out(ours.w, ours.h, 1, 16);
  for (size_t p = 0; p < ours.px.size(); p += 4) {
    const long v = std::lround(ours.px[p] / 65535. * 32768);
    out.px[p] = out.px[p + 1] = out.px[p + 2] = uint16_t(std::lround(v * 65535. / 32768)), out.px[p + 3] = 65535;
  }
  return out;
}

// The pixels selected (50% and more) of an image within r, as the rectangle holding them; {} when none.
PixelRect selected_within(const golden::Image& im, bool photoshop, const PixelRect& r) {
  PixelRect b{r.x1, r.y1, r.x0, r.y0};
  for (int y = std::max(0, r.y0); y < std::min(im.h, r.y1); y++)
    for (int x = std::max(0, r.x0); x < std::min(im.w, r.x1); x++) {
      const uint16_t s = im.at(x, y)[0];
      if (photoshop ? golden::ps15(s) < 16384 : s < 32768) continue;
      b = {std::min(b.x0, x), std::min(b.y0, y), std::max(b.x1, x + 1), std::max(b.y1, y + 1)};
    }
  return b.empty() ? PixelRect{} : b;
}

// What an entry of S31's says of the hypotheses: ours against Photoshop's, and what the other hypothesis would be where
// the entry tells them apart (the fill rule, combining), the ellipse's anti-aliasing width fitted, the rectangles'
// edges both ways.
struct Finding {
  std::string name;
  Apart ours, other;    // other.max < 0: the entry has no other hypothesis
  double aa_width = 0;  // fitted, for an anti-aliased ellipse
  bool edges_alike = true;
};

Finding check_entry(golden::Set& set, const golden::Entry& e, const golden::Image& theirs) {
  Finding f;
  f.name = e.name;
  f.other.max = -1;
  const int w = e.json["canvas"][0], h = e.json["canvas"][1];
  const json& steps = e.json["select"];
  const Hypotheses main;
  f.ours = apart(image_of(run_steps(set, e, main), w, h), theirs);
  std::string what = "S31's " + str(f.ours);
  bool lasso = false, combining = false, ellipse_aa = false;
  for (const json& s : steps) {
    lasso = lasso || s["op"] == "lasso";
    combining = combining || mode_of(s) != SelectMode::New;
    ellipse_aa = ellipse_aa || (s["op"] == "ellipse" && s.value("antiAlias", true));
  }
  if (lasso || (combining && steps[0]["op"] == "load")) {
    Hypotheses other = main;
    if (lasso) other.rule = main.rule == FillRule::EvenOdd ? FillRule::NonZero : FillRule::EvenOdd;
    if (combining) other.how = main.how == SoftCombine::MinMax ? SoftCombine::Product : SoftCombine::MinMax;
    f.other = apart(image_of(run_steps(set, e, other), w, h), theirs);
    what += std::string("; ") + (lasso ? (other.rule == FillRule::NonZero ? "non-zero" : "even-odd") : "") +
            (combining ? (other.how == SoftCombine::Product ? "a + b - ab, a (1 - b), ab" : "max, min(a, 1 - b), min")
                       : "") +
            " instead: " + str(f.other);
  }
  if (ellipse_aa) {  // golden section on the width's logarithm, 1/4 to 4 px
    const auto cost = [&](double lw) {
      Hypotheses hw = main;
      hw.aa = std::exp(lw);
      return apart(image_of(run_steps(set, e, hw), w, h), theirs).rms;
    };
    const double phi = (std::sqrt(5.) - 1) / 2;
    double a = std::log(.25), b = std::log(4.), c = b - phi * (b - a), d = a + phi * (b - a), fc = cost(c),
           fd = cost(d);
    for (int i = 0; i < 24; i++) {
      if (fc < fd)
        b = d, d = c, fd = fc, c = b - phi * (b - a), fc = cost(c);
      else
        a = c, c = d, fc = fd, d = a + phi * (b - a), fd = cost(d);
    }
    f.aa_width = std::exp((a + b) / 2);
    char s[96];
    std::snprintf(s, sizeof s, "; anti-aliasing width fitted %.3f px (rms %.4f; kEllipseAntiAliasWidth %g)", f.aa_width,
                  cost(std::log(f.aa_width)), kEllipseAntiAliasWidth);
    what += s;
  }
  for (const json& s : steps) {  // the rectangles' edges: where Photoshop rounds fractions
    if (s["op"] != "rect") continue;
    const json& r = s["rect"];
    const double x0 = r[0], y0 = r[1], x1 = x0 + r[2].get<double>(), y1 = y0 + r[3].get<double>();
    const PixelRect around{int(std::floor(x0)) - 3, int(std::floor(y0)) - 3, int(std::ceil(x1)) + 3,
                           int(std::ceil(y1)) + 3};
    const PixelRect ps = selected_within(theirs, true, around);
    const PixelRect us = selected_within(image_of(run_steps(set, e, main), w, h), false, around);
    f.edges_alike = f.edges_alike && ps == us;
    char t[200];
    std::snprintf(t, sizeof t, "; rect [%g, %g, %g, %g]: Photoshop's pixels x %d-%d, y %d-%d, ours x %d-%d, y %d-%d",
                  x0, y0, x1, y1, ps.x0, ps.x1 - 1, ps.y0, ps.y1 - 1, us.x0, us.x1 - 1, us.y0, us.y1 - 1);
    what += t;
  }
  std::printf("ps select %s: %s\n", e.name.c_str(), what.c_str());
  std::fflush(stdout);
  return f;
}

// Every S31 entry of a set that has its image: its findings.
std::vector<Finding> check_s31(golden::Set& set, const char* test) {
  std::vector<Finding> out;
  int waiting = 0;
  for (const golden::Entry& e : set.entries()) {
    if (!is_s31(e)) continue;
    const golden::Image theirs = set.image(e);
    if (!theirs) {
      waiting++;
      continue;
    }
    CHECK(theirs.bits == 16 && theirs.channels == 1 && theirs.w == e.json["canvas"][0].get<int>() &&
          theirs.h == e.json["canvas"][1].get<int>());
    out.push_back(check_entry(set, e, theirs));
  }
  if (waiting) std::printf("%s: %d of S31's images not there\n", test, waiting);
  return out;
}

// A stand-in for Photoshop's set of the sample's S31 entries (tests/golden/ps/select/sample/), made here by h, in a
// folder of its own; check_s31() on it.
std::vector<Finding> stand_in(const Hypotheses& h) {
  golden::Set sample("select/sample");
  struct TempDir {
    fs::path path = fs::temp_directory_path() / ("pb-selection-" + std::to_string(std::random_device{}()));
    TempDir() { fs::create_directories(path); }
    ~TempDir() {
      std::error_code e;
      fs::remove_all(path, e);
    }
  } tmp;
  json index = sample.index();
  index["inputs"] = golden::kRoot.string();  // the inputs where they are
  index["images"] = json::array();
  for (const golden::Entry& e : sample.entries()) {
    if (!is_s31(e)) continue;
    index["images"].push_back(e.json);
    const int w = e.json["canvas"][0], ht = e.json["canvas"][1];
    golden::write_png(tmp.path / (e.name + ".png"), as_photoshop(image_of(run_steps(sample, e, h), w, ht)));
  }
  std::ofstream(tmp.path / "index.json") << index.dump();
  golden::Set set(tmp.path);
  CHECK(set.ready("test_selection_ps_stand_in"));
  return check_s31(set, "test_selection_ps_stand_in");
}

}  // namespace

TEST(test_selection_rect) { check_rect(); }
TEST(test_selection_ellipse) { check_ellipse(); }
TEST(test_selection_polygon) { check_polygon(); }
TEST(test_selection_modes) { check_modes(); }
TEST(test_selection_inverse) { check_inverse(); }
TEST(test_selection_all) { check_select_all(); }
TEST(test_selection_d1) { check_d1_size(); }
TEST(test_selection_deselect) { check_deselect(); }
TEST(test_selection_move) { check_move(); }
TEST(test_selection_bounds) { check_bounds(); }
TEST(test_selection_steps) { check_steps(); }

// The comparison on stand-ins: made by the hypotheses, ours come back within half a level of Photoshop's 16 bits and
// the rectangles' edges alike; made by the others (the other fill rule and way of combining, an anti-aliasing width of
// 2), ours are seen far off where the entries tell them apart, the others close, and the width found.
TEST(test_selection_ps_stand_in) {
  if (!fs::exists(golden::kRoot / "select" / "sample" / "index.json")) {
    std::printf("test_selection_ps_stand_in: skipped, no tests/golden/ps/select/sample/index.json\n");
    return;
  }
  const std::vector<Finding> same = stand_in(Hypotheses{});
  CHECK(same.size() >= 5);
  std::set<std::string> names;
  for (const Finding& f : same) {
    names.insert(f.name);
    CHECK(f.ours.max <= .5 / 32768 * 255 + 1e-9 && f.edges_alike);
    if (f.other.max >= 0) CHECK(f.other.max > 1);  // the other hypothesis seen to be off
    if (f.aa_width > 0) CHECK(std::abs(f.aa_width - kEllipseAntiAliasWidth) < .05);
  }
  CHECK(names.count("lasso-star-noaa") && names.count("ellipse") && names.count("rect-fractions") &&
        names.count("subtract-ramps") && names.count("inverse-soft"));
  Hypotheses others;
  others.rule = FillRule::NonZero, others.how = SoftCombine::Product, others.aa = 2;
  for (const Finding& f : stand_in(others)) {
    if (f.name == "lasso-star-noaa") CHECK(f.ours.max > 200 && f.other.max <= .01);
    if (f.name == "subtract-ramps") CHECK(f.ours.max > 30 && f.other.max <= .01);
    if (f.name == "ellipse") CHECK(f.ours.max > 1 && std::abs(f.aa_width - 2) < .05);
  }
}

// Photoshop's images, once B33 has brought them (until then said to be skipped): how far each hypothesis is.
TEST(test_selection_ps) {
  golden::Set set("select");
  if (!set.ready("test_selection_ps")) return;
  const std::vector<Finding> found = check_s31(set, "test_selection_ps");
  set.skipped("test_selection_ps");
  std::printf("test_selection_ps: %zu of S31's images compared\n", found.size());
}
