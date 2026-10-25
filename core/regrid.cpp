#include "regrid.h"

#include <algorithm>
#include <climits>
#include <cstring>
#include <map>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include "jobs.h"
#include "profile.h"

namespace pb {
namespace {

constexpr int S = kTileSize;
// The tiles whose pixels int holds.
constexpr int64_t kLowTile = int64_t(INT_MIN) >> kTileLog2, kHighTile = int64_t(INT_MAX) >> kTileLog2;

// Entries past the channels 0 and samples within the depth, as tile.cpp keeps pixels, so that they compare with ==.
Pixel normal(Format f, Pixel v) {
  for (int c = 0; c < 4; c++) v[size_t(c)] = c < f.channels ? std::min(v[size_t(c)], uint16_t(max_value(f.depth))) : 0;
  return v;
}
bool transparent(Format f, const Pixel& v) { return f.alpha() && v[size_t(f.channels - 1)] == 0; }
bool alike(Format f, const Pixel& a, const Pixel& b) { return a == b || (transparent(f, a) && transparent(f, b)); }

void store(Format f, const Pixel& v, uint8_t* dst) {
  if (f.depth == Depth::U16) return void(std::memcpy(dst, v.data(), size_t(f.channels) * 2));
  for (int c = 0; c < f.channels; c++) dst[c] = uint8_t(v[size_t(c)]);
}
// n >= 1 pixels of v from dst on: one stored, then doubled.
void repeat(Format f, const Pixel& v, uint8_t* dst, size_t n) {
  store(f, v, dst);
  const size_t total = n * size_t(f.bytes());
  for (size_t done = size_t(f.bytes()); done < total; done *= 2) std::memcpy(dst + done, dst, std::min(done, total - done));
}

// Pixels or tiles x0 <= x < x1, y0 <= y < y1, wide enough for any int plus or minus another.
struct Box {
  int64_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  bool empty() const { return x0 >= x1 || y0 >= y1; }
};
bool operator==(const Box& a, const Box& b) { return a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1; }
Box meet(const Box& a, const Box& b) {
  return {std::max(a.x0, b.x0), std::max(a.y0, b.y0), std::min(a.x1, b.x1), std::min(a.y1, b.y1)};
}
Box pixels_of(int64_t tx, int64_t ty) { return {tx * S, ty * S, (tx + 1) * S, (ty + 1) * S}; }
Box tiles_of(const Box& px) {  // the tiles over pixels px, not empty
  return {px.x0 >> kTileLog2, px.y0 >> kTileLog2, ((px.x1 - 1) >> kTileLog2) + 1, ((px.y1 - 1) >> kTileLog2) + 1};
}
Box box(const PixelRect& r) { return {r.x0, r.y0, r.x1, r.y1}; }

// A placement in 64 bits (crop()'s shift may pass int's range on its way to the clip), both ways.
struct Map {
  bool t = false, mx = false, my = false;
  int64_t dx = 0, dy = 0;
  bool shift_only() const { return !t && !mx && !my; }
  // Where the pixels of b go.
  Box forward(const Box& b) const {
    int64_t u0 = t ? b.y0 : b.x0, u1 = t ? b.y1 : b.x1, v0 = t ? b.x0 : b.y0, v1 = t ? b.x1 : b.y1;
    if (mx) std::swap(u0, u1), u0 = -u0, u1 = -u1;  // pixels a to b - 1 to -b to -a - 1
    if (my) std::swap(v0, v1), v0 = -v0, v1 = -v1;
    return {u0 + dx, v0 + dy, u1 + dx, v1 + dy};
  }
  // The pixels that go to b.
  Box inverse(const Box& b) const {
    int64_t u0 = b.x0 - dx, u1 = b.x1 - dx, v0 = b.y0 - dy, v1 = b.y1 - dy;
    if (mx) std::swap(u0, u1), u0 = -u0, u1 = -u1;
    if (my) std::swap(v0, v1), v0 = -v0, v1 = -v1;
    return t ? Box{v0, u0, v1, u1} : Box{u0, v0, u1, v1};
  }
  // The pixel that goes to (x, y).
  void inverse(int64_t x, int64_t y, int64_t& sx, int64_t& sy) const {
    int64_t u = x - dx, v = y - dy;
    if (mx) u = -1 - u;
    if (my) v = -1 - v;
    sx = t ? v : u, sy = t ? u : v;
  }
};
Map map_of(const Placement& p) { return {p.transpose, p.mirror_x, p.mirror_y, p.dx, p.dy}; }

void check_range(const Box& tiles) {
  if (tiles.x0 < kLowTile || tiles.y0 < kLowTile || tiles.x1 - 1 > kHighTile || tiles.y1 - 1 > kHighTile)
    throw std::out_of_range("pb: place: pixels past int's range");
}

// n pixels of B bytes into dst, packed, from src on, `step` bytes apart.
template <size_t B>
void gather(const uint8_t* src, ptrdiff_t step, uint8_t* dst, size_t n) {
  if (step == ptrdiff_t(B)) return void(std::memcpy(dst, src, n * B));
  for (size_t i = 0; i < n; i++) std::memcpy(dst + i * B, src + ptrdiff_t(i) * step, B);
}
void gather(size_t bpp, const uint8_t* src, ptrdiff_t step, uint8_t* dst, size_t n) {
  switch (bpp) {
    case 1: return gather<1>(src, step, dst, n);
    case 2: return gather<2>(src, step, dst, n);
    case 4: return gather<4>(src, step, dst, n);
    default: return gather<8>(src, step, dst, n);
  }
}

// Tile (tx, ty) of g moved by m: its pixels within w (those landing within the clip; all of them when `whole`).
TilePtr tile_out(const TileGrid& g, const Map& m, int64_t tx, int64_t ty, const Box& w, bool whole) {
  const Format f = g.format();
  const Pixel fill = g.fill();
  const Box src = m.inverse(w);     // the pixels it gets: kTileSize square when whole
  const Box under = tiles_of(src);  // the tiles they lie on, 1 or 2 along each axis
  if (whole && m.shift_only() && under.x1 - under.x0 == 1 && under.y1 - under.y0 == 1) {  // a tile whole, shifted
    const auto it = g.tiles().find(TileXY{int(under.x0), int(under.y0)});
    return it == g.tiles().end() ? nullptr : it->second;
  }
  TilePtr at[2][2];                 // [y][x]; null where empty
  bool data = false, uniform = true, first = true;
  Pixel v = fill;  // of the solid and empty ones, when all alike
  for (int64_t y = under.y0; y < under.y1; y++)
    for (int64_t x = under.x0; x < under.x1; x++) {
      const auto it = g.tiles().find(TileXY{int(x), int(y)});
      const TilePtr t = it == g.tiles().end() ? nullptr : it->second;
      at[y - under.y0][x - under.x0] = t;
      if (t && !t->is_solid()) {
        data = true;
        continue;
      }
      const Pixel tv = t ? t->value() : fill;
      if (first)
        v = tv, first = false;
      else if (!alike(f, v, tv))
        uniform = false;
    }
  if (!data && uniform) {
    if (alike(f, v, fill)) return nullptr;
    // Every tile under it is solid alike v: the one it lies most over, the first along an axis on a tie. Along an axis
    // that is the one a quarter turn or flip back picks again, so that turns and flips undone give the tile back.
    if (whole) {
      const size_t i = under.x1 - under.x0 == 2 && (under.x0 + 1) * S - src.x0 < src.x1 - (under.x0 + 1) * S;
      const size_t j = under.y1 - under.y0 == 2 && (under.y0 + 1) * S - src.y0 < src.y1 - (under.y0 + 1) * S;
      return at[j][i];
    }
  }
  PROFILE_ZONE("regrid_tile");
  const std::shared_ptr<Tile> out = Tile::make(f, fill);
  {
    Pin pins[2][2];
    const uint8_t* px[2][2] = {};  // the data tiles' pixels
    for (int j = 0; j < 2; j++)
      for (int i = 0; i < 2; i++)
        if (at[j][i] && !at[j][i]->is_solid()) pins[j][i] = at[j][i]->pin(), px[j][i] = pins[j][i].data();
    const WritePin p = out->write();
    const size_t bpp = size_t(f.bytes()), row = size_t(S) * bpp;
    // Along a row out, the pixel read moves by (ux, uy), `step` bytes within a tile.
    const int64_t sign = m.mx ? -1 : 1, ux = m.t ? 0 : sign, uy = m.t ? sign : 0;
    const ptrdiff_t step = sign * ptrdiff_t(m.t ? row : bpp);
    for (int64_t y = w.y0; y < w.y1; y++) {
      uint8_t* dst = p.data() + size_t(y - ty * S) * row + size_t(w.x0 - tx * S) * bpp;
      int64_t sx, sy;
      m.inverse(w.x0, y, sx, sy);
      for (int64_t n = w.x1 - w.x0; n > 0;) {
        const int64_t cx = sx >> kTileLog2, cy = sy >> kTileLog2;
        // The pixels of the row this tile gives before the next one.
        int64_t k = ux > 0 ? (cx + 1) * S - sx : ux < 0 ? sx - cx * S + 1 : uy > 0 ? (cy + 1) * S - sy : sy - cy * S + 1;
        k = std::min(k, n);
        const size_t j = size_t(cy - under.y0), i = size_t(cx - under.x0);
        if (px[j][i])
          gather(bpp, px[j][i] + (size_t(sy - cy * S) * S + size_t(sx - cx * S)) * bpp, step, dst, size_t(k));
        else if (at[j][i])
          repeat(f, at[j][i]->value(), dst, size_t(k));
        // else empty: the tile's own fill
        dst += size_t(k) * bpp, sx += ux * k, sy += uy * k, n -= k;
      }
    }
  }
  return compact(out, fill);
}

// g moved by m, only the pixels landing within *clip when given.
TileGrid apply(const TileGrid& g, const Map& m, const Box* clip) {
  PROFILE_ZONE("regrid");
  TileGrid out(g.format(), g.fill());
  if (g.tiles().empty() || (clip && clip->empty())) return out;
  if (!clip && m.shift_only() && m.dx % S == 0 && m.dy % S == 0) {  // by whole tiles: the same tiles, new keys
    const int64_t ox = m.dx / S, oy = m.dy / S;
    const TileRect b = g.bounds();
    check_range({b.x0 + ox, b.y0 + oy, b.x1 + ox, b.y1 + oy});
    for (const auto& [xy, t] : g.tiles()) out.set(int(xy.x + ox), int(xy.y + oy), t);
    return out;
  }
  // The tiles out: under where the tiles in go.
  std::vector<TileXY> todo;
  for (const auto& [xy, t] : g.tiles()) {
    Box to = m.forward(pixels_of(xy.x, xy.y));
    if (clip) to = meet(to, *clip);
    if (to.empty()) continue;
    const Box tiles = tiles_of(to);
    check_range(tiles);
    for (int64_t y = tiles.y0; y < tiles.y1; y++)
      for (int64_t x = tiles.x0; x < tiles.x1; x++) todo.push_back({int(x), int(y)});
  }
  std::sort(todo.begin(), todo.end());
  todo.erase(std::unique(todo.begin(), todo.end()), todo.end());
  std::vector<TilePtr> made(todo.size());
  parallel_for(int(todo.size()), [&](int begin, int end) {
    for (int i = begin; i < end; i++) {
      const TileXY xy = todo[size_t(i)];
      const Box tile = pixels_of(xy.x, xy.y), w = clip ? meet(tile, *clip) : tile;
      made[size_t(i)] = tile_out(g, m, xy.x, xy.y, w, w == tile);
    }
  });
  for (size_t i = 0; i < todo.size(); i++)
    if (made[i]) out.set(todo[i].x, todo[i].y, std::move(made[i]));
  return out;
}

// --- Content bounds.

// Whether a pixel counts against a value, its bytes read as a word: (word ^ want) & mask is not 0. Against a
// transparent value only the alpha's bytes count; against any other, all of them.
struct Counts {
  uint64_t want = 0, mask = 0;
};
Counts counts_against(Format f, const Pixel& value) {
  uint8_t want[8] = {}, mask[8] = {};
  const size_t bpp = size_t(f.bytes());
  if (transparent(f, value)) {
    const size_t a = f.depth == Depth::U16 ? 2 : 1;  // alpha's bytes, a pixel's last
    std::memset(mask + bpp - a, 0xff, a);
  } else {
    store(f, value, want);
    std::memset(mask, 0xff, bpp);
  }
  Counts c;
  std::memcpy(&c.want, want, 8);
  std::memcpy(&c.mask, mask, 8);
  return c;
}

enum class Side { Left, Right, Top, Bottom };
bool leading(Side s) { return s == Side::Left || s == Side::Top; }  // towards lower coordinates
bool columns(Side s) { return s == Side::Left || s == Side::Right; }

// Of a data tile's pixels d within x0 <= x < x1, y0 <= y < y1 (its own coordinates), the outermost column (Left,
// Right) or row (Top, Bottom) on side s holding a pixel that counts; -1 when none does. Each row scanned from that side
// no further than the best column found yet.
template <size_t B>
int scan(const uint8_t* d, Side s, int x0, int y0, int x1, int y1, const Counts& c) {
  const auto counts = [&](int x, int y) {
    uint64_t v = 0;
    std::memcpy(&v, d + (size_t(y) * S + size_t(x)) * B, B);
    return ((v ^ c.want) & c.mask) != 0;
  };
  switch (s) {
    case Side::Left: {
      int best = x1;
      for (int y = y0; y < y1 && best > x0; y++)
        for (int x = x0; x < best; x++)
          if (counts(x, y)) {
            best = x;
            break;
          }
      return best < x1 ? best : -1;
    }
    case Side::Right: {
      int best = x0 - 1;
      for (int y = y0; y < y1 && best < x1 - 1; y++)
        for (int x = x1 - 1; x > best; x--)
          if (counts(x, y)) {
            best = x;
            break;
          }
      return best >= x0 ? best : -1;
    }
    case Side::Top:
      for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
          if (counts(x, y)) return y;
      return -1;
    case Side::Bottom:
      for (int y = y1 - 1; y >= y0; y--)
        for (int x = x0; x < x1; x++)
          if (counts(x, y)) return y;
      return -1;
  }
  return -1;
}
int scan(size_t bpp, const uint8_t* d, Side s, int x0, int y0, int x1, int y1, const Counts& c) {
  switch (bpp) {
    case 1: return scan<1>(d, s, x0, y0, x1, y1, c);
    case 2: return scan<2>(d, s, x0, y0, x1, y1, c);
    case 4: return scan<4>(d, s, x0, y0, x1, y1, c);
    default: return scan<8>(d, s, x0, y0, x1, y1, c);
  }
}

using Line = std::vector<std::pair<int64_t, const Tile*>>;  // the stored tiles of a column or row: (place along, tile)

// What content_bounds() looks at.
struct Bounds {
  Format f;
  Pixel value;
  Counts counts;
  bool fill_counts = false;  // empty tiles count whole
  Box area, tiles;           // the pixels looked at, the tiles over them
  std::map<int64_t, Line> cols, rows;  // the stored tiles over the area, by column and by row
};

// The outermost pixel column (Left, Right) or row (Top, Bottom) on side s holding a pixel that counts in tile column
// or row l (its stored tiles `tiles`, null for none); nullopt when nothing in it counts.
std::optional<int64_t> in_line(const Bounds& b, Side s, int64_t l, const Line* tiles) {
  const bool cols = columns(s);
  bool whole = b.fill_counts && (!tiles || int64_t(tiles->size()) < (cols ? b.tiles.y1 - b.tiles.y0 : b.tiles.x1 - b.tiles.x0));
  Line data;
  for (size_t i = 0; tiles && i < tiles->size(); i++) {
    const auto& pt = (*tiles)[i];
    if (!pt.second->is_solid())
      data.push_back(pt);
    else if (!alike(b.f, pt.second->value(), b.value))
      whole = true;
  }
  if (whole) {  // the line's edge on that side, within the area
    const int64_t lo = std::max(l * S, cols ? b.area.x0 : b.area.y0), hi = std::min((l + 1) * S, cols ? b.area.x1 : b.area.y1);
    return leading(s) ? lo : hi - 1;
  }
  if (data.empty()) return std::nullopt;
  PROFILE_ZONE("content_bounds_scan");
  std::vector<int> found(data.size(), -1);
  parallel_for(int(data.size()), [&](int begin, int end) {
    for (int i = begin; i < end; i++) {
      const auto& [along, t] = data[size_t(i)];
      const int64_t tx = cols ? l : along, ty = cols ? along : l;
      const Box w = meet(pixels_of(tx, ty), b.area);
      const Pin p = t->pin();
      found[size_t(i)] = scan(size_t(b.f.bytes()), p.data(), s, int(w.x0 - tx * S), int(w.y0 - ty * S),
                              int(w.x1 - tx * S), int(w.y1 - ty * S), b.counts);
    }
  });
  std::optional<int64_t> best;
  for (const int at : found)
    if (at >= 0) {
      const int64_t v = l * S + at;
      best = !best ? v : leading(s) ? std::min(*best, v) : std::max(*best, v);
    }
  return best;
}

// The outermost pixel column or row on side s holding a pixel that counts: from that side in, line by line of tiles
// until one holds any.
std::optional<int64_t> outermost(const Bounds& b, Side s) {
  const std::map<int64_t, Line>& lines = columns(s) ? b.cols : b.rows;
  if (b.fill_counts) {  // every line over the area, as empty tiles count
    const int64_t lo = columns(s) ? b.tiles.x0 : b.tiles.y0, hi = columns(s) ? b.tiles.x1 : b.tiles.y1;
    for (int64_t i = 0; i < hi - lo; i++) {
      const int64_t l = leading(s) ? lo + i : hi - 1 - i;
      const auto it = lines.find(l);
      if (const auto v = in_line(b, s, l, it == lines.end() ? nullptr : &it->second)) return v;
    }
  } else if (leading(s)) {  // only lines holding tiles
    for (auto it = lines.begin(); it != lines.end(); ++it)
      if (const auto v = in_line(b, s, it->first, &it->second)) return v;
  } else {
    for (auto it = lines.rbegin(); it != lines.rend(); ++it)
      if (const auto v = in_line(b, s, it->first, &it->second)) return v;
  }
  return std::nullopt;
}

PixelRect bounds(const TileGrid& g, Pixel value, const Box& area) {
  PROFILE_ZONE("content_bounds");
  if (area.empty()) return {};
  Bounds b;
  b.f = g.format();
  b.value = normal(b.f, value);
  b.counts = counts_against(b.f, b.value);
  b.fill_counts = !alike(b.f, g.fill(), b.value);
  b.area = area;
  b.tiles = tiles_of(area);
  for (const auto& [xy, t] : g.tiles())
    if (xy.x >= b.tiles.x0 && xy.x < b.tiles.x1 && xy.y >= b.tiles.y0 && xy.y < b.tiles.y1) {
      b.cols[xy.x].push_back({xy.y, t.get()});
      b.rows[xy.y].push_back({xy.x, t.get()});
    }
  const std::optional<int64_t> left = outermost(b, Side::Left);
  if (!left) return {};  // nothing counts; else every side finds a pixel
  const int64_t right = *outermost(b, Side::Right), top = *outermost(b, Side::Top), bottom = *outermost(b, Side::Bottom);
  const auto to_int = [](int64_t v) { return int(std::clamp<int64_t>(v, INT_MIN, INT_MAX)); };
  return {to_int(*left), to_int(top), to_int(right + 1), to_int(bottom + 1)};
}

}  // namespace

TileGrid place(const TileGrid& grid, const Placement& p, const std::optional<PixelRect>& clip) {
  if (!clip) return apply(grid, map_of(p), nullptr);
  const Box c = box(*clip);
  return apply(grid, map_of(p), &c);
}

PixelRect place(const PixelRect& r, const Placement& p) {
  if (r.empty()) return {};
  const Box b = map_of(p).forward(box(r));
  if (b.x0 < INT_MIN || b.y0 < INT_MIN || b.x1 > INT_MAX || b.y1 > INT_MAX)
    throw std::out_of_range("pb: place: pixels past int's range");
  return {int(b.x0), int(b.y0), int(b.x1), int(b.y1)};
}

TileGrid shift(const TileGrid& grid, int dx, int dy) {
  Placement p;
  p.dx = dx, p.dy = dy;
  return place(grid, p);
}

Placement rotation(int quarters, int width, int height) {
  Placement p;
  switch ((quarters % 4 + 4) % 4) {
    case 1:  // (x, y) to (height - 1 - y, x)
      p.transpose = p.mirror_x = true, p.dx = height;
      break;
    case 2:  // to (width - 1 - x, height - 1 - y)
      p.mirror_x = p.mirror_y = true, p.dx = width, p.dy = height;
      break;
    case 3:  // to (y, width - 1 - x)
      p.transpose = p.mirror_y = true, p.dy = width;
      break;
  }
  return p;
}

TileGrid rotate(const TileGrid& grid, int quarters, int width, int height) {
  return place(grid, rotation(quarters, width, height));
}

Placement mirroring(Mirror m, int width, int height) {
  Placement p;
  if (m == Mirror::Horizontal)
    p.mirror_x = true, p.dx = width;
  else
    p.mirror_y = true, p.dy = height;
  return p;
}

TileGrid flip(const TileGrid& grid, Mirror m, int width, int height) { return place(grid, mirroring(m, width, height)); }

TileGrid crop(const TileGrid& grid, const PixelRect& r) {
  if (r.empty()) return TileGrid(grid.format(), grid.fill());
  Map m;
  m.dx = -int64_t(r.x0), m.dy = -int64_t(r.y0);
  const Box clip{0, 0, int64_t(r.x1) - r.x0, int64_t(r.y1) - r.y0};
  return apply(grid, m, &clip);
}

PixelRect content_bounds(const TileGrid& grid) {
  return bounds(grid, grid.fill(), {INT_MIN, INT_MIN, int64_t(INT_MAX) + 1, int64_t(INT_MAX) + 1});
}

PixelRect content_bounds(const TileGrid& grid, Pixel value, const PixelRect& area) {
  return bounds(grid, value, box(area));
}

}  // namespace pb
