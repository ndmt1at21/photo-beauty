// The HAL's memory (gpu.h, K1c): uploads through a ring of staging chunks, a batch submitted early so that staging
// holds two batches' worth however much a caller records; buffers kept for reuse or freed once released; K1's tile
// budget counting what the device takes of RAM.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "check.h"
#include "mip.h"
#include "tile.h"
#include "tilecache.h"

using namespace pb;

namespace {
constexpr size_t MB = size_t(1) << 20;

// Until done() or 30 s, polling: the frees of the device's pool jobs run meanwhile.
template <class F>
bool eventually(F done) {
  const auto t0 = std::chrono::steady_clock::now();
  while (!done())
    if (std::chrono::steady_clock::now() - t0 > std::chrono::seconds(30)) return false;
    else std::this_thread::sleep_for(std::chrono::milliseconds(1));
  return true;
}
}  // namespace

TEST(test_gpu_memory) {
  auto dev = test::gpu_device();
  if (!dev) return;
  const size_t others = tile_stats().charged;  // what other caches count
  // Whether device memory is RAM, as on integrated GPUs (the Iris Xe) and lavapipe (CI's, WSL's): what a buffer adds.
  gpu::BufferPtr probe = dev->buffer(MB);
  const bool ram = dev->held().ram == MB;
  CHECK(ram || dev->name().find("llvmpipe") == std::string::npos);
  const auto counted = [&] {  // the budget counts the device's RAM
    const gpu::Device::Held h = dev->held();
    return h.ram >= (ram ? h.device : 0) + h.staging && tile_stats().charged == others + h.ram;
  };

  // Released, a buffer is kept for the next of its size, or freed once nothing uses it (keep false: a store that grew).
  // What was freed counts for some seconds more, as drivers keep it a while to reuse; the device's calls after that
  // give it back.
  {
    gpu::BufferPtr kept = dev->buffer(8 * MB), freed = dev->buffer(8 * MB, false);
    CHECK(dev->held().device == 17 * MB && dev->held().kept == 0);
    CHECK(eventually(counted));
    dev->fill(*kept, 1);
    dev->fill(*freed, 2);
    dev->submit();
    kept = freed = nullptr;  // the submission holds them
    CHECK(dev->held().device == 17 * MB && dev->held().kept == 0);
    const size_t charged = tile_stats().charged;
    dev->flush();
    CHECK(dev->held().kept == 8 * MB);
    CHECK(eventually([&] { return dev->held().device == 9 * MB; }));
    const auto t0 = std::chrono::steady_clock::now();
    CHECK(eventually(counted) && tile_stats().charged == charged);
    kept = dev->buffer(8 * MB);  // the one kept
    CHECK(dev->held().device == 9 * MB && dev->held().kept == 0);
    CHECK(eventually([&] {
      dev->buffer(4096);  // a call: kept, taken again by the next
      const gpu::Device::Held h = dev->held();
      return h.ram == (ram ? h.device : 0) + h.staging && counted();
    }));
    CHECK(std::chrono::steady_clock::now() - t0 > std::chrono::seconds(6));
  }

  // 96 MB in pieces of 1 byte to 6 MB, some at odd offsets, with no submit() between them: staging holds two batches
  // of up to 32 MB and a chunk each, not all of it. A few pieces again, first wrong then right: in order across the
  // submissions that made.
  const size_t n = 96 * MB;
  std::mt19937 rng(5);
  std::vector<uint8_t> want(n), junk(MB);
  for (uint8_t& v : want) v = uint8_t(rng());
  for (uint8_t& v : junk) v = uint8_t(rng());
  {
    const gpu::BufferPtr dst = dev->buffer(n, false);
    size_t most = 0;
    for (size_t at = 0, i = 0; at < n; i++) {
      size_t len = i % 5 == 1 ? 1 + rng() % 1000 : i % 7 == 3 ? 512 * 1024 + 3 : 256 * 1024;
      if (i == 9) len = 6 * MB;  // more than a chunk
      if (i == 30) len = 4 * MB;  // a chunk
      len = std::min(len, n - at);
      dev->upload(*dst, want.data() + at, len, at);
      most = std::max(most, dev->held().staging);
      at += len;
    }
    for (int k = 0; k < 4; k++) {
      const size_t at = rng() % (n - MB), len = 1 + rng() % MB;
      dev->upload(*dst, junk.data(), len, at);
      dev->upload(*dst, want.data() + at, len, at);
    }
    CHECK(most >= 32 * MB && most <= 2 * 36 * MB + 6 * MB);
    std::vector<uint8_t> got(n);
    dev->download(*dst, got.data(), n);
    dev->flush();
    CHECK(got == want);
    CHECK(eventually(counted));
  }
  probe = nullptr;
  dev->trim();
  CHECK(eventually([&] { return dev->held().device == 0 && dev->held().kept == 0; }));
  CHECK(eventually(counted));
  dev.reset();
  CHECK(tile_stats().charged == others);  // given back
}

// make_room() pushes tiles out on its thread until the budget holds what is about to be allocated, so that the device
// buffers the budget counts never take it past the budget, not even until the Background job catches up.
TEST(test_gpu_memory_room) {
  clear_mips();
  trim_tiles();
  const auto held = [] {  // what the budget counts
    const TileStats s = tile_stats();
    return s.raw - s.pinned + s.compressed + s.charged;
  };
  const size_t old = tile_budget(), budget = held() + 64 * MB;
  set_tile_budget(budget);
  const Format f{Depth::U8, 4};
  std::vector<TilePtr> tiles;
  const auto fill = [&] {  // tiles up to 85 %, short of the Background job's 90 %
    while (held() + 256 * 1024 < budget / 100 * 85) {
      const std::shared_ptr<Tile> t = Tile::make(f);
      const WritePin w = t->write();
      for (size_t i = 0; i < 256 * 1024; i++) w.data()[i] = uint8_t(i / 4096 + tiles.size());
      tiles.push_back(t);
    }
  };
  fill();
  make_room(24 * MB);
  CHECK(held() + 24 * MB <= budget);
  if (auto dev = test::gpu_device()) {
    fill();
    const gpu::BufferPtr b = dev->buffer(24 * MB);
    const gpu::Device::Held h = dev->held();
    CHECK(h.ram < 24 * MB || held() <= budget);  // when the budget counts it
  }
  set_tile_budget(old);
}

