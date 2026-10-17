// pb-bench gpu-composite: a 1920 x 1080 frame of D1 (E1's make_d1()) composited on the GPU (GpuComposite), fit to the
// screen (mip level 2, its mips built first) and at 100 % (the centre, level 0): the first frame, every layer tile it
// shows going up (fit: also from a new GpuComposite, its device memory allocated), and the median of 9 composites out
// of the atlas (an opacity changes each time, as a slider drags); then painting at 100 %: a stroke on layer 8 (Burn,
// the middle of the stack) changing 20 of the frame's tiles each frame, its first frame keeping what lies below the
// layer and the median of 9 after it starting from there, and the same on layer 4 (High, in the isolated group); last
// the first frame of an opened JPEG (one 8-bit layer) fit to the screen, a new GpuComposite's start included. Wall
// time until the GPU is done, in ms. On the device there is: lavapipe in WSL, the Iris Xe from the mingw build.
#include <algorithm>
#include <cstdio>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "bench.h"
#include "docs.h"
#include "gpu_composite.h"
#include "mip.h"

using namespace pb;

namespace {

constexpr int kWidth = 8256, kHeight = 5504;  // D1's

// An opened 45 MP JPEG: one 8-bit layer of D1's size, opaque.
DocPtr photo() {
  std::mt19937_64 rng(3);
  auto g = std::make_shared<TileGrid>(Format{Depth::U8, 4});
  for (int ty = 0; ty * kTileSize < kHeight; ty++)
    for (int tx = 0; tx * kTileSize < kWidth; tx++) {
      const std::shared_ptr<Tile> t = Tile::make({Depth::U8, 4});
      {
        const WritePin w = t->write();
        auto* p = reinterpret_cast<uint64_t*>(w.data());  // 2 pixels a word
        for (int i = 0; i < kTileSize * kTileSize / 2; i++) p[i] = rng() | 0xff000000ff000000;
      }
      g->set(tx, ty, t);
    }
  auto l = std::make_shared<Layer>();
  l->id = 1, l->pixels = std::move(g);
  auto root = std::make_shared<Layer>();
  root->kind = Kind::Group, root->children = {l};
  auto d = std::make_shared<DocState>();
  d->width = kWidth, d->height = kHeight, d->root = std::move(root);
  return d;
}

// The same document with child i of the root changed by `change`.
DocPtr with_child(const DocPtr& d, size_t i, const std::function<void(Layer&)>& change) {
  auto root = std::make_shared<Layer>(*d->root);
  auto l = std::make_shared<Layer>(*root->children[i]);
  change(*l);
  root->children[i] = l;
  auto out = std::make_shared<DocState>(*d);
  out->root = std::move(root);
  return out;
}
// With the frequency separation group, over every tile, at another opacity: every composite tile anew out of the same
// layer tiles.
DocPtr with_opacity(const DocPtr& d, float opacity) {
  return with_child(d, 1, [&](Layer& l) { l.props.opacity = opacity; });
}

void frame(gpu::Device& dev, GpuComposite& gc, const DocPtr& d, const char* name, int level, TileRect r) {
  std::vector<TileXY> tiles;
  for (int ty = r.y0; ty < r.y1; ty++)
    for (int tx = r.x0; tx < r.x1; tx++) tiles.push_back({tx, ty});
  const auto run = [&](const DocPtr& doc) {
    const auto t0 = pb::bench::Clock::now();
    gc.composite({doc}, level, tiles);
    dev.flush();
    return pb::bench::ms_since(t0);
  };
  const std::string n = std::string("gpu_composite_d1_") + name;
  pb::bench::report((n + "_first").c_str(), run(d), "ms");
  pb::bench::report((n + "_uploaded").c_str(), gc.stats().uploaded, "tiles");
  std::vector<double> ms;
  for (int i = 0; i < 9; i++) ms.push_back(run(with_opacity(d, 0.5f + 0.01f * float(i))));
  std::nth_element(ms.begin(), ms.begin() + 4, ms.end());
  pb::bench::report(n.c_str(), ms[4], "ms");
  pb::bench::report((n + "_tiles").c_str(), gc.stats().composited, "tiles");  // composited by each of them
}

// Frames of a stroke on `layer` at 100 %: each one's stroke buffer has new tiles over `dirty` (as the stroke moves on
// there), the frame composites all of `tiles`.
void stroke(gpu::Device& dev, GpuComposite& gc, const DocPtr& d, LayerId layer, const char* name,
            const std::vector<TileXY>& tiles, const std::vector<TileXY>& dirty) {
  std::mt19937_64 rng(2);
  auto buffer = std::make_shared<TileGrid>(Format{Depth::U16, 1});
  const auto run = [&] {
    for (const TileXY t : dirty) {
      const std::shared_ptr<Tile> c = Tile::make({Depth::U16, 1});
      {
        const WritePin w = c->write();
        auto* p = reinterpret_cast<uint64_t*>(w.data());  // 4 samples a word
        for (int i = 0; i < kTileSize * kTileSize / 4; i++) p[i] = rng();
      }
      buffer->set(t.x, t.y, c);
    }
    const RenderInput in{d, StrokeOverlay{layer, std::make_shared<const TileGrid>(*buffer), {0.8f, 0.5f, 0.4f}, 0.7f, false}};
    const auto t0 = pb::bench::Clock::now();
    gc.composite(in, 0, tiles);
    dev.flush();
    return pb::bench::ms_since(t0);
  };
  const std::string n = std::string("gpu_composite_d1_") + name;
  pb::bench::report((n + "_first").c_str(), run(), "ms");
  pb::bench::report((n + "_first_layers").c_str(), gc.stats().layers, "tiles");  // layer tiles read
  std::vector<double> ms;
  for (int i = 0; i < 9; i++) ms.push_back(run());
  std::nth_element(ms.begin(), ms.begin() + 4, ms.end());
  pb::bench::report(n.c_str(), ms[4], "ms");
  pb::bench::report((n + "_layers").c_str(), gc.stats().layers, "tiles");
  pb::bench::report((n + "_tiles").c_str(), gc.stats().composited, "tiles");
}

}  // namespace

BENCH(gpu_composite) {
  const auto dev = gpu::Device::create(true);
  if (!dev) return void(std::printf("gpu_composite: no Vulkan device\n"));
  std::printf("gpu_composite: %s, %s\n", dev->name().c_str(), dev->driver().c_str());
  const DocPtr d = pb::bench::make_d1();
  // Fit to the screen: zoom 1080 / 5504 = 0.196, level 2, where D1 is 2064 x 1376, 9 x 6 tiles; their mips first, as
  // the canvas has them made before it shows them (K2's prefetch(), in a Visible job).
  const TileRect fit{0, 0, 9, 6};
  std::vector<TileXY> shown;
  for (int ty = fit.y0; ty < fit.y1; ty++)
    for (int tx = fit.x0; tx < fit.x1; tx++) shown.push_back({tx, ty});
  auto t0 = pb::bench::Clock::now();
  const std::function<void(const Layer&)> mips = [&](const Layer& l) {
    if (l.pixels) prefetch(*l.pixels, 2, fit);
    for (const LayerPtr& c : l.children) mips(*c);
  };
  mips(*d->root);
  pb::bench::report("gpu_composite_d1_fit_mips", pb::bench::ms_since(t0), "ms");
  {  // A new GpuComposite (its kernels: the driver compiles them, or has them cached) and its first frame: the device
     // memory for it allocated, its tiles up.
    t0 = pb::bench::Clock::now();
    GpuComposite once(*dev);
    pb::bench::report("gpu_composite_new", pb::bench::ms_since(t0), "ms");
    t0 = pb::bench::Clock::now();
    once.composite({d}, 2, shown);
    dev->flush();
    pb::bench::report("gpu_composite_d1_fit_cold", pb::bench::ms_since(t0), "ms");
  }
  // The same first frame from another, its buffers the ones the first let go of (Device::buffer() keeps them 10 s):
  // _first without allocating, cold - first what allocating took.
  GpuComposite gc(*dev);
  frame(*dev, gc, d, "fit", 2, fit);
  pb::bench::report("gpu_composite_d1_fit_device", double(gc.bytes()) / (1 << 20), "MB");
  // 100 %: the 1920 x 1080 at the centre, columns 3168 to 5088, rows 2212 to 3292.
  const TileRect centre{12, 8, 20, 13};
  frame(*dev, gc, d, "100", 0, centre);
  pb::bench::report("gpu_composite_d1_100_device", double(gc.bytes()) / (1 << 20), "MB");
  // Painting there: 20 of its 40 tiles, 5 x 4 in the middle.
  std::vector<TileXY> tiles, dirty;
  for (int ty = centre.y0; ty < centre.y1; ty++)
    for (int tx = centre.x0; tx < centre.x1; tx++) {
      tiles.push_back({tx, ty});
      if (tx >= 13 && tx < 18 && ty >= 9 && ty < 13) dirty.push_back({tx, ty});
    }
  stroke(*dev, gc, d, 8, "stroke", tiles, dirty);           // Burn, mid-stack
  stroke(*dev, gc, d, 4, "stroke_in_group", tiles, dirty);  // High, in the isolated frequency separation group
  pb::bench::report("gpu_composite_device", double(gc.bytes()) / (1 << 20), "MB");

  // The first frame of an opened JPEG, fit to the screen (its mips made first, as the canvas has them): a new
  // GpuComposite, its kernels and memory, the layer's tiles up.
  const DocPtr jpeg = photo();
  t0 = pb::bench::Clock::now();
  prefetch(*jpeg->root->children[0]->pixels, 2, fit);
  pb::bench::report("gpu_composite_jpeg_fit_mips", pb::bench::ms_since(t0), "ms");
  t0 = pb::bench::Clock::now();
  GpuComposite first(*dev);
  first.composite({jpeg}, 2, shown);
  dev->flush();
  pb::bench::report("gpu_composite_jpeg_fit_first", pb::bench::ms_since(t0), "ms");
  pb::bench::report("gpu_composite_jpeg_fit_device", double(first.bytes()) / (1 << 20), "MB");
}
