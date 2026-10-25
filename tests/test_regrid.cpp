// Exact geometry of tile grids (regrid.h, T30). Every placement (the eight symmetries, shifts of either sign, odd and by
// whole tiles, with and without a clip) against a reference that puts each pixel read from the grid where regrid.h says
// it goes, in every format, on grids holding every kind of tile; the tiles out within where pixels land, none that
// compact() would store otherwise. Shifts by whole tiles keep every tile; empty and solid surroundings come out without
// pixels allocated; four quarter turns and two flips give the grid back, the solid tiles of solid surroundings with
// their ids; rotate() and flip() against their formulas; crop() and Move Selection's cut; content bounds against a scan
// of every pixel on random grids, against a value within an area; what place() refuses.
#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>
#include <set>
#include <stdexcept>
#include <vector>

#include "check.h"
#include "regrid.h"
#include "tilecache.h"

using namespace pb;

namespace {

constexpr int S = kTileSize;
constexpr Format kFormats[] = {{Depth::U8, 1},  {Depth::U8, 2},  {Depth::U8, 4},
                               {Depth::U16, 1}, {Depth::U16, 2}, {Depth::U16, 4}};

void put(Format f, const Pixel& v, uint8_t* p) {
  for (int c = 0; c < f.channels; c++)
    if (f.depth == Depth::U16) {
      const uint16_t s = v[size_t(c)];
      std::memcpy(p + 2 * c, &s, 2);
    } else {
      p[c] = uint8_t(v[size_t(c)]);
    }
}
Pixel get(Format f, const uint8_t* p) {
  Pixel v{};
  for (int c = 0; c < f.channels; c++)
    if (f.depth == Depth::U16)
      std::memcpy(&v[size_t(c)], p + 2 * c, 2);
    else
      v[size_t(c)] = p[c];
  return v;
}
bool alike(Format f, const Pixel& a, const Pixel& b) {
  return a == b || (f.alpha() && a[size_t(f.channels - 1)] == 0 && b[size_t(f.channels - 1)] == 0);
}

// Pixels r of a grid's plane, rows packed: every one `fill` to begin with, unless null. reset() keeps the buffer: fresh
// pages cost more than the checks that fill them (WSL2).
struct Block {
  Format f;
  PixelRect r;
  std::vector<uint8_t> px;
  Block() = default;
  Block(Format f_, PixelRect r_, Pixel fill) { reset(f_, r_, &fill); }
  Block& reset(Format f_, PixelRect r_, const Pixel* fill) {
    f = f_, r = r_;
    px.resize(size_t(r.x1 - r.x0) * size_t(r.y1 - r.y0) * size_t(f.bytes()));
    if (!fill || px.empty()) return *this;
    put(f, *fill, px.data());
    for (size_t done = size_t(f.bytes()); done < px.size(); done *= 2)
      std::memcpy(px.data() + done, px.data(), std::min(done, px.size() - done));
    return *this;
  }
  size_t stride() const { return size_t(r.x1 - r.x0) * size_t(f.bytes()); }
  uint8_t* at(int64_t x, int64_t y) { return px.data() + size_t(y - r.y0) * stride() + size_t(x - r.x0) * size_t(f.bytes()); }
  const uint8_t* at(int64_t x, int64_t y) const {
    return px.data() + size_t(y - r.y0) * stride() + size_t(x - r.x0) * size_t(f.bytes());
  }
};
// The same pixels, transparent ones alike whatever their colour (as grids keep them: compact() drops a tile of them).
bool same(const Block& a, const Block& b) {
  if (a.f != b.f || a.r != b.r) return false;
  if (a.px == b.px) return true;
  const size_t bpp = size_t(a.f.bytes());
  for (size_t i = 0; i < a.px.size(); i += bpp)
    if (std::memcmp(&a.px[i], &b.px[i], bpp) != 0 && !alike(a.f, get(a.f, &a.px[i]), get(a.f, &b.px[i]))) return false;
  return true;
}
const Block& read(const TileGrid& g, PixelRect r, Block& into) {
  into.reset(g.format(), r, nullptr);
  g.read_rect(r.x0, r.y0, r.x1 - r.x0, r.y1 - r.y0, into.px.data(), into.stride());
  return into;
}
Block read(const TileGrid& g, PixelRect r) {
  Block b;
  read(g, r, b);
  return b;
}
PixelRect pixels(TileRect t) { return {t.x0 * S, t.y0 * S, t.x1 * S, t.y1 * S}; }
PixelRect grow(PixelRect r, int m) { return {r.x0 - m, r.y0 - m, r.x1 + m, r.y1 + m}; }
PixelRect meet(PixelRect a, PixelRect b) {
  return {std::max(a.x0, b.x0), std::max(a.y0, b.y0), std::min(a.x1, b.x1), std::min(a.y1, b.y1)};
}

// splitmix64: noise fast enough for whole tiles.
struct Noise {
  uint64_t s;
  uint64_t next() {
    uint64_t z = (s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
  }
  int below(int n) { return int(next() % uint64_t(n)); }
};

// Any pixel of f; with alpha, a quarter transparent (keeping a colour), a quarter opaque.
Pixel random_pixel(Format f, Noise& n) {
  Pixel v{};
  for (int c = 0; c < f.channels; c++) v[size_t(c)] = uint16_t(n.next() % uint64_t(max_value(f.depth) + 1));
  if (f.alpha()) {
    const int k = n.below(4);
    if (k == 0) v[size_t(f.channels - 1)] = 0;
    if (k == 1) v[size_t(f.channels - 1)] = uint16_t(max_value(f.depth));
  }
  return v;
}

// A data tile of f, as grids may hold one (not compacted), its pixels from `paint`.
template <class Paint>
TilePtr data_tile(Format f, Pixel fill, Paint paint) {
  const std::shared_ptr<Tile> t = Tile::make(f, fill);
  {
    const WritePin p = t->write();
    paint(p.data());
  }
  return t;
}

// A data tile whose pixels cannot be read back (its frame elsewhere is not zstd): pinning it throws. What a function
// that must not read a tile's pixels is given.
TilePtr unreadable(Format f) {
  static const uint8_t junk[32] = {7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7};
  return Tile::elsewhere(f, nullptr, junk, sizeof junk);
}
bool throws_reading(const std::function<void()>& run) {
  try {
    run();
  } catch (const std::runtime_error&) {
    return true;
  }
  return false;
}

// A grid over tiles r holding every kind of tile: empty; solid (some alike fill, a transparent one keeping a colour);
// data of noise, mostly fill with a few pixels and a run, all fill or all one value (neither compacted), a block of one
// value in fill.
TileGrid random_grid(Format f, Pixel fill, TileRect r, Noise& n) {
  TileGrid g(f, fill);
  const size_t bpp = size_t(f.bytes()), all = size_t(S) * S;
  for (int ty = r.y0; ty < r.y1; ty++)
    for (int tx = r.x0; tx < r.x1; tx++) {
      const Pixel v = random_pixel(f, n);
      switch (n.below(9)) {
        case 0:
          break;  // empty
        case 1:
          g.set(tx, ty, Tile::solid(f, v));
          break;
        case 2:
          g.set(tx, ty, Tile::solid(f, n.below(2) ? fill : v));
          break;
        case 3:
        case 4:
          g.set(tx, ty, data_tile(f, fill, [&](uint8_t* d) {
                  for (size_t i = 0; i < all * bpp; i += 8) {
                    const uint64_t w = n.next();
                    std::memcpy(d + i, &w, std::min<size_t>(8, all * bpp - i));
                  }
                }));
          break;
        case 5:
          g.set(tx, ty, data_tile(f, fill, [&](uint8_t* d) {
                  for (int k = 0; k < 6; k++) put(f, random_pixel(f, n), d + size_t(n.below(int(all))) * bpp);
                  const size_t at = size_t(n.below(int(all) - 40));
                  for (size_t i = 0; i < 40; i++) put(f, v, d + (at + i) * bpp);
                }));
          break;
        case 6:
          g.set(tx, ty, data_tile(f, fill, [](uint8_t*) {}));  // all fill
          break;
        case 7:
          g.set(tx, ty, data_tile(f, fill, [&](uint8_t* d) {
                  for (size_t i = 0; i < all; i++) put(f, v, d + i * bpp);
                }));
          break;
        default:
          g.set(tx, ty, data_tile(f, fill, [&](uint8_t* d) {
                  const int x0 = n.below(S), y0 = n.below(S), x1 = x0 + 1 + n.below(S - x0), y1 = y0 + 1 + n.below(S - y0);
                  for (int y = y0; y < y1; y++)
                    for (int x = x0; x < x1; x++) put(f, v, d + (size_t(y) * S + size_t(x)) * bpp);
                }));
      }
    }
  return g;
}

// Where p puts pixel (x, y), as regrid.h has it.
void forward(const Placement& p, int64_t x, int64_t y, int64_t& X, int64_t& Y) {
  int64_t u = p.transpose ? y : x, v = p.transpose ? x : y;
  if (p.mirror_x) u = -1 - u;
  if (p.mirror_y) v = -1 - v;
  X = u + p.dx, Y = v + p.dy;
}
// Where p puts the pixels of r: its corners'.
PixelRect forward(const Placement& p, PixelRect r) {
  int64_t ax, ay, bx, by;
  forward(p, r.x0, r.y0, ax, ay);
  forward(p, r.x1 - 1, r.y1 - 1, bx, by);
  return {int(std::min(ax, bx)), int(std::min(ay, by)), int(std::max(ax, bx) + 1), int(std::max(ay, by) + 1)};
}

// place(g, p, clip) against every pixel of g's tiles put where p says: over where they land and a margin round it, and
// its tiles all over where pixels land, none past the clip, none (but g's own) that compact() would store otherwise.
void check_place(const TileGrid& g, const Placement& p, const std::optional<PixelRect>& clip) {
  const TileGrid out = place(g, p, clip);
  CHECK(out.format() == g.format() && out.fill() == g.fill());
  if (g.tiles().empty()) {
    CHECK(out.tiles().empty());
    return;
  }
  const Format f = g.format();
  const PixelRect src = pixels(g.bounds()), to = forward(p, src);
  CHECK(place(src, p) == to);
  const PixelRect win = grow(to, 37);
  static Block want, in, got;
  const Pixel fill = g.fill();
  want.reset(f, win, &fill);
  read(g, src, in);
  const size_t bpp = size_t(f.bytes());
  for (int y = src.y0; y < src.y1; y++) {
    // Along a row in, where p puts its pixels moves by (sx, sy) a pixel.
    int64_t X, Y, X1, Y1;
    forward(p, src.x0, y, X, Y);
    forward(p, src.x0 + 1, y, X1, Y1);
    const int64_t sx = X1 - X, sy = Y1 - Y;
    const uint8_t* from = in.at(src.x0, y);
    for (int x = src.x0; x < src.x1; x++, from += bpp, X += sx, Y += sy)
      if (!clip || (X >= clip->x0 && X < clip->x1 && Y >= clip->y0 && Y < clip->y1)) {
        uint8_t* dst = want.at(X, Y);
        for (size_t b = 0; b < bpp; b++) dst[b] = from[b];
      }
  }
  read(out, win, got);
  CHECK(same(got, want));
  std::set<const Tile*> own;
  for (const auto& [xy, t] : g.tiles()) own.insert(t.get());
  const PixelRect lands = clip ? meet(to, *clip) : to;
  for (const auto& [xy, t] : out.tiles()) {
    const PixelRect r{xy.x * S, xy.y * S, (xy.x + 1) * S, (xy.y + 1) * S};
    CHECK(!meet(r, lands).empty());
    if (!own.count(t.get())) CHECK(compact(t, g.fill()) == t);
  }
}

// The fills a grid of f may have: transparent with alpha, else black and white; an opaque colour too for 8-bit RGBA.
std::vector<Pixel> fills(Format f) {
  const uint16_t m = uint16_t(max_value(f.depth));
  if (f.channels == 1) return {Pixel{0}, Pixel{m}};
  if (f.channels == 2 || f.depth == Depth::U16) return {Pixel{}};
  return {Pixel{}, Pixel{uint16_t(m / 5), uint16_t(m / 2), m, m}};
}

void check_placements() {
  Noise n{1};
  for (const Format f : kFormats)
    for (const Pixel& fill : fills(f)) {
      const TileGrid g = random_grid(f, fill, {-1, -1, 1, 1}, n);
      const auto offset = [&](int range) { return n.below(2 * range + 1) - range; };
      // Shifts: the task's, by one, odd past several tiles, either sign; by whole tiles; none.
      for (const auto& d : std::vector<std::pair<int, int>>{{17, -33}, {-1, 1}, {1, 0}, {0, -1}, {-300, 515}, {3 * S + 5, -2 * S - 1}})
        check_place(g, Placement{false, false, false, d.first, d.second}, std::nullopt);
      check_place(g, Placement{false, false, false, offset(900), offset(900)}, std::nullopt);
      check_place(g, Placement{false, false, false, 2 * S, -S}, std::nullopt);
      check_place(g, Placement{}, std::nullopt);
      // The eight symmetries, by odd offsets and by whole tiles.
      for (int k = 0; k < 8; k++) {
        const bool t = k & 1, mx = k & 2, my = k & 4;
        check_place(g, Placement{t, mx, my, offset(700), offset(700)}, std::nullopt);
        check_place(g, Placement{t, mx, my, S * (n.below(5) - 2), S * (n.below(5) - 2)}, std::nullopt);
      }
      // Clips: across tiles, within one, past everything, empty; by whole tiles.
      const PixelRect clips[] = {{-100, -77, 300, 260}, {10, 20, 30, 25}, {-5000, -5000, 5000, 5000}, {7, 7, 7, 300},
                                 {-S, 0, S, 2 * S}};
      for (const PixelRect& c : clips) {
        check_place(g, Placement{false, false, false, offset(400), offset(400)}, c);
        check_place(g, Placement{n.below(2) == 1, n.below(2) == 1, n.below(2) == 1, offset(400), offset(400)}, c);
      }
    }
}

// Shifts by whole tiles keep every tile under its new key; so does a clip they lie within whole. An odd shift of a
// solid block (Select All of a 5000 x 2500 canvas) allocates the tiles along its edge only, the others solid ones of
// the block.
void check_whole_tiles() {
  Noise n{2};
  for (const Format f : kFormats) {
    const TileGrid g = random_grid(f, {}, {-2, -1, 3, 2}, n);
    for (const auto& d : std::vector<std::pair<int, int>>{{2, -3}, {-1, 0}, {0, 0}, {-40, 70}}) {
      const TileGrid out = shift(g, d.first * S, d.second * S);
      CHECK(out.tiles().size() == g.tiles().size());
      for (const auto& [xy, t] : g.tiles()) {
        const Tile* o = out.get(xy.x + d.first, xy.y + d.second);
        CHECK(o == t.get() && o->id() == t->id());
      }
    }
    const TileGrid c = place(g, Placement{false, false, false, S, 0}, PixelRect{-S, -S, 3 * S, 2 * S});
    for (const auto& [xy, t] : g.tiles())
      if (xy.x >= -2 && xy.x < 2 && xy.y >= -1 && xy.y < 2) CHECK(c.get(xy.x + 1, xy.y) == t.get());
    // Without reading a pixel: tiles that cannot be read move all the same; an odd shift does read them.
    TileGrid bad(f);
    for (int ty = 0; ty < 2; ty++)
      for (int tx = 0; tx < 3; tx++) bad.set(tx, ty, unreadable(f));
    const TileGrid moved = shift(bad, -S, 3 * S);
    for (const auto& [xy, t] : bad.tiles()) CHECK(moved.get(xy.x - 1, xy.y + 3) == t.get());
    CHECK(throws_reading([&] { shift(bad, 1, 0); }));
  }
  const Format f{Depth::U16, 1};
  TileGrid all(f);
  const TilePtr one = Tile::solid(f, Pixel{65535});
  for (int ty = 0; ty < 10; ty++)
    for (int tx = 0; tx < 20; tx++) all.set(tx, ty, one);
  std::set<const Tile*> block;
  for (const auto& [xy, t] : all.tiles()) block.insert(t.get());
  const size_t raw = tile_stats().raw;
  const TileGrid moved = shift(all, 17, -33);
  size_t data = 0, solid = 0;
  for (const auto& [xy, t] : moved.tiles()) {
    const bool edge = xy.x == 0 || xy.x == 20 || xy.y == -1 || xy.y == 9;
    CHECK(t->is_solid() != edge);
    if (t->is_solid()) CHECK(block.count(t.get()) && t->value() == Pixel{65535});
    (t->is_solid() ? solid : data)++;
  }
  CHECK(solid == 19 * 9 && data == 21 * 11 - 19 * 9);
  CHECK(tile_stats().raw == raw + data * size_t(S) * S * 2);
  CHECK(read(moved, {0, -50, 40, -20}).px == read(all, {-17, -17, 23, 13}).px);  // one channel
}

// The same pixels in two grids of a format and fill: tile by tile, skipping tiles they share.
bool same_grids(const TileGrid& a, const TileGrid& b) {
  std::set<std::pair<int, int>> at;
  for (const auto& [xy, t] : a.tiles()) at.insert({xy.y, xy.x});
  for (const auto& [xy, t] : b.tiles()) at.insert({xy.y, xy.x});
  for (const auto& [y, x] : at) {
    if (a.get(x, y) == b.get(x, y)) continue;
    const PixelRect r{x * S, y * S, (x + 1) * S, (y + 1) * S};
    static Block ta, tb;
    if (!same(read(a, r, ta), read(b, r, tb))) return false;
  }
  return true;
}

// Four quarter turns and two flips each way of a canvas that is no whole number of tiles give the grid back: the same
// pixels; the solid tiles amid a solid block (every tile within two of it alike) the same tiles; the turns and flips
// against their formulas. In two formats also canvases of whole tiles and of half tiles (ties between the tiles a tile
// out lies over), two half turns, three quarters and back, and turning by -1 or 5 as by 3 or 1.
void check_turns() {
  Noise n{3};
  for (const Format f : kFormats)
    for (const auto& canvas : std::vector<std::pair<int, int>>{{1000, 700}, {4 * S, 3 * S}, {S + S / 2, 2 * S + S / 2}}) {
      const bool more = f == Format{Depth::U8, 1} || f == Format{Depth::U16, 2};
      if (canvas.first != 1000 && !more) continue;
      const int w = canvas.first, h = canvas.second;
      TileGrid g = random_grid(f, {}, {-1, -2, 7, 5}, n);
      Pixel v = random_pixel(f, n);
      if (alike(f, v, Pixel{})) v[size_t(f.channels - 1)] = 1;  // not fill
      for (int ty = -1; ty < 4; ty++)
        for (int tx = 0; tx < 6; tx++) g.set(tx, ty, Tile::solid(f, v));
      const auto amid = [&](const TileXY& xy) {  // solid alike v within two tiles
        for (int y = xy.y - 2; y <= xy.y + 2; y++)
          for (int x = xy.x - 2; x <= xy.x + 2; x++) {
            const Tile* t = g.get(x, y);
            if (!t || !t->is_solid() || !alike(f, t->value(), v)) return false;
          }
        return true;
      };
      const auto given_back = [&](const TileGrid& back) {
        CHECK(same_grids(back, g));
        int kept = 0;
        for (const auto& [xy, t] : g.tiles())
          if (amid(xy)) {
            CHECK(back.get(xy.x, xy.y) == t.get());
            kept++;
          }
        CHECK(kept >= 2);
      };
      TileGrid r = g;
      for (int i = 0; i < 4; i++) r = rotate(r, 1, i % 2 ? h : w, i % 2 ? w : h);
      given_back(r);
      given_back(flip(flip(g, Mirror::Horizontal, w, h), Mirror::Horizontal, w, h));
      given_back(flip(flip(g, Mirror::Vertical, w, h), Mirror::Vertical, w, h));
      // The formulas, at pixels on the canvas and past it.
      const TileGrid q1 = rotate(g, 1, w, h), q2 = rotate(g, 2, w, h), q3 = rotate(g, 3, w, h);
      const TileGrid fh = flip(g, Mirror::Horizontal, w, h), fv = flip(g, Mirror::Vertical, w, h);
      const PixelRect all = grow(pixels(g.bounds()), 3);
      for (int k = 0; k < 400; k++) {
        const int x = all.x0 + n.below(all.x1 - all.x0), y = all.y0 + n.below(all.y1 - all.y0);
        const Pixel p = get(f, read(g, {x, y, x + 1, y + 1}).px.data());
        const auto at = [&](const TileGrid& t, int X, int Y) { return get(f, read(t, {X, Y, X + 1, Y + 1}).px.data()); };
        CHECK(alike(f, at(q1, h - 1 - y, x), p));
        CHECK(alike(f, at(q2, w - 1 - x, h - 1 - y), p));
        CHECK(alike(f, at(q3, y, w - 1 - x), p));
        CHECK(alike(f, at(fh, w - 1 - x, y), p));
        CHECK(alike(f, at(fv, x, h - 1 - y), p));
      }
      if (!more) continue;
      given_back(rotate(q2, 2, w, h));
      given_back(rotate(q3, -3, h, w));
      CHECK(same_grids(rotate(g, -1, w, h), q3) && same_grids(rotate(g, 5, w, h), q1));
    }
}

// crop(): the pixels within r from (0, 0), fill elsewhere, no tile past its size; by whole tiles the tiles within it
// kept. Move Selection's cut: a selection moved and cut to the canvas holds no tile wholly past it (A40).
void check_crop() {
  Noise n{4};
  for (const Format f : kFormats) {
    const TileGrid g = random_grid(f, fills(f).back(), {-2, -1, 2, 2}, n);
    for (const PixelRect& r : std::vector<PixelRect>{{-300, -100, 200, 333}, {-2 * S, -S, 0, S}, {5, 6, 7, 8},
                                                     {-1000, -1000, 1000, 1000}, {3, 3, 3, 3}}) {
      const TileGrid c = crop(g, r);
      const int w = r.x1 - r.x0, h = r.y1 - r.y0;
      Block want(f, grow({0, 0, w, h}, 20), g.fill());
      if (!r.empty()) {
        const Block in = read(g, r);
        for (int y = 0; y < h; y++) std::memcpy(want.at(0, y), in.at(r.x0, r.y0 + y), size_t(w) * size_t(f.bytes()));
      }
      CHECK(same(read(c, want.r), want));
      for (const auto& [xy, t] : c.tiles()) CHECK(xy.x >= 0 && xy.y >= 0 && xy.x * S < w && xy.y * S < h);
    }
    const TileGrid whole = crop(g, {-S, -S, S, 2 * S});
    for (const auto& [xy, t] : g.tiles())
      if (xy.x >= -1 && xy.x < 1 && xy.y >= -1 && xy.y < 2) CHECK(whole.get(xy.x + 1, xy.y + 1) == t.get());
  }
  // A selection of a 900 x 600 canvas moved by (-130, 250) and cut to it.
  const Format f{Depth::U8, 1};
  const int W = 900, H = 600, dx = -130, dy = 250;
  TileGrid sel = random_grid(f, {}, {0, 0, 4, 3}, n);
  sel = crop(sel, {0, 0, W, H});
  const TileGrid moved = crop(sel, {-dx, -dy, W - dx, H - dy});
  for (const auto& [xy, t] : moved.tiles()) CHECK(xy.x >= 0 && xy.y >= 0 && xy.x * S < W && xy.y * S < H);
  Block want(f, {-50, -50, W + 50, H + 50}, Pixel{});
  const Block in = read(sel, {0, 0, W, H});
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++)
      if (x + dx >= 0 && x + dx < W && y + dy >= 0 && y + dy < H) *want.at(x + dx, y + dy) = *in.at(x, y);
  CHECK(read(moved, want.r).px == want.px);  // one channel
}

// The pixels within area not alike value, scanned one by one: their bounds.
PixelRect scanned(const TileGrid& g, Pixel value, PixelRect area) {
  const Format f = g.format();
  if (area.empty()) return {};
  for (int c = 0; c < 4; c++) value[size_t(c)] = c < f.channels ? std::min<uint16_t>(value[size_t(c)], uint16_t(max_value(f.depth))) : 0;
  static Block buffer;
  const Block& b = read(g, area, buffer);
  uint8_t want[8];
  put(f, value, want);
  const size_t bpp = size_t(f.bytes());
  const bool clear = f.alpha() && value[size_t(f.channels - 1)] == 0;  // then any pixel with alpha counts
  PixelRect r{INT_MAX, INT_MAX, INT_MIN, INT_MIN};
  for (int y = area.y0; y < area.y1; y++) {
    const uint8_t* p = b.at(area.x0, y);
    for (int x = area.x0; x < area.x1; x++, p += bpp)
      if (clear ? (p[bpp - 1] | p[bpp - (f.depth == Depth::U16 ? 2 : 1)]) != 0 : std::memcmp(p, want, bpp) != 0)
        r = {std::min(r.x0, x), std::min(r.y0, y), std::max(r.x1, x + 1), std::max(r.y1, y + 1)};
  }
  return r.x0 == INT_MAX ? PixelRect{} : r;
}

// content_bounds() on random grids, sparse ones and ones whose outer tiles hold nothing, against a scan of every pixel;
// against a value within an area, empty tiles counting where fill() is not alike it.
void check_bounds() {
  Noise n{5};
  for (const Format f : kFormats)
    for (const Pixel& fill : fills(f)) {
      CHECK(content_bounds(TileGrid(f, fill)) == PixelRect{});
      for (int k = 0; k < 6; k++) {
        TileGrid g = random_grid(f, fill, {-1 - n.below(2), -1 - n.below(2), 1 + n.below(2), 1 + n.below(2)}, n);
        if (k == 1) {  // nothing but fill: solid alike it, data all fill, transparent pixels of any colour
          TileGrid h(f, fill);
          h.set(0, 0, Tile::solid(f, fill));
          h.set(1, 0, data_tile(f, fill, [](uint8_t*) {}));
          if (f.alpha() && fill[size_t(f.channels - 1)] == 0) h.set(-1, 2, Tile::solid(f, Pixel{7}));  // alpha 0
          g = h;
          CHECK(content_bounds(g) == PixelRect{});
        }
        if (k == 2) {  // a few pixels deep inside tiles holding nothing else, past outer tiles holding nothing
          TileGrid h(f, fill);
          for (int ty = -2; ty < 3; ty++)
            for (int tx = -2; tx < 3; tx++) h.set(tx, ty, data_tile(f, fill, [](uint8_t*) {}));
          const Pixel v = fill[size_t(f.channels - 1)] ? Pixel{} : Pixel{1, 1, 1, 1};
          for (int i = 0; i < 3; i++) {
            const int x = -S + n.below(3 * S), y = -S + n.below(3 * S);
            Block px(f, {x, y, x + 1, y + 1}, v);
            h.write_rect(x, y, 1, 1, px.px.data(), px.stride());
          }
          g = h;
        }
        const PixelRect around = grow(pixels(g.bounds()), 5);
        CHECK(content_bounds(g) == scanned(g, fill, around));
        if (k == 3) {  // only the outer tiles read: the inner ones cannot be
          TileGrid h(f, fill);
          for (int ty = 0; ty < 4; ty++)
            for (int tx = 0; tx < 5; tx++)
              h.set(tx, ty, tx % 4 && ty % 3 ? unreadable(f) : data_tile(f, fill, [&](uint8_t* d) {
                for (size_t i = 0; i < size_t(S) * S * size_t(f.bytes()); i += 8) {
                  const uint64_t w = n.next() | 0x0101010101010101ull;  // no sample 0: none transparent
                  std::memcpy(d + i, &w, std::min<size_t>(8, size_t(S) * S * size_t(f.bytes()) - i));
                }
              }));
          const Pixel opaque = fill[size_t(f.channels - 1)] ? Pixel{} : Pixel{9, 9, 9, uint16_t(max_value(f.depth))};
          h.set(0, 1, Tile::solid(f, opaque));
          CHECK(content_bounds(h) == (PixelRect{0, 0, 5 * S, 4 * S}));
          CHECK(throws_reading([&] { content_bounds(h, fill, {S, S, 4 * S, 3 * S}); }));
        }
        // Against another value, within an area cutting across tiles.
        const PixelRect area{around.x0 + n.below(400) - 100, around.y0 + n.below(400) - 100, around.x1 - n.below(400),
                             around.y1 - n.below(400)};
        const Pixel values[] = {fill, random_pixel(f, n),
                                g.tiles().empty() ? fill : g.tiles().begin()->second->value()};
        for (const Pixel& v : values) CHECK(content_bounds(g, v, area) == scanned(g, v, area));
      }
    }
}

// What place() refuses: pixels past int's range, a rectangle moved past it.
void check_range() {
  const Format f{Depth::U8, 4};
  TileGrid g(f);
  g.set(INT_MAX >> kTileLog2, 0, Tile::solid(f, Pixel{1, 2, 3, 4}));
  g.set(0, INT_MIN >> kTileLog2, Tile::solid(f, Pixel{1, 2, 3, 4}));
  CHECK(shift(g, 0, 0).tiles().size() == 2);
  bool threw = false;
  try {
    shift(g, S, 0);
  } catch (const std::out_of_range&) {
    threw = true;
  }
  CHECK(threw);
  threw = false;
  try {
    shift(g, 1, 0);
  } catch (const std::out_of_range&) {
    threw = true;
  }
  CHECK(threw);
  threw = false;
  try {
    place(PixelRect{0, 0, 10, 10}, Placement{false, false, false, INT_MAX - 5, 0});
  } catch (const std::out_of_range&) {
    threw = true;
  }
  CHECK(threw);
  CHECK(place(PixelRect{}, Placement{false, true, false, 5, 5}) == PixelRect{});
  CHECK(place(PixelRect{0, 0, 10, 20}, rotation(1, 100, 50)) == (PixelRect{30, 0, 50, 10}));
}

}  // namespace

TEST(test_regrid_place) { check_placements(); }
TEST(test_regrid_whole_tiles) { check_whole_tiles(); }
TEST(test_regrid_turns) { check_turns(); }
TEST(test_regrid_crop) { check_crop(); }
TEST(test_content_bounds) { check_bounds(); }
TEST(test_regrid_range) { check_range(); }
