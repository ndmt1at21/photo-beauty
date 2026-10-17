// pb-bench composite: one tile of a document shaped as D1 (plan.md: 16 bits, 15 layers, every one with pixels at the
// tile) composited on the CPU, the median of 21 runs; then the same with 1,000 small layers elsewhere, which it skips.
#include <algorithm>
#include <chrono>
#include <memory>
#include <random>
#include <vector>

#include "bench.h"
#include "render.h"

using namespace pb;

namespace {

LayerPtr layer(LayerId id, std::shared_ptr<const TileGrid> px, Blend b = Blend::Normal, float opacity = 1) {
  auto l = std::make_shared<Layer>();
  l->id = id;
  l->props.blend = b, l->props.opacity = opacity;
  l->pixels = std::move(px);
  return l;
}

double median_ms(const RenderInput& in) {
  std::vector<double> ms;
  for (int i = 0; i < 21; i++) {
    const auto t0 = std::chrono::steady_clock::now();
    const TilePtr t = composite_tile(in, 0, 0, 0);
    ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
  }
  std::nth_element(ms.begin(), ms.begin() + 10, ms.end());
  return ms[10];
}

}  // namespace

BENCH(composite) {
  constexpr Format f{Depth::U16, 4};
  std::mt19937 rng(1);
  const auto pixels = [&](bool opaque) {  // random, at tile (0, 0)
    const std::shared_ptr<Tile> t = Tile::make(f);
    {
      const WritePin w = t->write();
      auto* p = reinterpret_cast<uint16_t*>(w.data());
      for (int i = 0; i < kTileSize * kTileSize * 4; i++) p[i] = uint16_t(i % 4 == 3 && opaque ? 65535 : rng());
    }
    auto g = std::make_shared<TileGrid>(f);
    g->set(0, 0, t);
    return std::shared_ptr<const TileGrid>(std::move(g));
  };
  // D1's layers; its adjustment layers as pixel layers in other modes until P1 brings adjustments.
  auto retouch = std::make_shared<Layer>();  // heal, clone, dodge and burn: an isolated group
  retouch->id = 4, retouch->kind = Kind::Group;
  retouch->children = {layer(5, pixels(false)), layer(6, pixels(false)), layer(7, pixels(false), Blend::SoftLight),
                       layer(8, pixels(false), Blend::Overlay, 0.8f), layer(9, pixels(false), Blend::Normal, 0.6f)};
  auto root = std::make_shared<Layer>();
  root->kind = Kind::Group;
  root->children = {layer(1, pixels(true)), layer(2, pixels(true)), layer(3, pixels(true), Blend::LinearLight),  // frequency separation
                    retouch,
                    layer(10, pixels(false), Blend::Multiply, 0.5f), layer(11, pixels(false), Blend::Screen, 0.4f),
                    layer(12, pixels(false), Blend::ColorDodge, 0.3f), layer(13, pixels(false), Blend::Darken, 0.7f),
                    layer(14, pixels(false), Blend::Difference, 0.2f),
                    layer(15, pixels(false))};  // the text
  auto d = std::make_shared<DocState>();
  d->width = 8256, d->height = 5504, d->depth = Depth::U16, d->root = root;
  pb::bench::report("composite_tile_d1", median_ms({d}), "ms");

  auto elsewhere = std::make_shared<TileGrid>(f);
  elsewhere->set(9, 9, Tile::solid(f, {100, 200, 300, 65535}));
  auto more = std::make_shared<Layer>(*root);
  for (LayerId id = 16; id < 1016; id++) more->children.push_back(layer(id, elsewhere, Blend::Multiply));
  auto d2 = std::make_shared<DocState>(*d);
  d2->root = more;
  pb::bench::report("composite_tile_d1_1000_layers_elsewhere", median_ms({d2}), "ms");
}
