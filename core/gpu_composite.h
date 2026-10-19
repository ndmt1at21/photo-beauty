#pragma once
// The composite on the GPU (plan.md, Ghép): the tiles a frame shows, composited in one dispatch out of an atlas of layer
// tiles in device memory, into a cache of composite tiles there that the canvas presents (V2). It means what
// composite_tile() (render.h) means, to the bit where the CPU follows IEEE single precision (shaders/lib/blend.slang).
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "gpu.h"
#include "render.h"

namespace pb {

class GpuComposite {
 public:
  // At most `bytes` of device memory (0: min(1 GB, Device::memory() / 4)): a quarter for composite tiles, a quarter for
  // the accumulators below the stroke's layer, the rest for the atlas: the layer tiles of one format at a time (the
  // document's) and the stroke buffer's, least recently used out first. Each takes memory as frames need it, twice as
  // much at a time (what it held copied over), so that a first frame waits only for what it shows. The device
  // outlives it.
  explicit GpuComposite(gpu::Device&, size_t bytes = 0);
  ~GpuComposite();
  GpuComposite(const GpuComposite&) = delete;
  GpuComposite& operator=(const GpuComposite&) = delete;

  // Records the composite of `tiles` at mip level `level` of `in`, the pixels composite_tile() gives them out of the
  // same plan (composite_plan(), composite.h, which builds the mips it lacks: K2's prefetch() first for a level not
  // yet seen), groups and the stroke included: for each its slot in output(), or -1 where nothing is there
  // (transparent). Uploads the tiles the atlas lacks, then dispatches once for the tiles not in the cache, which keeps
  // a composite tile by the ids of the tiles in it, its layers' modes and alphas and the stroke's; more only when the
  // atlas cannot hold a frame's tiles at once. Where the stroke paints (RenderInput::stroke, on layer k), what lies
  // below k there goes into a cache keyed alike: the accumulator under k, and one more for each group around k (the
  // backdrop under the group, and under k in it); later frames start from it, compositing k with the stroke and the
  // steps above k only. The caller submits; what it records after reads the slots, which stay until the next call.
  // Plans of groups nested deeper than 10 composite on the CPU (composite_tile(), uploaded). Throws
  // std::invalid_argument as composite_plan() does, std::length_error for more tiles than the composite tiles' share
  // of the budget holds. One thread at a time, the device's.
  std::vector<int> composite(const RenderInput&, int level, const std::vector<TileXY>& tiles);

  // The composite tiles: slot i from byte i * kTileSize * kTileSize * 8 on, RGBA U16 rows as composite_tile()'s.
  // Another buffer after a composite() that grew it.
  gpu::Buffer& output() const;
  int output_tiles() const;  // slots in output()
  // Device memory held, which the device counts in K1's tile budget where it is RAM (gpu.h): integrated GPUs (the Iris
  // Xe) take it from there. What a store held before it grew is freed once the copy ran, not kept for reuse.
  size_t bytes() const;

  struct Stats {  // of the last composite()
    int uploaded = 0;    // tiles into the atlas: layers' and the stroke buffer's
    int composited = 0;  // output tiles on the GPU
    int cached = 0;      // output tiles the cache had
    int below = 0;       // of composited, those that started from the cache below the stroke's layer
    int layers = 0;      // layer steps the GPU composited: the layer tiles it read, solid ones too
    int cpu = 0;         // output tiles from composite_tile()
    int dispatches = 0;
  };
  Stats stats() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace pb
