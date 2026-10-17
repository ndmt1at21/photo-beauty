// composite_tile() (B1) against Photoshop's composites of the same two layers, and C1's dab masks against Photoshop's
// dabs. tests/golden/ps/ holds pb-bench ps-inputs' base.png and top.png and what tools/ps-golden/blend.jsx made of them
// in Photoshop (its README says how); an image not there yet is skipped, and said so.
#include <spng.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "check.h"
#include "dab.h"
#include "render.h"

using namespace pb;

namespace {

const std::string kDir = PB_SOURCE_DIR "/tests/golden/ps/";
constexpr Format kRgba16{Depth::U16, 4};
constexpr double kPi = 3.14159265358979323846;
constexpr Blend kModes[] = {Blend::Normal,  Blend::Darken,     Blend::Multiply,  Blend::ColorBurn,
                            Blend::Lighten, Blend::Screen,     Blend::ColorDodge, Blend::Overlay,
                            Blend::SoftLight, Blend::LinearLight, Blend::Difference};

// A PNG's pixels as RGBA, 16-bit levels; w 0 when there is no such file.
struct Image {
  int w = 0, h = 0, bits = 0;
  std::vector<uint16_t> px;
  const uint16_t* at(int x, int y) const { return &px[(size_t(y) * size_t(w) + size_t(x)) * 4]; }
};
Image read_png(const std::string& path) {
  Image im;
  const std::unique_ptr<FILE, int (*)(FILE*)> f(std::fopen(path.c_str(), "rb"), std::fclose);
  if (!f) return im;
  const std::unique_ptr<spng_ctx, void (*)(spng_ctx*)> ctx(spng_ctx_new(0), spng_ctx_free);
  spng_ihdr ihdr{};
  size_t bytes = 0;
  CHECK(spng_set_png_file(ctx.get(), f.get()) == 0 && spng_get_ihdr(ctx.get(), &ihdr) == 0 &&
        spng_decoded_image_size(ctx.get(), SPNG_FMT_RGBA16, &bytes) == 0);
  im.px.resize(bytes / 2);
  CHECK(spng_decode_image(ctx.get(), im.px.data(), bytes, SPNG_FMT_RGBA16, 0) == 0);  // host-endian
  im.w = int(ihdr.width), im.h = int(ihdr.height), im.bits = ihdr.bit_depth;
  return im;
}

// The image as a layer's pixels, every colour sample d levels up (down for d < 0), but none onto 0 or 65535 or off
// them: see slack below.
std::shared_ptr<const TileGrid> pixels(const Image& im, int d) {
  std::vector<uint16_t> px = im.px;
  for (size_t i = 0; i < px.size(); i++)
    if (i % 4 != 3 && px[i] > 0 && px[i] < 65535) px[i] = uint16_t(std::clamp(px[i] + d, 1, 65534));
  auto g = std::make_shared<TileGrid>(kRgba16);
  g->write_rect(0, 0, im.w, im.h, reinterpret_cast<const uint8_t*>(px.data()), size_t(im.w) * 8);
  return g;
}

// B1's composite of top over base as blend.jsx stacks them: top in mode m at opacity o, colours moved db, dt levels.
std::vector<uint16_t> composite(const Image& base, const Image& top, Blend m, float o, int db = 0, int dt = 0) {
  const auto layer = [](LayerId id, std::shared_ptr<const TileGrid> px, Blend b, float opacity) {
    auto l = std::make_shared<Layer>();
    l->id = id, l->props.blend = b, l->props.opacity = opacity, l->pixels = std::move(px);
    return LayerPtr(std::move(l));
  };
  auto root = std::make_shared<Layer>();
  root->kind = Kind::Group;
  root->children = {layer(1, pixels(base, db), Blend::Normal, 1), layer(2, pixels(top, dt), m, o)};
  auto d = std::make_shared<DocState>();
  d->width = base.w, d->height = base.h, d->depth = Depth::U16, d->root = std::move(root);
  TileGrid out(kRgba16);
  for (int ty = 0; ty * kTileSize < d->height; ty++)
    for (int tx = 0; tx * kTileSize < d->width; tx++) out.set(tx, ty, composite_tile({d}, 0, tx, ty));
  std::vector<uint16_t> px(base.px.size());
  out.read_rect(0, 0, base.w, base.h, reinterpret_cast<uint8_t*>(px.data()), size_t(base.w) * 8);
  return px;
}

// Sample c of a pixel, 0..1, premultiplied unless it is the alpha: how much it shows, so that the colour of a pixel
// all but transparent, which neither keeps precisely, weighs little.
double shown(const uint16_t* p, int c) { return c == 3 ? p[3] / 65535. : p[c] / 65535. * (p[3] / 65535.); }

}  // namespace

// Each P0 mode at 100 and 50% (`<mode>-<opacity>.png`, kBlendNames' names): B1 within a level of 8 bits of Photoshop
// (B4's bar) past the slack the inputs leave. Photoshop keeps 16-bit samples as 0..32768: base.png and top.png hold
// levels it reads exactly, but level v is v / 32768 to it and 2v / 65535 to B1, up to a 16-bit level apart, which
// moves a dodge or burn by its edge a lot; so where B1 makes much of moving each colour sample of either layer a level,
// Photoshop's result may be as far off as that.
TEST(test_ps_golden_blend) {
  Image base, top;
  std::vector<std::string> missing;
  bool ok = true;
  for (const Blend m : kModes)
    for (const int pct : {100, 50}) {
      const std::string name = std::string(kBlendNames[int(m)]) + "-" + std::to_string(pct);
      const Image ps = read_png(kDir + name + ".png");
      if (!ps.w) {
        missing.push_back(name);
        continue;
      }
      if (!base.w) base = read_png(kDir + "base.png"), top = read_png(kDir + "top.png");
      CHECK(base.w && top.w == base.w && top.h == base.h);
      if (ps.bits != 16 || ps.w != base.w || ps.h != base.h) {
        std::printf("ps golden %s: %d x %d at %d bits, not %d x %d at 16: made otherwise than blend.jsx says\n",
                    name.c_str(), ps.w, ps.h, ps.bits, base.w, base.h);
        ok = false;
        continue;
      }
      const float o = float(std::lround(pct * 255 / 100.)) / 255;  // Photoshop keeps opacity as a byte, as PSD does
      const auto ours = composite(base, top, m, o);
      std::vector<std::vector<uint16_t>> moved;
      for (const int db : {-1, 1})
        for (const int dt : {-1, 1}) moved.push_back(composite(base, top, m, o, db, dt));
      double worst = 0, raw = 0;
      size_t at = 0;
      for (size_t i = 0; i < ours.size(); i++) {
        const size_t p = i & ~size_t(3);
        const int c = int(i & 3);
        const double got = shown(&ours[p], c), off = std::abs(got - shown(&ps.px[p], c));
        double slack = 0;
        for (const auto& mv : moved) slack = std::max(slack, std::abs(shown(&mv[p], c) - got));
        raw = std::max(raw, off);
        if (off - slack > worst) worst = off - slack, at = i;
      }
      const size_t p = at & ~size_t(3);
      const int c = int(at & 3), x = int(p / 4 % size_t(base.w)), y = int(p / 4 / size_t(base.w));
      std::printf("ps golden %-15s %.3f levels of 8 bits off past the slack (%.3f before), at (%d, %d) channel %d: "
                  "B1 %.5f, Photoshop %.5f (shown)\n",
                  name.c_str(), worst * 255, raw * 255, x, y, c, shown(&ours[p], c), shown(&ps.px[p], c));
      if (name == "normal-50")  // pixel (0, 255): black under white, the red shows the opacity
        std::printf("ps golden: Photoshop's 50%% opacity is %.6f (%.6f here)\n", ps.at(0, 255)[0] / 65535., double(o));
      ok = ok && worst <= 1. / 255;
    }
  if (!missing.empty()) {
    std::string names;  // when only some are
    for (size_t i = 0; i < missing.size() && missing.size() < 22; i++) names += " " + missing[i];
    std::printf("test_ps_golden_blend: skipped %zu of 22, not in tests/golden/ps/ yet (tools/ps-golden/README.md)%s\n",
                missing.size(), names.c_str());
  }
  CHECK(ok);
}

// C1's dab masks against Photoshop's round brush (dabs.png: black on white at 100% opacity and flow, a row per size
// and a column per hardness, a dab in the middle of each 128 px cell, as blend.jsx's dabs() stamps them): how far
// apart, and how far out each covers a half and a tenth, to fit dab.cpp's kFalloffPower by. Printed only, no CHECK
// until Photoshop's dabs are here and the curve fitted.
TEST(test_ps_golden_dabs) {
  const Image ps = read_png(kDir + "dabs.png");
  if (!ps.w)
    return void(std::printf("test_ps_golden_dabs: skipped, no tests/golden/ps/dabs.png yet "
                            "(tools/ps-golden/README.md)\n"));
  constexpr int kCell = 128;
  const int sizes[] = {5, 19, 100}, hardness[] = {0, 50, 90, 100};
  CHECK(ps.bits == 16 && ps.w == 4 * kCell && ps.h == 3 * kCell);
  for (int j = 0; j < 3; j++)
    for (int i = 0; i < 4; i++) {
      const int x0 = i * kCell, y0 = j * kCell;
      const auto cover = [&](int x, int y) { return 1 - ps.at(x, y)[0] / 65535.; };  // Photoshop's: black is 1
      double sum = 0, cx = 0, cy = 0;  // its centre: the coverage's centroid
      for (int y = y0; y < y0 + kCell; y++)
        for (int x = x0; x < x0 + kCell; x++) {
          const double c = cover(x, y);
          sum += c, cx += c * (x + .5), cy += c * (y + .5);
        }
      if (!(sum > 0)) {
        std::printf("ps dab %d px, hardness %d%%: none in Photoshop's\n", sizes[j], hardness[i]);
        continue;
      }
      const DabMask m = make_dab_mask({float(cx / sum), float(cy / sum), float(sizes[j])}, hardness[i] / 100.f);
      const auto c1 = [&](int x, int y) {
        x -= m.x, y -= m.y;
        return x >= 0 && x < m.w && y >= 0 && y < m.h ? (*m.px)[size_t(y) * size_t(m.w) + size_t(x)] / 65535. : 0.;
      };
      double worst = 0, total = 0;
      int n = 0, half[2] = {}, tenth[2] = {};  // pixels covered at least so much: C1's, Photoshop's
      for (int y = y0; y < y0 + kCell; y++)
        for (int x = x0; x < x0 + kCell; x++) {
          const double a = c1(x, y), b = cover(x, y);
          worst = std::max(worst, std::abs(a - b));
          if (a > 0 || b > 0) total += std::abs(a - b), n++;
          half[0] += a >= .5, half[1] += b >= .5, tenth[0] += a >= .1, tenth[1] += b >= .1;
        }
      const auto radius = [&](int area) { return std::sqrt(area / kPi) / (sizes[j] / 2.); };  // of the dab's
      std::printf("ps dab %3d px, hardness %3d%%: C1 off by %.1f levels of 8 bits at most, %.2f on average; half "
                  "covered to %.2f r (Photoshop %.2f), a tenth to %.2f r (%.2f); centre (%.2f, %.2f)\n",
                  sizes[j], hardness[i], worst * 255, total / std::max(n, 1) * 255, radius(half[0]), radius(half[1]),
                  radius(tenth[0]), radius(tenth[1]), cx / sum - x0, cy / sum - y0);
    }
}
