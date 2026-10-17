// Tiles and grids (tile.h): copy on write, ids, empty and solid tiles, compaction, rectangles across tiles at any
// sign, published copies read on other threads.
#include <atomic>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <set>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#include "check.h"
#include "tile.h"

using namespace pb;

namespace {

constexpr int S = kTileSize;
constexpr Format kFormats[] = {{Depth::U8, 1},  {Depth::U8, 2},  {Depth::U8, 4},
                               {Depth::U16, 1}, {Depth::U16, 2}, {Depth::U16, 4}};

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
// Opaque: a tile of transparent pixels empties, which check_compact() covers on its own.
Pixel random_px(Format f, std::mt19937& rng) {
  Pixel v{};
  for (int c = 0; c < f.channels; c++) v[c] = uint16_t(rng() % unsigned(max_value(f.depth) + 1));
  if (f.alpha() && v[f.channels - 1] == 0) v[f.channels - 1] = 1;
  return v;
}
// w x h pixels of v, packed rows.
std::vector<uint8_t> block(Format f, int w, int h, const Pixel& v) {
  std::vector<uint8_t> out(size_t(w) * h * f.bytes());
  for (size_t i = 0; i < size_t(w) * h; i++) put_px(f, out.data() + i * f.bytes(), v);
  return out;
}
std::vector<uint8_t> random_block(Format f, int w, int h, std::mt19937& rng) {
  std::vector<uint8_t> out(size_t(w) * h * f.bytes());
  for (size_t i = 0; i < size_t(w) * h; i++) put_px(f, out.data() + i * f.bytes(), random_px(f, rng));
  return out;
}
std::vector<uint8_t> read(const TileGrid& g, int x, int y, int w, int h) {
  std::vector<uint8_t> out(size_t(w) * h * g.format().bytes(), 0xcd);
  g.read_rect(x, y, w, h, out.data(), size_t(w) * g.format().bytes());
  return out;
}
void write(TileGrid& g, int x, int y, int w, int h, const std::vector<uint8_t>& px) {
  g.write_rect(x, y, w, h, px.data(), size_t(w) * g.format().bytes());
}
std::map<TileXY, uint64_t> ids(const TileGrid& g) {
  std::map<TileXY, uint64_t> out;
  for (const auto& [xy, t] : g.tiles()) out[xy] = t->id();
  return out;
}
int floor_tile(int v) { return int(std::floor(double(v) / S)); }

// What a grid should read back over a region, pixel by pixel.
struct Plane {
  Format f;
  int x0, y0, w, h;
  std::vector<uint8_t> px;
  uint8_t* at(int x, int y) { return px.data() + (size_t(y - y0) * w + size_t(x - x0)) * f.bytes(); }
};

// Random rectangles written at odd coordinates of either sign, across tiles: each kind of content (random, one
// value, the fill), every pixel read back as written, from the grid and from a copy taken halfway; tiles a write
// touches get ids never seen before, the others keep theirs.
void check_rects(Format f, Pixel fill, unsigned seed) {
  std::mt19937 rng(seed);
  const int X0 = -S - 37, Y0 = -S - 29, W = 3 * S + 78, H = 2 * S + 82;  // tiles -2..2 across, -2..1 down
  const size_t bpp = size_t(f.bytes());
  TileGrid g(f, fill);
  Plane m{f, X0, Y0, W, H, block(f, W, H, fill)};
  std::unique_ptr<TileGrid> copy;
  Plane at_copy = m;
  std::set<uint64_t> seen;
  for (int i = 0; i < 60; i++) {
    const int x = X0 + int(rng() % unsigned(W)), y = Y0 + int(rng() % unsigned(H));
    const int w = 1 + int(rng() % unsigned(X0 + W - x)), h = 1 + int(rng() % unsigned(Y0 + H - y));
    const unsigned kind = rng() % 3;  // random pixels, one value, the fill
    const Pixel one = kind == 2 ? fill : random_px(f, rng);
    const size_t stride = size_t(w) * bpp + 3;  // rows further apart than their bytes
    std::vector<uint8_t> src(stride * size_t(h));
    for (int r = 0; r < h; r++)
      for (int c = 0; c < w; c++) {
        const Pixel v = kind == 0 ? random_px(f, rng) : one;
        put_px(f, src.data() + size_t(r) * stride + size_t(c) * bpp, v);
        put_px(f, m.at(x + c, y + r), v);
      }
    const std::map<TileXY, uint64_t> before = ids(g);
    g.write_rect(x, y, w, h, src.data(), stride);
    for (const auto& [xy, id] : ids(g)) {
      const bool touched = xy.x >= floor_tile(x) && xy.x <= floor_tile(x + w - 1) && xy.y >= floor_tile(y) &&
                           xy.y <= floor_tile(y + h - 1);
      CHECK(id != 0);
      CHECK(touched ? !seen.count(id) : before.count(xy) && before.at(xy) == id);
    }
    for (const auto& [xy, id] : before)
      if (!(xy.x >= floor_tile(x) && xy.x <= floor_tile(x + w - 1) && xy.y >= floor_tile(y) && xy.y <= floor_tile(y + h - 1)))
        CHECK(g.get(xy.x, xy.y));
    for (const auto& [xy, id] : ids(g)) seen.insert(id);

    if (i % 6 == 0) {
      CHECK(read(g, X0, Y0, W, H) == m.px);
      const int rx = X0 + int(rng() % unsigned(W)), ry = Y0 + int(rng() % unsigned(H));
      const int rw = 1 + int(rng() % unsigned(X0 + W - rx)), rh = 1 + int(rng() % unsigned(Y0 + H - ry));
      std::vector<uint8_t> want(size_t(rw) * rh * bpp);
      for (int r = 0; r < rh; r++) std::memcpy(want.data() + size_t(r) * rw * bpp, m.at(rx, ry + r), size_t(rw) * bpp);
      CHECK(read(g, rx, ry, rw, rh) == want);
    }
    if (i == 30) {
      copy = std::make_unique<TileGrid>(g);
      at_copy = m;
    }
  }
  CHECK(read(g, X0, Y0, W, H) == m.px);
  CHECK(read(*copy, X0, Y0, W, H) == at_copy.px);  // it saw none of the later writes
  // Nor does the grid see the copy's.
  const std::vector<uint8_t> px = random_block(f, 2 * S, S, rng);
  write(*copy, -S - 7, -11, 2 * S, S, px);
  CHECK(read(*copy, -S - 7, -11, 2 * S, S) == px);
  CHECK(read(g, X0, Y0, W, H) == m.px);
}

// Writes in place while only the grid holds a tile, else copies first: a published copy, a TilePtr kept elsewhere,
// another grid set() with the tile all keep what they had. Counted in raw bytes.
void check_copy_on_write(Format f) {
  std::mt19937 rng(7);
  const size_t tile = size_t(S) * S * f.bytes(), base = tile_stats().raw;
  {
    TileGrid g(f);
    const std::vector<uint8_t> a = random_block(f, S, S, rng), b = random_block(f, S, S, rng);
    write(g, -S, -S, S, S, a);  // tile (-1, -1)
    CHECK(tile_stats().raw == base + tile);
    const Tile* t = g.get(-1, -1);
    const uint64_t id = t->id();
    g.write_rect(-S + 3, -S + 5, 7, 9, b.data(), size_t(S) * f.bytes());  // in place: same tile, new id
    CHECK(g.get(-1, -1) == t && t->id() != id && tile_stats().raw == base + tile);
    const std::vector<uint8_t> was = read(g, -S, -S, S, S);
    {
      const auto published = std::make_shared<const TileGrid>(g);
      CHECK(published->get(-1, -1) == t && published->get(-1, -1)->id() == t->id());  // pointers, not pixels
      write(g, -S, -S, S, S, b);
      CHECK(tile_stats().raw == base + 2 * tile);
      CHECK(read(*published, -S, -S, S, S) == was && read(g, -S, -S, S, S) == b);
      CHECK(g.get(-1, -1) != t && g.get(-1, -1)->id() != t->id() && published->get(-1, -1)->id() == t->id());
    }
    CHECK(tile_stats().raw == base + tile);  // the copy's tile went with it
    const Tile* now = g.get(-1, -1);
    write(g, -S, -S, S, S, a);  // no copy held it: in place again
    CHECK(g.get(-1, -1) == now && tile_stats().raw == base + tile);
    {
      const TilePtr held = g.tiles().at({-1, -1});
      g.write(-1, -1).data()[0] ^= 1;
      std::vector<uint8_t> want = a;
      want[0] ^= 1;
      CHECK(g.get(-1, -1) != held.get() && read(g, -S, -S, S, S) == want);
      CHECK(std::memcmp(held->pin().data(), a.data(), tile) == 0);
    }
    {
      TileGrid other(f);
      other.set(5, 5, g.tiles().at({-1, -1}));
      const std::vector<uint8_t> in_other = read(other, 5 * S, 5 * S, S, S);
      write(g, -S, -S, S, S, b);  // set() shared it: copied
      CHECK(read(other, 5 * S, 5 * S, S, S) == in_other);
      write(other, 5 * S, 5 * S, S, S, a);
      CHECK(read(g, -S, -S, S, S) == b);
    }
    CHECK(tile_stats().raw == base + tile);
  }
  CHECK(tile_stats().raw == base);
}

// Neither empty nor solid tiles allocate pixels: reads of them, writes of one value over whole tiles (solid) or of
// the fill (empty) leave nothing allocated; write() makes a data tile of the value it had.
void check_no_alloc(Format f) {
  std::mt19937 rng(11);
  const size_t base = tile_stats().raw;
  const Pixel fill = f.alpha() ? Pixel{} : Pixel{uint16_t(max_value(f.depth))};  // a white mask
  Pixel v = random_px(f, rng);
  while (v == fill) v = random_px(f, rng);
  {
    TileGrid g(f, fill);
    g.set(0, 0, Tile::solid(f, v));
    g.set(-3, 2, Tile::solid(f, v));
    const int x = -3 * S - 5, y = -10, w = 4 * S + 10, h = 3 * S + 20;
    const std::vector<uint8_t> px = read(g, x, y, w, h);
    CHECK(tile_stats().raw == base);
    for (int r = 0; r < h; r++)
      for (int c = 0; c < w; c++) {
        const int tx = floor_tile(x + c), ty = floor_tile(y + r);
        const bool solid = (tx == 0 && ty == 0) || (tx == -3 && ty == 2);
        CHECK(get_px(f, px.data() + (size_t(r) * w + c) * f.bytes()) == (solid ? v : fill));
      }
    write(g, S, 0, 2 * S, S, block(f, 2 * S, S, v));
    CHECK(tile_stats().raw == base);
    CHECK(g.get(1, 0)->is_solid() && g.get(1, 0)->value() == v && g.get(2, 0)->is_solid() && g.get(2, 0)->value() == v);
    write(g, 0, 0, S, S, block(f, S, S, fill));
    CHECK(!g.get(0, 0) && tile_stats().raw == base);
    {
      const WritePin p = g.write(-3, 2);  // a solid tile turns data of its value
      CHECK(tile_stats().raw == base + size_t(S) * S * f.bytes() && get_px(f, p.data() + 77 * f.bytes()) == v);
    }
    CHECK(!g.get(-3, 2)->is_solid());
    {
      const WritePin p = g.write(9, -9);  // an empty one, data of the fill
      CHECK(get_px(f, p.data() + (size_t(S) * S - 1) * f.bytes()) == fill);
    }
    CHECK(read(g, 9 * S, -9 * S, S, S) == block(f, S, S, fill));
  }
  CHECK(tile_stats().raw == base);
}

// One value everywhere goes solid; the fill, or transparent pixels whatever their colour, empty; a single pixel off,
// wherever it is, keeps the tile data.
void check_compact(Format f) {
  std::mt19937 rng(5);
  const Pixel fill = f.alpha() ? Pixel{} : Pixel{uint16_t(max_value(f.depth))};
  TileGrid g(f, fill);
  Pixel v = random_px(f, rng);
  while (v == fill) v = random_px(f, rng);
  Pixel w = v;
  w[0] = uint16_t(v[0] ^ 1);
  write(g, 0, 0, S, S, block(f, S, S, v));
  CHECK(g.get(0, 0)->is_solid() && g.get(0, 0)->value() == v);
  write(g, 0, 0, S, S, block(f, S, S, fill));
  CHECK(!g.get(0, 0));
  for (const size_t at : {size_t(1), size_t(S - 1), size_t(S), size_t(S) * S - 1}) {
    std::vector<uint8_t> px = block(f, S, S, v);
    put_px(f, px.data() + at * f.bytes(), w);
    write(g, 0, 0, S, S, px);
    CHECK(g.get(0, 0) && !g.get(0, 0)->is_solid());
    CHECK(read(g, 0, 0, S, S) == px);
  }
  if (f.alpha()) {
    std::vector<uint8_t> px = random_block(f, S, S, rng);
    for (size_t i = 0; i < size_t(S) * S; i++) {
      Pixel p = get_px(f, px.data() + i * f.bytes());
      p[f.channels - 1] = 0;
      put_px(f, px.data() + i * f.bytes(), p);
    }
    write(g, -S, S, S, S, px);  // erased: transparent, colours left over
    CHECK(!g.get(-1, 1) && read(g, -S, S, S, S) == block(f, S, S, fill));
    Pixel last = get_px(f, px.data() + (size_t(S) * S - 1) * f.bytes());
    last[f.channels - 1] = 1;
    put_px(f, px.data() + (size_t(S) * S - 1) * f.bytes(), last);
    write(g, -S, S, S, S, px);  // all but the last pixel transparent
    CHECK(g.get(-1, 1) && !g.get(-1, 1)->is_solid() && read(g, -S, S, S, S) == px);
    Pixel tinted{};
    tinted[0] = 5;
    CHECK(!compact(Tile::solid(f, tinted), fill));  // transparent of another colour is the fill all the same
  }
  const TilePtr s = Tile::solid(f, v);
  CHECK(compact(s, fill) == s && !compact(Tile::solid(f, fill), fill) && !compact(nullptr, fill));
}

// A writer keeps changing a grid and handing copies to readers, which check each copy they take is whole, then drop
// it. A published tile written in place would show the writer's later pixels while a reader holds it; written in
// place after a reader dropped it, unordered with that reader's reads, TSan sees the race.
void check_threads() {
  const Format f{Depth::U8, 1};
  using Published = std::pair<int, TileGrid>;  // a version and the grid as it had it
  TileGrid g(f);
  std::vector<uint8_t> px(2 * size_t(S) * S);
  auto paint = [&](int v) {
    for (size_t i = 0; i < px.size(); i++) px[i] = uint8_t(v + i % 7);
    write(g, -S, 0, 2 * S, S, px);
  };
  std::mutex m;
  std::shared_ptr<const Published> slot;  // the newest copy no reader took yet
  std::atomic<bool> done{false};
  std::atomic<int> reads{0};
  auto reader = [&] {
    while (!done.load()) {
      std::shared_ptr<const Published> p;
      {
        const std::lock_guard l(m);
        p = std::move(slot);
      }
      if (!p) {
        std::this_thread::yield();
        continue;
      }
      const std::vector<uint8_t> got = read(p->second, -S, 0, 2 * S, S);
      for (size_t i = 0; i < got.size(); i++) CHECK(got[i] == uint8_t(p->first + i % 7));
      reads++;
    }
  };
  std::thread r1(reader), r2(reader);
  for (int v = 1; v <= 300 || reads.load() < 50; v++) {
    paint(v);
    auto p = std::make_shared<const Published>(v, g);
    const std::lock_guard l(m);
    slot = std::move(p);
  }
  done = true;
  r1.join(), r2.join();
}

}  // namespace

TEST(test_tile) {
  for (int v = 0; v <= 255; v++) CHECK(from_unit(to_unit(v, Depth::U8), Depth::U8) == v);
  for (int v = 0; v <= 65535; v++) CHECK(from_unit(to_unit(v, Depth::U16), Depth::U16) == v);
  CHECK(from_unit(0.5f, Depth::U8) == 128 && from_unit(-0.1f, Depth::U8) == 0 && from_unit(1.5f, Depth::U8) == 255);
  CHECK(from_unit(std::nanf(""), Depth::U16) == 0);

  bool threw = false;
  try {
    TileGrid bad(Format{Depth::U8, 3});
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  CHECK(threw);
  threw = false;
  try {
    TileGrid g(Format{Depth::U8, 4});
    g.set(0, 0, Tile::solid({Depth::U16, 4}, {}));
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  CHECK(threw);

  const auto t = Tile::make({Depth::U8, 4});
  const uint64_t id = t->id();
  CHECK(id != 0 && t->write().data() && t->id() != id && !t->is_solid());
  TileGrid g(Format{Depth::U8, 4});
  CHECK(g.bounds().x1 == 0 && g.bounds().y1 == 0);
  g.set(-3, 2, Tile::solid(g.format(), {1, 2, 3, 4}));
  g.set(4, -1, Tile::solid(g.format(), {1, 2, 3, 4}));
  const TileRect b = g.bounds();
  CHECK(b.x0 == -3 && b.y0 == -1 && b.x1 == 5 && b.y1 == 3);

  unsigned seed = 1;
  for (const Format f : kFormats) {
    check_rects(f, Pixel{}, seed++);
    if (!f.alpha()) check_rects(f, Pixel{uint16_t(max_value(f.depth))}, seed++);  // a white mask
    check_copy_on_write(f);
    check_no_alloc(f);
    check_compact(f);
  }
  check_threads();
}
