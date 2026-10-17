// The GPU composite (gpu_composite.h) against the CPU's, composite_tile() (B1): random flat documents in every P0 mode,
// both depths, RGB and grey, with data, solid and missing tiles, the canvas's edge, and the Color Burn edge where an
// ulp moves hundreds of levels; what the atlas uploads and the cache keeps; what goes to the CPU until B3. And the
// integer arithmetic of shaders/lib/blend.slang that makes the two agree, in C++, against the CPU's.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <random>
#include <stdexcept>
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

// The atlas uploads a tile once and the cache keeps a composite tile until its tiles or layers change; an atlas smaller
// than a frame's tiles takes a dispatch for each part it holds; groups and the stroke go to the CPU till B3; the errors.
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

  GpuComposite gc(*dev, 16 << 20);
  RenderInput in = with(0, grids[0], 0.7f);
  CHECK(max_diff(*dev, gc, in, tiles) == 0);
  CHECK(stats(gc, 20, 4, 0, 1));
  CHECK(max_diff(*dev, gc, in, tiles) == 0);  // the second frame: nothing to do
  CHECK(stats(gc, 0, 0, 4, 0));
  in = with(2, grids[2], 0.3f);  // another opacity: every tile again, out of the atlas
  CHECK(max_diff(*dev, gc, in, tiles) == 0);
  CHECK(stats(gc, 0, 4, 0, 1));
  auto g = std::make_shared<TileGrid>(*grids[3]);  // a new tile: it alone goes up, its output tile alone composites
  g->set(1, 0, data());
  in = with(3, g, 0.7f);
  CHECK(max_diff(*dev, gc, in, tiles) == 0);
  CHECK(stats(gc, 1, 1, 3, 1));

  // 8 MB: 4 output tiles, an atlas of 12 tiles for the frame's 20, 5 a tile: two dispatches, the tiles of the second
  // in the slots of the first's. Least recently used out first, a frame larger than the atlas goes up whole each time.
  GpuComposite small(*dev, 8 << 20);
  CHECK(small.output_tiles() == 4);
  CHECK(max_diff(*dev, small, in, tiles) == 0);
  CHECK(stats(small, 20, 4, 0, 2));
  in = with(2, grids[2], 0.5f);
  CHECK(max_diff(*dev, small, in, tiles) == 0);
  CHECK(stats(small, 20, 4, 0, 2));
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
  for (const TileXY t : rect(0, 0, 5, 1)) band->set(t.x, t.y, Tile::solid(f, {1, 2, 3, 65535}));
  CHECK(throws(small, {doc({pixel(1, band)}, f, 5 * S, S)}, rect(0, 0, 5, 1)));
  CHECK(!throws(small, {doc({pixel(1, band)}, f, 5 * S, S)}, rect(0, 0, 4, 2)));  // the second row is past the canvas
  std::vector<LayerPtr> many;
  for (int j = 0; j < 13; j++) {
    auto one = std::make_shared<TileGrid>(f);
    one->set(0, 0, data());
    many.push_back(pixel(LayerId(j + 1), one, Blend::Multiply, 0.5f));
  }
  CHECK(throws(small, {doc(many, f, S, S)}, {{0, 0}}));
  CHECK(max_diff(*dev, small, in, tiles) == 0);  // none the worse for it

  // A group (but a full-opacity Pass Through) where it has layers with pixels, and the stroke where it has a tile:
  // composite_tile()'s pixels.
  CHECK(max_diff(*dev, gc, {doc({layers[0], group(9, {layers[2], layers[3]}, Blend::Normal, 0.8f)}, f, 2 * S, 2 * S)}, tiles) == 0);
  CHECK(gc.stats().cpu == 4);
  auto corner = std::make_shared<TileGrid>(f);
  corner->set(0, 0, data());
  CHECK(max_diff(*dev, gc, {doc({layers[0], group(9, {pixel(10, corner)}, Blend::Screen)}, f, 2 * S, 2 * S)}, tiles) == 0);
  CHECK(gc.stats().cpu == 1 && gc.stats().composited == 3);
  auto cover = std::make_shared<TileGrid>(Format{Depth::U16, 1});
  cover->set(1, 1, make_tile({Depth::U16, 1}, [&](size_t) { return Pixel{uint16_t(rng())}; }));
  const RenderInput stroked{doc(layers, f, 2 * S, 2 * S), StrokeOverlay{3, cover, {0.2f, 0.4f, 0.6f}, 0.5f, false}};
  CHECK(max_diff(*dev, gc, stroked, tiles) == 0);
  CHECK(gc.stats().cpu == 1 && gc.stats().composited == 3);
  bool threw = false;
  try {
    gc.composite(in, 32, tiles);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  CHECK(threw);
}
