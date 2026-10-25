#include "selection.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <utility>

#include "gauss.h"
#include "jobs.h"
#include "profile.h"

namespace pb {
namespace {

constexpr int S = kTileSize;
constexpr double kMaxCoord = 16777216;  // 2^24 px: what a shape's coordinates may reach, far past any canvas
constexpr double kPi = 3.14159265358979323846;

std::invalid_argument bad(const std::string& why) { return std::invalid_argument("pb: selection: " + why); }

void check_canvas(int w, int h) {
  if (w < 1 || h < 1) throw bad("a canvas without pixels");
}
void check_coord(double v) {
  if (!(std::abs(v) <= kMaxCoord)) throw bad("a coordinate not finite, or past 2^24 px");
}
// A selection's grid: one channel, 0 where empty.
void check_grid(const TileGrid& g) {
  if (g.format().channels != 1 || g.fill()[0] != 0) throw bad("a selection is a grid of one channel, 0 where empty");
}

int across_of(int w) { return int((int64_t(w) + S - 1) >> kTileLog2); }  // tiles of a canvas's row
uint16_t max_of(Format f) { return uint16_t(max_value(f.depth)); }

// Coverage 0..1 as a level of `max`, to the nearest (NaN: 0).
uint16_t level(double c, int max) { return !(c > 0) ? 0 : c >= 1 ? uint16_t(max) : uint16_t(c * max + 0.5); }

// An integer pixel coordinate from a real one, clamped to lo..hi first (no overflow for any finite value).
int clamped(double v, int lo, int hi) { return int(std::clamp(v, double(lo), double(hi))); }

// ---- Rasterizing ----------------------------------------------------------------------------------------------------

// A shape's coverage of pixel row y: cov[i] for pixel box.x0 + i, 0..1. cov has box width + 1 entries, zeroed; the last
// is spare. Called on any thread, rows of a tile row in order on one.
using Row = std::function<void(int y, double* cov)>;

// The shape whose rows of pixels `box` (on the canvas) `row` gives, as a selection's grid: pixels elsewhere 0. Tile
// rows in parallel; each row's coverage to levels, tile by tile, a tile's pixels allocated only once those on the
// canvas stop being one value (until then it stays that value: empty or a solid tile in the end, the shape's inside
// one shared tile).
TileGrid raster(int w, int h, Depth depth, const PixelRect& box, const Row& row) {
  PROFILE_ZONE("selection_raster");
  const Format f{depth, 1};
  TileGrid out(f);
  if (box.empty()) return out;
  const int max = max_value(depth);
  const int tx0 = box.x0 >> kTileLog2, tx1 = ((box.x1 - 1) >> kTileLog2) + 1;
  const int ty0 = box.y0 >> kTileLog2, ty1 = ((box.y1 - 1) >> kTileLog2) + 1;
  const int across = tx1 - tx0, down = ty1 - ty0, n = box.x1 - box.x0;
  const TilePtr full = Tile::solid(f, {uint16_t(max)});
  std::vector<TilePtr> made(size_t(across) * size_t(down));
  parallel_for(down, [&](int b0, int b1) {
    std::vector<double> cov(size_t(n) + 1);
    std::vector<uint16_t> lv(size_t(across) * S);  // the row's levels from tile column tx0's left edge
    struct Open {                                  // a tile of the tile row being made
      bool seen = false;
      uint16_t v = 0;  // what its pixels on the canvas held so far, while it has no pixels
      std::shared_ptr<Tile> tile;
      std::optional<WritePin> pin;
    };
    std::vector<Open> opened(static_cast<size_t>(across));
    for (int b = b0; b < b1; b++) {
      const int ya = (ty0 + b) * S, yb = std::min(ya + S, h);
      for (int y = ya; y < yb; y++) {
        std::fill(lv.begin(), lv.end(), uint16_t(0));
        if (y >= box.y0 && y < box.y1) {
          std::fill(cov.begin(), cov.end(), 0.);
          row(y, cov.data());
          uint16_t* o = lv.data() + (box.x0 - tx0 * S);
          for (int i = 0; i < n; i++) o[i] = level(cov[size_t(i)], max);
        }
        for (int i = 0; i < across; i++) {
          const int xn = std::min(S, w - (tx0 + i) * S);  // the tile's pixels on the canvas
          const uint16_t* r = lv.data() + size_t(i) * S;
          Open& t = opened[size_t(i)];
          if (!t.tile) {
            if (!t.seen) t.seen = true, t.v = r[0];
            if (std::all_of(r, r + xn, [&](uint16_t v) { return v == t.v; })) continue;
            t.tile = Tile::make(f, {t.v});  // the rows so far hold t.v
            t.pin.emplace(t.tile->write());
          }
          uint8_t* d = t.pin->data() + size_t(y - ya) * S * size_t(f.bytes());
          if (depth == Depth::U16)
            std::memcpy(d, r, size_t(xn) * 2);
          else
            for (int x = 0; x < xn; x++) d[x] = uint8_t(r[x]);
        }
      }
      for (int i = 0; i < across; i++) {
        Open& t = opened[size_t(i)];
        t.pin.reset();  // unpinned before it is shared
        TilePtr& done = made[size_t(b) * size_t(across) + size_t(i)];
        if (t.tile)
          done = std::move(t.tile);  // its pixels on the canvas differ: compact() would keep it
        else if (t.v)
          done = t.v == max ? full : Tile::solid(f, {t.v});
        t = Open{};
      }
    }
  });
  for (int b = 0; b < down; b++)
    for (int i = 0; i < across; i++)
      if (TilePtr& t = made[size_t(b) * size_t(across) + size_t(i)]) out.set(tx0 + i, ty0 + b, std::move(t));
  return out;
}

// Pixels x0 <= x < x1, y0 <= y < y1 within the canvas.
PixelRect on_canvas(double x0, double y0, double x1, double y1, int w, int h) {
  return {clamped(x0, 0, w), clamped(y0, 0, h), clamped(x1, 0, w), clamped(y1, 0, h)};
}

SelectionBox normal(const SelectionBox& b) {
  check_coord(b.x0), check_coord(b.y0), check_coord(b.x1), check_coord(b.y1);
  return {std::min(b.x0, b.x1), std::min(b.y0, b.y1), std::max(b.x0, b.x1), std::max(b.y0, b.y1)};
}

// ---- The ellipse's exact area ---------------------------------------------------------------------------------------

// The unit disc within x >= t.
double half_plane(double t) {
  if (t <= -1) return kPi;
  if (t >= 1) return 0;
  return std::acos(t) - t * std::sqrt(1 - t * t);
}
// The unit disc within x >= u, y >= v, for u, v >= 0: the integral of sqrt(1 - x^2) - v from u to sqrt(1 - v^2).
double corner(double u, double v) {
  if (u >= 1 || v >= 1 || u * u + v * v >= 1) return 0;
  const auto prim = [](double x) { return (x * std::sqrt(std::max(0., 1 - x * x)) + std::asin(x)) / 2; };
  const double xm = std::sqrt(1 - v * v);
  return std::max(0., prim(xm) - prim(u) - v * (xm - u));
}
// The unit disc within x >= u, y >= v, any u, v: from corner() by its symmetries.
double quadrant(double u, double v) {
  if (u >= 0 && v >= 0) return corner(u, v);
  if (v >= 0) return half_plane(v) - corner(-u, v);
  if (u >= 0) return half_plane(u) - corner(u, -v);
  return kPi - half_plane(-u) - half_plane(-v) + corner(-u, -v);
}
// The unit disc within [u0, u1] x [v0, v1].
double disc_in_rect(double u0, double u1, double v0, double v1) {
  return std::max(0., quadrant(u0, v0) - quadrant(u1, v0) - quadrant(u0, v1) + quadrant(u1, v1));
}

// ---- The polygon ----------------------------------------------------------------------------------------------------

struct Edge {
  double x0, y0, x1, y1;  // y0 < y1
  double dir;             // +1 where the outline runs down (y growing), -1 up
  double x_at(double y) const { return x0 + (y - y0) * (x1 - x0) / (y1 - y0); }
};

// The integral from -inf to t of min(max(s, 0), 1) ds: t^2 / 2 within 0..1, t - 1/2 past it.
double ramp_area(double t) { return t <= 0 ? 0 : t < 1 ? t * t / 2 : t - .5; }

// Adds to acc the winding an edge's piece within one row brings, `d` (its direction times its height within the row)
// from xl to xr: pixel i (of all the plane) gets d times the share of the piece's height left of its right side, so
// that the running sum over i is the integral of the winding over each pixel. acc[0] gathers what lands left of x0
// (pixels x0 + k at acc[k + 1]), what lands at x1 and past is dropped.
void accumulate(double* acc, int x0, int x1, double d, double xl, double xr) {
  const bool wide = xr - xl > 1e-6;
  const double xm = (xl + xr) / 2;
  const auto upto = [&](double i) {  // d times the share of the piece left of pixel i's right side, i + 1
    return wide ? d * (ramp_area(i + 1 - xl) - ramp_area(i + 1 - xr)) / (xr - xl) : d * std::clamp(i + 1 - xm, 0., 1.);
  };
  const double fa = std::floor(xl), fb = std::floor(xr) + 1;  // where upto() rises: from 0 below fa to d from fb on
  if (fb < x0) {
    acc[0] += d;
    return;
  }
  if (fa >= x1) return;
  const int lo = std::max(int(fa), x0), hi = std::min(int(fb), x1 - 1);
  double before = upto(lo - 1);
  acc[0] += before;
  for (int i = lo; i <= hi; i++) {
    const double now = upto(i);
    acc[i - x0 + 1] += now - before;
    before = now;
  }
}

// A pixel's share selected from the integral of the winding over it, v (windings 0 and 1 the most of any canvas).
double covered(double v, FillRule rule) {
  v = std::abs(v);
  if (v <= 1) return v;
  if (rule == FillRule::NonZero) return 1;
  v = std::fmod(v, 2.);  // where the windings w and w + 1 meet in a pixel: the share of the odd one
  return v <= 1 ? v : 2 - v;
}

// ---- Selections as grids --------------------------------------------------------------------------------------------

struct Canvas {
  int w, h;
  int across, down;  // its tiles
  Canvas(int w_, int h_) : w(w_), h(h_), across(across_of(w_)), down(across_of(h_)) {}
  bool has(TileXY xy) const { return xy.x >= 0 && xy.y >= 0 && xy.x < across && xy.y < down; }
  int columns(int tx) const { return std::min(S, w - tx * S); }  // a tile's pixels on the canvas
  int rows(int ty) const { return std::min(S, h - ty * S); }
};

// Whether some pixel of g on the canvas is `at_least` or more: solid tiles at once, data tiles scanned in parallel.
template <class T>
bool any_at_least(const Canvas& c, unsigned at_least, const std::vector<const Tile*>& data,
                  const std::vector<TileXY>& at) {
  std::atomic<bool> found{false};
  parallel_for(int(data.size()), [&](int begin, int end) {
    for (int k = begin; k < end && !found.load(std::memory_order_relaxed); k++) {
      const Pin p = data[size_t(k)]->pin();
      const T* px = reinterpret_cast<const T*>(p.data());
      const int cols = c.columns(at[size_t(k)].x), rows = c.rows(at[size_t(k)].y);
      for (int y = 0; y < rows; y++)
        if (std::any_of(px + size_t(y) * S, px + size_t(y) * S + cols, [&](T v) { return v >= at_least; })) {
          found.store(true, std::memory_order_relaxed);
          break;
        }
    }
  });
  return found.load();
}
bool any_at_least(const TileGrid& g, int w, int h, unsigned at_least) {
  PROFILE_ZONE("selection_any");
  check_grid(g);
  check_canvas(w, h);
  const Canvas c(w, h);
  std::vector<const Tile*> data;
  std::vector<TileXY> at;
  for (const auto& [xy, t] : g.tiles()) {
    if (!c.has(xy)) continue;
    if (!t->is_solid())
      data.push_back(t.get()), at.push_back(xy);
    else if (t->value()[0] >= at_least)  // a tile of the canvas has a pixel on it
      return true;
  }
  return g.format().depth == Depth::U16 ? any_at_least<uint16_t>(c, at_least, data, at)
                                        : any_at_least<uint8_t>(c, at_least, data, at);
}

// g as a selection: null when it selects nothing on the canvas.
std::shared_ptr<const TileGrid> held(TileGrid g, int w, int h) {
  if (!any_at_least(g, w, h, 1)) return nullptr;
  return std::make_shared<const TileGrid>(std::move(g));
}

// A tile's samples by index (row by row), 0..max: an empty tile 0, a solid one its value.
template <class T>
struct Samples {
  const Tile* t;
  Pin pin;
  T v = 0;
  explicit Samples(const Tile* tile) : t(tile) {
    if (t && !t->is_solid())
      pin = t->pin();
    else if (t)
      v = T(t->value()[0]);
  }
  bool data() const { return pin.data() != nullptr; }
  T at(size_t i) const { return data() ? reinterpret_cast<const T*>(pin.data())[i] : v; }
};

// a op b by mode and how, levels of max.
template <class T>
T combined(T a, T b, SelectMode mode, SoftCombine how, uint32_t max) {
  if (how == SoftCombine::MinMax) {
    switch (mode) {
      case SelectMode::Add:
        return std::max(a, b);
      case SelectMode::Subtract:
        return std::min(a, T(max - b));
      default:
        return std::min(a, b);
    }
  }
  const uint64_t h = max / 2;
  switch (mode) {
    case SelectMode::Add:
      return T(a + (uint64_t(b) * (max - a) + h) / max);  // a + b - ab
    case SelectMode::Subtract:
      return T((uint64_t(a) * (max - b) + h) / max);  // a (1 - b)
    default:
      return T((uint64_t(a) * b + h) / max);  // ab
  }
}

// A tile out of two in, every pixel combined, compacted.
template <class T>
TilePtr combine_tile(Format f, const Tile* a, const Tile* b, SelectMode mode, SoftCombine how) {
  const uint32_t max = max_of(f);
  const Samples<T> sa(a), sb(b);
  auto out = Tile::make(f);
  {
    WritePin w = out->write();
    T* o = reinterpret_cast<T*>(w.data());
    for (size_t i = 0; i < size_t(S) * S; i++) o[i] = combined(sa.at(i), sb.at(i), mode, how, max);
  }
  return compact(out, Pixel{});
}

// Two tiles' combination where it needs no pixel: one of them, a solid tile, or empty; else `compute`.
struct Shortcut {
  bool compute = false;
  TilePtr tile;
};
Shortcut shortcut(const TilePtr& a, const TilePtr& b, SelectMode mode, SoftCombine how, Format f, const TilePtr& full) {
  const uint16_t max = max_of(f);
  if (a == b && how == SoftCombine::MinMax && mode != SelectMode::Subtract) return {false, a};  // max, min of itself
  const auto value = [](const TilePtr& t) { return t ? t->value()[0] : uint16_t(0); };
  const bool ad = a && !a->is_solid(), bd = b && !b->is_solid();
  if (!ad && !bd) {  // both one value: one of them where it is the value out
    const uint16_t v = combined<uint16_t>(value(a), value(b), mode, how, max);
    if (v == 0) return {false, nullptr};
    if (a && v == value(a)) return {false, a};
    if (b && v == value(b)) return {false, b};
    return {false, v == max ? full : Tile::solid(f, {v})};
  }
  const bool a0 = !a, b0 = !b, a1 = a && a->is_solid() && value(a) == max, b1 = b && b->is_solid() && value(b) == max;
  switch (mode) {  // the same either way of combining: 0 and 1 are their identities and their absorbing values
    case SelectMode::Add:
      if (a0 || b1) return {false, b};
      if (b0 || a1) return {false, a};
      break;
    case SelectMode::Subtract:
      if (b0) return {false, a};
      if (a0 || b1) return {false, nullptr};
      break;
    default:
      if (a0 || b0) return {false, nullptr};
      if (a1) return {false, b};
      if (b1) return {false, a};
  }
  return {true, nullptr};
}

}  // namespace

// ---- Shapes ---------------------------------------------------------------------------------------------------------

TileGrid rect_selection(const SelectionBox& box, int width, int height, Depth depth) {
  check_canvas(width, height);
  const SelectionBox b = normal(box);
  // pixel i is in when b.x0 <= i + 0.5 < b.x1: from ceil(x0 - 0.5) to before ceil(x1 - 0.5)
  const PixelRect r =
      on_canvas(std::ceil(b.x0 - .5), std::ceil(b.y0 - .5), std::ceil(b.x1 - .5), std::ceil(b.y1 - .5), width, height);
  const int n = r.x1 - r.x0;
  return raster(width, height, depth, r, [n](int, double* cov) { std::fill(cov, cov + n, 1.); });
}

TileGrid ellipse_selection(const SelectionBox& box, bool anti_alias, int width, int height, Depth depth,
                           double aa_width) {
  check_canvas(width, height);
  if (!(aa_width > 0 && aa_width <= 64)) throw bad("an anti-aliasing width outside 0 to 64 px");
  const SelectionBox b = normal(box);
  const double cx = (b.x0 + b.x1) / 2, cy = (b.y0 + b.y1) / 2, a = (b.x1 - b.x0) / 2, bb = (b.y1 - b.y0) / 2;
  if (!(a > 0 && bb > 0)) return TileGrid(Format{depth, 1});
  const double s = anti_alias ? aa_width : 0, half = s / 2;  // pixel i takes the box i + 0.5 +- half
  const PixelRect r = on_canvas(std::floor(cx - a - .5 - half), std::floor(cy - bb - .5 - half),
                                std::ceil(cx + a - .5 + half) + 1, std::ceil(cy + bb - .5 + half) + 1, width, height);
  if (!anti_alias) {
    const auto in = [&](int i, double dv2) {
      const double du = (i + .5 - cx) / a;
      return du * du + dv2 <= 1;
    };
    return raster(width, height, depth, r, [&, r](int y, double* cov) {
      const double dv = (y + .5 - cy) / bb, dv2 = dv * dv;
      if (dv2 > 1) return;
      const double w = a * std::sqrt(1 - dv2);
      int lo = clamped(std::ceil(cx - w - .5), r.x0, r.x1), hi = clamped(std::floor(cx + w - .5), r.x0 - 1, r.x1 - 1);
      while (lo > r.x0 && in(lo - 1, dv2)) lo--;  // the square root's last bit, against the test itself
      while (lo <= hi && !in(lo, dv2)) lo++;
      while (hi < r.x1 - 1 && in(hi + 1, dv2)) hi++;
      while (hi >= lo && !in(hi, dv2)) hi--;
      for (int i = lo; i <= hi; i++) cov[i - r.x0] = 1;
    });
  }
  const double area = a * bb / (s * s);  // the unit disc's area to a share of a pixel's box
  return raster(width, height, depth, r, [&, r](int y, double* cov) {
    const double v0 = (y + .5 - half - cy) / bb, v1 = (y + .5 + half - cy) / bb;
    if (v0 >= 1 || v1 <= -1) return;
    const double near = v0 <= 0 && v1 >= 0 ? 0 : std::min(std::abs(v0), std::abs(v1));
    const double far = std::max(std::abs(v0), std::abs(v1));
    const double outer = a * std::sqrt(std::max(0., 1 - near * near));  // the widest chord within the box's rows
    const double inner = far < 1 ? a * std::sqrt(1 - far * far) : -1;   // the narrowest, none where some row misses
    const auto exact = [&](int i) {
      const double l = i + .5 - half, rr = i + .5 + half;
      cov[i - r.x0] = std::min(1., area * disc_in_rect((l - cx) / a, (rr - cx) / a, v0, v1));
    };
    // whole: the box of pixel i within the narrowest chord, l >= cx - inner and r <= cx + inner
    const int f0 = inner < 0 ? r.x1 : clamped(std::ceil(cx - inner - .5 + half), r.x0, r.x1);
    const int f1 = inner < 0 ? r.x0 - 1 : clamped(std::floor(cx + inner - .5 - half), r.x0 - 1, r.x1 - 1);
    const int p0 = clamped(std::floor(cx - outer - .5 - half), r.x0, r.x1);
    const int p1 = clamped(std::ceil(cx + outer - .5 + half), r.x0 - 1, r.x1 - 1);
    if (f0 > f1) {
      for (int i = p0; i <= p1; i++) exact(i);
      return;
    }
    for (int i = p0; i < f0; i++) exact(i);
    for (int i = f0; i <= f1; i++) cov[i - r.x0] = 1;
    for (int i = f1 + 1; i <= p1; i++) exact(i);
  });
}

TileGrid polygon_selection(const std::vector<SelectionPoint>& points, bool anti_alias, int width, int height,
                           Depth depth, FillRule rule) {
  check_canvas(width, height);
  for (const SelectionPoint& p : points) check_coord(p[0]), check_coord(p[1]);
  if (points.size() < 3) return TileGrid(Format{depth, 1});
  std::vector<Edge> edges;
  double x0 = points[0][0], x1 = x0, y0 = points[0][1], y1 = y0;
  for (size_t k = 0; k < points.size(); k++) {
    const SelectionPoint &p = points[k], &q = points[(k + 1) % points.size()];
    x0 = std::min(x0, p[0]), x1 = std::max(x1, p[0]), y0 = std::min(y0, p[1]), y1 = std::max(y1, p[1]);
    if (p[1] < q[1])
      edges.push_back({p[0], p[1], q[0], q[1], 1});
    else if (p[1] > q[1])
      edges.push_back({q[0], q[1], p[0], p[1], -1});
  }
  const PixelRect r = on_canvas(std::floor(x0), std::floor(y0), std::ceil(x1) + 1, std::ceil(y1) + 1, width, height);
  if (r.empty() || edges.empty()) return TileGrid(Format{depth, 1});
  // The edges of each tile row the shape covers.
  const int ty0 = r.y0 >> kTileLog2, ty1 = ((r.y1 - 1) >> kTileLog2) + 1;
  std::vector<std::vector<Edge>> rows(size_t(ty1 - ty0));
  for (const Edge& e : edges) {
    const int a = std::max(ty0, clamped(std::floor(e.y0 / S), ty0, ty1)), z = clamped(std::ceil(e.y1 / S), ty0, ty1);
    for (int t = a; t < z; t++) rows[size_t(t - ty0)].push_back(e);
  }
  if (!anti_alias) {
    return raster(width, height, depth, r, [&, r](int y, double* cov) {
      thread_local std::vector<std::pair<double, double>> cross;  // x, direction
      cross.clear();
      const double yc = y + .5;
      for (const Edge& e : rows[size_t((y >> kTileLog2) - ty0)])
        if (e.y0 <= yc && yc < e.y1) cross.push_back({e.x_at(yc), e.dir});
      std::sort(cross.begin(), cross.end());
      double wind = 0;
      for (size_t k = 0; k + 1 < cross.size(); k++) {
        wind += cross[k].second;
        const int w = int(std::lround(wind));
        if (rule == FillRule::EvenOdd ? (w & 1) == 0 : w == 0) continue;
        // centres x + 0.5 within [cross k, cross k + 1)
        const int lo = clamped(std::ceil(cross[k].first - .5), r.x0, r.x1);
        const int hi = clamped(std::ceil(cross[k + 1].first - .5), r.x0, r.x1);
        for (int i = lo; i < hi; i++) cov[i - r.x0] = 1;
      }
    });
  }
  const int n = r.x1 - r.x0;
  return raster(width, height, depth, r, [&, r, n](int y, double* cov) {
    for (const Edge& e : rows[size_t((y >> kTileLog2) - ty0)]) {
      if (e.y1 <= y || e.y0 >= y + 1) continue;
      const double ya = std::max(e.y0, double(y)), yb = std::min(e.y1, double(y + 1));
      const double xa = e.x_at(ya), xb = e.x_at(yb);
      accumulate(cov, r.x0, r.x1, e.dir * (yb - ya), std::min(xa, xb), std::max(xa, xb));
    }
    double sum = cov[0];  // the winding's integral, pixel by pixel
    for (int k = 1; k <= n; k++) {
      sum += cov[k];
      cov[k - 1] = covered(sum, rule);
    }
  });
}

// ---- Grids of selections --------------------------------------------------------------------------------------------

std::shared_ptr<const TileGrid> combine_selection(const std::shared_ptr<const TileGrid>& selection,
                                                  const std::shared_ptr<const TileGrid>& shape, SelectMode mode,
                                                  int width, int height, SoftCombine how) {
  PROFILE_ZONE("selection_combine");
  check_canvas(width, height);
  if (selection) check_grid(*selection);
  if (shape) check_grid(*shape);
  if (selection && shape && selection->format() != shape->format()) throw bad("selections of two depths");
  const Canvas c(width, height);
  // a grid as it is when it holds no tile past the canvas, else its tiles on it
  const auto on_canvas_only = [&](const std::shared_ptr<const TileGrid>& g) -> std::shared_ptr<const TileGrid> {
    if (std::all_of(g->tiles().begin(), g->tiles().end(), [&](const auto& kv) { return c.has(kv.first); })) return g;
    auto cut = std::make_shared<TileGrid>(g->format());
    for (const auto& [xy, t] : g->tiles())
      if (c.has(xy)) cut->set(xy.x, xy.y, t);
    return cut;
  };
  const auto kept = [&](const std::shared_ptr<const TileGrid>& g) -> std::shared_ptr<const TileGrid> {
    if (!g || !any_at_least(*g, width, height, 1)) return nullptr;
    return on_canvas_only(g);
  };
  if (mode == SelectMode::New) return kept(shape);
  if (!shape) return mode == SelectMode::Intersect ? nullptr : kept(selection);
  if (!selection) return mode == SelectMode::Add ? kept(shape) : nullptr;
  const Format f = shape->format();
  const TilePtr full = Tile::solid(f, {max_of(f)});
  // every tile of the canvas either holds, combined
  std::vector<TileXY> at;
  for (const auto* g : {selection.get(), shape.get()})
    for (const auto& kv : g->tiles())
      if (c.has(kv.first)) at.push_back(kv.first);
  std::sort(at.begin(), at.end());
  at.erase(std::unique(at.begin(), at.end()), at.end());
  std::vector<TilePtr> out(at.size());
  std::vector<int> work;
  bool as_selection = true, as_shape = true;  // every tile out is that grid's own
  for (size_t k = 0; k < at.size(); k++) {
    const auto find = [&](const TileGrid& g) {
      const auto it = g.tiles().find(at[k]);
      return it == g.tiles().end() ? TilePtr() : it->second;
    };
    const TilePtr a = find(*selection), b = find(*shape);
    const Shortcut s = shortcut(a, b, mode, how, f, full);
    if (s.compute) {
      work.push_back(int(k));
      as_selection = as_shape = false;
      continue;
    }
    out[k] = s.tile;
    as_selection = as_selection && s.tile == a;
    as_shape = as_shape && s.tile == b;
  }
  parallel_for(int(work.size()), [&](int begin, int end) {
    for (int j = begin; j < end; j++) {
      const size_t k = size_t(work[size_t(j)]);
      const Tile* a = selection->get(at[k].x, at[k].y);
      const Tile* b = shape->get(at[k].x, at[k].y);
      out[k] = f.depth == Depth::U16 ? combine_tile<uint16_t>(f, a, b, mode, how)
                                     : combine_tile<uint8_t>(f, a, b, mode, how);
    }
  });
  if (as_selection) return kept(selection);
  if (as_shape) return kept(shape);
  TileGrid g(f);
  for (size_t k = 0; k < at.size(); k++)
    if (out[k]) g.set(at[k].x, at[k].y, std::move(out[k]));
  return held(std::move(g), width, height);
}

std::shared_ptr<const TileGrid> all_selected(int width, int height, Depth depth) {
  check_canvas(width, height);
  const Format f{depth, 1};
  const Canvas c(width, height);
  auto g = std::make_shared<TileGrid>(f);
  const TilePtr full = Tile::solid(f, {uint16_t(max_value(depth))});
  for (int ty = 0; ty < c.down; ty++)
    for (int tx = 0; tx < c.across; tx++) g->set(tx, ty, full);
  return g;
}

std::shared_ptr<const TileGrid> inverse_selection(const TileGrid& selection, int width, int height) {
  PROFILE_ZONE("selection_inverse");
  check_grid(selection);
  check_canvas(width, height);
  const Format f = selection.format();
  const uint16_t max = max_of(f);
  const Canvas c(width, height);
  const TilePtr full = Tile::solid(f, {max});
  TileGrid g(f);
  std::vector<std::pair<TileXY, const Tile*>> data;
  for (int ty = 0; ty < c.down; ty++)
    for (int tx = 0; tx < c.across; tx++) {
      const Tile* t = selection.get(tx, ty);
      if (!t) {
        g.set(tx, ty, full);
      } else if (t->is_solid()) {
        const uint16_t v = uint16_t(max - t->value()[0]);
        if (v) g.set(tx, ty, v == max ? full : Tile::solid(f, {v}));
      } else {
        data.push_back({{tx, ty}, t});
      }
    }
  std::vector<TilePtr> made(data.size());
  parallel_for(int(data.size()), [&](int begin, int end) {
    for (int k = begin; k < end; k++) {
      const Pin p = data[size_t(k)].second->pin();
      auto t = Tile::make(f);
      {
        WritePin w = t->write();
        const size_t n = size_t(S) * S;
        if (f.depth == Depth::U16) {
          const uint16_t* s = reinterpret_cast<const uint16_t*>(p.data());
          uint16_t* o = reinterpret_cast<uint16_t*>(w.data());
          for (size_t i = 0; i < n; i++) o[i] = uint16_t(max - s[i]);
        } else {
          for (size_t i = 0; i < n; i++) w.data()[i] = uint8_t(max - p.data()[i]);
        }
      }
      made[size_t(k)] = compact(t, Pixel{});
    }
  });
  for (size_t k = 0; k < data.size(); k++)
    if (made[k]) g.set(data[k].first.x, data[k].first.y, std::move(made[k]));
  return held(std::move(g), width, height);
}

std::shared_ptr<const TileGrid> moved_selection(const TileGrid& selection, int dx, int dy, int width, int height) {
  PROFILE_ZONE("selection_moved");
  check_grid(selection);
  check_canvas(width, height);
  if (dx <= -width || dx >= width || dy <= -height || dy >= height) return nullptr;  // all of it off the canvas
  // where pixels land on the canvas from pixels on it: x on it and x - dx on it
  const PixelRect clip{std::max(0, dx), std::max(0, dy), std::min(width, width + dx), std::min(height, height + dy)};
  return held(place(selection, Placement{false, false, false, dx, dy}, clip), width, height);
}

PixelRect selection_bounds(const TileGrid& selection, int width, int height) {
  check_grid(selection);
  check_canvas(width, height);
  return content_bounds(selection, Pixel{}, PixelRect{0, 0, width, height});
}

bool selection_visible(const TileGrid& selection, int width, int height) {
  return any_at_least(selection, width, height, (unsigned(max_value(selection.format().depth)) + 1) / 2);
}

bool selects_anything(const TileGrid& selection, int width, int height) {
  return any_at_least(selection, width, height, 1);
}

// ---- Commands -------------------------------------------------------------------------------------------------------

std::optional<SelectionStep> select_shape(const DocState& d, const std::shared_ptr<const TileGrid>& shape,
                                          SelectMode mode, std::string name) {
  if (shape && shape->format() != mask_format(d)) throw bad("a shape not in the document's mask format");
  std::shared_ptr<const TileGrid> next = combine_selection(d.selection, shape, mode, d.width, d.height);
  if (next == d.selection) return std::nullopt;
  return SelectionStep{SetSelection{std::move(next)}, std::move(name)};
}

namespace {

// A tool's shape at mouse up, as the options bar says.
std::optional<SelectionStep> tool(const DocState& d, TileGrid shape, const ShapeOptions& o, const char* name) {
  if (o.feather > 0) shape = feather_selection(shape, o.feather, d.width, d.height, false);
  if (!selects_anything(shape, d.width, d.height)) return o.mode == SelectMode::New ? deselect(d) : std::nullopt;
  return select_shape(d, std::make_shared<const TileGrid>(std::move(shape)), o.mode, name);
}
void check_options(const ShapeOptions& o) {
  if (!(o.feather >= 0 && o.feather <= kFeatherMax)) throw bad("a Feather outside 0 to 1,000 px");
  if (uint8_t(o.mode) > uint8_t(SelectMode::Intersect)) throw bad("no such mode");
}

}  // namespace

std::optional<SelectionStep> rectangular_marquee(const DocState& d, const SelectionBox& box, const ShapeOptions& o) {
  check_options(o);
  return tool(d, rect_selection(box, d.width, d.height, d.depth), o, kRectangularMarquee);
}

std::optional<SelectionStep> elliptical_marquee(const DocState& d, const SelectionBox& box, const ShapeOptions& o) {
  check_options(o);
  return tool(d, ellipse_selection(box, o.anti_alias, d.width, d.height, d.depth), o, kEllipticalMarquee);
}

std::optional<SelectionStep> lasso(const DocState& d, const std::vector<SelectionPoint>& points,
                                   const ShapeOptions& o) {
  check_options(o);
  return tool(d, polygon_selection(points, o.anti_alias, d.width, d.height, d.depth), o, kLasso);
}

std::optional<SelectionStep> polygonal_lasso(const DocState& d, const std::vector<SelectionPoint>& points,
                                             const ShapeOptions& o) {
  check_options(o);
  return tool(d, polygon_selection(points, o.anti_alias, d.width, d.height, d.depth), o, kPolygonalLasso);
}

SelectionStep select_all(const DocState& d) {
  return {SetSelection{all_selected(d.width, d.height, d.depth)}, kSelectAll};
}

std::optional<SelectionStep> deselect(const DocState& d) {
  if (!d.selection) return std::nullopt;
  return SelectionStep{SetSelection{}, kDeselect};
}

std::optional<SelectionStep> reselect(const DocState& d) {
  if (!d.reselect || d.reselect == d.selection) return std::nullopt;
  return SelectionStep{SetSelection{d.reselect}, kReselect};
}

std::optional<SelectionStep> select_inverse(const DocState& d) {
  if (!d.selection) return std::nullopt;
  return SelectionStep{SetSelection{inverse_selection(*d.selection, d.width, d.height)}, kSelectInverse};
}

std::optional<SelectionStep> move_selection(const DocState& d, int dx, int dy) {
  if (!d.selection || (dx == 0 && dy == 0)) return std::nullopt;
  return SelectionStep{SetSelection{moved_selection(*d.selection, dx, dy, d.width, d.height)}, kMoveSelection};
}

std::optional<SelectionStep> modify_feather(const DocState& d, double radius, bool at_canvas_bounds) {
  if (!(radius >= 0 && radius <= kFeatherMax)) throw bad("a Feather Radius outside 0 to 1,000 px");
  if (!d.selection || radius == 0) return std::nullopt;
  return SelectionStep{SetSelection{held(feather_selection(*d.selection, radius, d.width, d.height, at_canvas_bounds),
                                         d.width, d.height)},
                       kFeather};
}

PixelRect selection_bounds(const DocState& d) {
  return d.selection ? selection_bounds(*d.selection, d.width, d.height) : PixelRect{};
}

bool selection_visible(const DocState& d) { return d.selection && selection_visible(*d.selection, d.width, d.height); }

}  // namespace pb
