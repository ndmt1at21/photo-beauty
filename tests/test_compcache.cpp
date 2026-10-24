// K30's cache of the composite (compcache.h): composite_cached() and composite_grid() give composite_tile()'s tiles to
// the bit at levels 0 to 2 (random documents in both depths, RGB and grey, as test_gpu_composite's), on the CPU and the
// GPU (lavapipe: lots of kGridBatch, a stack deeper than its atlas left to the CPU), out of the cache the second time
// (nothing composited again); an edit composites again only the tiles it changed, undo and choosing layers nothing; a
// stroke's composite is not kept, a preview's is; the cache's tiles are the store's (tile_stats(), pushed out and
// back, let go of when the store asks); the prefill waits for the document to stand still, fills the view's tiles
// first, as many as the cache keeps, and stops when the document changes.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <random>
#include <thread>
#include <utility>
#include <vector>

#include "check.h"
#include "compcache.h"
#include "composite.h"
#include "mip.h"
#include "tilecache.h"

using namespace pb;

namespace {

constexpr int S = kTileSize;
constexpr size_t kPixels = size_t(S) * S;
constexpr size_t kOutBytes = kPixels * 8;
constexpr Blend kModes[] = {Blend::Normal,     Blend::Darken,  Blend::Multiply,   Blend::ColorBurn,
                            Blend::Lighten,    Blend::Screen,  Blend::ColorDodge, Blend::Overlay,
                            Blend::SoftLight,  Blend::LinearLight, Blend::Difference};
constexpr Format kFormats[] = {{Depth::U16, 4}, {Depth::U8, 4}, {Depth::U16, 2}, {Depth::U8, 2}};
using Clock = std::chrono::steady_clock;
using std::chrono::milliseconds;
using std::chrono::seconds;

// A sample of depth d, the edges 0 and max often.
uint16_t sample(std::mt19937& rng, Depth d) {
  const unsigned m = unsigned(max_value(d)), r = rng() % 8;
  return uint16_t(r == 0 ? 0 : r == 1 ? m : rng() % (m + 1));
}
TilePtr make_tile(Format f, const std::function<Pixel(size_t)>& px) {
  const std::shared_ptr<Tile> t = Tile::make(f);
  const WritePin w = t->write();
  for (size_t i = 0; i < kPixels; i++) {
    const Pixel v = px(i);
    for (int c = 0; c < f.channels; c++)
      if (f.depth == Depth::U16)
        std::memcpy(w.data() + (i * size_t(f.channels) + size_t(c)) * 2, &v[size_t(c)], 2);
      else
        w.data()[i * size_t(f.channels) + size_t(c)] = uint8_t(v[size_t(c)]);
  }
  return t;
}
// A data tile of random samples, alpha `alpha` throughout unless negative.
TilePtr noise(std::mt19937& rng, Format f, int alpha = -1) {
  return make_tile(f, [&](size_t) {
    Pixel v{};
    for (int c = 0; c < f.channels; c++)
      v[size_t(c)] = alpha >= 0 && c == f.channels - 1 ? uint16_t(alpha) : sample(rng, f.depth);
    return v;
  });
}
LayerPtr pixel(LayerId id, std::shared_ptr<const TileGrid> px, Blend b = Blend::Normal, float opacity = 1,
               float fill = 1) {
  auto l = std::make_shared<Layer>();
  l->id = id;
  l->props.blend = b, l->props.opacity = opacity, l->props.fill = fill;
  l->pixels = std::move(px);
  return l;
}
LayerPtr group(LayerId id, std::vector<LayerPtr> children, Blend b, float opacity = 1) {
  auto l = std::make_shared<Layer>();
  l->id = id, l->kind = Kind::Group;
  l->props.blend = b, l->props.opacity = opacity;
  l->children = std::move(children);
  return l;
}
DocPtr doc(std::vector<LayerPtr> layers, Format f, int w, int h) {
  auto root = std::make_shared<Layer>();
  root->kind = Kind::Group;
  root->children = std::move(layers);
  auto d = std::make_shared<DocState>();
  d->width = w, d->height = h, d->mode = f.channels == 4 ? Mode::Rgb : Mode::Gray, d->depth = f.depth;
  d->root = std::move(root);
  d->next_id = 1000;
  return d;
}
std::vector<TileXY> tiles_of(TileRect r) {
  std::vector<TileXY> t;
  for (int y = r.y0; y < r.y1; y++)
    for (int x = r.x0; x < r.x1; x++) t.push_back({x, y});
  return t;
}
bool inside(TileXY t, TileRect r) { return t.x >= r.x0 && t.x < r.x1 && t.y >= r.y0 && t.y < r.y1; }
bool operator==(TileRect a, TileRect b) { return a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1; }

// Both null, or alike: solid of one value, or data of the same pixels.
bool same(const TilePtr& a, const TilePtr& b) {
  if (!a || !b) return !a && !b;
  if (a->format() != b->format() || a->is_solid() != b->is_solid()) return false;
  if (a->is_solid()) return a->value() == b->value();
  const Pin p = a->pin(), q = b->pin();
  return std::memcmp(p.data(), q.data(), kPixels * size_t(a->format().bytes())) == 0;
}
// A composite tile's samples, zeros where null (as the GPU's slot -1).
[[maybe_unused]] std::vector<uint16_t> samples(const TilePtr& t) {
  std::vector<uint16_t> px(kPixels * 4);
  if (t) {
    TileGrid g({Depth::U16, 4});
    g.set(0, 0, t);
    g.read_rect(0, 0, S, S, reinterpret_cast<uint8_t*>(px.data()), S * 8);
  }
  return px;
}
TilePtr tile_at(const TileGrid& g, TileXY t) {
  const auto it = g.tiles().find(t);
  return it == g.tiles().end() ? nullptr : it->second;
}
// Grid g's tile at t against composite_tile()'s: the same where the GPU rounds as the CPU (x86-64: IEEE single), else
// within a level of 8 bits (G1), as test_gpu_composite_matches_cpu.
bool matches(const RenderInput& in, int level, const TileGrid& g, TileXY t) {
  const TilePtr want = composite_tile(in, level, t.x, t.y), got = tile_at(g, t);
#if defined(__x86_64__) || defined(_M_X64)
  return same(got, want);
#else
  const std::vector<uint16_t> a = samples(got), b = samples(want);
  for (size_t i = 0; i < a.size(); i++)
    if (std::abs(int(a[i]) - int(b[i])) > 257) return false;
  return true;
#endif
}

// Random layers over the canvas's 3 x 2 tiles of 600 x 300 (its last column and row in part) and two tiles past it:
// every P0 mode and a P1 one (Normal till B31) at random opacities and fills, of data, solid and missing tiles; an
// isolated group at 80 % and a Pass Through one at 60 %, a hidden layer, one at 0 %.
std::vector<LayerPtr> random_layers(std::mt19937& rng, Format f) {
  const auto value = [&] {
    Pixel v{};
    for (int c = 0; c < f.channels; c++) v[size_t(c)] = sample(rng, f.depth);
    return v;
  };
  const auto grid = [&] {
    auto g = std::make_shared<TileGrid>(f);
    for (const TileXY t : tiles_of({-1, 0, 4, 2})) {
      const unsigned r = rng() % 8;
      if (r < 5) g->set(t.x, t.y, make_tile(f, [&](size_t) { return value(); }));
      if (r == 5) g->set(t.x, t.y, Tile::solid(f, value()));
    }
    return std::shared_ptr<const TileGrid>(std::move(g));
  };
  const auto opacity = [&] { return rng() % 4 ? 0.05f + 0.95f * float(rng() % 1000) / 999 : 1.f; };
  std::vector<LayerPtr> layers;
  LayerId id = 1;
  for (const Blend b : kModes) layers.push_back(pixel(id++, grid(), b, opacity(), opacity()));
  layers.push_back(pixel(id++, grid(), Blend::HardLight, opacity()));
  layers.push_back(group(id, {pixel(id + 1, grid(), Blend::Multiply), pixel(id + 2, grid(), Blend::Screen, 0.7f)},
                         Blend::Overlay, 0.8f));
  id += 3;
  layers.push_back(group(id, {pixel(id + 1, grid(), Blend::Darken)}, Blend::PassThrough, 0.6f));
  id += 2;
  auto hidden = std::make_shared<Layer>(*pixel(id++, grid(), Blend::Difference));
  hidden->props.visible = false;
  layers.push_back(hidden);
  layers.push_back(pixel(id++, grid(), Blend::Screen, 0));
  std::shuffle(layers.begin(), layers.end(), rng);
  return layers;
}
constexpr int kW = 2 * S + 88, kH = S + 44;

// Tiles that composite to something: those whose plan has steps.
size_t planned(const RenderInput& in, int level, const std::vector<TileXY>& tiles) {
  size_t n = 0;
  for (const TileXY t : tiles) n += !composite_plan(in, level, t.x, t.y).steps.empty();
  return n;
}

// Until ok() or `most` passes: whether ok().
bool until(const std::function<bool()>& ok, Clock::duration most) {
  const auto t0 = Clock::now();
  while (!ok()) {
    if (Clock::now() - t0 > most) return false;
    std::this_thread::sleep_for(milliseconds(2));
  }
  return true;
}

// The GPU composite_grid() uses, for a test's scope: left before the device goes.
struct UseGpu {
  std::mutex lock;
  explicit UseGpu(gpu::Device& dev) { set_composite_gpu(&dev, &lock); }
  ~UseGpu() { set_composite_gpu(nullptr, nullptr); }
};

}  // namespace

// composite_cached() is composite_tile() to the bit everywhere, past the canvas too, at levels 0 to 2, in both depths,
// RGB and grey: once composited, the same tile from the cache, nothing composited again; composite_grid() gives the
// cache's tiles, none past the canvas. A stroke's composite is the moment's, every time; a preview's is kept.
TEST(test_compcache) {
  CHECK(canvas_tiles(*doc({}, kFormats[0], kW, kH), 0) == (TileRect{0, 0, 3, 2}));
  CHECK(canvas_tiles(*doc({}, kFormats[0], kW, kH), 1) == (TileRect{0, 0, 2, 1}));
  CHECK(canvas_tiles(*doc({}, kFormats[0], kW, kH), 2) == (TileRect{0, 0, 1, 1}));
  CHECK(canvas_tiles(*doc({}, kFormats[0], 1, 1), 31) == (TileRect{0, 0, 1, 1}));
  CHECK(canvas_tiles(*doc({}, kFormats[0], 0, 5), 0) == (TileRect{}));
  std::mt19937 rng(41);
  for (const Format f : kFormats) {
    const RenderInput in{doc(random_layers(rng, f), f, kW, kH)};
    clear_composites();
    for (int level = 0; level <= 2; level++) {
      const TileRect c = canvas_tiles(*in.doc, level), around{c.x0 - 1, c.y0 - 1, c.x1 + 1, c.y1 + 1};
      const std::vector<TileXY> tiles = tiles_of(around);
      const size_t n = planned(in, level, tiles);
      const uint64_t built = composites_built();
      std::vector<TilePtr> first;
      for (const TileXY t : tiles) {
        first.push_back(composite_cached(in, level, t.x, t.y));
        CHECK(same(first.back(), composite_tile(in, level, t.x, t.y)));
        CHECK(inside(t, c) || !first.back());
      }
      CHECK(n > 0 && composites_built() == built + n);
      for (size_t i = 0; i < tiles.size(); i++) CHECK(composite_cached(in, level, tiles[i].x, tiles[i].y) == first[i]);
      const TileGrid g = composite_grid(in, level, around);
      CHECK(composites_built() == built + n && g.format() == (Format{Depth::U16, 4}));
      for (size_t i = 0; i < tiles.size(); i++) CHECK(g.get(tiles[i].x, tiles[i].y) == first[i].get());
      std::printf("test_compcache: %s %d channels, level %d: %zu tiles, %zu composited\n",
                  f.depth == Depth::U16 ? "U16" : "U8", f.channels, level, tiles.size(), n);
    }
  }

  // The same layers on a canvas 10 pixels narrower: its last column of tiles another composite.
  const Format f{Depth::U16, 4};
  const std::vector<LayerPtr> layers = random_layers(rng, f);
  const DocPtr d = doc(layers, f, kW, kH), narrower = doc(layers, f, kW - 10, kH);
  for (const TileXY t : tiles_of({0, 0, 3, 2})) {
    const TilePtr wide = composite_cached({d}, 0, t.x, t.y);
    const uint64_t built = composites_built();
    const TilePtr cut = composite_cached({narrower}, 0, t.x, t.y);
    CHECK(same(cut, composite_tile({narrower}, 0, t.x, t.y)));
    CHECK((composites_built() == built) == (t.x < 2) && (cut == wide) == (t.x < 2));
  }

  // A stroke on layer 1, over its tile (0, 0) and the next: each call composites it afresh, the same as
  // composite_tile(); the document's own tiles stay in the cache.
  auto buffer = std::make_shared<TileGrid>(Format{Depth::U16, 1});
  buffer->set(0, 0, make_tile({Depth::U16, 1}, [&](size_t) { return Pixel{sample(rng, Depth::U16)}; }));
  buffer->set(1, 0, Tile::solid({Depth::U16, 1}, {40000}));
  const RenderInput stroke{d, StrokeOverlay{1, buffer, {0.9f, 0.2f, 0.4f}, 0.7f, false}};
  const TilePtr plain = composite_cached({d}, 0, 0, 0);
  for (int i = 0; i < 2; i++) {
    const uint64_t built = composites_built();
    CHECK(same(composite_cached(stroke, 0, 0, 0), composite_tile(stroke, 0, 0, 0)));
    const TileGrid g = composite_grid(stroke, 0, {0, 0, 2, 1});
    CHECK(matches(stroke, 0, g, {0, 0}) && matches(stroke, 0, g, {1, 0}));
    CHECK(composites_built() == built + 3);
  }
  CHECK(composite_cached({d}, 0, 0, 0) == plain && !same(plain, composite_tile(stroke, 0, 0, 0)));
  // A filter's preview of layer 2 at level 0: tile (1, 0) another, kept as any.
  auto tiles = std::make_shared<TileGrid>(f);
  tiles->set(1, 0, make_tile(f, [&](size_t) { return Pixel{sample(rng, f.depth), 0, 30000, 50000}; }));
  const RenderInput preview{d, {}, LayerPreview{2, 0, tiles}};
  const uint64_t built = composites_built();
  const TilePtr shown = composite_cached(preview, 0, 1, 0);
  CHECK(same(shown, composite_tile(preview, 0, 1, 0)) && !same(shown, composite_tile({d}, 0, 1, 0)));
  CHECK(composite_cached(preview, 0, 1, 0) == shown && composites_built() == built + 1);
}

// An edit composites again the tiles it changed only: a layer's tile, that composite tile at level 0 and the one over
// it at level 1; a layer's opacity, mode or visibility, those it has a step in; a group's, those of its layers. Undo
// finds its tiles in the cache; choosing layers changes no tile.
TEST(test_compcache_edits) {
  std::mt19937 rng(43);
  const Format f{Depth::U16, 4};
  const auto data = [&] { return noise(rng, f); };
  // 4 x 3 tiles of data in every layer but 5 (two tiles): two layers, an isolated group of two at 80 %, a Pass Through
  // one at 60 % of one.
  const auto grid = [&](TileRect r) {
    auto g = std::make_shared<TileGrid>(f);
    for (const TileXY t : tiles_of(r)) g->set(t.x, t.y, data());
    return std::shared_ptr<const TileGrid>(std::move(g));
  };
  const TileRect all{0, 0, 4, 3};
  const DocPtr d0 = doc({pixel(1, grid(all)), pixel(2, grid(all), Blend::Multiply, 0.9f),
                         group(3,
                               {pixel(4, grid(all), Blend::Screen), pixel(5, grid({1, 1, 3, 2}), Blend::Overlay, 0.5f)},
                               Blend::Normal, 0.8f),
                         group(6, {pixel(7, grid(all), Blend::SoftLight)}, Blend::PassThrough, 0.6f)},
                        f, 4 * S, 3 * S);
  const TileRect all1 = canvas_tiles(*d0, 1);  // 2 x 2 (512 x 384)
  clear_composites();
  const auto edit = [&](const DocPtr& d, size_t want0, size_t want1) {  // composites at levels 0 and 1, then checked
    const uint64_t built = composites_built();
    const TileGrid g0 = composite_grid({d}, 0, all);
    const size_t at0 = size_t(composites_built() - built);
    const TileGrid g1 = composite_grid({d}, 1, all1);
    const size_t at1 = size_t(composites_built() - built) - at0;
    for (const TileXY t : tiles_of(all)) CHECK(matches({d}, 0, g0, t));
    for (const TileXY t : tiles_of(all1)) CHECK(matches({d}, 1, g1, t));
    std::printf("test_compcache_edits: %zu, %zu composited (%zu, %zu)\n", at0, at1, want0, want1);
    return at0 == want0 && at1 == want1;
  };
  CHECK(edit(d0, 12, 4));
  CHECK(edit(d0, 0, 0));
  // Layer 2's tile (2, 1): that tile, and (1, 0) at level 1.
  auto g2 = std::make_shared<TileGrid>(*find_layer(*d0, 2)->pixels);
  g2->set(2, 1, data());
  const DocPtr d1 = run(*d0, SetPixels{2, g2});
  CHECK(edit(d1, 1, 1));
  CHECK(edit(d0, 0, 0));  // undo
  CHECK(edit(d1, 0, 0));  // redo
  CHECK(edit(run(*d1, SelectLayers{{4, 7}}), 0, 0));
  // Layer 5's opacity: its two tiles, and the one they lie in at level 1.
  LayerProps p = find_layer(*d1, 5)->props;
  p.opacity = 0.3f;
  const DocPtr d2 = run(*d1, SetProps{5, p});
  CHECK(edit(d2, 2, 2));  // level 1: 5's tiles (1, 1), (2, 1) lie under (0, 0) and (1, 0)
  p.blend = Blend::Difference;
  CHECK(edit(run(*d2, SetProps{5, p}), 2, 2));
  p.visible = false;
  CHECK(edit(run(*d2, SetProps{5, p}), 2, 2));
  // Group 3's opacity, layer 1's mode: every tile.
  LayerProps q = find_layer(*d2, 3)->props;
  q.opacity = 0.5f;
  CHECK(edit(run(*d2, SetProps{3, q}), 12, 4));
  LayerProps r = find_layer(*d2, 1)->props;
  r.blend = Blend::Darken;
  CHECK(edit(run(*d2, SetProps{1, r}), 12, 4));
}

// The cache's tiles are the store's: raw pixels in tile_stats() once composited (the caller's grid dropped, the cache
// still holds them), given back when the cache lets go; pushed out to zstd or scratch with the others nothing pins,
// read back when pinned, the same pixels, nothing composited again.
TEST(test_compcache_store) {
  std::mt19937 rng(47);
  const Format f{Depth::U16, 4};
  const auto grid = [&] {
    auto g = std::make_shared<TileGrid>(f);
    for (const TileXY t : tiles_of({0, 0, 3, 2}))
      g->set(t.x, t.y, noise(rng, f, 65535));
    return std::shared_ptr<const TileGrid>(std::move(g));
  };
  const RenderInput in{doc({pixel(1, grid()), pixel(2, grid(), Blend::Overlay, 0.5f)}, f, 3 * S, 2 * S)};
  clear_composites();
  const TileStats s0 = tile_stats();
  {
    const TileGrid g = composite_grid(in, 0, canvas_tiles(*in.doc, 0));
    size_t data = 0;
    for (const auto& [at, t] : g.tiles()) data += !t->is_solid();
    CHECK(data == 6 && tile_stats().raw == s0.raw + data * kOutBytes);
  }
  CHECK(tile_stats().raw == s0.raw + 6 * kOutBytes);
  clear_composites();
  CHECK(tile_stats().raw == s0.raw);
  uint64_t built = composites_built();
  composite_grid(in, 0, canvas_tiles(*in.doc, 0));
  CHECK(composites_built() == built + 6 && tile_stats().raw == s0.raw + 6 * kOutBytes);
  trim_tiles();  // every tile nothing pins out of RAM, the cache's too
  const TileStats s1 = tile_stats();
  CHECK(s1.raw == s1.pinned && s1.compressed + s1.scratch > 0);
  built = composites_built();
  for (const TileXY t : tiles_of({0, 0, 3, 2})) {
    const TilePtr back = composite_cached(in, 0, t.x, t.y);
    CHECK(same(back, composite_tile(in, 0, t.x, t.y)));
  }
  CHECK(composites_built() == built);
  clear_composites();
}

// The store over budget with nothing left in RAM to push out (a user charging past 90 %) asks the cache to let go, as
// mip.cpp's: of entries nothing else holds, least recently used first, about what it wants (10 % of the budget and 64
// KB: 13 tiles of data), the one held elsewhere kept.
TEST(test_compcache_release) {
  std::mt19937 rng(61);
  const Format f{Depth::U16, 4};
  auto g = std::make_shared<TileGrid>(f);
  for (const TileXY t : tiles_of({0, 0, 4, 4}))
    g->set(t.x, t.y, noise(rng, f, 65535));
  const RenderInput in{doc({pixel(1, g)}, f, 4 * S, 4 * S)};
  const size_t old = tile_budget(), budget = size_t(64) << 20;
  clear_composites();
  clear_mips();
  set_tile_budget(budget);
  std::vector<TileXY> order = tiles_of({0, 0, 4, 4});
  std::shuffle(order.begin(), order.end(), rng);
  std::vector<TilePtr> made;
  for (const TileXY t : order) made.push_back(composite_cached(in, 0, t.x, t.y));  // oldest first
  const TilePtr held = made[0];
  made.clear();
  trim_tiles();  // nothing of it in RAM, nor of what other tests left
  const size_t base = tile_stats().charged;
  CHECK(base < budget / 10 * 9);
  const size_t over = budget / 10 * 9 - base + 64 * 1024;
  BudgetUser* self = nullptr;
  BudgetUser user([&](size_t) {
    self->charge(-ptrdiff_t(over));
    return over;
  });
  self = &user;
  user.charge(ptrdiff_t(over));
  CHECK(until([&] { return tile_stats().charged <= base; }, seconds(30)));
  set_tile_budget(old);
  for (size_t i = 0; i < order.size(); i++) {
    const uint64_t built = composites_built();
    const TilePtr t = composite_cached(in, 0, order[i].x, order[i].y);
    CHECK((composites_built() == built) == (i == 0 || i >= 14));
    CHECK(i || t == held);
  }
  clear_composites();
}

// On the GPU (lavapipe): composite_tile()'s tiles to the bit (x86-64) in both depths, RGB and grey, levels 0 to 2, in
// device memory of its own; out of the cache the second time; more tiles than a lot (kGridBatch); a stack deeper than
// its atlas holds on the CPU; a stroke's afresh each time; an edit only its tile.
TEST(test_compcache_gpu) {
  auto dev = pb::test::gpu_device();
  if (!dev) return;
  const UseGpu use(*dev);
  std::mt19937 rng(53);
  const size_t device = dev->held().device;
  for (const Format f : kFormats) {
    const RenderInput in{doc(random_layers(rng, f), f, kW, kH)};
    clear_composites();
    for (int level = 0; level <= 2; level++) {
      const TileRect c = canvas_tiles(*in.doc, level), around{c.x0 - 1, c.y0 - 1, c.x1 + 1, c.y1 + 1};
      const size_t n = planned(in, level, tiles_of(around));
      const uint64_t built = composites_built();
      const TileGrid g = composite_grid(in, level, around);
      CHECK(composites_built() == built + n);
      for (const TileXY t : tiles_of(around)) CHECK(matches(in, level, g, t));
      const TileGrid again = composite_grid(in, level, around);
      CHECK(composites_built() == built + n && again.tiles().size() == g.tiles().size());
      for (const auto& [at, t] : g.tiles()) CHECK(again.get(at.x, at.y) == t.get());
    }
    CHECK(dev->held().device > device);  // the GPU's work
  }

  // 6 x 5 tiles of three 8-bit layers: two lots, the second in part; then one tile changed: it alone.
  const Format f{Depth::U8, 4};
  const auto data = [&] { return noise(rng, f); };
  const TileRect all{0, 0, 6, 5};
  std::vector<LayerPtr> layers;
  for (LayerId id = 1; id <= 3; id++) {
    auto g = std::make_shared<TileGrid>(f);
    for (const TileXY t : tiles_of(all)) g->set(t.x, t.y, data());
    layers.push_back(pixel(id, g, kModes[size_t(id) * 3], 0.8f));
  }
  const DocPtr d = doc(layers, f, 6 * S, 5 * S);
  CHECK(tiles_of(all).size() > size_t(kGridBatch));
  uint64_t built = composites_built();
  TileGrid g = composite_grid({d}, 0, all);
  CHECK(composites_built() == built + 30);
  for (const TileXY t : tiles_of(all)) CHECK(matches({d}, 0, g, t));
  auto g2 = std::make_shared<TileGrid>(*layers[1]->pixels);
  g2->set(4, 3, data());
  const DocPtr d2 = run(*d, SetPixels{2, g2});
  built = composites_built();
  g = composite_grid({d2}, 0, all);
  CHECK(composites_built() == built + 1);
  for (const TileXY t : tiles_of(all)) CHECK(matches({d2}, 0, g, t));
  // A stroke on layer 3: composited afresh each time, on the GPU too.
  auto buffer = std::make_shared<TileGrid>(Format{Depth::U16, 1});
  buffer->set(2, 2, make_tile({Depth::U16, 1}, [&](size_t) { return Pixel{sample(rng, Depth::U16)}; }));
  const RenderInput stroke{d2, StrokeOverlay{3, buffer, {0.1f, 0.6f, 0.3f}, 0.9f, false}};
  for (int i = 0; i < 2; i++) {
    built = composites_built();
    g = composite_grid(stroke, 0, {1, 1, 4, 4});
    CHECK(composites_built() == built + 9);
    for (const TileXY t : tiles_of({1, 1, 4, 4})) CHECK(matches(stroke, 0, g, t));
  }

  // 40 layers of data on one tile, more than the atlas holds (2 x kGridBatch 16-bit tiles): on the CPU, the same.
  const Format f16{Depth::U16, 4};
  std::vector<LayerPtr> deep;
  for (LayerId id = 1; id <= 40; id++) {
    auto t = std::make_shared<TileGrid>(f16);
    t->set(0, 0, noise(rng, f16, 20000));
    deep.push_back(pixel(id, t, kModes[id % 11], 0.5f));
  }
  const RenderInput tall{doc(deep, f16, S, S)};
  built = composites_built();
  g = composite_grid(tall, 0, {0, 0, 1, 1});
  CHECK(composites_built() == built + 1 && same(tile_at(g, {0, 0}), composite_tile(tall, 0, 0, 0)));
  clear_composites();
}

// The prefill: nothing while the document changes (until a second after), then all of level 0 (another `still`: once
// that passes, not before); with room for six tiles, the view's first, then the canvas's from its top left; a change
// stops the job at its next tile, the new state filled once it stands still.
TEST(test_compcache_prefill) {
  std::mt19937 rng(59);
  const Format f{Depth::U16, 4};
  // Solid tiles throughout, a value each: composites to make, no pixels to hold (the budget goes small below).
  const auto layers = [&](int nx, int ny, int n) {
    std::vector<LayerPtr> l;
    for (LayerId id = 1; id <= LayerId(n); id++) {
      auto g = std::make_shared<TileGrid>(f);
      for (const TileXY t : tiles_of({0, 0, nx, ny}))
        g->set(t.x, t.y,
               Tile::solid(f, {uint16_t(rng()), uint16_t(rng()), uint16_t(rng()), uint16_t(20000 + rng() % 40000)}));
      l.push_back(pixel(id, g, kModes[id % 11], 0.9f));
    }
    return l;
  };
  const DocPtr d = doc(layers(8, 6, 3), f, 8 * S, 6 * S - 10);
  const TileRect all = canvas_tiles(*d, 0), view{2, 1, 5, 3};
  const auto past = Clock::now() - seconds(2);
  {
    clear_composites();
    Prefill p;
    const uint64_t built = composites_built();
    p.set(d, Clock::now(), view);
    std::this_thread::sleep_for(milliseconds(100));
    CHECK(composites_built() == built && !p.wait(milliseconds(0)));  // a second yet to pass
    p.set(d, past, view);
    CHECK(p.wait(seconds(60)) && composites_built() == built + 48);
    composite_grid({d}, 0, all);
    CHECK(composites_built() == built + 48);
    p.set(d, past, view);  // the same again: nothing to do
    CHECK(p.wait(milliseconds(0)));
  }
  {
    clear_composites();
    Prefill p(milliseconds(200));
    const auto t0 = Clock::now();
    p.set(d, t0, {0, 0, 0, 0});
    CHECK(p.wait(seconds(60)) && Clock::now() - t0 >= milliseconds(200));
    const uint64_t built = composites_built();
    composite_grid({d}, 0, all);
    CHECK(composites_built() == built);
  }
  {
    // Room for six tiles: the view's six.
    const size_t budget = tile_budget();
    set_tile_budget(4 * 6 * (kOutBytes + 1024));
    clear_composites();
    {
      Prefill p;
      p.set(d, past, view);
      CHECK(p.wait(seconds(60)));
    }
    set_tile_budget(budget);
    for (const TileXY t : tiles_of(all)) {
      const uint64_t built = composites_built();
      composite_cached({d}, 0, t.x, t.y);
      CHECK((composites_built() == built) == inside(t, view));
    }
    // The view past the canvas's top left edge: its two tiles on the canvas, then the canvas's first four.
    set_tile_budget(4 * 6 * (kOutBytes + 1024));
    clear_composites();
    {
      Prefill p;
      p.set(d, past, {-3, -2, 1, 2});
      CHECK(p.wait(seconds(60)));
    }
    set_tile_budget(budget);
    for (const TileXY t : tiles_of(all)) {
      const uint64_t built = composites_built();
      composite_cached({d}, 0, t.x, t.y);
      CHECK((composites_built() == built) == (inside(t, {0, 0, 1, 2}) || inside(t, {1, 0, 5, 1})));
    }
  }
  {
    // A change stops the job: 48 x 32 tiles of four layers, its job running; then the new state, once it stands still.
    const DocPtr big = doc(layers(48, 32, 4), f, 48 * S, 32 * S);
    const size_t n = 48 * 32;
    clear_composites();
    Prefill p(std::chrono::hours(1));  // only a change long past starts it
    const uint64_t built = composites_built();
    p.set(big, past - std::chrono::hours(1), {0, 0, 48, 32});
    CHECK(until([&] { return composites_built() > built; }, seconds(60)));
    LayerProps q = find_layer(*big, 2)->props;
    q.opacity = 0.4f;
    const DocPtr big2 = run(*big, SetProps{2, q});
    p.set(big2, Clock::now(), {0, 0, 48, 32});
    uint64_t now = composites_built();
    CHECK(until([&] {
            std::this_thread::sleep_for(milliseconds(100));
            const uint64_t was = now;
            now = composites_built();
            return now == was;
          }, seconds(30)));
    std::printf("test_compcache_prefill: stopped after %zu of %zu tiles\n", size_t(now - built), n);
    CHECK(now - built < n && !p.wait(milliseconds(0)));
    p.set(big2, past - std::chrono::hours(1), {0, 0, 48, 32});
    CHECK(p.wait(seconds(120)));
    const uint64_t filled = composites_built();
    composite_grid({big2}, 0, {0, 0, 48, 32});
    CHECK(composites_built() == filled);
    p.set(nullptr, Clock::now(), {});  // none: nothing to wait for
    CHECK(p.wait(milliseconds(0)));
  }
  clear_composites();
}

// view_tiles(): the tiles of a level under a view, turned or not; none for an empty one.
TEST(test_compcache_view_tiles) {
  View v;
  v.x = 2000, v.y = 1500, v.zoom = 1, v.width = 1000, v.height = 600;  // document pixels 1500..2500 x 1200..1800
  CHECK(view_tiles(v, 0) == (TileRect{5, 4, 10, 8}));
  CHECK(view_tiles(v, 1) == (TileRect{2, 2, 5, 4}));  // 750..1250 x 600..900
  v.angle = 90;                                        // 1700..2300 x 1000..2000
  CHECK(view_tiles(v, 0) == (TileRect{6, 3, 9, 8}));
  v.angle = 0, v.zoom = 2, v.dpr = 2;  // 1 logical pixel a document pixel, as at 100 %
  CHECK(view_tiles(v, 0) == (TileRect{5, 4, 10, 8}));
  v.zoom = 0.25, v.dpr = 1, v.x = 0, v.y = 0;  // -2000..2000 x -1200..1200
  CHECK(view_tiles(v, 0) == (TileRect{-8, -5, 8, 5}));
  v.width = 0;
  CHECK(view_tiles(v, 0) == (TileRect{}));
}
