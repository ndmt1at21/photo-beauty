// pb-bench regrid [dx dy]: T30's moves (regrid.h) on a layer the size of D1 (8256 x 5504, 16-bit RGBA as D1's pixel
// layers, built here: smooth colours and grain hashed from the place, opaque over the canvas, transparent past it, so
// that every one of its 33 x 22 tiles holds data, 372 MB of them). Shifts it by (17, -33) (the task's number, unless
// given) and by whole tiles, turns it 90° clockwise and flips it horizontally with D1's canvas, crops 100 px off each
// side, finds its content bounds; each once, printing the milliseconds and, for those that make tiles, the data tiles
// out. And the floor of whatever makes new tiles: as many fresh data tiles as the shift made, from the tile store,
// filled, in parallel (on WSL2 mostly the kernel faulting fresh pages in, tilecache.cpp).
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "bench.h"
#include "hash.h"
#include "jobs.h"
#include "regrid.h"
#include "tile.h"

using namespace pb;

namespace {

constexpr int S = kTileSize, kW = 8256, kH = 5504;

TileGrid layer() {
  const Format f{Depth::U16, 4};
  const int tw = (kW + S - 1) / S, th = (kH + S - 1) / S;
  std::vector<TilePtr> tiles(size_t(tw) * size_t(th));
  parallel_for(tw * th, [&](int begin, int end) {
    for (int i = begin; i < end; i++) {
      const int tx = i % tw, ty = i / tw;
      const std::shared_ptr<Tile> t = Tile::make(f);
      {
        const WritePin p = t->write();
        auto* d = reinterpret_cast<uint16_t*>(p.data());
        for (int y = 0; y < S; y++)
          for (int x = 0; x < S; x++) {
            const int X = tx * S + x, Y = ty * S + y;
            if (X >= kW || Y >= kH) continue;  // transparent past the canvas
            const uint32_t h = hash3(uint32_t(X), uint32_t(Y), 30);
            uint16_t* q = d + (size_t(y) * S + size_t(x)) * 4;
            q[0] = uint16_t(int64_t(X) * 60000 / kW + (h & 0x3ff));
            q[1] = uint16_t(int64_t(Y) * 60000 / kH + (h >> 10 & 0x3ff));
            q[2] = uint16_t(int64_t(X + Y) * 30000 / (kW + kH) + (h >> 20 & 0x3ff));
            q[3] = 65535;
          }
      }
      tiles[size_t(i)] = t;
    }
  });
  TileGrid g(f);
  for (int i = 0; i < tw * th; i++) g.set(i % tw, i / tw, std::move(tiles[size_t(i)]));
  return g;
}

size_t data_tiles(const TileGrid& g) {
  size_t n = 0;
  for (const auto& [xy, t] : g.tiles()) n += !t->is_solid();
  return n;
}

}  // namespace

BENCH(regrid) {
  using pb::bench::report;
  const int dx = args.size() >= 2 ? std::atoi(args[0].c_str()) : 17, dy = args.size() >= 2 ? std::atoi(args[1].c_str()) : -33;
  const TileGrid g = layer();
  report("regrid_layer_data_tiles", double(data_tiles(g)), "tiles");
  const auto measure = [&](const char* name, auto run) {
    const auto t0 = pb::bench::Clock::now();
    const TileGrid out = run();
    report(name, pb::bench::ms_since(t0), "ms");
    report((std::string(name) + "_data_tiles").c_str(), double(data_tiles(out)), "tiles");
  };
  char name[64];
  std::snprintf(name, sizeof name, "regrid_shift_%d_%d_d1", dx, dy);
  size_t made = 0;
  measure(name, [&] {
    TileGrid out = shift(g, dx, dy);
    made = data_tiles(out);
    return out;
  });
  {
    std::vector<std::shared_ptr<Tile>> fresh(made);
    const auto t0 = pb::bench::Clock::now();
    parallel_for(int(made), [&](int begin, int end) {
      for (int i = begin; i < end; i++) (fresh[size_t(i)] = Tile::make(g.format()))->write();
    });
    report("regrid_fresh_tiles_d1", pb::bench::ms_since(t0), "ms");
  }
  measure("regrid_shift_tiles_d1", [&] { return shift(g, S, -2 * S); });
  measure("regrid_rotate90_d1", [&] { return rotate(g, 1, kW, kH); });
  measure("regrid_flip_d1", [&] { return flip(g, Mirror::Horizontal, kW, kH); });
  measure("regrid_crop_d1", [&] { return crop(g, {100, 100, kW - 100, kH - 100}); });
  const auto t0 = pb::bench::Clock::now();
  const PixelRect b = content_bounds(g);
  report("regrid_bounds_d1", pb::bench::ms_since(t0), "ms");
  if (b != PixelRect{0, 0, kW, kH}) std::printf("regrid: content bounds %d %d %d %d, not D1's canvas\n", b.x0, b.y0, b.x1, b.y1);
}
