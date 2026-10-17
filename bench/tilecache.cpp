// pb-bench tilecache [photo]: what zstd does to the tiles the store pushes out (tilecache.h), at levels 1 (the store's)
// and 3: the ratio of raw to compressed bytes, MB/s of raw pixels compressed and decompressed on one thread, and the
// share of tiles that level 1 shrinks by an eighth, which the store keeps compressed in RAM (the others go to scratch
// raw). Data tiles only, as grids keep them: empty and solid ones never reach zstd. Kinds of tile, as D1 and D2 have:
// - photo16: a 16-bit RGB photo at full size, `photo` (a TIFF or PNG; default samples/photo16.tif);
// - crops8: an 8-bit layer of feathered pieces of that photo covering 30% of it (D2's layers, as E1 makes them);
// - paint8: an 8-bit layer of soft and hard strokes in many colours covering 30% of it (D1's painted layers);
// - mask8: a mask of large soft strokes;
// - grey16: a dodge and burn layer, 50% grey with faint white and black strokes over 30% (D1's).
// Then what a pin costs, its tile in RAM: one thread, and 8 at once on tiles of their own (the store has one lock); and
// what a new tile costs, made, written once (its pixels allocated at the first pin) and dropped: 1 thread, and 8.
#include <zstd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "bench.h"
#include "dab.h"
#include "open.h"
#include "tile.h"

using namespace pb;

namespace {

using Bytes = std::vector<uint8_t>;

// The data tiles of a grid, raw.
std::vector<Bytes> data_tiles(const TileGrid& g) {
  std::vector<Bytes> out;
  const size_t n = size_t(kTileSize) * kTileSize * g.format().bytes();
  for (const auto& [xy, t] : g.tiles())
    if (!t->is_solid()) {
      const Pin p = t->pin();
      out.emplace_back(p.data(), p.data() + n);
    }
  return out;
}

void measure(const char* kind, const std::vector<Bytes>& tiles) {
  using pb::bench::report;
  const std::string k = std::string("tilecache_") + kind;
  report((k + "_tiles").c_str(), double(tiles.size()), "tiles");
  if (tiles.empty()) return;
  ZSTD_CCtx* c = ZSTD_createCCtx();
  ZSTD_DCtx* d = ZSTD_createDCtx();
  for (const int level : {1, 3}) {
    std::vector<Bytes> z(tiles.size());
    size_t raw = 0, packed = 0, kept = 0;
    auto t0 = pb::bench::Clock::now();
    for (size_t i = 0; i < tiles.size(); i++) {
      z[i].resize(ZSTD_compressBound(tiles[i].size()));
      z[i].resize(ZSTD_compressCCtx(c, z[i].data(), z[i].size(), tiles[i].data(), tiles[i].size(), level));
      raw += tiles[i].size(), packed += z[i].size();
      kept += z[i].size() <= tiles[i].size() - tiles[i].size() / 8;
    }
    const double squeeze = pb::bench::ms_since(t0);
    Bytes back(tiles[0].size());
    t0 = pb::bench::Clock::now();
    for (size_t i = 0; i < tiles.size(); i++) {
      back.resize(tiles[i].size());
      if (ZSTD_decompressDCtx(d, back.data(), back.size(), z[i].data(), z[i].size()) != back.size() || back != tiles[i])
        std::fprintf(stderr, "tilecache: %s: tile %zu does not come back\n", kind, i);
    }
    const double unsqueeze = pb::bench::ms_since(t0);
    const std::string l = k + "_z" + std::to_string(level);
    report((l + "_ratio").c_str(), double(raw) / double(packed), "x");
    report((l + "_compress").c_str(), double(raw) / 1048576 / (squeeze / 1000), "MB/s");
    report((l + "_decompress").c_str(), double(raw) / 1048576 / (unsqueeze / 1000), "MB/s");
    report((l + "_kept").c_str(), 100. * double(kept) / double(tiles.size()), "%");
  }
  ZSTD_freeCCtx(c);
  ZSTD_freeDCtx(d);
}

// A float RGBA plane, not premultiplied, painted over (Normal) with strokes of C1's dabs.
struct Plane {
  int w, h;
  std::vector<float> px;
  std::vector<bool> hit;
  size_t covered = 0;  // pixels a dab touched
  Plane(int w, int h, std::array<float, 4> v) : w(w), h(h), px(size_t(w) * h * 4), hit(size_t(w) * h) {
    for (size_t i = 0; i < px.size(); i++) px[i] = v[i % 4];
  }

  void stroke(std::mt19937& rng, float size, float hardness, float flow, std::array<float, 3> rgb) {
    BrushParams b;
    b.size = size, b.hardness = hardness, b.flow = flow, b.pressure_size = false;
    DabPlacer placer(b);
    std::uniform_real_distribution<float> u(0, 1);
    float x = u(rng) * float(w), y = u(rng) * float(h), dir = u(rng) * 6.2832f;
    const int events = 10 + int(rng() % 40);
    for (int i = 0; i <= events; i++) {
      if (i == events) {
        for (const Dab& d : placer.finish()) dab(d, hardness, rgb);
        break;
      }
      for (const Dab& d : placer.add({i / 120., x, y, 1})) dab(d, hardness, rgb);
      dir += (u(rng) - 0.5f) * 0.8f;
      x += std::cos(dir) * size * (0.3f + u(rng)), y += std::sin(dir) * size * (0.3f + u(rng));
    }
  }
  void dab(const Dab& d, float hardness, const std::array<float, 3>& rgb) {
    const DabMask m = dab_mask(d, hardness);
    for (int j = std::max(m.y, 0); j < std::min(m.y + m.h, h); j++)
      for (int i = std::max(m.x, 0); i < std::min(m.x + m.w, w); i++) {
        const float a = (*m.px)[size_t(j - m.y) * size_t(m.w) + size_t(i - m.x)] / 65535.f * d.flow;
        if (a <= 0) continue;
        over(i, j, rgb, a);
      }
  }
  void over(int i, int j, const std::array<float, 3>& rgb, float a) {
    const size_t at = size_t(j) * w + i;
    float* p = &px[at * 4];
    covered += !hit[at];
    hit[at] = true;
    const float out = a + p[3] * (1 - a);
    for (int c = 0; c < 3; c++) p[c] = (rgb[c] * a + p[c] * p[3] * (1 - a)) / out;
    p[3] = out;
  }
  // Into a grid of format f (channels 4: RGBA; 1: alpha alone, a mask).
  TileGrid grid(Format f) const {
    TileGrid g(f);
    std::vector<uint8_t> row(size_t(w) * f.bytes());
    for (int y = 0; y < h; y++) {
      for (int x = 0; x < w; x++)
        for (int c = 0; c < f.channels; c++) {
          const float v = px[(size_t(y) * w + x) * 4 + (f.channels == 1 ? 3 : c)];
          const uint16_t q = from_unit(v, f.depth);
          if (f.depth == Depth::U16)
            std::memcpy(&row[(size_t(x) * f.channels + c) * 2], &q, 2);
          else
            row[size_t(x) * f.channels + c] = uint8_t(q);
        }
      g.write_rect(0, y, w, 1, row.data(), row.size());
    }
    return g;
  }
};

void pin_cost() {
  for (const int threads : {1, 8}) {
    std::vector<TilePtr> tiles;
    for (int t = 0; t < threads; t++) {
      const std::shared_ptr<Tile> x = Tile::make({Depth::U8, 1});
      x->write();
      tiles.push_back(x);
    }
    constexpr int kPins = 1 << 20;  // each thread's
    std::vector<unsigned> sums(tiles.size());
    const auto t0 = pb::bench::Clock::now();
    std::vector<std::thread> v;
    for (int t = 0; t < threads; t++)
      v.emplace_back([&, t] {
        for (int i = 0; i < kPins; i++) sums[size_t(t)] += tiles[size_t(t)]->pin().data()[i & 63];
      });
    for (std::thread& t : v) t.join();
    pb::bench::report(threads == 1 ? "tilecache_pin" : "tilecache_pin_8_threads", pb::bench::ms_since(t0) * 1e6 / kPins,
                      "ns");
  }
}

void new_tile_cost() {
  constexpr Format f{Depth::U8, 4};
  constexpr int kTiles = 4096;  // each thread's
  for (const int threads : {1, 8}) {
    const auto t0 = pb::bench::Clock::now();
    std::vector<std::thread> v;
    for (int t = 0; t < threads; t++)
      v.emplace_back([f] {
        for (int i = 0; i < kTiles; i++) {
          const std::shared_ptr<Tile> x = Tile::make(f);
          std::memset(x->write().data(), i, size_t(kTileSize) * kTileSize * 4);
        }
      });
    for (std::thread& t : v) t.join();
    pb::bench::report(threads == 1 ? "tilecache_new_tile" : "tilecache_new_tile_8_threads",
                      pb::bench::ms_since(t0) * 1000 / kTiles, "us");
  }
}

struct Photo {
  int width = 0, height = 0;
  std::vector<uint16_t> rgba;
};
// The photo at full size, RGBA16; none when there is no such file or it is not 16-bit RGB.
Photo load(const std::string& path) {
  if (!std::filesystem::exists(path)) {
    std::printf("tilecache: no %s: photo16 and crops8 skipped\n", path.c_str());
    return {};
  }
  const DocPtr d = open_image(path);
  if (d->mode != Mode::Rgb || d->depth != Depth::U16) {
    std::printf("tilecache: %s is not 16-bit RGB: photo16 and crops8 skipped\n", path.c_str());
    return {};
  }
  Photo f{d->width, d->height, std::vector<uint16_t>(size_t(d->width) * d->height * 4)};
  d->root->children[0]->pixels->read_rect(0, 0, f.width, f.height, reinterpret_cast<uint8_t*>(f.rgba.data()),
                                          size_t(f.width) * 8);
  std::printf("tilecache: %s %dx%d\n", std::filesystem::path(path).filename().string().c_str(), f.width, f.height);
  return f;
}
const uint16_t* at(const Photo& f, int x, int y) { return f.rgba.data() + (size_t(y) * f.width + x) * 4; }

std::vector<Bytes> photo_tiles(const Photo& f) {
  TileGrid g({Depth::U16, 4});
  const int tw = f.width / kTileSize, th = f.height / kTileSize;  // whole tiles only
  g.write_rect(0, 0, tw * kTileSize, th * kTileSize, reinterpret_cast<const uint8_t*>(at(f, 0, 0)), size_t(f.width) * 8);
  return data_tiles(g);
}

}  // namespace

BENCH(tilecache) {
  constexpr int W = 4096;
  std::mt19937 rng(1);
  std::uniform_real_distribution<float> u(0, 1);
  if (const Photo f = load(args.empty() ? "samples/photo16.tif" : args[0]);
      f.width >= 2048 && f.height >= 2048) {
    measure("photo16", photo_tiles(f));
    Plane p(W, W, {0, 0, 0, 0});
    while (p.covered < size_t(W) * W * 3 / 10) {  // pieces 200 to 1,500 px, edges feathered 0 to 30 px
      const int w = 200 + int(rng() % 1300), h = 200 + int(rng() % 1300), feather = int(rng() % 31);
      const int sx = int(rng() % unsigned(f.width - w)), sy = int(rng() % unsigned(f.height - h));
      const int dx = int(rng() % unsigned(W + w)) - w, dy = int(rng() % unsigned(W + w)) - w;
      for (int j = std::max(0, -dy); j < h && dy + j < W; j++)
        for (int i = std::max(0, -dx); i < w && dx + i < W; i++) {
          const int edge = std::min(std::min(i, w - 1 - i), std::min(j, h - 1 - j));
          const float a = feather ? std::min(1.f, float(edge + 1) / float(feather + 1)) : 1.f;
          const uint16_t* c = at(f, sx + i, sy + j);
          p.over(dx + i, dy + j, {c[0] / 65535.f, c[1] / 65535.f, c[2] / 65535.f}, a);
        }
    }
    measure("crops8", data_tiles(p.grid({Depth::U8, 4})));
  }
  {
    Plane p(W, W, {0, 0, 0, 0});
    while (p.covered < size_t(W) * W * 3 / 10)
      p.stroke(rng, 15 + 150 * u(rng) * u(rng), u(rng), 0.3f + 0.7f * u(rng), {u(rng), u(rng), u(rng)});
    measure("paint8", data_tiles(p.grid({Depth::U8, 4})));
  }
  {
    Plane p(W, W, {0, 0, 0, 0});
    while (p.covered < size_t(W) * W * 4 / 10) p.stroke(rng, 150 + 500 * u(rng), 0.6f * u(rng), 1, {1, 1, 1});
    measure("mask8", data_tiles(p.grid({Depth::U8, 1})));
  }
  {
    Plane p(W, W, {0.5f, 0.5f, 0.5f, 1});
    while (p.covered < size_t(W) * W * 3 / 10) {
      const float v = rng() % 2 ? 1.f : 0.f;
      p.stroke(rng, 40 + 260 * u(rng), 0, 0.05f + 0.1f * u(rng), {v, v, v});
    }
    measure("grey16", data_tiles(p.grid({Depth::U16, 4})));
  }
  pin_cost();
  new_tile_cost();
}
