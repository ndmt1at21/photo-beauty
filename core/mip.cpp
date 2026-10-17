#include "mip.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <climits>
#include <iterator>
#include <list>
#include <map>
#include <mutex>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include "jobs.h"
#include "profile.h"
#include "tilecache.h"

namespace pb {
namespace {

constexpr int S = kTileSize, H = kTileSize / 2;

// MurmurHash3_x64_128 (public domain) over 16-byte blocks.
class Hash {
 public:
  void add(uint64_t k1, uint64_t k2) {
    constexpr uint64_t c1 = 0x87c37b91114253d5ull, c2 = 0x4cf5ad432745937full;
    h1_ ^= rotl(k1 * c1, 31) * c2;
    h1_ = (rotl(h1_, 27) + h2_) * 5 + 0x52dce729;
    h2_ ^= rotl(k2 * c2, 33) * c1;
    h2_ = (rotl(h2_, 31) + h1_) * 5 + 0x38495ab5;
    n_ += 16;
  }
  std::pair<uint64_t, uint64_t> get() const {
    uint64_t h1 = h1_ ^ n_, h2 = h2_ ^ n_;
    h1 += h2, h2 += h1;
    h1 = fmix(h1), h2 = fmix(h2);
    h1 += h2, h2 += h1;
    return {h1, h2};
  }

 private:
  static uint64_t rotl(uint64_t x, int r) { return x << r | x >> (64 - r); }
  static uint64_t fmix(uint64_t k) {
    k = (k ^ k >> 33) * 0xff51afd7ed558ccdull;
    k = (k ^ k >> 33) * 0xc4ceb9fe1a85ec53ull;
    return k ^ k >> 33;
  }
  uint64_t h1_ = 0, h2_ = 0, n_ = 0;
};

using Key = std::pair<uint64_t, uint64_t>;

// What an entry costs: its pixels, and roughly its Tile and nodes.
size_t cost(const TilePtr& t) { return 256 + (t && !t->is_solid() ? size_t(S) * S * size_t(t->format().bytes()) : 0); }

// Entries most recently used first within a cap, as Lru (lru.h), but for a cap given each time and entries let go of
// on request. Tiles it lets go of go to `out`, for the caller to free once unlocked.
struct Part {
  struct Item {
    Key key;
    TilePtr tile;  // null: empty
    size_t bytes;  // cost()
  };
  std::list<Item> items;
  std::map<Key, std::list<Item>::iterator> index;
  size_t used = 0;

  const Item* get(const Key& k) {  // marks it used
    const auto it = index.find(k);
    if (it == index.end()) return nullptr;
    items.splice(items.begin(), items, it->second);
    return &*it->second;
  }
  void put(const Key& k, TilePtr t, std::vector<TilePtr>& out) {  // replaces k's entry
    if (const auto it = index.find(k); it != index.end()) drop(it->second, out);
    const size_t bytes = cost(t);
    items.push_front({k, std::move(t), bytes});
    index[k] = items.begin();
    used += bytes;
  }
  void drop(std::list<Item>::iterator it, std::vector<TilePtr>& out) {
    used -= it->bytes;
    index.erase(it->key);
    out.push_back(std::move(it->tile));
    items.erase(it);
  }
  void trim(size_t cap, std::vector<TilePtr>& out) {  // keeps the newest entry whatever it costs
    while (used > cap && items.size() > 1) drop(std::prev(items.end()), out);
  }
};

struct Cache {
  std::mutex m;
  Part fine, coarse;  // levels 1 and 2; 3 and up
  BudgetUser user{[this](size_t want) { return release(want); }};

  Part& at(int level) { return level < 3 ? fine : coarse; }
  void trim(size_t budget, std::vector<TilePtr>& out) {  // lock held
    fine.trim(budget / 8, out);
    coarse.trim(budget / 16, out);
  }
  // The store over budget with no tile left to push out: lets go of tiles nothing else holds, least recently used
  // first, levels 1 and 2 before 3 and up, about `want` bytes of pixels, and gives what that freed in RAM.
  size_t release(size_t want) {
    PROFILE_ZONE("mip_release");
    const TileStats was = tile_stats();
    std::vector<TilePtr> out;
    {
      const std::lock_guard l(m);
      size_t pixels = 0;
      for (Part* p : {&fine, &coarse})
        for (auto it = p->items.end(); it != p->items.begin() && pixels < want;) {
          const auto at = std::prev(it);
          if (at->tile && !at->tile->is_solid() && at->tile.use_count() == 1) {
            pixels += at->bytes;
            p->drop(at, out);
          } else {
            it = at;
          }
        }
    }
    out.clear();
    const TileStats now = tile_stats();
    const size_t a = was.raw + was.compressed, b = now.raw + now.compressed;
    return a > b ? a - b : 0;
  }
};
// Never destroyed: the pool's jobs may still build mips while static objects go at exit.
Cache& cache() {
  static Cache& c = *new Cache;
  return c;
}
std::atomic<uint64_t> g_built{0};

void check_level(int level, int lowest) {
  if (level < lowest || level > 31) throw std::invalid_argument("pb: mip: no such level");
}

// The key of tile (tx, ty) at level >= 1 of g: the level, g's format and fill, then the position and id of each tile
// stored under it, row by row; `tiles` counts those.
struct Scan {
  Key key;
  size_t tiles = 0;
};
Scan scan(const TileGrid& g, int level, int tx, int ty) {
  const int64_t n = int64_t(1) << level, end = int64_t(INT_MAX) + 1;
  const int64_t x0 = std::max<int64_t>(tx * n, INT_MIN), x1 = std::min<int64_t>((tx + int64_t(1)) * n, end);
  const int64_t y0 = std::max<int64_t>(ty * n, INT_MIN), y1 = std::min<int64_t>((ty + int64_t(1)) * n, end);
  const Format f = g.format();
  const Pixel fill = g.fill();
  Hash h;
  h.add(uint64_t(level) | uint64_t(f.channels) << 8 | uint64_t(f.depth) << 16,
        uint64_t(fill[0]) | uint64_t(fill[1]) << 16 | uint64_t(fill[2]) << 32 | uint64_t(fill[3]) << 48);
  Scan s;
  const auto& m = g.tiles();
  for (auto it = m.lower_bound({int(x0), int(y0)}); it != m.end() && it->first.y < y1;) {
    const TileXY p = it->first;
    if (p.x < x0) {
      it = m.lower_bound({int(x0), p.y});
    } else if (p.x >= x1) {
      if (p.y + int64_t(1) >= y1) break;
      it = m.lower_bound({int(x0), p.y + 1});
    } else {
      h.add(uint64_t(p.x - x0) | uint64_t(p.y - y0) << 32, it->second->id());
      s.tiles++;
      ++it;
    }
  }
  s.key = h.get();
  return s;
}

// What averaging pixels of v gives: v, but 0 throughout for a transparent one.
Pixel averaged(Format f, const Pixel& v) { return f.alpha() && v[f.channels - 1] == 0 ? Pixel{} : v; }

// Quadrant k (2 * row + column) of out, a tile's pixels: in's 2 x 2 averages (mip.h), or v throughout where in is null.
template <class T, int C>
void quadrant(const uint8_t* in_bytes, const Pixel& v, int k, uint8_t* out_bytes) {
  using W = std::conditional_t<sizeof(T) == 1, uint32_t, uint64_t>;  // fits 2 * 4 * max² + 4 * max
  T* out = reinterpret_cast<T*>(out_bytes) + (size_t(k >> 1) * H * S + size_t(k & 1) * H) * C;
  if (!in_bytes) {
    for (int y = 0; y < H; y++)
      for (T* o = out + size_t(y) * S * C; o < out + (size_t(y) * S + H) * C; o += C)
        for (int c = 0; c < C; c++) o[c] = T(v[c]);
    return;
  }
  const T* in = reinterpret_cast<const T*>(in_bytes);
  for (int y = 0; y < H; y++) {
    const T* r0 = in + size_t(2 * y) * S * C;
    const T* r1 = r0 + size_t(S) * C;
    T* o = out + size_t(y) * S * C;
    for (int x = 0; x < H; x++, r0 += 2 * C, r1 += 2 * C, o += C) {
      if constexpr (C == 1) {
        o[0] = T((W(r0[0]) + r0[1] + r1[0] + r1[1] + 2) >> 2);
      } else {
        const W a0 = r0[C - 1], a1 = r0[2 * C - 1], a2 = r1[C - 1], a3 = r1[2 * C - 1], A = a0 + a1 + a2 + a3;
        o[C - 1] = T((A + 2) >> 2);
        if (A < 2) {  // transparent
          for (int c = 0; c < C - 1; c++) o[c] = 0;
        } else if (a0 == a1 && a0 == a2 && a0 == a3) {  // one alpha (opaque, say): the plain average is the same
          for (int c = 0; c < C - 1; c++) o[c] = T((W(r0[c]) + r0[C + c] + r1[c] + r1[C + c] + 2) >> 2);
        } else {
          for (int c = 0; c < C - 1; c++)
            o[c] = T((2 * (r0[c] * a0 + r0[C + c] * a1 + r1[c] * a2 + r1[C + c] * a3) + A) / (2 * A));
        }
      }
    }
  }
}
using Quadrant = void (*)(const uint8_t*, const Pixel&, int, uint8_t*);
Quadrant quadrant_for(Format f) {
  const bool u8 = f.depth == Depth::U8;
  if (f.channels == 1) return u8 ? quadrant<uint8_t, 1> : quadrant<uint16_t, 1>;
  if (f.channels == 2) return u8 ? quadrant<uint8_t, 2> : quadrant<uint16_t, 2>;
  return u8 ? quadrant<uint8_t, 4> : quadrant<uint16_t, 4>;
}

// Tile (tx, ty) at level >= 1 from the four below it: empty or solid ones all alike give an empty or solid tile,
// anything else a data tile, compacted.
TilePtr build(const TileGrid& g, int level, int tx, int ty) {
  const Format f = g.format();
  std::array<TilePtr, 4> below;
  std::array<Pixel, 4> value{};  // of the empty and solid ones
  bool alike = true;
  for (int k = 0; k < 4; k++) {
    below[k] = mip(g, level - 1, 2 * tx + (k & 1), 2 * ty + (k >> 1));
    if (below[k] && !below[k]->is_solid()) {
      alike = false;
      continue;
    }
    value[k] = averaged(f, below[k] ? below[k]->value() : g.fill());
    alike = alike && value[k] == value[0];
  }
  if (alike) return value[0] == averaged(f, g.fill()) ? nullptr : Tile::solid(f, value[0]);
  PROFILE_ZONE("mip_average");
  const std::shared_ptr<Tile> t = Tile::make(f);
  {
    const WritePin w = t->write();
    const Quadrant q = quadrant_for(f);
    for (int k = 0; k < 4; k++) q(below[k] ? below[k]->pin().data() : nullptr, value[k], k, w.data());
  }
  g_built.fetch_add(1, std::memory_order_relaxed);
  return compact(t, g.fill());
}

}  // namespace

TilePtr mip(const TileGrid& g, int level, int tx, int ty) {
  check_level(level, 0);
  if (level == 0) {
    const auto it = g.tiles().find({tx, ty});
    return it == g.tiles().end() ? nullptr : it->second;
  }
  PROFILE_ZONE("mip");
  const Scan s = scan(g, level, tx, ty);
  if (!s.tiles) return nullptr;  // fill throughout
  Cache& c = cache();
  {
    const std::lock_guard l(c.m);
    if (const Part::Item* e = c.at(level).get(s.key)) return e->tile;
  }
  TilePtr t = build(g, level, tx, ty);
  const size_t budget = tile_budget();
  std::vector<TilePtr> out;  // freed once unlocked
  const std::lock_guard l(c.m);
  if (const Part::Item* e = c.at(level).get(s.key)) return e->tile;  // another thread's, built meanwhile
  c.at(level).put(s.key, t, out);
  c.trim(budget, out);
  return t;
}

int mip_missing(const TileGrid& g, int level, int tx, int ty, int most) {
  check_level(level, 0);
  if (level == 0) return 0;
  const Scan s = scan(g, level, tx, ty);
  if (!s.tiles) return 0;
  {
    Cache& c = cache();
    const std::lock_guard l(c.m);
    if (c.at(level).index.count(s.key)) return 0;  // there, without marking it used as mip() does
  }
  int n = 1;  // this one; the four below it while n <= most, each counted up to what is left
  for (int k = 0; k < 4 && n <= most; k++) n += mip_missing(g, level - 1, 2 * tx + (k & 1), 2 * ty + (k >> 1), most - n);
  return n;
}

void prefetch(const TileGrid& g, int level, TileRect r) {
  check_level(level, 0);
  PROFILE_ZONE("mip_prefetch");
  for (int l = 1; l <= level; l++) {
    const int64_t n = int64_t(1) << (level - l);  // level l's tiles across one of `level`'s
    const int64_t x0 = r.x0 * n, y0 = r.y0 * n, w = (r.x1 - int64_t(r.x0)) * n, h = (r.y1 - int64_t(r.y0)) * n;
    if (w <= 0 || h <= 0) return;
    parallel_for(int(w * h), [&](int begin, int end) {
      for (int i = begin; i < end; i++) mip(g, l, int(x0 + i % w), int(y0 + i / w));
    });
  }
}

void attach_mip(const TileGrid& g, int level, int tx, int ty, TilePtr t) {
  check_level(level, 1);
  if (t && t->format() != g.format()) throw std::invalid_argument("pb: attach_mip: a tile of another format");
  const Scan s = scan(g, level, tx, ty);
  if (!s.tiles) return;  // mip() gives null there without looking
  const size_t budget = tile_budget();
  std::vector<TilePtr> out;
  Cache& c = cache();
  const std::lock_guard l(c.m);
  c.at(level).put(s.key, std::move(t), out);
  c.trim(budget, out);
}

void clear_mips() {
  Part fine, coarse;  // the entries, freed once unlocked
  Cache& c = cache();
  const std::lock_guard l(c.m);
  std::swap(fine, c.fine);
  std::swap(coarse, c.coarse);
}

uint64_t mips_built() { return g_built.load(std::memory_order_relaxed); }

}  // namespace pb
