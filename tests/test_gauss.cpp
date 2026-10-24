// Gaussian blur on tiles (gauss.h, S30). The kernels: direct against a Gaussian written out in doubles, in every
// format (alpha weighted); boxes within 1% of it on hard edges, bars and corners at sigma 20 and either side of
// kDirectSigma; both of the sampled kernel's variance, centred, keeping the mass. The grids: sums kept; a constant
// plane unchanged through the shortcut and through the kernel; the same pixels however the plane is cut into tiles
// (against one block of pixels); the canvas's edges extended and cleared; uniform and empty neighbourhoods without
// filtering or allocating (Select All the size of D1); level 1 at sigma / 2 against level 0 halved. Feather
// Selection: 50% on the edge, what it refuses.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <random>
#include <stdexcept>
#include <vector>

#include "check.h"
#include "gauss.h"
#include "mip.h"
#include "tilecache.h"

using namespace pb;

namespace {

constexpr int S = kTileSize;
constexpr Format kFormats[] = {{Depth::U8, 1},  {Depth::U8, 2},  {Depth::U8, 4},
                               {Depth::U16, 1}, {Depth::U16, 2}, {Depth::U16, 4}};

// Pixels in memory, rows packed.
struct Image {
  Format f;
  int w = 0, h = 0;
  std::vector<uint8_t> px;
  Image(Format f_, int w_, int h_) : f(f_), w(w_), h(h_), px(size_t(w_) * size_t(h_) * size_t(f_.bytes())) {}
  size_t stride() const { return size_t(w) * size_t(f.bytes()); }
  uint8_t* at(int x, int y) { return px.data() + size_t(y) * stride() + size_t(x) * size_t(f.bytes()); }
  const uint8_t* at(int x, int y) const { return px.data() + size_t(y) * stride() + size_t(x) * size_t(f.bytes()); }
  int get(int x, int y, int c) const {
    const uint8_t* p = at(x, y);
    if (f.depth == Depth::U8) return p[c];
    uint16_t v;
    std::memcpy(&v, p + 2 * c, 2);
    return v;
  }
  void set(int x, int y, int c, int v) {
    uint8_t* p = at(x, y);
    if (f.depth == Depth::U8) return void(p[c] = uint8_t(v));
    const uint16_t u = uint16_t(v);
    std::memcpy(p + 2 * c, &u, 2);
  }
  void put(int x, int y, const Pixel& v) {
    for (int c = 0; c < f.channels; c++) set(x, y, c, v[size_t(c)]);
  }
};

Image read(const TileGrid& g, int x, int y, int w, int h) {
  Image im(g.format(), w, h);
  g.read_rect(x, y, w, h, im.px.data(), im.stride());
  return im;
}

TileGrid grid_of(const Image& im, int x, int y, Pixel fill = {}) {
  TileGrid g(im.f, fill);
  g.write_rect(x, y, im.w, im.h, im.px.data(), im.stride());
  return g;
}

// gauss_pixels() of the w x h pixels whose region (margin and all) starts at (ox, oy) of `in`.
Image blur_block(const Image& in, int ox, int oy, int w, int h, double sigma) {
  const int m = gauss_margin(sigma);
  CHECK(ox >= 0 && oy >= 0 && ox + w + 2 * m <= in.w && oy + h + 2 * m <= in.h);
  Image out(in.f, w, h);
  gauss_pixels(in.f, in.at(ox, oy), in.stride(), w, h, sigma, out.px.data(), out.stride());
  return out;
}

// The sampled Gaussian out to ceil(3 sigma), renormalised, and its variance.
std::vector<double> sampled(double sigma) {
  const int m = int(std::ceil(3 * sigma));
  std::vector<double> k(size_t(2 * m + 1));
  double sum = 0;
  for (int i = -m; i <= m; i++) sum += k[size_t(i + m)] = std::exp(-double(i) * i / (2 * sigma * sigma));
  for (double& v : k) v /= sum;
  return k;
}
double variance(const std::vector<double>& k) {
  const int m = int(k.size() / 2);
  double v = 0;
  for (int i = -m; i <= m; i++) v += k[size_t(i + m)] * i * i;
  return v;
}

// The Gaussian written out: doubles 0..1, colours premultiplied, the sampled kernel across then down, divided back,
// rounded as the CPU paths round (from_unit). Of the w x h pixels whose region (ceil(3 sigma) round them) starts at
// (ox, oy) of `in`.
Image reference(const Image& in, int ox, int oy, int w, int h, double sigma) {
  const std::vector<double> k = sampled(sigma);
  const int m = int(k.size() / 2), C = in.f.channels, rw = w + 2 * m, rh = h + 2 * m;
  CHECK(ox >= 0 && oy >= 0 && ox + rw <= in.w && oy + rh <= in.h);
  const double top = max_value(in.f.depth);
  std::vector<double> p(size_t(rw) * size_t(rh) * size_t(C)), across(size_t(w) * size_t(rh) * size_t(C));
  for (int y = 0; y < rh; y++)
    for (int x = 0; x < rw; x++) {
      double* q = &p[(size_t(y) * rw + size_t(x)) * C];
      const double a = in.f.alpha() ? in.get(ox + x, oy + y, C - 1) / top : 1;
      for (int c = 0; c < C; c++) q[c] = in.get(ox + x, oy + y, c) / top * (c < C - 1 || !in.f.alpha() ? a : 1);
    }
  for (int y = 0; y < rh; y++)
    for (int x = 0; x < w; x++)
      for (int c = 0; c < C; c++) {
        double s = 0;
        for (int t = 0; t <= 2 * m; t++) s += k[size_t(t)] * p[(size_t(y) * rw + size_t(x + t)) * C + size_t(c)];
        across[(size_t(y) * w + size_t(x)) * C + size_t(c)] = s;
      }
  Image out(in.f, w, h);
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      double v[4] = {};
      for (int c = 0; c < C; c++)
        for (int t = 0; t <= 2 * m; t++) v[c] += k[size_t(t)] * across[(size_t(y + t) * w + size_t(x)) * C + size_t(c)];
      if (!in.f.alpha()) {
        out.set(x, y, 0, from_unit(float(v[0]), in.f.depth));
        continue;
      }
      const int alpha = from_unit(float(v[C - 1]), in.f.depth);
      out.set(x, y, C - 1, alpha);
      for (int c = 0; c < C - 1; c++) out.set(x, y, c, alpha ? from_unit(float(v[c] / v[C - 1]), in.f.depth) : 0);
    }
  return out;
}

// The largest difference between samples of a and b; with alpha, colours where both are transparent do not count,
// nor where one is (a rounding of alpha between 0 and 1 level), alpha itself does.
int max_diff(const Image& a, const Image& b) {
  CHECK(a.f == b.f && a.w == b.w && a.h == b.h);
  const int C = a.f.channels;
  int d = 0;
  for (int y = 0; y < a.h; y++)
    for (int x = 0; x < a.w; x++) {
      const bool colours = !a.f.alpha() || (a.get(x, y, C - 1) && b.get(x, y, C - 1));
      for (int c = 0; c < C; c++)
        if (colours || c == C - 1) d = std::max(d, std::abs(a.get(x, y, c) - b.get(x, y, c)));
    }
  return d;
}
bool same(const Image& a, const Image& b) { return a.f == b.f && a.w == b.w && a.h == b.h && a.px == b.px; }

uint32_t mix(uint32_t x) {  // a hash of a place, for noise the same on every run
  x ^= x >> 16, x *= 0x7feb352d, x ^= x >> 15, x *= 0x846ca68b;
  return x ^ x >> 16;
}

// A plane with something of everything: noise, smooth shades, hard squares and thin bars, a flat area large enough to
// give solid tiles; with alpha, a transparent part (colours at random under it), soft and random alpha.
Image pattern(Format f, int w, int h, uint32_t seed) {
  const int top = max_value(f.depth), C = f.channels;
  Image im(f, w, h);
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      const uint32_t n = mix(uint32_t(x) * 7919u + uint32_t(y) * 104729u + seed * 15485863u);
      Pixel v{};
      const bool flat = x >= w / 2 && y >= h / 2;
      for (int c = 0; c < C; c++) {
        double u;
        if (flat)
          u = 0.3 + 0.2 * c;
        else if (x < w / 4)
          u = double(mix(n + uint32_t(c)) % 1000) / 999;  // noise
        else if (((x / 13) ^ (y / 29)) % 3 == 0)
          u = ((x % 41) < 1 + (y % 7)) ? 1 : 0.1 * c;  // hard bars of 1 to 7 px
        else
          u = 0.5 + 0.45 * std::sin(x * 0.031 + c) * std::cos(y * 0.017 - c);
        v[size_t(c)] = uint16_t(std::lround(std::clamp(u, 0., 1.) * top));
      }
      if (f.alpha()) {
        int a = top;
        if (!flat && y < h / 3 && x >= w / 4 && x < w / 2)
          a = 0;  // transparent, colours at random
        else if (!flat && x < w / 4)
          a = int(mix(n ^ 0x55u) % unsigned(top + 1));
        else if (!flat && y >= h / 3 && y < h / 2)
          a = int(std::lround(top * std::clamp((x - w / 4) / double(w / 2), 0., 1.)));
        v[size_t(C - 1)] = uint16_t(a);
      }
      im.put(x, y, v);
    }
  return im;
}

bool throws(void (*fn)()) {
  try {
    fn();
  } catch (const std::invalid_argument&) {
    return true;
  }
  return false;
}

// --- Kernels.

void check_margins() {
  CHECK(sigma_of_radius(0) == 0 && sigma_of_radius(7.5) == 7.5 * kSigmaPerRadius);
  CHECK(gauss_margin(0) == 0 && gauss_margin(0.1) == 1 && gauss_margin(1) == 3 && gauss_margin(8) == 24);
  int last = 0;
  for (double s = 0.05; s <= 200; s *= 1.05) {
    const int m = gauss_margin(s);
    CHECK(m >= last && m >= int(std::ceil(3 * s)) && m <= 3 + int(std::ceil(4.5 * s)));
    last = m;
  }
  CHECK(throws([] { gauss_margin(-1); }));
  CHECK(throws([] { gauss_margin(std::numeric_limits<double>::quiet_NaN()); }));
  CHECK(throws([] { gauss_margin(kMaxSigma * 1.01); }));
  CHECK(throws([] { gauss_margin(std::numeric_limits<double>::infinity()); }));
  CHECK(gauss_margin(kMaxSigma) > 0);
  CHECK(throws([] {
    uint8_t px[64] = {};
    gauss_pixels(Format{Depth::U8, 3}, px, 9, 1, 1, 0.1, px + 32, 3);
  }));
}

// A line across a column: the kernel itself, at full 16-bit scale. Its mass, centre and variance (the sampled
// kernel's, which the boxes take on).
void check_profile() {
  const Format f{Depth::U16, 1};
  for (const double sigma : {0.8, 3., 8., 8.5, 20., 50.}) {
    const int m = gauss_margin(sigma), half = m + 2, w = 2 * half + 1;
    Image in(f, w + 2 * m, 1 + 2 * m);
    for (int y = 0; y < in.h; y++) in.set(half + m, y, 0, 65535);
    const Image out = blur_block(in, 0, 0, w, 1, sigma);
    double s0 = 0, s1 = 0, s2 = 0;
    for (int x = 0; x < w; x++) {
      const double v = out.get(x, 0, 0), d = x - half;
      s0 += v, s1 += v * d, s2 += v * d * d;
    }
    const double mean = s1 / s0, var = s2 / s0 - mean * mean, want = variance(sampled(sigma));
    std::printf("gauss: sigma %g: mass %.5f, centre %+.5f px, variance %.4f of the sampled kernel's\n", sigma,
                s0 / 65535, mean, var / want);
    CHECK(std::abs(s0 / 65535 - 1) < 2e-3 && std::abs(mean) < 1e-3 && std::abs(var / want - 1) < 5e-3);
    for (int x = 0; x < half; x++) CHECK(std::abs(out.get(x, 0, 0) - out.get(w - 1 - x, 0, 0)) <= 1);  // symmetric
  }
}

// Direct: as the Gaussian written out in doubles, to a level, in every format.
void check_direct() {
  uint32_t seed = 1;
  for (const Format f : kFormats)
    for (const double sigma : {0.4, 1.3, 3.7, 8.}) {
      const int m = gauss_margin(sigma), w = 83, h = 61;
      const Image in = pattern(f, w + 2 * m, h + 2 * m, seed++);
      const int d = max_diff(blur_block(in, 0, 0, w, h, sigma), reference(in, 0, 0, w, h, sigma));
      if (d > 1) std::fprintf(stderr, "gauss: direct, %d channels, sigma %g: %d levels off\n", f.channels, sigma, d);
      CHECK(d <= 1);
    }
}

// Boxes: within 1% of the full range of the direct kernel on squares, bars and edges of every size about sigma's, and
// on a disk, at 16 bits so that rounding does not count.
void check_boxes() {
  const Format f{Depth::U16, 1};
  for (const double sigma : {8.25, 20., 27.}) {
    const int unit = int(std::lround(sigma)), cell = 8 * unit, mb = gauss_margin(sigma), md = int(std::ceil(3 * sigma));
    const int M = std::max(mb, md), w = 6 * cell, h = 3 * cell;
    Image in(f, w + 2 * M, h + 2 * M);
    const double sizes[] = {0.1, 0.25, 0.5, 1, 1.5, 2.5};
    for (int i = 0; i < 6; i++) {
      const int s = std::max(1, int(std::lround(sizes[i] * sigma * 2))), x0 = M + i * cell + cell / 2 - s / 2;
      for (int y = 0; y < s; y++)  // a square
        for (int x = 0; x < s; x++) in.set(x0 + x, M + cell / 2 - s / 2 + y, 0, 65535);
      for (int y = 0; y < cell; y++)  // a bar down
        for (int x = 0; x < s; x++) in.set(x0 + x, M + cell + y, 0, 65535);
    }
    for (int y = M + 2 * cell; y < M + 3 * cell; y++)  // a disk, and an edge half the way across
      for (int x = M; x < M + w; x++) {
        const double dx = x - (M + cell), dy = y - (M + 2.5 * cell);
        if (dx * dx + dy * dy < 4. * unit * unit * 4 || x >= M + 3 * cell) in.set(x, y, 0, 65535);
      }
    const int d = max_diff(blur_block(in, M - mb, M - mb, w, h, sigma), reference(in, M - md, M - md, w, h, sigma));
    std::printf("gauss: sigma %g: boxes %.3f%% off the direct kernel at most\n", sigma, 100. * d / 65535);
    CHECK(d <= 655);
  }
}

// --- Grids.

// The whole of what a grid stores, and of what gauss() made of it, through read_rect().
std::vector<int64_t> sums(const TileGrid& g, Format f) {
  std::vector<int64_t> s(4);
  const TileRect b = g.bounds();
  const Image im = read(g, b.x0 * S, b.y0 * S, (b.x1 - b.x0) * S, (b.y1 - b.y0) * S);
  const int C = f.channels;
  for (int y = 0; y < im.h; y++)
    for (int x = 0; x < im.w; x++) {
      const int64_t a = f.alpha() ? im.get(x, y, C - 1) : 1;
      for (int c = 0; c < C; c++) s[size_t(c)] += im.get(x, y, c) * (c < C - 1 ? a : 1);
    }
  return s;
}

// Without a canvas nothing is lost: values (premultiplied colours, alpha) sum as before, to rounding: what fades below
// half a level past the edges of things rounds to 0, at 8 bits 1.7e-4 of the sum here at sigma 20 (their length, the
// band from 2.65 to 3 sigma, a quarter of a level); at 16 within 3e-6, 1.0e-6 seen at sigma 20 in premultiplied colours
// under soft alpha, the boxes rounding to 16 bits between their passes too (2e-7 without that).
void check_sums() {
  for (const Format f : {Format{Depth::U16, 1}, Format{Depth::U8, 1}, Format{Depth::U16, 4}})
    for (const double sigma : {2., 8., 20.}) {
      const double most = f.depth == Depth::U16 ? 3e-6 : 5e-4;
      const TileGrid g = grid_of(pattern(f, 500, 380, 7), 100, -50);
      const std::vector<int64_t> a = sums(g, f), b = sums(gauss(g, sigma), f);
      for (int c = 0; c < f.channels; c++) {
        const double e = std::abs(double(b[size_t(c)]) / double(a[size_t(c)]) - 1);
        if (e > most) std::fprintf(stderr, "gauss: sigma %g, channel %d: the sum %g off\n", sigma, c, e);
        CHECK(e <= most);
      }
    }
}

// A constant plane stays as it is: solid tiles shared as they are, with no pixel allocated; data tiles of one value
// (filtered) give it back exactly; one colour under alpha at random keeps its colour: exactly at 8 bits, at 16 through
// the boxes to a level, their premultiplied colours rounded to 16 bits between the passes (1 level seen where alpha
// comes out about half, by the canvas's edges, where extending repeats one row's rounding).
void check_constant() {
  for (const Format f : kFormats) {
    const int top = max_value(f.depth);
    Pixel v{uint16_t(top / 3), uint16_t(top / 5), uint16_t(top / 7), uint16_t(top / 2)};
    if (f.channels == 2) v[1] = uint16_t(top / 2);
    GaussEdges e;
    e.width = 5 * S - 37, e.height = 4 * S - 5;
    TileGrid solid(f), data(f), shades(f);
    std::mt19937 rng(unsigned(f.channels * 10 + int(f.depth)));
    for (int ty = 0; ty < 4; ty++)
      for (int tx = 0; tx < 5; tx++) {
        solid.set(tx, ty, Tile::solid(f, v));
        data.set(tx, ty, Tile::make(f, v));  // pixels allocated, all v: not compacted
        const std::shared_ptr<Tile> t = Tile::make(f, v);
        {
          const WritePin p = t->write();
          for (size_t i = 0; f.alpha() && i < size_t(S) * S; i++) {
            const uint16_t a = uint16_t(rng() % unsigned(top + 1));
            uint8_t* q = p.data() + i * size_t(f.bytes()) + size_t(f.channels - 1) * (f.depth == Depth::U16 ? 2 : 1);
            f.depth == Depth::U16 ? void(std::memcpy(q, &a, 2)) : void(*q = uint8_t(a));
          }
        }
        shades.set(tx, ty, t);
      }
    for (const double sigma : {3., 20.}) {
      const size_t raw = tile_stats().raw;
      const TileGrid a = gauss(solid, sigma, e);
      CHECK(tile_stats().raw == raw && a.tiles().size() == 20);
      for (const auto& [xy, t] : a.tiles()) CHECK(t == solid.tiles().at(xy));
      const TileGrid b = gauss(data, sigma, e);
      const Pixel want = Tile::solid(f, v)->value();
      CHECK(b.tiles().size() == 20);
      for (const auto& [xy, t] : b.tiles()) CHECK(t->is_solid() && t->value() == want);
      if (!f.alpha()) continue;
      const Image in = read(shades, 0, 0, e.width, e.height), out = read(gauss(shades, sigma, e), 0, 0, e.width, e.height);
      const int C = f.channels, off = f.depth == Depth::U16 && sigma > kDirectSigma ? 1 : 0;
      for (int y = 0; y < out.h; y++)
        for (int x = 0; x < out.w; x++) {
          CHECK(out.get(x, y, C - 1) > 0);
          for (int c = 0; c < C - 1; c++) CHECK(std::abs(out.get(x, y, c) - in.get(x, y, c)) <= off);
        }
    }
  }
}

// However the plane is cut into tiles, the same pixels as one block of them through gauss_pixels(), to the bit:
// every format, a fill not 0, tiles of every kind, the plane 6 px into a tile, so that empty tiles lie within the
// margin of things either way, direct and boxes (gauss() by strips of tiles, the block and gauss_tile() a region at a
// time); gauss_tile() as gauss()'s tiles.
void check_tiling() {
  uint32_t seed = 100;
  for (const Format f : kFormats)
    for (const double sigma : {0.6, 3., 8., 11., 30., 120.}) {
      if (sigma > 100 && f.channels == 2) continue;  // the long one: 1 and 4 channels
      const int x0 = -250, y0 = 6, w = 900, h = 800, m = gauss_margin(sigma);
      const Pixel fill = f.alpha() ? Pixel{} : Pixel{uint16_t(max_value(f.depth) / 3)};
      const TileGrid g = grid_of(pattern(f, w, h, seed++), x0, y0, fill);
      size_t kinds[3] = {};  // empty is fill; solid, data
      for (const auto& [xy, t] : g.tiles()) kinds[t->is_solid() ? 1 : 2]++;
      CHECK(kinds[1] > 0 && kinds[2] > 0);
      const TileGrid out = gauss(g, sigma);
      // The block: what the grid reads round the plane, margin and all; out to as far as it reaches.
      const int bx = x0 - m, by = y0 - m, bw = w + 2 * m, bh = h + 2 * m;
      const Image block = blur_block(read(g, bx - m, by - m, bw + 2 * m, bh + 2 * m), 0, 0, bw, bh, sigma);
      if (!same(read(out, bx, by, bw, bh), block)) {
        std::fprintf(stderr, "gauss: %d channels, sigma %g: tiles and a block differ by %d\n", f.channels, sigma,
                     max_diff(read(out, bx, by, bw, bh), block));
        CHECK(false);
      }
      // Past that, fill only, and no tile.
      const TileRect b = out.bounds();
      CHECK(b.x0 * S >= bx - S && b.x1 * S <= bx + bw + S && b.y0 * S >= by - S && b.y1 * S <= by + bh + S);
      for (const TileXY xy : {TileXY{-2, 0}, TileXY{0, 1}, TileXY{1, 2}, TileXY{-1, 3}, TileXY{3, -1}}) {
        const TilePtr t = gauss_tile(g, sigma, xy.x, xy.y);
        TileGrid one(f, fill);
        one.set(xy.x, xy.y, t);
        CHECK(same(read(one, xy.x * S, xy.y * S, S, S), read(out, xy.x * S, xy.y * S, S, S)));
      }
    }
}

// The canvas's edges, both ways: extended (what is selected up to the edge stays so) or 0 past it (fading there, 50%
// on it), as one block padded so, to the bit; whatever lies past the canvas in its edge tiles does not count; tiles
// over the canvas only; Select All stays as it is, or fades at the edges.
void check_edges() {
  for (const Depth depth : {Depth::U8, Depth::U16}) {
    const Format f{depth, 1};
    const int W = 600, H = 500, top = max_value(depth);
    Image all(f, 3 * S, 2 * S);
    std::mt19937 rng(5);
    for (int y = 0; y < all.h; y++)
      for (int x = 0; x < all.w; x++) {
        int v = 0;
        if (x >= W || y >= H)
          v = int(rng() % unsigned(top + 1));  // past the canvas: anything
        else if ((x < 220 && y < 260) || (x >= 380 && y >= 300))
          v = top;  // touching the edges, top left and bottom right
        else
          v = int(std::lround(top * std::clamp(1.5 - std::hypot(x - 450., y - 120.) / 60, 0., 1.)));  // soft
        all.set(x, y, 0, v);
      }
    const TileGrid sel = grid_of(all, 0, 0);
    TileGrid every(f);
    for (int ty = 0; ty < 2; ty++)
      for (int tx = 0; tx < 3; tx++) every.set(tx, ty, Tile::solid(f, Pixel{uint16_t(top)}));
    for (const bool bounds : {false, true})
      for (const double radius : {3., 20., 120.}) {
        const double sigma = sigma_of_radius(radius);
        const int m = gauss_margin(sigma);
        const TileGrid out = feather_selection(sel, radius, W, H, bounds);
        for (const auto& [xy, t] : out.tiles()) CHECK(xy.x >= 0 && xy.x < 3 && xy.y >= 0 && xy.y < 2);
        Image padded(f, W + 2 * m, H + 2 * m);  // the canvas, extended or 0 past it
        for (int y = 0; y < padded.h; y++)
          for (int x = 0; x < padded.w; x++) {
            const int cx = x - m, cy = y - m;
            const bool on = cx >= 0 && cx < W && cy >= 0 && cy < H;
            padded.set(x, y, 0, on || !bounds ? all.get(std::clamp(cx, 0, W - 1), std::clamp(cy, 0, H - 1), 0) : 0);
          }
        const Image got = read(out, 0, 0, W, H);
        CHECK(same(got, blur_block(padded, 0, 0, W, H, sigma)));
        const TileGrid sa = feather_selection(every, radius, W, H, bounds);
        if (!bounds) {
          CHECK(sa.tiles().size() == 6);
          for (const auto& [xy, t] : sa.tiles()) CHECK(t == every.tiles().at(xy));
        }
        if (radius > 20) continue;  // what follows: things larger than the blur
        const double edge = got.get(0, 100, 0) / double(top), corner = got.get(0, 0, 0) / double(top);
        const double phi = 0.5 * std::erfc(-0.5 / sigma / std::sqrt(2.));  // Phi(0.5 / sigma): 0.5 px inside
        if (!bounds) CHECK(got.get(0, 100, 0) == top && got.get(0, 0, 0) == top && got.get(W - 1, H - 1, 0) == top);
        if (bounds) {
          CHECK(std::abs(edge - phi) < 0.01 && std::abs(corner - phi * phi) < 0.01);
          const Image s = read(sa, 0, 0, W, H);
          CHECK(std::abs(s.get(W / 2, 0, 0) / double(top) - phi) < 0.01 && s.get(W / 2, H / 2, 0) == top);
          CHECK(std::abs(s.get(W - 1, H - 1, 0) / double(top) - phi * phi) < 0.01);
        }
      }
  }
}

// Uniform and empty neighbourhoods are not filtered: Select All the size of D1 (8256 x 5504, 16 bits) feathered by
// 50 px shares its tiles and allocates nothing; nothing selected stays so; an ellipse on a 3000 x 2000 canvas gives
// data tiles only where the blur of a tile reads a data tile or two values, its other tiles the input's own or none,
// allocates only the data tiles it keeps, and gives each tile as gauss_tile() does.
void check_uniform() {
  const Format f{Depth::U16, 1};
  {
    const int W = 8256, H = 5504;
    TileGrid all(f);
    for (int ty = 0; ty < (H + S - 1) / S; ty++)
      for (int tx = 0; tx < (W + S - 1) / S; tx++) all.set(tx, ty, Tile::solid(f, Pixel{65535}));
    const size_t raw = tile_stats().raw;
    const TileGrid out = feather_selection(all, 50, W, H, false);
    CHECK(tile_stats().raw == raw && out.tiles().size() == all.tiles().size());
    for (const auto& [xy, t] : out.tiles()) CHECK(t == all.tiles().at(xy));
    CHECK(feather_selection(TileGrid(f), 50, W, H, false).tiles().empty());
    CHECK(feather_selection(TileGrid(f), 50, W, H, true).tiles().empty());
    CHECK(gauss(TileGrid(f), 50).tiles().empty());
  }
  const Format g8{Depth::U8, 1};
  const int W = 3000, H = 2000;
  TileGrid sel(g8);
  for (int ty = 0; ty < (H + S - 1) / S; ty++)
    for (int tx = 0; tx < (W + S - 1) / S; tx++) {
      // Coverage of the ellipse by 2 x 2 samples a pixel; the tile solid inside, empty outside.
      const auto inside = [](double x, double y) {
        const double u = (x - 1500) / 1100, v = (y - 1000) / 700;
        return u * u + v * v < 1;
      };
      Image t(g8, S, S);
      int n = 0;
      for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) {
          int c = 0;
          for (int k = 0; k < 4; k++) c += inside(tx * S + x + (k % 2 + 0.5) / 2, ty * S + y + (k / 2 + 0.5) / 2);
          t.set(x, y, 0, (c * 255 + 2) / 4);
          n += c;
        }
      if (n == 4 * S * S)
        sel.set(tx, ty, Tile::solid(g8, Pixel{255}));
      else if (n > 0)
        sel.write_rect(tx * S, ty * S, S, S, t.px.data(), t.stride());
    }
  const double radius = 30;
  const int m = gauss_margin(sigma_of_radius(radius));
  const size_t raw = tile_stats().raw;
  const TileGrid out = feather_selection(sel, radius, W, H, false);
  // By strips, the same pixels as gauss_tile() at every tile, where runs down end on solid rows as on empty ones.
  GaussEdges e;
  e.width = W, e.height = H;
  for (int ty = 0; ty < (H + S - 1) / S; ty++)
    for (int tx = 0; tx < (W + S - 1) / S; tx++) {
      TileGrid one(g8);
      one.set(tx, ty, gauss_tile(sel, sigma_of_radius(radius), tx, ty, e));
      CHECK(same(read(one, tx * S, ty * S, S, S), read(out, tx * S, ty * S, S, S)));
    }
  size_t kept = 0;
  for (int ty = 0; ty < (H + S - 1) / S; ty++)
    for (int tx = 0; tx < (W + S - 1) / S; tx++) {
      // The input under the blur of this tile, the canvas's edges extended: one kind of tile, solid or empty, or not.
      int kinds = 0;  // 1 empty, 2 solid, 4 data
      for (int y = std::max(0, (ty * S - m) / S); y <= std::min(H - 1, (ty + 1) * S + m - 1) / S; y++)
        for (int x = std::max(0, (tx * S - m) / S); x <= std::min(W - 1, (tx + 1) * S + m - 1) / S; x++) {
          const Tile* t = sel.get(x, y);
          kinds |= !t ? 1 : t->is_solid() ? 2 : 4;
        }
      const Tile* in = sel.get(tx, ty);
      const Tile* o = out.get(tx, ty);
      if (kinds == 1) CHECK(!o);
      if (kinds == 2) CHECK(o == in);
      if (o && !o->is_solid()) kept++;
    }
  CHECK(kept > 0 && tile_stats().raw == raw + kept * size_t(S) * S);
}

// The convention of mip levels: level 1 blurred by sigma / 2 matches level 0 blurred by sigma, then halved, to 1%.
void check_levels() {
  const Format f{Depth::U16, 1};
  Image im(f, 1024, 768);
  for (int y = 0; y < im.h; y++)
    for (int x = 0; x < im.w; x++) {
      const bool box = x > 200 && x < 520 && y > 150 && y < 600, disk = std::hypot(x - 760., y - 380.) < 150;
      im.set(x, y, 0, box || disk ? 65535 : int(mix(uint32_t(x * 31 + y * 977)) % 8000));
    }
  const TileGrid g = grid_of(im, 0, 0);
  const TileGrid full = gauss(g, 16);
  TileGrid half(f), level1(f);
  for (int ty = -1; ty < 3; ty++)
    for (int tx = -1; tx < 4; tx++) {
      half.set(tx, ty, mip(g, 1, tx, ty));
      level1.set(tx, ty, mip(full, 1, tx, ty));
    }
  const int d = max_diff(read(gauss(half, 8), -S / 2, -S / 2, 3 * S, 2 * S), read(level1, -S / 2, -S / 2, 3 * S, 2 * S));
  std::printf("gauss: level 1 at sigma 8 against level 0 at 16, halved: %.3f%% at most\n", 100. * d / 65535);
  CHECK(d <= 655);
  clear_mips();
}

}  // namespace

TEST(test_gauss_kernels) {
  check_margins();
  check_profile();
  check_direct();
  check_boxes();
}

TEST(test_gauss_grids) {
  check_sums();
  check_constant();
  check_tiling();
  check_edges();
  check_uniform();
  check_levels();
}

// Feather Selection: sigma_of_radius() through gauss() with the canvas's edges, 50% on a hard edge (as Photoshop puts
// it), radius 0 the selection itself, and what it refuses.
TEST(test_feather_selection) {
  const Format f{Depth::U16, 1};
  const int W = 1000, H = 800;
  Image im(f, W, H);
  for (int y = 250; y < 550; y++)
    for (int x = 300; x < 700; x++) im.set(x, y, 0, 65535);
  const TileGrid sel = grid_of(im, 0, 0);
  for (const bool bounds : {false, true}) {
    GaussEdges e;
    e.width = W, e.height = H, e.extend = !bounds;
    const TileGrid a = feather_selection(sel, 10, W, H, bounds), b = gauss(sel, sigma_of_radius(10), e);
    CHECK(same(read(a, 0, 0, W, H), read(b, 0, 0, W, H)));
    const TileGrid z = feather_selection(sel, 0, W, H, bounds);
    CHECK(z.tiles().size() == sel.tiles().size());
    for (const auto& [xy, t] : z.tiles()) CHECK(t == sel.tiles().at(xy));
  }
  const Image out = read(feather_selection(sel, 10, W, H, false), 0, 0, W, H);
  const int in = out.get(300, 400, 0), past = out.get(299, 400, 0);
  const double phi = 0.5 * std::erfc(-0.05 / std::sqrt(2.));  // Phi(0.5 / 10)
  CHECK(std::abs(in + past - 65535) <= 2 && std::abs(in / 65535. - phi) < 2e-3);
  CHECK(out.get(500, 400, 0) == 65535 && out.get(100, 400, 0) == 0);
  static const TileGrid s(f), rgba(Format{Depth::U16, 4}), grey(Format{Depth::U8, 2});
  CHECK(throws([] { feather_selection(rgba, 2, 10, 10, false); }));
  CHECK(throws([] { feather_selection(grey, 2, 10, 10, false); }));
  CHECK(throws([] { feather_selection(s, -0.5, 10, 10, false); }));
  CHECK(throws([] { feather_selection(s, kFeatherMax + 0.5, 10, 10, false); }));
  CHECK(throws([] { feather_selection(s, std::numeric_limits<double>::quiet_NaN(), 10, 10, false); }));
  CHECK(throws([] { feather_selection(s, 2, 0, 10, false); }));
  CHECK(throws([] { feather_selection(s, 2, 10, -1, true); }));
  CHECK(feather_selection(s, kFeatherMax, 10, 10, true).tiles().empty());
}
