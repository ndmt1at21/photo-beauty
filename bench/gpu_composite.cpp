// pb-bench gpu-composite: a 1920 x 1080 frame of a document shaped as D1 composited on the GPU (GpuComposite), fit to
// the screen (mip level 2, its mips built first) and at 100 % (the centre, level 0): the first frame, every layer tile
// it shows going up (the fit one also allocating the atlas), and the median of 9 composites out of the atlas (an
// opacity changes each time, as a slider drags), wall time until the GPU is done, in ms. On the device there is:
// lavapipe in WSL, the Iris Xe from the mingw build.
#include <algorithm>
#include <cstdio>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "bench.h"
#include "gpu_composite.h"
#include "mip.h"

using namespace pb;

namespace {

constexpr Format kFormat{Depth::U16, 4};
constexpr int kWidth = 8256, kHeight = 5504;

// D1 (plan.md: 8256 x 5504, 16 bits, 15 layers) until E1 makes it: the base and frequency separation's low (Normal) and
// high (Linear Light) over all of it; 5 painted layers over about 20 % of its tiles (heal, clone, dodge and burn:
// Normal, Normal, Soft Light, Overlay, Normal at 60 %), 5 pixel layers standing for the adjustment layers over about
// 30 % (Soft Light, Overlay, Multiply, Screen, Multiply at 50 %), the text over one tile; its group a Pass Through, as
// B2 composites flat lists (B3 adds groups). Random pixels, about 2 GB.
DocPtr d1() {
  std::mt19937_64 rng(1);
  const int nx = (kWidth + kTileSize - 1) / kTileSize, ny = (kHeight + kTileSize - 1) / kTileSize;
  // cover: the share of the tiles with pixels, 0 for the one at the centre only
  const auto layer = [&](LayerId id, double cover, Blend blend, float opacity, bool opaque) {
    auto g = std::make_shared<TileGrid>(kFormat);
    for (int ty = 0; ty < ny; ty++)
      for (int tx = 0; tx < nx; tx++) {
        if (cover ? rng() % 1000 >= cover * 1000 : tx != nx / 2 || ty != ny / 2) continue;
        const std::shared_ptr<Tile> t = Tile::make(kFormat);
        {
          const WritePin w = t->write();
          auto* p = reinterpret_cast<uint64_t*>(w.data());  // a pixel: 4 samples, alpha the top 16 bits
          for (int i = 0; i < kTileSize * kTileSize; i++) p[i] = rng() | (opaque ? uint64_t(0xffff) << 48 : 0);
        }
        g->set(tx, ty, t);
      }
    auto l = std::make_shared<Layer>();
    l->id = id;
    l->props.blend = blend, l->props.opacity = opacity;
    l->pixels = std::move(g);
    return LayerPtr(std::move(l));
  };
  auto retouch = std::make_shared<Layer>();
  retouch->id = 4, retouch->kind = Kind::Group;
  retouch->props.blend = Blend::PassThrough;
  retouch->children = {layer(5, 0.2, Blend::Normal, 1, false), layer(6, 0.2, Blend::Normal, 1, false),
                       layer(7, 0.2, Blend::SoftLight, 1, false), layer(8, 0.2, Blend::Overlay, 0.8f, false),
                       layer(9, 0.2, Blend::Normal, 0.6f, false)};
  auto root = std::make_shared<Layer>();
  root->kind = Kind::Group;
  root->children = {layer(1, 1, Blend::Normal, 1, true), layer(2, 1, Blend::Normal, 1, true),
                    layer(3, 1, Blend::LinearLight, 1, true), retouch,
                    layer(10, 0.3, Blend::SoftLight, 1, false), layer(11, 0.3, Blend::Overlay, 0.7f, false),
                    layer(12, 0.3, Blend::Multiply, 0.5f, false), layer(13, 0.3, Blend::Screen, 0.4f, false),
                    layer(14, 0.3, Blend::Multiply, 0.5f, false), layer(15, 0, Blend::Normal, 1, false)};
  auto d = std::make_shared<DocState>();
  d->width = kWidth, d->height = kHeight, d->depth = kFormat.depth, d->root = std::move(root);
  return d;
}

// The same document with frequency separation's low layer, over every tile, at another opacity: every composite tile
// anew out of the same layer tiles.
DocPtr with_opacity(const DocPtr& d, float opacity) {
  auto root = std::make_shared<Layer>(*d->root);
  auto low = std::make_shared<Layer>(*root->children[1]);
  low->props.opacity = opacity;
  root->children[1] = low;
  auto out = std::make_shared<DocState>(*d);
  out->root = std::move(root);
  return out;
}

void frame(snapone::gpu::Device& dev, GpuComposite& gc, const DocPtr& d, const char* name, int level, TileRect r) {
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

}  // namespace

BENCH(gpu_composite) {
  const auto dev = snapone::gpu::Device::create(true);
  if (!dev) return void(std::printf("gpu_composite: no Vulkan device\n"));
  std::printf("gpu_composite: %s, %s\n", dev->name().c_str(), dev->driver().c_str());
  const DocPtr d = d1();
  GpuComposite gc(*dev);
  // Fit to the screen: zoom 1080 / 5504 = 0.196, level 2, where D1 is 2064 x 1376, 9 x 6 tiles; their mips first, as
  // the canvas has them made before it shows them (K2's prefetch(), in a Visible job).
  const TileRect fit{0, 0, 9, 6};
  const auto t0 = pb::bench::Clock::now();
  const std::function<void(const Layer&)> mips = [&](const Layer& l) {
    if (l.pixels) prefetch(*l.pixels, 2, fit);
    for (const LayerPtr& c : l.children) mips(*c);
  };
  mips(*d->root);
  pb::bench::report("gpu_composite_d1_fit_mips", pb::bench::ms_since(t0), "ms");
  frame(*dev, gc, d, "fit", 2, fit);
  // 100 %: the 1920 x 1080 at the centre, columns 3168 to 5088, rows 2212 to 3292.
  frame(*dev, gc, d, "100", 0, {12, 8, 20, 13});
}
