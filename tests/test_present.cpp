// The canvas's frames (present.h): an sRGB document on an sRGB screen comes out as the composite's 8-bit values, the
// pasteboard and checkerboard around and under it; other profiles and screens as lcms converts; the GPU's frames as the
// CPU's at the zooms and turns the canvas takes, all four kinds of document; tiles whose mips the cache lacks from a
// coarser level, or the checkerboard where none has them, then sharp once the Visible job made them; the few mips a
// stroke changed made by the frame itself; the targets a presenter takes.
#include <lcms2.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <vector>

#include "check.h"
#include "color.h"
#include "composite.h"
#include "mip.h"
#include "present.h"

using namespace pb;

namespace {

constexpr int S = kTileSize;

// on_ready()'s calls, to wait for.
struct Ready {
  std::mutex m;
  std::condition_variable cv;
  int n = 0;
  std::function<void()> fn() {
    return [this] {
      const std::lock_guard l(m);
      n++;
      cv.notify_all();
    };
  }
  int count() {
    const std::lock_guard l(m);
    return n;
  }
  void wait_past(int k) {  // until more than k calls, a minute at most
    std::unique_lock l(m);
    CHECK(cv.wait_for(l, std::chrono::seconds(60), [&] { return n > k; }));
  }
};

// A frame's pixels: rows packed, RGBA8 a uint32_t.
struct Image {
  int w = 0, h = 0;
  std::vector<uint32_t> px;
  const uint8_t* at(int x, int y) const { return reinterpret_cast<const uint8_t*>(&px[size_t(y) * size_t(w) + size_t(x)]); }
};

// A frame, waited for: on the GPU into a buffer, downloaded, as the canvas would copy it into its texture.
Image shoot(Presenter& p, gpu::Device* dev, const RenderInput& in, const View& v, const Look& look = {},
            Presenter::Frame* out = nullptr) {
  Image im;
  const View::Pixels px = v.pixels();
  im.w = px.width, im.h = px.height;
  im.px.resize(size_t(im.w) * size_t(im.h));
  Target t;
  gpu::BufferPtr buf;
  if (dev)
    t.buffer = (buf = dev->buffer(im.px.size() * 4)).get();
  else
    t.pixels = im.px.data();
  const Presenter::Frame f = p.frame(in, v, t, look);
  if (dev) {
    dev->download(*buf, im.px.data(), im.px.size() * 4);
    dev->wait(dev->submit());
  }
  if (out) *out = f;
  return im;
}

// Frames until one is not pending: the sharp one.
Image settle(Presenter& p, Ready& r, gpu::Device* dev, const RenderInput& in, const View& v, const Look& look = {},
             Presenter::Frame* out = nullptr) {
  for (;;) {
    const int n = r.count();
    Presenter::Frame f;
    Image im = shoot(p, dev, in, v, look, &f);
    if (!f.pending) {
      if (out) *out = f;
      return im;
    }
    r.wait_past(n);
  }
}

int max_diff(const Image& a, const Image& b) {
  CHECK(a.w == b.w && a.h == b.h);
  int m = 0;
  for (int y = 0; y < a.h; y++)
    for (int x = 0; x < a.w; x++)
      for (int c = 0; c < 4; c++) m = std::max(m, std::abs(int(a.at(x, y)[c]) - int(b.at(x, y)[c])));
  return m;
}

View view(double x, double y, double zoom, double angle = 0, double w = 320, double h = 240, double dpr = 1) {
  View v;
  v.x = x, v.y = y, v.zoom = zoom, v.angle = angle, v.width = w, v.height = h, v.dpr = dpr;
  return v;
}

uint32_t hash(uint32_t x, uint32_t y, uint32_t seed) {
  uint32_t h = x * 0x9e3779b1u ^ (y + 0x7f4a7c15u) * 0x85ebca77u ^ seed * 0xc2b2ae3du;
  h ^= h >> 15, h *= 0x2c1b3c6du, h ^= h >> 12, h *= 0x297a2d39u, h ^= h >> 15;
  return h;
}

// A pixel layer of format f over a w x h canvas: tiles where has(tx, ty), pixels px(x, y) (R, G, B, alpha 0..1; grey
// takes R), compacted as a grid stores them.
LayerPtr layer(LayerId id, Format f, int w, int h, const std::function<bool(int, int)>& has,
               const std::function<std::array<double, 4>(int, int)>& px, Blend blend = Blend::Normal, float opacity = 1) {
  auto g = std::make_shared<TileGrid>(f);
  const int m = max_value(f.depth);
  for (int ty = 0; ty * S < h; ty++)
    for (int tx = 0; tx * S < w; tx++) {
      if (!has(tx, ty)) continue;
      const std::shared_ptr<Tile> t = Tile::make(f);
      {
        const WritePin wp = t->write();
        for (int y = 0; y < S; y++)
          for (int x = 0; x < S; x++) {
            const std::array<double, 4> v = px(tx * S + x, ty * S + y);
            const double c[4] = {v[0], f.channels == 4 ? v[1] : v[3], v[2], v[3]};
            for (int i = 0; i < f.channels; i++) {
              const uint16_t q = uint16_t(std::lround(std::clamp(c[i], 0., 1.) * m));
              uint8_t* at = wp.data() + (size_t(y) * S + size_t(x)) * size_t(f.bytes()) + size_t(i) * (f.depth == Depth::U16 ? 2 : 1);
              if (f.depth == Depth::U16)
                std::memcpy(at, &q, 2);
              else
                *at = uint8_t(q);
            }
          }
      }
      g->set(tx, ty, compact(t, {}));
    }
  auto l = std::make_shared<Layer>();
  l->id = id, l->props.blend = blend, l->props.opacity = opacity, l->pixels = std::move(g);
  return l;
}

DocPtr document(Format f, int w, int h, std::vector<LayerPtr> layers, std::shared_ptr<const std::vector<uint8_t>> icc) {
  auto root = std::make_shared<Layer>();
  root->kind = Kind::Group;
  root->children = std::move(layers);
  auto d = std::make_shared<DocState>();
  d->width = w, d->height = h, d->mode = f.channels == 4 ? Mode::Rgb : Mode::Gray, d->depth = f.depth;
  d->icc = std::move(icc), d->root = std::move(root);
  d->next_id = 10;
  return d;
}

std::shared_ptr<const std::vector<uint8_t>> profile(ColorSpace s) {
  return std::make_shared<const std::vector<uint8_t>>(icc_profile(s));
}

// The test document, 1500 x 1100 (partial tiles at the right and bottom): an opaque background of smooth colours and
// grain but for one tile (transparent there: checkerboard); blobs in Multiply at 70 % over half the tiles, their
// alpha smooth; specks in Screen, alpha 0, 1 and between, here and there. Its layers made once a format.
DocPtr sample(Format f, std::shared_ptr<const std::vector<uint8_t>> icc = nullptr) {
  constexpr int W = 1500, H = 1100;
  static std::map<std::pair<int, int>, std::vector<LayerPtr>> made;
  std::vector<LayerPtr>& layers = made[{int(f.depth), f.channels}];
  if (!layers.empty()) return document(f, W, H, layers, std::move(icc));
  const auto grain = [](int x, int y, uint32_t seed) { return (hash(uint32_t(x), uint32_t(y), seed) % 1000) / 1000.; };
  LayerPtr bg = layer(
      1, f, W, H, [](int tx, int ty) { return tx != 4 || ty != 1; },
      [&](int x, int y) {
        return std::array<double, 4>{x / double(W) * 0.8 + 0.1 * grain(x, y, 1), y / double(H) * 0.9 + 0.05 * grain(x, y, 2),
                                     0.5 + 0.4 * std::sin(x * 0.01 + y * 0.02), 1};
      });
  LayerPtr blobs = layer(
      2, f, W, H, [](int tx, int ty) { return (tx + ty) % 2 == 0; },
      [](int x, int y) {
        const double a = 0.5 + 0.5 * std::sin(x * 0.03) * std::cos(y * 0.025);
        return std::array<double, 4>{0.9, 0.3 + 0.5 * a, 0.2, a};
      },
      Blend::Multiply, 0.7f);
  LayerPtr specks = layer(
      3, f, W, H, [](int tx, int ty) { return tx % 3 == 1 || ty == 3; },
      [&](int x, int y) {
        const uint32_t r = hash(uint32_t(x), uint32_t(y), 3);
        const double a = r % 4 == 0 ? 0 : r % 4 == 1 ? 1 : (r >> 8) % 1000 / 1000.;
        return std::array<double, 4>{grain(x, y, 4), grain(x, y, 5), grain(x, y, 6), a};
      },
      Blend::Screen);
  layers = {bg, blobs, specks};
  return document(f, W, H, layers, std::move(icc));
}

constexpr Format kFormats[] = {{Depth::U8, 4}, {Depth::U16, 4}, {Depth::U8, 2}, {Depth::U16, 2}};

// Pixels of composite_tile() at a level: RGBA U16.
struct Composite {
  const RenderInput& in;
  int level;
  std::map<TileXY, TilePtr> tiles;
  Pixel at(int x, int y) {
    const TileXY t{x >> kTileLog2, y >> kTileLog2};
    auto it = tiles.find(t);
    if (it == tiles.end()) it = tiles.emplace(t, composite_tile(in, level, t.x, t.y)).first;
    const TilePtr& tile = it->second;
    if (!tile || tile->is_solid()) return tile ? tile->value() : Pixel{};
    const Pin p = tile->pin();
    const uint16_t* q = reinterpret_cast<const uint16_t*>(p.data()) + (size_t(y & (S - 1)) * S + size_t(x & (S - 1))) * 4;
    return {q[0], q[1], q[2], q[3]};
  }
};

// An sRGB document on an sRGB screen: past the canvas the pasteboard; on it the composite at the view's level, nearest
// from 100 % up, bilinear below on premultiplied texels within the canvas, over the checkerboard (its squares from the
// document's corner): an opaque texel's own 8-bit values where it is alone (nearest, or bilinear on its centre), else
// within a level of the exact mix.
void check_srgb(gpu::Device* dev) {
  Look look;
  look.pasteboard = {30, 40, 50}, look.light = {250, 240, 230}, look.dark = {100, 110, 120};
  for (const Format f : kFormats) {
    const RenderInput in{sample(f, f.depth == Depth::U8 ? profile(ColorSpace::Srgb) : nullptr)};
    Ready r;
    Presenter p(dev, r.fn());
    std::map<int, Composite> refs;
    for (const View& v : {view(1150, 400, 1), view(1400, 1000, 1), view(1100, 300, 8), view(1150, 400, 0.5),
                          view(1000, 602, 0.6, 0, 300, 200)}) {  // bilinear across a row of tiles at its bottom edge
      const Image im = settle(p, r, dev, in, v, look);
      const int L = v.level();
      Composite& ref = refs.emplace(L, Composite{in, L, {}}).first->second;
      const double k = std::ldexp(1.0, -L);
      const int tw = (1500 + (1 << L) - 1) >> L, th = (1100 + (1 << L) - 1) >> L;  // the canvas at the level, texels
      const std::array<double, 2> corner = v.to_canvas(0, 0);
      int opaque = 0, under = 0, past = 0;
      for (int j = 0; j < im.h; j++)
        for (int i = 0; i < im.w; i++) {
          const uint8_t* got = im.at(i, j);
          CHECK(got[3] == 255);
          const std::array<double, 2> d = v.to_doc(i + 0.5, j + 0.5);
          if (d[0] < 0 || d[1] < 0 || d[0] >= 1500 || d[1] >= 1100) {
            past++;
            for (int c = 0; c < 3; c++) CHECK(got[c] == look.pasteboard[size_t(c)]);
            continue;
          }
          double u = d[0] * k, w = d[1] * k, fx = 0, fy = 0;
          if (v.zoom < 1) u -= 0.5, w -= 0.5, fx = u - std::floor(u), fy = w - std::floor(w);
          const int x0 = int(std::floor(u)), y0 = int(std::floor(w));
          double mix[4] = {};  // premultiplied
          Pixel alone{};
          bool solid = true;  // every texel read opaque
          for (int t = 0; t < 4; t++) {
            const double weight = (t & 1 ? fx : 1 - fx) * (t & 2 ? fy : 1 - fy);
            if (weight == 0) continue;
            alone = ref.at(std::clamp(x0 + (t & 1), 0, tw - 1), std::clamp(y0 + (t >> 1), 0, th - 1));
            solid = solid && alone[3] == 65535;
            const double a = alone[3] / 65535.;
            for (int c = 0; c < 3; c++) mix[c] += weight * alone[size_t(c)] / 65535. * a;
            mix[3] += weight * a;
          }
          const bool dark = (int(std::floor((i + 0.5 - corner[0]) / 8)) + int(std::floor((j + 0.5 - corner[1]) / 8))) & 1;
          const std::array<uint8_t, 3>& sq = dark ? look.dark : look.light;
          (solid ? opaque : under)++;
          if (fx == 0 && fy == 0 && alone[3] == 65535) {
            for (int c = 0; c < 3; c++) CHECK(got[c] == (alone[size_t(c)] + 128) / 257);
            continue;
          }
          for (int c = 0; c < 3; c++) {
            const double e = mix[3] > 0 ? mix[c] / mix[3] : 0;
            const double want = (e * mix[3] + sq[size_t(c)] / 255. * (1 - mix[3])) * 255;
            CHECK(std::abs(got[c] - want) <= (fx == 0 && fy == 0 ? 0.5 + 1e-3 : 1));
          }
        }
      CHECK(opaque > 1000 && (under > 1000 || v.x == 1400));
      CHECK((past > 1000) == (v.x == 1400));
    }
  }
}

// The canvas's colours against lcms, at 100 %: documents in Adobe RGB, ProPhoto, sRGB and a grey gamma 2.2 profile, on
// sRGB's screen and Display P3's, each pixel within a level of lcms's relative colorimetric conversion of it.
void check_profiles(gpu::Device* dev) {
  cmsToneCurve* gamma = cmsBuildGamma(nullptr, 2.2);
  cmsHPROFILE grey = cmsCreateGrayProfile(cmsD50_xyY(), gamma);
  cmsUInt32Number n = 0;
  CHECK(cmsSaveProfileToMem(grey, nullptr, &n));
  std::vector<uint8_t> grey_icc(n);
  CHECK(cmsSaveProfileToMem(grey, grey_icc.data(), &n));
  cmsCloseProfile(grey);
  cmsFreeToneCurve(gamma);
  struct Case {
    std::vector<uint8_t> doc;
    bool grey;
    ColorSpace screen;
  };
  const Case cases[] = {{icc_profile(ColorSpace::AdobeRgb), false, ColorSpace::Srgb},
                        {icc_profile(ColorSpace::AdobeRgb), false, ColorSpace::DisplayP3},
                        {icc_profile(ColorSpace::ProPhoto), false, ColorSpace::Srgb},
                        {icc_profile(ColorSpace::Srgb), false, ColorSpace::DisplayP3},
                        {grey_icc, true, ColorSpace::Srgb}};
  for (const Case& k : cases) {
    const Format f{Depth::U16, k.grey ? 2 : 4};
    // Opaque colours all over the cube: saturated ones past the screen's gamut too.
    LayerPtr l = layer(
        1, f, 512, 512, [](int, int) { return true; },
        [](int x, int y) {
          const uint32_t r = hash(uint32_t(x), uint32_t(y), 9);
          return std::array<double, 4>{(r & 1023) / 1023., (r >> 10 & 1023) / 1023., (r >> 20 & 1023) / 1023., 1};
        });
    const RenderInput in{document(f, 512, 512, {l}, std::make_shared<const std::vector<uint8_t>>(k.doc))};
    Look look;
    if (k.screen != ColorSpace::Srgb) look.screen = display_profile(icc_profile(k.screen));
    CHECK((look.screen == nullptr) == (k.screen == ColorSpace::Srgb));
    const std::vector<uint8_t>& to = icc_profile(k.screen);
    cmsHPROFILE a = cmsOpenProfileFromMem(k.doc.data(), cmsUInt32Number(k.doc.size()));
    cmsHPROFILE b = cmsOpenProfileFromMem(to.data(), cmsUInt32Number(to.size()));
    // Unoptimised: lcms's own 16-bit transforms resample the route into a grid, up to 27 levels off at the gamut's edge.
    cmsHTRANSFORM t = cmsCreateTransform(a, k.grey ? TYPE_GRAY_16 : TYPE_RGB_16, b, TYPE_RGB_16, INTENT_RELATIVE_COLORIMETRIC,
                                         cmsFLAGS_BLACKPOINTCOMPENSATION | cmsFLAGS_NOOPTIMIZE);
    CHECK(t);
    Ready r;
    Presenter p(dev, r.fn());
    const View v = view(256, 256, 1, 0, 256, 256);
    const Image im = settle(p, r, dev, in, v, look);
    Composite ref{in, 0, {}};
    int worst = 0, off = 0;
    for (int j = 0; j < im.h; j++)
      for (int i = 0; i < im.w; i++) {
        const std::array<double, 2> d = v.to_doc(i + 0.5, j + 0.5);
        const Pixel px = ref.at(int(d[0]), int(d[1]));
        uint16_t out[3];
        cmsDoTransform(t, px.data(), out, 1);  // RGB or grey U16, alpha after: lcms reads the first 3 or 1
        for (int c = 0; c < 3; c++) {
          const int d = std::abs(int(im.at(i, j)[c]) - (out[c] + 128) / 257);
          worst = std::max(worst, d);
          off += d > 0;
        }
      }
    std::printf("present: %s on %s: %d values off by a level of %d, at most %d\n", k.grey ? "grey 2.2" : "rgb",
                k.screen == ColorSpace::Srgb ? "sRGB" : "Display P3", off, im.w * im.h * 3, worst);
    CHECK(worst <= 1);
    cmsDeleteTransform(t);
    cmsCloseProfile(a);
    cmsCloseProfile(b);
  }
}

// The GPU's frames against the CPU's: every kind of document, fit to the frame, 50 %, 100 %, 800 %, turned, at a
// device pixel ratio of 2; within a level everywhere.
void check_gpu_cpu(gpu::Device& dev) {
  const View views[] = {view(750, 550, 0.2),  view(600, 400, 0.5),       view(900, 700, 1),
                        view(1300, 900, 8),   view(800, 500, 0.7, 30),   view(700, 600, 1.5, 200),
                        view(1450, 80, 0.33, -75, 160, 120, 2)};
  for (const Format f : kFormats) {
    const RenderInput in{sample(f, f.channels == 4 && f.depth == Depth::U16 ? profile(ColorSpace::AdobeRgb) : nullptr)};
    Ready rg, rc;
    Presenter g(&dev, rg.fn()), c(nullptr, rc.fn());
    for (const View& v : views) {
      const Image a = settle(g, rg, &dev, in, v), b = settle(c, rc, nullptr, in, v);
      int off = 0;
      for (int y = 0; y < a.h; y++)
        for (int x = 0; x < a.w; x++)
          for (int k = 0; k < 3; k++) off += a.at(x, y)[k] != b.at(x, y)[k];
      const int d = max_diff(a, b);
      if (d > 1 || off) std::printf("present: zoom %g, angle %g: GPU and CPU %d levels apart, %d values\n", v.zoom, v.angle, d, off);
      CHECK(d <= 1);
      CHECK(off * 1000 < a.w * a.h * 3);  // a level apart rarely: the division and square root only
    }
  }
}

// What the job makes and the frame shows meanwhile: level 2 in the cache but not level 1 (as a .pbd hands over its
// mips) shows the frame at level 1 from level 2, then sharp; nothing at any level, the checkerboard until the job made
// it, unless the frame can make it itself (kFrameMips); a stroke shows at once, and after pen-up the frame makes the
// few mips above the changed tiles itself.
void check_levels(gpu::Device* dev, Image* coarse_out) {
  Look look;
  // Smooth colours, a layer with alpha over part of it.
  const Format f{Depth::U16, 4};
  LayerPtr base = layer(
      1, f, 2048, 2048, [](int, int) { return true; },
      [](int x, int y) { return std::array<double, 4>{x / 2048., y / 2048., 0.5 + 0.3 * std::sin(x * 0.004), 1}; });
  LayerPtr over = layer(
      2, f, 2048, 2048, [](int tx, int ty) { return tx > 2 && ty < 6; },
      [](int x, int y) { return std::array<double, 4>{0.2, 0.7, 0.4, 0.5 + 0.4 * std::cos((x + y) * 0.003)}; },
      Blend::Overlay);
  const RenderInput in{document(f, 2048, 2048, {base, over}, nullptr)};
  clear_mips();
  std::map<std::array<int, 3>, TilePtr> level2;
  for (const LayerPtr& l : in.doc->root->children)
    for (int ty = 0; ty < 2; ty++)
      for (int tx = 0; tx < 2; tx++) level2[{int(l->id), tx, ty}] = mip(*l->pixels, 2, tx, ty);
  clear_mips();
  for (const LayerPtr& l : in.doc->root->children)
    for (int ty = 0; ty < 2; ty++)
      for (int tx = 0; tx < 2; tx++) attach_mip(*l->pixels, 2, tx, ty, level2[{int(l->id), tx, ty}]);
  Ready r;
  Presenter p(dev, r.fn());
  const View v = view(1024, 1024, 0.4, 0, 400, 300);  // level 1
  Presenter::Frame f1, f2;
  const Image coarse = shoot(p, dev, in, v, look, &f1);
  CHECK(f1.pending && f1.tiles == 4 && f1.coarser == 4 && f1.missing == 0 && f1.built == 0);
  const Image sharp = settle(p, r, dev, in, v, look, &f2);
  CHECK(f2.tiles == f1.tiles && f2.coarser == 0 && f2.missing == 0 && f2.built == 0);
  const int d = max_diff(coarse, sharp);
  CHECK(d > 0 && d <= 12);  // smooth colours: level 2 stood in for level 1 closely
  if (coarse_out) *coarse_out = coarse;

  // 13 opaque layers of solid tiles over 4096 x 4096, the top one's colour on top: at level 4 a tile of 85 mips a
  // layer, more than a frame makes.
  const auto solid = [](int layers, int tiles) {
    std::vector<LayerPtr> ls;
    for (int i = 0; i < layers; i++) {
      auto g = std::make_shared<TileGrid>(Format{Depth::U8, 4});
      const TilePtr t = Tile::solid({Depth::U8, 4}, {uint16_t(10 + 15 * i), uint16_t(200 - 9 * i), uint16_t(7 * i), 255});
      for (int ty = 0; ty < tiles; ty++)
        for (int tx = 0; tx < tiles; tx++) g->set(tx, ty, t);
      auto l = std::make_shared<Layer>();
      l->id = LayerId(i + 1), l->pixels = std::move(g);
      ls.push_back(l);
    }
    return document({Depth::U8, 4}, tiles * S, tiles * S, std::move(ls), nullptr);
  };
  clear_mips();
  look.light = {255, 255, 255}, look.dark = {204, 204, 204};
  const View far = view(2048, 2048, 1 / 16., 0, 300, 300);  // the 256 x 256 tile of level 4, pasteboard around
  {
    const RenderInput many{solid(13, 16)};
    Presenter::Frame g;
    const Image none = shoot(p, dev, many, far, look, &g);
    CHECK(g.pending && g.tiles == 1 && g.missing == 1 && g.coarser == 0 && g.built == 0);
    for (int j = 22; j < 278; j++)
      for (int i = 22; i < 278; i++) CHECK(none.at(i, j)[0] == 255 || none.at(i, j)[0] == 204);
    const Image top = settle(p, r, dev, many, far, look, &g);
    CHECK(g.missing == 0 && g.built == 0);
    CHECK(top.at(150, 150)[0] == 10 + 15 * 12 && top.at(150, 150)[1] == 200 - 9 * 12 && top.at(150, 150)[2] == 7 * 12);
    CHECK(top.at(5, 5)[0] == look.pasteboard[0]);
  }
  {
    const RenderInput few{solid(2, 16)};
    Presenter::Frame g;
    const Image now = shoot(p, dev, few, far, look, &g);
    CHECK(!g.pending && g.missing == 0 && g.coarser == 0 && g.built == 2 * 85);
    CHECK(now.at(150, 150)[0] == 25 && now.at(150, 150)[1] == 191);
  }

  // A stroke shows at once; after pen-up the frame makes the mips above the two changed tiles itself.
  const View fit = view(1024, 1024, 0.15, 0, 320, 320);  // level 2
  settle(p, r, dev, in, fit, look);
  auto buffer = std::make_shared<TileGrid>(Format{Depth::U16, 1});
  buffer->set(1, 1, Tile::solid({Depth::U16, 1}, {40000}));
  buffer->set(2, 1, Tile::solid({Depth::U16, 1}, {65535}));
  RenderInput stroke = in;
  stroke.stroke = StrokeOverlay{1, buffer, {1, 0, 0}, 1, false};
  Presenter::Frame g;
  const Image before = shoot(p, dev, in, fit, look), painted = shoot(p, dev, stroke, fit, look, &g);
  CHECK(!g.pending && g.coarser == 0 && g.missing == 0);
  CHECK(max_diff(before, painted) > 100);
  auto pixels = std::make_shared<TileGrid>(*base->pixels);
  const uint16_t red[4] = {65535, 0, 0, 65535};
  pixels->write_rect(300, 300, 1, 1, reinterpret_cast<const uint8_t*>(red), 8);  // in tile (1, 1)
  pixels->write_rect(700, 300, 1, 1, reinterpret_cast<const uint8_t*>(red), 8);  // in tile (2, 1)
  const RenderInput up{run(*in.doc, SetPixels{1, pixels})};
  const Image after = shoot(p, dev, up, fit, look, &g);
  CHECK(!g.pending && g.coarser == 0 && g.missing == 0 && g.built == 2 + 1);  // two at level 1, their parent at 2
  CHECK(max_diff(before, after) > 0);
}

// An edit shows in the next frame: a layer's opacity, then the layer hidden, each frame as a new presenter's.
void check_edits(gpu::Device* dev) {
  const RenderInput in{sample({Depth::U16, 4})};
  Ready r, r2;
  Presenter p(dev, r.fn());
  const View v = view(700, 500, 0.45, 15);
  const Image a = settle(p, r, dev, in, v);
  LayerProps props = find_layer(*in.doc, 2)->props;
  props.opacity = 0.3f;
  const RenderInput faded{run(*in.doc, SetProps{2, props})};
  props.visible = false;
  const RenderInput hidden{run(*faded.doc, SetProps{2, props})};
  for (const RenderInput* e : {&faded, &hidden}) {
    const Image b = settle(p, r, dev, *e, v);
    Presenter fresh(dev, r2.fn());
    CHECK(max_diff(a, b) > 10 && max_diff(b, settle(fresh, r2, dev, *e, v)) == 0);
  }
}

void check_throws(gpu::Device* dev) {
  Presenter p(dev);
  std::vector<uint32_t> px(64 * 64);
  gpu::BufferPtr buf = dev ? dev->buffer(px.size() * 4) : nullptr;
  Target t, other;
  (dev ? t.buffer : other.buffer) = dev ? buf.get() : reinterpret_cast<gpu::Buffer*>(&px);  // the CPU reads no buffer
  (dev ? other.pixels : t.pixels) = px.data();
  const View v = view(10, 10, 1, 0, 64, 64);
  const auto throws = [&](const RenderInput& in, const Target& tt) {
    try {
      p.frame(in, v, tt);
    } catch (const std::invalid_argument&) {
      return true;
    }
    return false;
  };
  const RenderInput in{sample({Depth::U8, 2})};
  CHECK(throws(RenderInput{}, t));
  CHECK(throws(in, other));  // a GPU presenter takes a buffer, a CPU one pixels
  CHECK(throws(in, Target{}));
  const Presenter::Frame none = p.frame(in, view(10, 10, 1, 0, 0, 64), t);  // an empty canvas
  CHECK(none.tiles == 0 && none.submission == 0);
  const Presenter::Frame f = p.frame(in, v, t);
  CHECK(f.tiles == 1 && !f.pending);
  if (dev) dev->wait(f.submission);
}

}  // namespace

TEST(test_present) {
  check_throws(nullptr);
  check_srgb(nullptr);
  check_profiles(nullptr);
  Image cpu_coarse;
  check_levels(nullptr, &cpu_coarse);
  check_edits(nullptr);
  const auto dev = test::gpu_device();
  if (!dev) return;
  std::printf("present: %s\n", dev->name().c_str());
  check_throws(dev.get());
  check_srgb(dev.get());
  check_profiles(dev.get());
  Image gpu_coarse;
  check_levels(dev.get(), &gpu_coarse);
  CHECK(max_diff(cpu_coarse, gpu_coarse) <= 1);  // the coarser level's frame alike too
  check_edits(dev.get());
  check_gpu_cpu(*dev);
  clear_mips();
}
