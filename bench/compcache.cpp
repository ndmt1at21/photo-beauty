// pb-bench compcache [doc.pbd] [cpu] [--budget <MB>]: the tools' composite (compcache.h) of all of D1 at level 0 (33 x
// 22 tiles), the document opened from kDocs/d1.pbd (read only; or the .pbd given) as the app opens it.
// composite_grid() on the CPU with the cache empty: first with the layer tiles still in the file (each one's first pin
// decompresses its frame), then with them in RAM; then out of the cache (nothing to composite); on the GPU there is
// (lavapipe in WSL, the Iris Xe from the mingw build; not with `cpu`) with the cache empty again, its tiles against
// the CPU's; last the prefill's job from set() to done, the CPU's. Seconds; MB of composite tiles (the store's raw
// pixels they add) and of device memory.
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>

#include "bench.h"
#include "compcache.h"
#include "docs.h"
#include "pbd.h"
#include "tilecache.h"

using namespace pb;
using pb::bench::Clock;
using pb::bench::report;

namespace {

constexpr double kMB = 1 << 20;
double seconds_since(Clock::time_point t) { return pb::bench::ms_since(t) / 1000; }

// Both null, or alike: solid of one value, or data of the same pixels.
bool same(const Tile* a, const Tile* b) {
  if (!a || !b) return !a && !b;
  if (a->is_solid() || b->is_solid()) return a->is_solid() && b->is_solid() && a->value() == b->value();
  const Pin p = a->pin(), q = b->pin();
  return std::equal(p.data(), p.data() + size_t(kTileSize) * kTileSize * 8, q.data());
}

}  // namespace

BENCH(compcache) {
  bool cpu = false;
  std::string file = std::string(bench::kDocs) + "/d1.pbd";
  for (size_t i = 0; i < args.size(); i++)
    if (args[i] == "cpu")
      cpu = true;
    else if (args[i] == "--budget" && i + 1 < args.size())
      set_tile_budget(size_t(std::stod(args[++i]) * kMB));
    else
      file = args[i];
  if (!std::filesystem::exists(file)) return void(std::printf("compcache: no %s (pb-bench make d1)\n", file.c_str()));
  const std::unique_ptr<gpu::Device> dev = cpu ? nullptr : gpu::Device::create(true);
  std::printf("compcache: %s; %s; tile budget %zu MB\n", file.c_str(),
              dev ? (dev->name() + ", " + dev->driver()).c_str() : "the CPU", tile_budget() >> 20);
  auto t0 = Clock::now();
  const RenderInput in{open_pbd(file).doc};
  report("compcache_open", seconds_since(t0), "s");
  const TileRect all = canvas_tiles(*in.doc, 0);
  const std::string n = "compcache_" + std::filesystem::path(file).stem().string();
  // composite_grid() of all of it: seconds, and how many tiles it composited.
  const auto grid = [&](const std::string& name) {
    const uint64_t built = composites_built();
    const auto t = Clock::now();
    TileGrid g = composite_grid(in, 0, all);
    report((n + name).c_str(), seconds_since(t), "s");
    report((n + name + "_composited").c_str(), double(composites_built() - built), "tiles");
    return g;
  };

  clear_composites();
  TileStats s0 = tile_stats();
  {
    const TileGrid cold = grid("_cpu_cold");
    size_t data = 0;
    for (const auto& [at, t] : cold.tiles()) data += !t->is_solid();
    report((n + "_tiles").c_str(), double(cold.tiles().size()), "tiles");
    report((n + "_mb").c_str(), double(data) * kTileSize * kTileSize * 8 / kMB, "MB");
    const TileStats s1 = tile_stats();  // the layer tiles' pixels too, now in RAM
    report((n + "_cold_raw_mb").c_str(), (double(s1.raw) - double(s0.raw)) / kMB, "MB");
  }
  clear_composites();
  s0 = tile_stats();
  const TileGrid ref = grid("_cpu");
  report((n + "_cpu_raw_mb").c_str(), (double(tile_stats().raw) - double(s0.raw)) / kMB, "MB");
  grid("_cached");
  if (dev) {
    std::mutex lock;
    set_composite_gpu(dev.get(), &lock);
    clear_composites();
    {
      const TileGrid g = grid("_gpu");
      report((n + "_gpu_device_mb").c_str(), double(dev->held().device) / kMB, "MB");
      size_t differ = 0;
      for (int y = all.y0; y < all.y1; y++)
        for (int x = all.x0; x < all.x1; x++) differ += !same(g.get(x, y), ref.get(x, y));
      report((n + "_gpu_differ").c_str(), double(differ), "tiles");
    }
    set_composite_gpu(nullptr, nullptr);
  }
  clear_composites();
  {
    Prefill p;
    const uint64_t built = composites_built();
    t0 = Clock::now();
    p.set(in.doc, t0 - std::chrono::seconds(1), {});  // stood still for a second
    p.wait(std::chrono::minutes(10));
    report((n + "_prefill").c_str(), seconds_since(t0), "s");
    report((n + "_prefill_composited").c_str(), double(composites_built() - built), "tiles");
  }
  const TileStats s = tile_stats();
  report((n + "_store_raw").c_str(), double(s.raw) / kMB, "MB");
  report((n + "_store_compressed").c_str(), double(s.compressed) / kMB, "MB");
  report((n + "_store_scratch").c_str(), double(s.scratch) / kMB, "MB");
  report((n + "_peak_rss").c_str(), double(bench::peak_resident_bytes()) / kMB, "MB");
  clear_composites();
}
