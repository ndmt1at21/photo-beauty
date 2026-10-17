// Mip levels (mip.h): 2 x 2 averages weighted by alpha against a plain reference in every format, empty and solid
// tiles kept so without allocating, a changed tile rebuilding its ancestors only, what makes a key, the caps following
// the tile budget, the store asking the cache to let go, tiles handed over from a file, several threads at once; times
// levels 1-6 of a grid of data tiles the size of D1.
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <stdexcept>
#include <thread>
#include <vector>

#include "check.h"
#include "mip.h"
#include "tilecache.h"

using namespace pb;

namespace {

constexpr int S = kTileSize;
constexpr size_t kPixels = size_t(S) * S;
constexpr Format kFormats[] = {{Depth::U8, 1},  {Depth::U8, 2},  {Depth::U8, 4},
                               {Depth::U16, 1}, {Depth::U16, 2}, {Depth::U16, 4}};

// Each check counts builds and bytes from a clean start.
void reset() { clear_mips(); }

void put_px(Format f, uint8_t* p, const Pixel& v) {
  for (int c = 0; c < f.channels; c++)
    if (f.depth == Depth::U16)
      std::memcpy(p + 2 * c, &v[c], 2);
    else
      p[c] = uint8_t(v[c]);
}
Pixel get_px(Format f, const uint8_t* p) {
  Pixel v{};
  for (int c = 0; c < f.channels; c++)
    if (f.depth == Depth::U16)
      std::memcpy(&v[c], p + 2 * c, 2);
    else
      v[c] = p[c];
  return v;
}
// Alpha 0, full or between at random.
Pixel random_px(Format f, std::mt19937& rng) {
  const int m = max_value(f.depth);
  Pixel v{};
  for (int c = 0; c < f.channels; c++) v[c] = uint16_t(rng() % unsigned(m + 1));
  if (f.alpha() && rng() % 2) v[f.channels - 1] = uint16_t(rng() % 2 ? m : 0);
  return v;
}
// A data tile of random pixels, with alpha a quarter of them transparent and a quarter opaque; alpha >= 0: that alpha
// throughout. Random bits in bulk: a draw per sample takes most of the test's time under TSan.
std::shared_ptr<Tile> random_tile(Format f, std::mt19937& rng, int alpha = -1) {
  auto t = Tile::make(f);
  const WritePin w = t->write();
  std::mt19937_64 bits(rng());
  const size_t bpp = size_t(f.bytes());
  for (size_t i = 0; i < kPixels * bpp; i += 8) {
    const uint64_t r = bits();
    std::memcpy(w.data() + i, &r, 8);
  }
  if (!f.alpha()) return t;
  uint64_t modes = 0;
  for (size_t i = 0; i < kPixels; i++) {
    if (i % 32 == 0) modes = bits();
    Pixel v = get_px(f, w.data() + i * bpp);
    uint16_t& a = v[f.channels - 1];
    const unsigned mode = unsigned(modes >> (2 * (i % 32))) & 3;
    a = uint16_t(alpha >= 0 ? alpha : mode == 0 ? 0 : mode == 1 ? max_value(f.depth) : a);
    put_px(f, w.data() + i * bpp, v);
  }
  return t;
}
// A tile's pixels, packed: fill() where it is null.
std::vector<uint8_t> bytes_of(const TilePtr& t, const TileGrid& g) {
  const Format f = g.format();
  std::vector<uint8_t> out(kPixels * f.bytes());
  if (t && !t->is_solid())
    std::memcpy(out.data(), t->pin().data(), out.size());
  else
    for (size_t i = 0; i < kPixels; i++) put_px(f, out.data() + i * f.bytes(), t ? t->value() : g.fill());
  return out;
}
double ms_since(std::chrono::steady_clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

// mip.h's 2 x 2 average, written out.
Pixel average(Format f, const Pixel* p) {
  Pixel o{};
  if (!f.alpha()) {
    o[0] = uint16_t((p[0][0] + p[1][0] + p[2][0] + p[3][0] + 2) / 4);
    return o;
  }
  const int a = f.channels - 1;
  const uint64_t A = uint64_t(p[0][a]) + p[1][a] + p[2][a] + p[3][a];
  o[a] = uint16_t((A + 2) / 4);
  for (int c = 0; c < a; c++) {
    uint64_t sum = 0;
    for (int i = 0; i < 4; i++) sum += uint64_t(p[i][c]) * p[i][a];
    o[c] = o[a] ? uint16_t((2 * sum + A) / (2 * A)) : 0;
  }
  return o;
}

// The reference mip, pixel by pixel: level 0 read from the grid, each level above averaged from the four tiles below;
// packed pixels, null where no tile is stored under it (fill throughout).
class Reference {
 public:
  explicit Reference(const TileGrid& g) : g_(g) {}
  const std::vector<uint8_t>* at(int level, int tx, int ty) {
    if (const auto it = memo_.find({level, tx, ty}); it != memo_.end()) return &it->second;
    if (!any_under(level, tx, ty)) return nullptr;
    const Format f = g_.format();
    const size_t bpp = size_t(f.bytes());
    std::vector<uint8_t> px(kPixels * bpp);
    if (level == 0) {
      g_.read_rect(tx * S, ty * S, S, S, px.data(), S * bpp);
    } else {
      const std::vector<uint8_t>* below[4];
      for (int k = 0; k < 4; k++) below[k] = at(level - 1, 2 * tx + (k & 1), 2 * ty + (k >> 1));
      for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) {
          const std::vector<uint8_t>* b = below[(y >= S / 2) * 2 + (x >= S / 2)];
          Pixel p[4];
          for (int i = 0; i < 4; i++) {
            const int bx = 2 * (x % (S / 2)) + (i & 1), by = 2 * (y % (S / 2)) + (i >> 1);
            p[i] = b ? get_px(f, b->data() + (size_t(by) * S + size_t(bx)) * bpp) : g_.fill();
          }
          put_px(f, px.data() + (size_t(y) * S + size_t(x)) * bpp, average(f, p));
        }
    }
    return &(memo_[{level, tx, ty}] = std::move(px));
  }

 private:
  bool any_under(int level, int tx, int ty) const {
    for (const auto& [xy, t] : g_.tiles())
      if (xy.x >> level == tx && xy.y >> level == ty) return true;
    return false;
  }
  const TileGrid& g_;
  std::map<std::array<int, 3>, std::vector<uint8_t>> memo_;
};

// Data (some of one alpha throughout), solid (some transparent, of some colour) and empty tiles at random across the
// origin; one solid value under a level-1 and a level-2 tile; data tiles of one value; the fill as a solid tile; a
// faint tile (alpha 1 at one pixel in four) that averages to transparent; tiles far apart, for the levels up to 6.
TileGrid sample(Format f, Pixel fill, std::mt19937& rng) {
  TileGrid g(f, fill);
  const int m = max_value(f.depth);
  for (int ty = -3; ty < 3; ty++)
    for (int tx = -4; tx < 2; tx++) {
      const unsigned kind = rng() % 5;
      if (kind < 2) g.set(tx, ty, random_tile(f, rng));
      if (kind == 2) g.set(tx, ty, random_tile(f, rng, int(rng() % unsigned(m + 1))));
      if (kind == 3) g.set(tx, ty, Tile::solid(f, random_px(f, rng)));
    }
  Pixel v{uint16_t(m / 3), uint16_t(m / 5), uint16_t(m / 7), 0};
  v[f.channels - 1] = uint16_t(m / 2);
  for (int ty = -6; ty < -4; ty++)
    for (int tx = 4; tx < 6; tx++) g.set(tx, ty, Tile::solid(f, v));  // under level-1 tile (2, -3)
  for (int ty = 4; ty < 8; ty++)
    for (int tx = 4; tx < 8; tx++) g.set(tx, ty, Tile::solid(f, v));  // under level-2 tile (1, 1)
  for (int ty = 2; ty < 4; ty++)
    for (int tx = 14; tx < 16; tx++) g.set(tx, ty, Tile::make(f, v));  // data, averaging to solid
  g.set(12, 0, Tile::solid(f, fill));
  if (f.alpha()) {
    auto faint = random_tile(f, rng, 0);
    const WritePin w = faint->write();
    for (int y = 0; y < S; y += 2)
      for (int x = 0; x < S; x += 2) {
        uint8_t* p = w.data() + (size_t(y) * S + size_t(x)) * f.bytes();
        Pixel px = get_px(f, p);
        px[f.channels - 1] = 1;
        put_px(f, p, px);
      }
    g.set(10, -2, faint);  // alone under level-1 tile (5, -1)
  }
  g.set(-70, 40, random_tile(f, rng));
  g.set(33, -65, random_tile(f, rng));
  g.set(100, 100, Tile::solid(f, v));
  return g;
}

// Every mip tile over the sample's tiles, and their neighbours, levels 1 to 6: the reference's pixels, null where
// they are all fill, solid where uniform; asked again, all of them come from the cache.
void check_reference(Format f, Pixel fill, unsigned seed) {
  reset();
  std::mt19937 rng(seed);
  const TileGrid g = sample(f, fill, rng);
  Reference ref(g);
  std::set<std::array<int, 3>> todo;
  for (const auto& [xy, t] : g.tiles())
    for (int l = 1; l <= 6; l++)
      for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) todo.insert({l, (xy.x >> l) + dx, (xy.y >> l) + dy});
  const std::vector<uint8_t> fill_px = bytes_of(nullptr, g);  // transparent pixels average to 0 throughout, as {}
  int solid = 0, empty = 0, data = 0;
  for (const auto& [l, tx, ty] : todo) {
    const TilePtr t = mip(g, l, tx, ty);
    const std::vector<uint8_t>* want = ref.at(l, tx, ty);
    if (!want) {
      CHECK(!t);
      continue;
    }
    CHECK(bytes_of(t, g) == *want);
    const bool all_fill = *want == fill_px, uniform = *want == bytes_of(Tile::solid(f, get_px(f, want->data())), g);
    CHECK(all_fill ? !t : uniform ? t && t->is_solid() : t && !t->is_solid());
    (all_fill ? empty : uniform ? solid : data)++;
  }
  CHECK(solid >= 3 && empty >= 1 + f.alpha() && data > 20);  // the sample's cases came up
  CHECK(mip(g, 1, 2, -3)->value() == g.get(4, -6)->value() && mip(g, 2, 1, 1)->value() == g.get(4, 4)->value());
  CHECK(mip(g, 1, 7, 1)->is_solid() && !mip(g, 1, 6, 0) && (!f.alpha() || !mip(g, 1, 5, -1)));
  const uint64_t built = mips_built();
  for (const auto& [l, tx, ty] : todo) mip(g, l, tx, ty);
  CHECK(mips_built() == built);
}

// Changing a pixel of one level-0 tile builds its ancestors again, one a level; every other mip tile comes back as it
// was, the same tile.
void check_rebuild() {
  reset();
  const Format f{Depth::U8, 4};
  std::mt19937 rng(3);
  TileGrid g(f);
  for (int ty = -2; ty < 3; ty++)
    for (int tx = -3; tx < 3; tx++) g.set(tx, ty, random_tile(f, rng));
  const int top = 5;
  prefetch(g, top, {-1, -1, 1, 1});
  auto all = [&] {
    std::map<std::array<int, 3>, TilePtr> out;
    for (int l = 1; l <= top; l++)
      for (int ty = -2 >> l; ty <= 2 >> l; ty++)
        for (int tx = -3 >> l; tx <= 2 >> l; tx++) out[{l, tx, ty}] = mip(g, l, tx, ty);
    return out;
  };
  const uint64_t built = mips_built();
  const auto before = all();
  CHECK(mips_built() == built);  // prefetch() built them all
  const uint8_t px[4] = {1, 2, 3, 4};
  g.write_rect(2 * S + 5, -2 * S + 7, 1, 1, px, 4);  // in tile (2, -2)
  const auto after = all();
  CHECK(mips_built() == built + top);
  for (const auto& [k, t] : after) CHECK((t == before.at(k)) == (k[1] != 2 >> k[0] || k[2] != -2 >> k[0]));
}

// The same tile one place over under a mip tile, or over another fill: other keys.
void check_keys() {
  reset();
  const Format f{Depth::U8, 1};
  std::mt19937 rng(4);
  const TilePtr t = random_tile(f, rng);
  TileGrid a(f), b(f), white(f, {255});
  a.set(0, 0, t), b.set(1, 0, t), white.set(0, 0, t);
  const std::vector<uint8_t> in_a = bytes_of(mip(a, 1, 0, 0), a);
  CHECK(bytes_of(mip(b, 1, 0, 0), b) != in_a && bytes_of(mip(white, 1, 0, 0), white) != in_a);
}

// A grid of 20,000 x 15,000 pixels in solid tiles of one colour (79 x 59 tiles) built to level 6: every mip tile
// wholly over them is solid and allocates nothing. Those across its right and bottom edges, over solid and empty
// tiles both, hold pixels: as many as there are of them, nothing else.
void check_solid() {
  reset();
  const Format f{Depth::U8, 4};
  const Pixel v{200, 120, 40, 255};
  const int nx = (20000 + S - 1) / S, ny = (15000 + S - 1) / S;
  TileGrid g(f);
  for (int ty = 0; ty < ny; ty++)
    for (int tx = 0; tx < nx; tx++) g.set(tx, ty, Tile::solid(f, v));
  const size_t raw = tile_stats().raw;
  const uint64_t built = mips_built();
  const auto t0 = std::chrono::steady_clock::now();
  prefetch(g, 6, {0, 0, (nx + 63) / 64, (ny + 63) / 64});
  const double ms = ms_since(t0);
  size_t edge = 0;
  for (int l = 1; l <= 6; l++) {
    const int n = 1 << l;
    for (int ty = 0; ty < (ny + n - 1) / n; ty++)
      for (int tx = 0; tx < (nx + n - 1) / n; tx++) {
        const TilePtr t = mip(g, l, tx, ty);
        if (tx < nx / n && ty < ny / n) {
          CHECK(t && t->is_solid() && t->value() == v);
        } else {
          CHECK(t && !t->is_solid());
          edge++;
        }
      }
  }
  CHECK(mips_built() == built + edge && tile_stats().raw == raw + edge * kPixels * size_t(f.bytes()));
  std::printf("mip: 20000 x 15000 px of solid tiles to level 6 in %.1f ms, pixels only in the %zu tiles across its edges\n",
              ms, edge);
}

using Weak = std::vector<std::weak_ptr<const Tile>>;
bool expired(const Weak& v, size_t i) { return v.at(i).expired(); }

// The caps follow the tile budget, an eighth of it for levels 1 and 2 and a sixteenth for 3 and up, read again at each
// new entry: what falls out of them, least recently used first, frees its tiles; churn at levels 1 and 2 leaves levels
// 3 and up be. (Liveness, not raw bytes: a budget this small has the store pushing tiles out meanwhile.)
void check_caps() {
  reset();
  const size_t old = tile_budget();
  const Format f{Depth::U8, 1};
  const size_t entry = kPixels + 256;  // a data entry as the caps count it
  std::mt19937 rng(5);
  TileGrid g(f), other(f), third(f);
  for (int ty = 0; ty < 8; ty++)
    for (int tx = 0; tx < 8; tx++) g.set(tx, ty, random_tile(f, rng)), other.set(tx, ty, random_tile(f, rng));
  third.set(0, 0, random_tile(f, rng));
  set_tile_budget(64 * entry);  // 8 entries at levels 1 and 2, 4 at 3 and up
  Weak l1;
  for (int ty = 0; ty < 4; ty++)
    for (int tx = 0; tx < 4; tx++) l1.push_back(mip(g, 1, tx, ty));
  for (size_t i = 0; i < 16; i++) CHECK(expired(l1, i) == (i < 8));
  const Weak top{mip(g, 3, 0, 0)};  // levels 1 and 2 under it built anew meanwhile
  const uint64_t id = mip(g, 3, 0, 0)->id(), built = mips_built();
  Weak churn;
  for (int ty = 0; ty < 4; ty++)
    for (int tx = 0; tx < 4; tx++) churn.push_back(mip(other, 1, tx, ty));
  CHECK(!expired(top, 0) && mip(g, 3, 0, 0)->id() == id && mips_built() == built + 16);
  for (size_t i = 0; i < 16; i++) CHECK(expired(churn, i) == (i < 8));
  // A lower budget: 4 and 2 entries, as the next entry finds.
  set_tile_budget(32 * entry);
  for (size_t i = 8; i < 16; i++) CHECK(!expired(churn, i));
  const Weak one{mip(third, 1, 0, 0)};
  for (size_t i = 8; i < 16; i++) CHECK(expired(churn, i) == (i < 13));
  CHECK(!expired(one, 0) && !expired(top, 0));
  attach_mip(other, 3, 0, 0, random_tile(f, rng));
  CHECK(!expired(top, 0));
  attach_mip(third, 3, 0, 0, random_tile(f, rng));
  CHECK(expired(top, 0));
  mip(g, 1, 0, 0);  // out long since
  CHECK(mips_built() == built + 16 + 1 + 1);
  set_tile_budget(old);
}

// The store over budget with no tile left to push out asks its users (tilecache.h): the cache lets go of about what it
// wants of tiles nothing else holds, least recently used first, levels 1 and 2 before 3 and up.
void check_release() {
  reset();
  const size_t old = tile_budget(), budget = size_t(64) << 20;
  const Format f{Depth::U8, 4};
  TileGrid g(f);  // a solid tile under each level-1 tile: the cache takes data tiles handed over for them
  for (int i = 0; i < 32; i++) g.set(2 * i, 0, Tile::solid(f, {1, 2, 3, 4}));
  set_tile_budget(budget);  // levels 1 and 2: 8 MB, 31 of these entries; 3 and up: 4 MB
  trim_tiles();             // and what other tests left: nothing in RAM for the store to push out
  Weak fine, coarse;
  for (int i = 0; i < 28; i++) {  // pixels never pinned: nothing in RAM
    TilePtr t = Tile::make(f);
    fine.push_back(t);
    attach_mip(g, 1, i, 0, std::move(t));
  }
  for (int i = 0; i < 4; i++) {
    TilePtr t = Tile::make(f);
    coarse.push_back(t);
    attach_mip(g, 3, i, 0, std::move(t));
  }
  const TilePtr held = fine[0].lock();  // the oldest, held elsewhere: frees nothing, stays
  const size_t base = tile_stats().charged;
  CHECK(base < budget / 10 * 9);
  // Just past 90%, so the store wants 10% and 64 KB: 26 entries (256 KB and 256 bytes).
  const size_t over = budget / 10 * 9 - base + 64 * 1024;
  std::atomic<int> calls{0};
  BudgetUser* self = nullptr;
  BudgetUser user([&](size_t) {
    calls++;
    self->charge(-ptrdiff_t(over));
    return over;
  });
  self = &user;
  user.charge(ptrdiff_t(over));
  const auto t0 = std::chrono::steady_clock::now();
  while (tile_stats().charged > base && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(30))
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  CHECK(calls == 1);
  for (size_t i = 0; i < 28; i++) CHECK(expired(fine, i) == (i >= 1 && i <= 26));
  for (size_t i = 0; i < 4; i++) CHECK(!expired(coarse, i));
  CHECK(mip(g, 1, 0, 0) == held);
  set_tile_budget(old);
}

// Tiles a file kept come back from mip() as handed over, without building, for any grid of the same tiles; a changed
// tile under one is a new key. Level 0 is the grid's own tile; bad levels and formats throw.
void check_attach() {
  reset();
  const Format f{Depth::U16, 2};
  std::mt19937 rng(8);
  TileGrid g(f);
  for (const TileXY p : {TileXY{0, 0}, TileXY{3, 5}, TileXY{7, 7}, TileXY{6, 1}}) g.set(p.x, p.y, random_tile(f, rng));
  const TilePtr saved = Tile::solid(f, {123, 4567});
  attach_mip(g, 3, 0, 0, saved);
  const uint64_t built = mips_built();
  TileGrid copy = g;
  CHECK(mip(g, 3, 0, 0) == saved && mip(copy, 3, 0, 0) == saved && mips_built() == built);
  const uint8_t px[4] = {1, 2, 3, 4};
  copy.write_rect(7 * S + 3, 7 * S + 3, 1, 1, px, 4);
  CHECK(mip(copy, 3, 0, 0) != saved && mips_built() > built && mip(g, 3, 0, 0) == saved);
  attach_mip(g, 2, 9, 9, saved);  // nothing stored under it: null all the same
  CHECK(!mip(g, 2, 9, 9));
  CHECK(mip(g, 0, 3, 5) == g.tiles().at({3, 5}) && !mip(g, 0, 9, 9));
  auto throws = [](auto&& fn) {
    try {
      fn();
    } catch (const std::invalid_argument&) {
      return true;
    }
    return false;
  };
  CHECK(throws([&] { mip(g, -1, 0, 0); }) && throws([&] { mip(g, 32, 0, 0); }));
  CHECK(throws([&] { attach_mip(g, 3, 0, 0, Tile::solid({Depth::U8, 2}, {})); }));
  CHECK(throws([&] { attach_mip(g, 0, 0, 0, saved); }));
}

// Threads at once on one published grid, mip() and prefetch() over the same tiles under caps that keep evicting: each
// tile they get is the reference's (and TSan sees a race in the cache).
void check_threads() {
  const Format f{Depth::U8, 2};
  std::mt19937 rng(9);
  auto grid = std::make_shared<TileGrid>(f);
  for (int ty = 0; ty < 8; ty++)
    for (int tx = 0; tx < 8; tx++)
      if (rng() % 4) grid->set(tx, ty, random_tile(f, rng));
  const std::shared_ptr<const TileGrid> g = grid;
  Reference ref(*g);
  std::vector<std::array<int, 3>> keys;
  std::vector<std::vector<uint8_t>> want;
  for (int l = 1; l <= 3; l++)
    for (int ty = 0; ty < 8 >> l; ty++)
      for (int tx = 0; tx < 8 >> l; tx++) {
        keys.push_back({l, tx, ty});
        const std::vector<uint8_t>* w = ref.at(l, tx, ty);
        want.push_back(w ? *w : bytes_of(nullptr, *g));
      }
  const size_t entry = kPixels * size_t(f.bytes()) + 256, old = tile_budget();
  set_tile_budget(48 * entry);  // 6 entries at levels 1 and 2, 3 at 3 and up
  std::vector<std::thread> threads;
  for (unsigned i = 0; i < 8; i++)
    threads.emplace_back([&, i] {
      std::mt19937 r(i);
      for (unsigned n = 0; n < 40; n++) {
        if (n % 10 == i % 10) prefetch(*g, 3, {0, 0, 1, 1});
        const size_t k = r() % keys.size();
        CHECK(bytes_of(mip(*g, keys[k][0], keys[k][1], keys[k][2]), *g) == want[k]);
      }
    });
  for (std::thread& t : threads) t.join();
  set_tile_budget(old);
}

// Levels 1 to 6 of a grid of 16-bit data tiles the size of D1 (8256 x 5504 px), opaque as a photo or with alpha
// varying from pixel to pixel (the slowest case): how long prefetch() takes over each level.
[[maybe_unused]] void time_d1(bool opaque) {
  reset();
  const Format f{Depth::U16, 4};
  const int nx = (8256 + S - 1) / S, ny = (5504 + S - 1) / S;
  std::mt19937 rng(17);
  std::vector<std::shared_ptr<Tile>> kinds;  // copied about: random pixels cost more to make than to average
  for (int i = 0; i < 4; i++) kinds.push_back(random_tile(f, rng, opaque ? 65535 : -1));
  TileGrid g(f);
  for (int ty = 0; ty < ny; ty++)
    for (int tx = 0; tx < nx; tx++) {
      auto t = Tile::make(f);
      std::memcpy(t->write().data(), kinds[size_t(tx + ty) % kinds.size()]->pin().data(), kPixels * size_t(f.bytes()));
      g.set(tx, ty, t);
    }
  kinds.clear();
  std::printf("mip: D1 %d x %d tiles, 16-bit, %s, ms:", nx, ny, opaque ? "opaque" : "alpha varying");
  double total = 0;
  for (int l = 1; l <= 6; l++) {
    const int n = 1 << l;
    const auto t0 = std::chrono::steady_clock::now();
    prefetch(g, l, {0, 0, (nx + n - 1) / n, (ny + n - 1) / n});
    const double ms = ms_since(t0);
    total += ms;
    std::printf(" L%d %.1f", l, ms);
  }
  std::printf(", levels 1-6 %.1f\n", total);
}

}  // namespace

TEST(test_mip) {
  unsigned seed = 1;
  for (const Format f : kFormats) {
    check_reference(f, Pixel{}, seed++);
    if (!f.alpha()) check_reference(f, Pixel{uint16_t(max_value(f.depth))}, seed++);  // a white mask
  }
  check_rebuild();
  check_keys();
  check_solid();
  check_caps();
  check_release();
  check_attach();
  check_threads();
  // Timings mean nothing under a sanitizer, and TSan's shadow would make the D1 grid's 0.5 GB 2.5 GB.
#if !defined(__SANITIZE_ADDRESS__) && !defined(__SANITIZE_THREAD__)
  time_d1(true);
  time_d1(false);
#endif
  reset();
}
