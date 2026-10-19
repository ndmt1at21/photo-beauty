// The GPU composite (gpu_composite.h) against the CPU's, composite_tile() (B1): random flat documents in every P0 mode,
// both depths, RGB and grey, with data, solid and missing tiles, the canvas's edge, and the Color Burn edge where an
// ulp moves hundreds of levels; what the atlas uploads and the cache keeps; what goes to the CPU until B3. And the
// integer arithmetic of shaders/lib/blend.slang that makes the two agree, in C++, against the CPU's.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <random>
#include <stdexcept>
#include <thread>
#include <vector>

#include "check.h"
#include "gpu_composite.h"

using namespace pb;

namespace {

constexpr int S = kTileSize;
constexpr size_t kPixels = size_t(S) * S;
constexpr size_t kOutBytes = kPixels * 8;
constexpr Blend kModes[] = {Blend::Normal,     Blend::Darken,  Blend::Multiply,   Blend::ColorBurn,
                            Blend::Lighten,    Blend::Screen,  Blend::ColorDodge, Blend::Overlay,
                            Blend::SoftLight,  Blend::LinearLight, Blend::Difference};

// blend.slang's unit16(), div() and sqrt_rn() line by line, the GPU's quotient q and root s given.
float asf(uint32_t u) {
  float f;
  std::memcpy(&f, &u, 4);
  return f;
}
uint32_t asu(float f) {
  uint32_t u;
  std::memcpy(&u, &f, 4);
  return u;
}
uint32_t firstbithigh(uint32_t v) {
  uint32_t n = 31;
  while (!(v >> n)) n--;
  return n;
}
float unit16(uint32_t v) {
  if (v == 0) return 0;
  if (v >= 65535) return 1;
  const uint32_t lz = 15 - firstbithigh(v), w = ((v << 16) | v) << lz;
  return asf(((125 - lz) << 23) + (w >> 8) + ((w >> 7) & 1));
}
float div(float n, float d, float q) {
  const uint32_t un = asu(n), ud = asu(d);
  uint32_t N = (un & 0x7fffff) | 0x800000, D = (ud & 0x7fffff) | 0x800000;
  int e = int(un >> 23) - int(ud >> 23);
  if (N < D) N <<= 1, e--;
  if (!(n > 0) || (un >> 23) == 0 || (ud >> 23) == 0 || e < -104 || e > 126) return q;
  uint32_t M = uint32_t(q * asf(uint32_t(150 - e) << 23));
  int32_t R = int32_t((N << 23) - M * D);
  for (int i = 0; i < 16 && R < 0; i++) M--, R += int32_t(D);
  for (int i = 0; i < 16 && R >= int32_t(D); i++) M++, R -= int32_t(D);
  if (2 * R > int32_t(D)) M++;
  return asf((uint32_t(e + 126) << 23) + M);
}
float sqrt_rn(float x, float s) {
  const uint32_t ux = asu(x);
  if (!(x > 0) || (ux >> 23) == 0) return s;
  const int k = int(ux >> 23) - 150;
  const uint32_t odd = uint32_t(k) & 1, X = (ux & 0x7fffff) | 0x800000;
  const int p = (k - 24 + int(odd)) / 2;
  const uint32_t T = X << (24 - odd);
  uint32_t M = uint32_t(s * asf(uint32_t(127 - p) << 23));
  int32_t R = int32_t(T - M * M);
  for (int i = 0; i < 16 && R < 0; i++) M--, R += int32_t(2 * M + 1);
  for (int i = 0; i < 16 && R > int32_t(2 * M); i++) R -= int32_t(2 * M + 1), M++;
  if (R > int32_t(M)) M++;
  return asf((uint32_t(p + 149) << 23) + M);
}

// A sample of depth d, the edges 0 and max often (as test_composite.cpp's).
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
LayerPtr pixel(LayerId id, std::shared_ptr<const TileGrid> px, Blend b = Blend::Normal, float opacity = 1, float fill = 1) {
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
  return d;
}

// Tile t of the CPU composite at `level`, 4 samples a pixel row by row, zeros where it is null.
std::vector<uint16_t> cpu(const RenderInput& in, int level, TileXY t) {
  std::vector<uint16_t> px(kPixels * 4);
  if (const TilePtr c = composite_tile(in, level, t.x, t.y)) {
    TileGrid g({Depth::U16, 4});
    g.set(0, 0, c);
    g.read_rect(0, 0, S, S, reinterpret_cast<uint8_t*>(px.data()), S * 8);
  }
  return px;
}
// The same tiles of the GPU composite, zeros at slot -1; slots[i] gets tile i's.
std::vector<std::vector<uint16_t>> gpu_tiles(gpu::Device& dev, GpuComposite& gc, const RenderInput& in, int level,
                                       const std::vector<TileXY>& tiles, std::vector<int>* slots = nullptr) {
  const std::vector<int> at = gc.composite(in, level, tiles);
  std::vector<uint16_t> all(size_t(gc.output_tiles()) * kPixels * 4);
  dev.download(gc.output(), all.data(), all.size() * 2);
  dev.flush();
  std::vector<std::vector<uint16_t>> px;
  for (const int s : at)
    px.emplace_back(s < 0 ? std::vector<uint16_t>(kPixels * 4)
                          : std::vector<uint16_t>(all.begin() + ptrdiff_t(size_t(s) * kPixels * 4),
                                                  all.begin() + ptrdiff_t(size_t(s + 1) * kPixels * 4)));
  if (slots) *slots = at;
  return px;
}
// The largest difference between GPU and CPU over tiles, in 16-bit levels; how many samples differ to `differ`.
int max_diff(gpu::Device& dev, GpuComposite& gc, const RenderInput& in, const std::vector<TileXY>& tiles,
             size_t* differ = nullptr, int level = 0) {
  const auto got = gpu_tiles(dev, gc, in, level, tiles);
  int worst = 0;
  for (size_t i = 0; i < tiles.size(); i++) {
    const std::vector<uint16_t> want = cpu(in, level, tiles[i]);
    for (size_t j = 0; j < want.size(); j++) {
      const int d = std::abs(int(got[i][j]) - int(want[j]));
      worst = std::max(worst, d);
      if (differ && d) ++*differ;
    }
  }
  return worst;
}
std::vector<TileXY> rect(int x0, int y0, int x1, int y1) {
  std::vector<TileXY> t;
  for (int y = y0; y < y1; y++)
    for (int x = x0; x < x1; x++) t.push_back({x, y});
  return t;
}

}  // namespace

// blend.slang's integer arithmetic: samples to floats as composite.cpp's unit() for every 8- and 16-bit value; division
// and square root from results a few ulp off (Vulkan allows 2.5 ulp for a division) to the CPU's, over what the
// composite divides (colours, alphas, 1 - Cb) and roots (Cb).
TEST(test_gpu_composite_exact) {
  for (uint32_t v = 0; v <= 65535; v++) CHECK(unit16(v) == float(double(v) * (1.0 / 65535)));
  for (uint32_t v = 0; v <= 255; v++) CHECK(unit16(v * 257) == float(double(v) * (1.0 / 255)));
  std::mt19937 rng(5);
  const auto log_uniform = [&](double lo, double hi) { return float(std::exp2(lo + (hi - lo) * (rng() / 4294967296.0))); };
  for (int i = 0; i < 2'000'000; i++) {
    const int off = int(rng() % 9) - 4;  // ulps
    const float d = log_uniform(-24, 0), n = rng() % 2 ? log_uniform(-48, 1) : d * log_uniform(-1, 0);
    const float q = n / d, x = log_uniform(-48, 0), s = std::sqrt(x);
    CHECK(div(n, d, asf(asu(q) + uint32_t(off))) == q);
    CHECK(sqrt_rn(x, asf(asu(s) + uint32_t(off))) == s);
  }
  CHECK(div(0, 0.5f, 0) == 0 && sqrt_rn(0, 0) == 0 && sqrt_rn(1, asf(asu(1) - 2)) == 1);
}

// Random flat documents, 8 and 16 bits, RGB and grey: 11 modes and a P1 one (Normal till then) at random opacities
// and fills, data, solid and missing tiles, a layer past the canvas, a Pass Through group at full opacity, hidden and
// 0 % layers; and the edges of Color Burn and Color Dodge (near white burned by near black under Soft Light, near black
// dodged by near white), where an ulp of the backdrop moves hundreds of levels. G1 asks for a level of 8 bits; where the
// CPU rounds as IEEE says (x86-64), the GPU gives the same to the bit.
TEST(test_gpu_composite_matches_cpu) {
  auto dev = pb::test::gpu_device();
  if (!dev) return;
  std::mt19937 rng(29);
  for (const Format f : {Format{Depth::U16, 4}, Format{Depth::U8, 4}, Format{Depth::U16, 2}, Format{Depth::U8, 2}}) {
    const int m = max_value(f.depth);
    // Pixels of samples from lo up to lo + span - 1, alpha too unless `alpha` (a quarter of max up).
    const auto px = [&](int lo, int span, bool alpha = false) {
      return [&rng, lo, span, alpha, f, m] {
        Pixel v{};
        for (int c = 0; c < f.channels; c++) v[size_t(c)] = uint16_t(lo + int(rng() % unsigned(span)));
        if (alpha) v[size_t(f.channels - 1)] = uint16_t(m / 4 + int(rng() % unsigned(m - m / 4)));
        return v;
      };
    };
    const auto random_px = [&] {
      Pixel v{};
      for (int c = 0; c < f.channels; c++) v[size_t(c)] = sample(rng, f.depth);
      return v;
    };
    // A layer over the canvas's 3 x 2 tiles and two past it, each tile data, solid or missing.
    const auto grid = [&](const std::function<Pixel()>& value) {
      auto g = std::make_shared<TileGrid>(f);
      for (const TileXY t : rect(-1, 0, 4, 2)) {
        const unsigned r = rng() % 8;
        if (r < 5) g->set(t.x, t.y, make_tile(f, [&](size_t) { return value(); }));
        if (r == 5) g->set(t.x, t.y, Tile::solid(f, value()));
      }
      return std::shared_ptr<const TileGrid>(std::move(g));
    };
    const auto opacity = [&] { return rng() % 4 ? 0.05f + 0.95f * float(rng() % 1000) / 999 : 1.f; };
    std::vector<LayerPtr> layers;
    LayerId id = 1;
    for (const Blend b : kModes) layers.push_back(pixel(id++, grid(random_px), b, opacity(), opacity()));
    layers.push_back(pixel(id++, grid(random_px), Blend::HardLight, opacity()));  // Normal in P0
    layers.push_back(group(100, {pixel(id, grid(random_px), Blend::Multiply, 0.8f), pixel(id + 1, grid(random_px), Blend::Screen)},
                           Blend::PassThrough));
    id += 2;
    auto hidden = std::make_shared<Layer>(*pixel(id++, grid(random_px), Blend::Difference));
    hidden->props.visible = false;
    layers.push_back(hidden);
    layers.push_back(pixel(id++, grid(random_px), Blend::Screen, 0));
    std::shuffle(layers.begin(), layers.end(), rng);
    const std::vector<LayerPtr> edges{
        pixel(id, grid(px(m - 6, 7, true)), Blend::Normal), pixel(id + 1, grid(random_px), Blend::SoftLight, 0.5f),
        pixel(id + 2, grid(px(0, 4)), Blend::ColorBurn, 0.9f)};
    const std::vector<LayerPtr> dodge{pixel(id, grid(px(0, 4, true)), Blend::Normal),
                                      pixel(id + 1, grid(px(m - 3, 4)), Blend::ColorDodge, 0.9f)};

    GpuComposite gc(*dev, 32 << 20);
    for (const auto& l : {layers, edges, dodge})
      for (int level = 0; level <= 2; level++) {
        // The canvas, 600 x 300: its last column and row of tiles in part; at level 1 300 x 150, at level 2 150 x 75.
        const RenderInput in{doc(l, f, 2 * S + 88, S + 44)};
        const int nx = level ? 2 - level / 2 : 3, ny = level ? 1 : 2;  // its tiles at the level
        std::vector<TileXY> tiles = rect(0, 0, nx, ny);
        for (const TileXY t : {TileXY{-1, 0}, TileXY{nx, ny - 1}, TileXY{0, ny}, TileXY{nx - 1, -1}}) tiles.push_back(t);
        size_t differ = 0;
        const int worst = max_diff(*dev, gc, in, tiles, &differ, level);
        std::printf("test_gpu_composite_matches_cpu: %s %d channels, %zu layers, level %d: %zu samples off, by up to %d "
                    "16-bit levels\n",
                    f.depth == Depth::U16 ? "U16" : "U8", f.channels, l.size(), level, differ, worst);
        CHECK(worst <= 257);
#if defined(__x86_64__) || defined(_M_X64)
        CHECK(differ == 0);
#endif
        std::vector<int> slots;
        gpu_tiles(*dev, gc, in, level, tiles, &slots);
        for (size_t i = size_t(nx * ny); i < tiles.size(); i++) CHECK(slots[i] == -1);  // past the canvas
        CHECK(gc.stats().cpu == 0 && gc.stats().composited == 0);                       // the cache's
      }
  }
}


// The atlas uploads a tile once and the cache keeps a composite tile until its tiles or layers change; the stores take
// memory as frames need it, keeping what they held, within the budget, counted in K1's; an atlas smaller than a frame's
// tiles takes a dispatch for each part it holds; groups and the stroke on the GPU, groups nested past 10 on the CPU;
// another format; the errors.
TEST(test_gpu_composite_cache) {
  auto dev = pb::test::gpu_device();
  if (!dev) return;
  std::mt19937 rng(31);
  const Format f{Depth::U16, 4};
  const auto data = [&] {
    return make_tile(f, [&](size_t) { return Pixel{sample(rng, f.depth), sample(rng, f.depth), sample(rng, f.depth), sample(rng, f.depth)}; });
  };
  // 6 layers over 2 x 2 tiles: 5 of data, one solid.
  std::vector<std::shared_ptr<const TileGrid>> grids;
  std::vector<LayerPtr> layers;
  for (int j = 0; j < 6; j++) {
    auto g = std::make_shared<TileGrid>(f);
    for (const TileXY t : rect(0, 0, 2, 2)) g->set(t.x, t.y, j == 1 ? Tile::solid(f, {1000, 50000, 3000, 30000}) : data());
    grids.push_back(g);
    layers.push_back(pixel(LayerId(j + 1), g, kModes[size_t(j) * 2], 0.7f));
  }
  const std::vector<TileXY> tiles = rect(0, 0, 2, 2);
  const auto with = [&](size_t j, std::shared_ptr<const TileGrid> g, float opacity) {
    layers[j] = pixel(LayerId(j + 1), std::move(g), kModes[j * 2], opacity);
    return RenderInput{doc(layers, f, 2 * S, 2 * S)};
  };
  const auto stats = [](const GpuComposite& gc, int uploaded, int composited, int cached, int dispatches) {
    const GpuComposite::Stats s = gc.stats();
    return s.uploaded == uploaded && s.composited == composited && s.cached == cached && s.dispatches == dispatches && !s.cpu;
  };

  // Right after a frame (flushed): the device keeps only staging for reuse, no buffer a store held before it grew;
  // once its pool freed those, its buffers' device memory is `device`, and the tile budget counts what it takes.
  const size_t charged = tile_stats().charged;
  const auto held = [&](size_t device) {
    const gpu::Device::Held now = dev->held();
    if (now.kept != now.staging) return false;
    const auto t0 = std::chrono::steady_clock::now();
    for (;;) {
      const gpu::Device::Held h = dev->held();
      if (h.device == device && tile_stats().charged == charged + h.ram) return true;
      if (std::chrono::steady_clock::now() - t0 > std::chrono::seconds(30)) return false;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  };
  constexpr size_t kNone = 16;  // the buffer bound in place of a store's yet to have one
  {
    // 32 MB: 16 output tiles, 8 accumulators, an atlas of 32 tiles; none of it until a frame needs it.
    GpuComposite gc(*dev, 32 << 20);
    CHECK(gc.bytes() == 0 && gc.output_tiles() == 0);
    // A U16 RGBA tile, out or in the atlas, is kOutBytes; the jobs' and entries' buffers take 8 KB more.
    const auto bytes = [&](int tiles) { return gc.bytes() >= tiles * kOutBytes && gc.bytes() <= tiles * kOutBytes + 8192; };
    RenderInput in = with(0, grids[0], 0.7f);
    CHECK(max_diff(*dev, gc, in, {{0, 0}}) == 0);  // a tile: room for it alone
    CHECK(stats(gc, 5, 1, 0, 1) && gc.output_tiles() == 1 && bytes(1 + 5));
    CHECK(held(gc.bytes() + kNone));
    in = with(0, grids[0], 0.6f);  // it again, out of an atlas grown for the next one: what it held kept
    CHECK(max_diff(*dev, gc, in, rect(0, 0, 2, 1)) == 0);
    CHECK(stats(gc, 5, 2, 0, 1) && gc.output_tiles() == 2 && bytes(2 + 10));
    CHECK(max_diff(*dev, gc, in, tiles) == 0);  // all four: the two in the cache kept as it grows
    CHECK(stats(gc, 10, 2, 2, 1) && gc.output_tiles() == 4 && bytes(4 + 10));
    CHECK(held(gc.bytes() + kNone));
    CHECK(max_diff(*dev, gc, in, tiles) == 0);  // the second frame: nothing to do
    CHECK(stats(gc, 0, 0, 4, 0));
    in = with(2, grids[2], 0.3f);  // another opacity: every tile again, room for the 20 tiles at once
    CHECK(max_diff(*dev, gc, in, tiles) == 0);
    CHECK(stats(gc, 10, 4, 0, 1) && bytes(4 + 20) && held(gc.bytes() + kNone));
    in = with(2, grids[2], 0.4f);  // and again, out of the atlas; the cache's slots of the frame before go
    CHECK(max_diff(*dev, gc, in, tiles) == 0);
    CHECK(stats(gc, 0, 4, 0, 1) && bytes(4 + 20));
    auto g = std::make_shared<TileGrid>(*grids[3]);  // a new tile: it alone goes up, its output tile alone composites
    g->set(1, 0, data());
    in = with(3, g, 0.7f);
    CHECK(max_diff(*dev, gc, in, tiles) == 0);
    CHECK(stats(gc, 1, 1, 3, 1));
    CHECK(gc.bytes() <= size_t(32) << 20);

    // A group (but a full-opacity Pass Through) and the stroke where it has a tile: on the GPU too.
    CHECK(max_diff(*dev, gc, {doc({layers[0], group(9, {layers[2], layers[3]}, Blend::Normal, 0.8f)}, f, 2 * S, 2 * S)}, tiles) == 0);
    CHECK(gc.stats().composited == 4 && !gc.stats().cpu);
    auto cover = std::make_shared<TileGrid>(Format{Depth::U16, 1});
    cover->set(1, 1, make_tile({Depth::U16, 1}, [&](size_t) { return Pixel{uint16_t(rng())}; }));
    const RenderInput stroked{doc(layers, f, 2 * S, 2 * S), StrokeOverlay{3, cover, {0.2f, 0.4f, 0.6f}, 0.5f, false}};
    CHECK(max_diff(*dev, gc, stroked, tiles) == 0);
    CHECK(gc.stats().composited + gc.stats().cached == 4 && !gc.stats().cpu);
    // Groups nested 11 deep go through the CPU.
    LayerPtr deep = pixel(20, grids[0], Blend::Multiply, 0.6f);
    for (int j = 0; j < 11; j++) deep = group(LayerId(30 + j), {pixel(LayerId(50 + j), grids[4]), deep}, kModes[size_t(j)], 0.9f);
    CHECK(max_diff(*dev, gc, {doc({layers[1], deep}, f, 2 * S, 2 * S)}, tiles) == 0);
    CHECK(gc.stats().cpu == 4 && !gc.stats().composited);
    CHECK(max_diff(*dev, gc, {doc({layers[1], deep}, f, 2 * S, 2 * S)}, tiles) == 0);  // kept as the others
    CHECK(gc.stats().cached == 4 && !gc.stats().cpu);
    // A document of another format: an atlas of its own.
    const Format grey{Depth::U8, 2};
    auto g8 = std::make_shared<TileGrid>(grey);
    g8->set(0, 0, make_tile(grey, [&](size_t) { return Pixel{uint16_t(rng() % 256), uint16_t(rng() % 256)}; }));
    CHECK(max_diff(*dev, gc, {doc({pixel(1, g8, Blend::Screen, 0.5f), pixel(2, g8, Blend::Multiply)}, grey, S, S)}, {{0, 0}}) == 0);
    CHECK(gc.stats().uploaded == 1 && gc.stats().composited == 1);
    bool threw = false;
    try {
      gc.composite(in, 32, tiles);
    } catch (const std::invalid_argument&) {
      threw = true;
    }
    CHECK(threw);
  }
  CHECK(held(0));  // given back

  // 12 MB: 6 output tiles, an atlas of 12 tiles for the frame's 20, 5 a tile: two dispatches, the tiles of the second
  // in the slots of the first's. Least recently used out first, a frame larger than the atlas goes up whole each time.
  GpuComposite small(*dev, 12 << 20);
  RenderInput in = with(2, grids[2], 0.5f);
  CHECK(max_diff(*dev, small, in, tiles) == 0);
  CHECK(stats(small, 20, 4, 0, 2));
  in = with(2, grids[2], 0.6f);
  CHECK(max_diff(*dev, small, in, tiles) == 0);
  CHECK(stats(small, 20, 4, 0, 2));
  CHECK(small.bytes() <= size_t(12) << 20);
  // More tiles with something in them than output tiles; more data tiles in a tile than the atlas holds.
  const auto throws = [&](GpuComposite& c, const RenderInput& r, const std::vector<TileXY>& t) {
    try {
      c.composite(r, 0, t);
    } catch (const std::length_error&) {
      return true;
    }
    return false;
  };
  auto band = std::make_shared<TileGrid>(f);
  for (const TileXY t : rect(0, 0, 7, 1)) band->set(t.x, t.y, Tile::solid(f, {1, 2, 3, 65535}));
  CHECK(throws(small, {doc({pixel(1, band)}, f, 7 * S, S)}, rect(0, 0, 7, 1)));
  CHECK(!throws(small, {doc({pixel(1, band)}, f, 7 * S, S)}, rect(0, 0, 6, 2)));  // the second row is past the canvas
  std::vector<LayerPtr> many;
  for (int j = 0; j < 13; j++) {
    auto one = std::make_shared<TileGrid>(f);
    one->set(0, 0, data());
    many.push_back(pixel(LayerId(j + 1), one, Blend::Multiply, 0.5f));
  }
  CHECK(throws(small, {doc(many, f, S, S)}, {{0, 0}}));
  CHECK(max_diff(*dev, small, in, tiles) == 0);  // none the worse for it
}

// Random trees of groups nested 4 deep, each isolated (a P0 mode, any opacity) or Pass Through (at full opacity or
// not), with groups of their own beside, in both depths, RGB and grey, at levels 0 and 1; the stroke painting or
// erasing on a layer at every depth, a first frame keeping what lies below its layer and a second starting from there:
// composite_tile()'s pixels, to the bit where the CPU rounds as IEEE says (x86-64); G1 asks for a level of 8 bits.
TEST(test_gpu_composite_groups) {
  auto dev = pb::test::gpu_device();
  if (!dev) return;
  std::mt19937 rng(37);
  const Format coverage{Depth::U16, 1};
  int below = 0, painted = 0;  // tiles that started from the cache below the stroke's layer, of those it painted
  GpuComposite gc(*dev, 64 << 20);
  for (const Format f : {Format{Depth::U16, 4}, Format{Depth::U8, 4}, Format{Depth::U16, 2}, Format{Depth::U8, 2}}) {
    // Tiles over the canvas's 2 x 1 and one past it: data, solid or (but for the stroke buffer) missing.
    const auto grid = [&](Format g) {
      const auto px = [&] {
        Pixel v{};
        for (int c = 0; c < g.channels; c++) v[size_t(c)] = sample(rng, g.depth);
        return v;
      };
      auto out = std::make_shared<TileGrid>(g);
      for (const TileXY t : rect(-1, 0, 2, 1)) {
        const unsigned r = rng() % 8;
        if (r < 5 || (r > 5 && g == coverage)) out->set(t.x, t.y, make_tile(g, [&](size_t) { return px(); }));
        if (r == 5) out->set(t.x, t.y, Tile::solid(g, px()));
      }
      return std::shared_ptr<const TileGrid>(std::move(out));
    };
    const auto opacity = [&] { return rng() % 3 ? 0.05f + 0.95f * float(rng() % 1000) / 999 : 1.f; };
    const auto mode = [&] { return kModes[rng() % std::size(kModes)]; };
    for (int tree = 0; tree < 3; tree++) {
      LayerId id = 1;
      std::vector<LayerId> deep;  // a pixel layer at each depth, 0 to 4
      const auto kind = [&](std::vector<LayerPtr> c) { return group(id++, std::move(c), rng() % 2 ? Blend::PassThrough : mode(), opacity()); };
      // A group's children at `depth`: 1 to 3 layers, maybe a group of one beside them, the group below till depth 4.
      const std::function<std::vector<LayerPtr>(int)> children = [&](int depth) {
        std::vector<LayerPtr> c;
        for (int n = 1 + int(rng() % 3); n > 0; n--) c.push_back(pixel(id++, grid(f), mode(), opacity(), opacity()));
        deep.push_back(c[rng() % c.size()]->id);
        if (rng() % 2) c.insert(c.begin() + ptrdiff_t(rng() % (c.size() + 1)), kind({pixel(id++, grid(f), mode(), opacity())}));
        if (depth < 4) c.insert(c.begin() + ptrdiff_t(rng() % (c.size() + 1)), kind(children(depth + 1)));
        return c;
      };
      const DocPtr d = doc(children(0), f, S + 37, 60);  // a band, for lavapipe's sake
      CHECK(deep.size() == 5);
      const int level = tree == 2;  // at level 1 the canvas is part of tile 0
      const auto check = [&](const RenderInput& in) {
        size_t differ = 0;
        const int worst = max_diff(*dev, gc, in, {{0, 0}, {1, 0}, {-1, 0}}, &differ, level);
        if (differ)
          std::printf("test_gpu_composite_groups: %s %d channels, tree %d, the stroke at depth %d: %zu samples off, by up "
                      "to %d 16-bit levels\n",
                      f.depth == Depth::U16 ? "U16" : "U8", f.channels, tree, in.stroke ? int(std::find(deep.begin(), deep.end(), in.stroke->layer) - deep.begin()) : -1, differ, worst);
        CHECK(worst <= 257);
#if defined(__x86_64__) || defined(_M_X64)
        CHECK(differ == 0);
#endif
      };
      check({d});
      for (int depth = 0; depth <= 4; depth++) {
        const auto u = [&] { return float(rng() % 1001) / 1000; };
        StrokeOverlay s{deep[size_t(depth)], grid(coverage), {u(), u(), u()}, opacity(), depth % 2 == 1};
        check({d, s});  // the first frame: from the cache only where the steps below the layer were seen before
        s.buffer = grid(coverage);  // the stroke moves on, everywhere
        check({d, s});
        below += gc.stats().below, painted += gc.stats().composited;
      }
    }
  }
  std::printf("test_gpu_composite_groups: %d of %d tiles the stroke painted started from the cache below its layer\n", below, painted);
  CHECK(below > painted / 2);
}

// While the stroke paints layer k, the first frame composites every layer and keeps what lies below k; the next ones
// read k and the layers above it only (counted), from an accumulator, and one more for each group around k; a change
// below k misses the cache, one above k or in the opacity of a group around it does not.
TEST(test_gpu_composite_below) {
  auto dev = pb::test::gpu_device();
  if (!dev) return;
  std::mt19937 rng(41);
  const Format f{Depth::U16, 4}, coverage{Depth::U16, 1};
  const auto data = [&](Format g) {
    return make_tile(g, [&](size_t) {
      Pixel v{};
      for (int c = 0; c < g.channels; c++) v[size_t(c)] = sample(rng, g.depth);
      return v;
    });
  };
  const auto grid = [&](Format g) {  // 2 x 1 tiles of data
    auto out = std::make_shared<TileGrid>(g);
    for (const TileXY t : rect(0, 0, 2, 1)) out->set(t.x, t.y, data(g));
    return std::shared_ptr<const TileGrid>(std::move(out));
  };
  const auto changed = [&](const LayerPtr& l, int tx) {  // l with a new tile at (tx, 0)
    auto g = std::make_shared<TileGrid>(*l->pixels);
    g->set(tx, 0, data(f));
    auto out = std::make_shared<Layer>(*l);
    out->pixels = std::move(g);
    return LayerPtr(std::move(out));
  };
  GpuComposite gc(*dev, 64 << 20);
  StrokeOverlay s{3, grid(coverage), {0.3f, 0.6f, 0.9f}, 0.7f, false};
  // A frame of 2 x 1 tiles: composite_tile()'s pixels, out of so many tiles composited, from the cache below k, and
  // layer tiles read.
  const auto frame = [&](const std::vector<LayerPtr>& layers, int composited, int below, int read) {
    const bool same = max_diff(*dev, gc, {doc(layers, f, 2 * S, S), s}, rect(0, 0, 2, 1)) == 0;
    const GpuComposite::Stats st = gc.stats();
    if (!same || st.composited != composited || st.below != below || st.layers != read)
      std::printf("test_gpu_composite_below: %s, %d composited, %d from below, %d layers read\n", same ? "same" : "different",
                  st.composited, st.below, st.layers);
    return same && st.composited == composited && st.below == below && st.layers == read;
  };

  // 6 layers, the stroke on the third.
  std::vector<LayerPtr> flat;
  for (LayerId j = 1; j <= 6; j++) flat.push_back(pixel(j, grid(f), kModes[j], 0.8f));
  CHECK(frame(flat, 2, 0, 12));  // all 6, keeping what layers 1 and 2 make
  s.buffer = grid(coverage);
  CHECK(frame(flat, 2, 2, 8));  // layers 3 to 6
  CHECK(frame(flat, 0, 0, 0) && gc.stats().cached == 2);
  flat[4] = changed(flat[4], 1);  // above k: from the cache still
  CHECK(frame(flat, 1, 1, 4));
  flat[2] = pixel(3, flat[2]->pixels, Blend::Screen, 0.5f);  // k's own mode and opacity likewise
  CHECK(frame(flat, 2, 2, 8));
  flat[1] = changed(flat[1], 0);  // right below k: tile 0 anew, kept again
  CHECK(frame(flat, 1, 0, 6));
  s.buffer = grid(coverage);
  CHECK(frame(flat, 2, 2, 8));
  s.layer = 1;  // nothing below: no cache
  CHECK(frame(flat, 2, 0, 12));
  s.buffer = grid(coverage);
  CHECK(frame(flat, 2, 0, 12));

  // In a group: layers 1, 2, then 3, k = 4 and 5 in an isolated group, then 6: what lies below the group, and below k
  // in it.
  const auto in_group = [&](float opacity, Blend b) {
    return std::vector<LayerPtr>{flat[0], flat[1], group(7, {flat[2], flat[3], flat[4]}, b, opacity), flat[5]};
  };
  s.layer = 4;
  CHECK(frame(in_group(0.9f, Blend::Multiply), 2, 0, 12));
  s.buffer = grid(coverage);
  CHECK(frame(in_group(0.9f, Blend::Multiply), 2, 2, 6));  // layers 4, 5, 6
  CHECK(frame(in_group(0.4f, Blend::Overlay), 2, 2, 6));   // the group's opacity and mode
  // Two deep: 1, then 2 and a Pass Through group at 60 % of 3 = k and 4 in an isolated group, then 5, 6: three
  // accumulators, the last a copy of the second.
  const auto two_deep = std::vector<LayerPtr>{
      flat[0], group(8, {flat[1], group(9, {flat[2], flat[3]}, Blend::PassThrough, 0.6f)}, Blend::Screen, 0.7f), flat[4],
      flat[5]};
  s.layer = 3;
  CHECK(frame(two_deep, 2, 0, 12));
  s.buffer = grid(coverage);
  CHECK(frame(two_deep, 2, 2, 8));  // layers 3 to 6
  // Erasing likewise.
  s.erase = true, s.buffer = grid(coverage);
  CHECK(frame(two_deep, 2, 2, 8));
}
