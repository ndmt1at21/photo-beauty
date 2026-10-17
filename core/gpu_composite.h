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
  // At most `bytes` of device memory (0: min(1 GB, Device::memory() / 4)), a quarter for composite tiles, the rest for
  // the atlas: the layer tiles of one format at a time (the document's), least recently used out first. The device
  // outlives it.
  explicit GpuComposite(gpu::Device&, size_t bytes = 0);
  ~GpuComposite();
  GpuComposite(const GpuComposite&) = delete;
  GpuComposite& operator=(const GpuComposite&) = delete;

  // Records the composite of `tiles` at mip level `level` of `in`, the pixels composite_tile() gives them out of the
  // same plan (composite_plan(), composite.h, which builds the mips it lacks: K2's prefetch() first for a level not
  // yet seen): for each its slot in output(), or -1 where nothing is there (transparent). Uploads the layer tiles the
  // atlas lacks, then dispatches once for the tiles not in the cache, which keeps a composite tile by the ids of the
  // tiles in it and its layers' modes and alphas; more only when the atlas cannot hold a frame's tiles at once. The
  // caller submits; what it records after reads the slots, which stay until the next call. Tiles whose plan holds a
  // group (Push, Pop) or the stroke composite on the CPU (composite_tile(), uploaded) until B3. Throws
  // std::invalid_argument as composite_plan() does, std::length_error for more tiles than output_tiles(). One thread at
  // a time, the device's.
  std::vector<int> composite(const RenderInput&, int level, const std::vector<TileXY>& tiles);

  // The composite tiles: slot i from byte i * kTileSize * kTileSize * 8 on, RGBA U16 rows as composite_tile()'s.
  gpu::Buffer& output() const;
  int output_tiles() const;  // slots in output()
  // Device memory held, which K1 counts in its tile budget: integrated GPUs (the Iris Xe) take it from RAM.
  // ponytail: counted for discrete GPUs too; VK_EXT_memory_budget once the HAL (gpu_vulkan.cpp) has it.
  size_t bytes() const;

  struct Stats {  // of the last composite()
    int uploaded = 0;    // layer tiles into the atlas
    int composited = 0;  // output tiles on the GPU
    int cached = 0;      // output tiles the cache had
    int cpu = 0;         // output tiles from composite_tile()
    int dispatches = 0;
  };
  Stats stats() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace pb
