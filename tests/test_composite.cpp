// composite_tile() (render.h), its plan (composite.h) and its formulas (blend.h): each P0 mode against W3C's formula
// (Photoshop's where they differ), Normal, what is skipped, isolated and Pass Through groups, the stroke overlay, 8 bits
// against 16, grey against RGB, mip levels 1 to 6. Documents are made by hand.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <random>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

#include "blend.h"
#include "check.h"
#include "composite.h"
#include "mip.h"
#include "render.h"

using namespace pb;

namespace {

constexpr int S = kTileSize;
constexpr size_t kPixels = size_t(S) * S;
constexpr Format kRgba16{Depth::U16, 4}, kCover{Depth::U16, 1};
constexpr Blend kModes[] = {Blend::Normal,     Blend::Darken,  Blend::Multiply,  Blend::ColorBurn,
                            Blend::Lighten,    Blend::Screen,  Blend::ColorDodge, Blend::Overlay,
                            Blend::SoftLight,  Blend::LinearLight, Blend::Difference};

// B(Cb, Cs) afresh from the definitions: Screen as an inverted Multiply, Photoshop's Soft Light in its usual form.
double ref_b(Blend m, double cb, double cs) {
  switch (m) {
    case Blend::Darken: return std::min(cb, cs);
    case Blend::Multiply: return cb * cs;
    case Blend::ColorBurn: return cb == 1 ? 1 : cs == 0 ? 0 : 1 - std::min(1., (1 - cb) / cs);
    case Blend::Lighten: return std::max(cb, cs);
    case Blend::Screen: return 1 - (1 - cb) * (1 - cs);
    case Blend::ColorDodge: return cb == 0 ? 0 : cs == 1 ? 1 : std::min(1., cb / (1 - cs));
    case Blend::Overlay: return cb < 0.5 ? 2 * cb * cs : 1 - 2 * (1 - cb) * (1 - cs);
    case Blend::SoftLight:
      return cs < 0.5 ? 2 * cb * cs + cb * cb * (1 - 2 * cs) : 2 * cb * (1 - cs) + std::sqrt(cb) * (2 * cs - 1);
    case Blend::LinearLight: return std::clamp(cb + 2 * cs - 1, 0., 1.);
    case Blend::Difference: return std::abs(cb - cs);
    default: return cs;
  }
}
// W3C's source-over with blending on colours not premultiplied: the source (cs, as) over the backdrop (cb, ab) in m.
// {colour premultiplied, alpha} of the result.
std::array<double, 2> ref_over(Blend m, double cb, double ab, double cs, double as) {
  return {as * ((1 - ab) * cs + ab * ref_b(m, cb, cs)) + ab * (1 - as) * cb, as + ab * (1 - as)};
}

// A sample of depth d, the edges 0 and max often.
uint16_t sample(std::mt19937& rng, Depth d) {
  const unsigned m = unsigned(max_value(d)), r = rng() % 8;
  return uint16_t(r == 0 ? 0 : r == 1 ? m : rng() % (m + 1));
}
Pixel random_px(std::mt19937& rng, Format f) {
  Pixel v{};
  for (int c = 0; c < f.channels; c++) v[c] = sample(rng, f.depth);
  return v;
}
// A data tile of f, pixel i (row by row) px(i).
TilePtr make_tile(Format f, const std::function<Pixel(size_t)>& px) {
  const std::shared_ptr<Tile> t = Tile::make(f);
  const WritePin w = t->write();
  for (size_t i = 0; i < kPixels; i++) {
    const Pixel v = px(i);
    for (int c = 0; c < f.channels; c++)
      if (f.depth == Depth::U16)
        std::memcpy(w.data() + (i * f.channels + c) * 2, &v[c], 2);
      else
        w.data()[i * f.channels + c] = uint8_t(v[c]);
  }
  return t;
}
std::shared_ptr<const TileGrid> grid(Format f, const std::vector<std::pair<TileXY, TilePtr>>& tiles) {
  auto g = std::make_shared<TileGrid>(f);
  for (const auto& [xy, t] : tiles) g->set(xy.x, xy.y, t);
  return g;
}
std::shared_ptr<const TileGrid> at00(const TilePtr& t) { return grid(t->format(), {{{0, 0}, t}}); }
std::shared_ptr<const TileGrid> grey(uint16_t v) { return at00(Tile::solid(kRgba16, {v, v, v, 65535})); }
const uint16_t* samples(const Pin& p) { return reinterpret_cast<const uint16_t*>(p.data()); }

LayerPtr pixel(LayerId id, std::shared_ptr<const TileGrid> px, Blend b = Blend::Normal, float opacity = 1, float fill = 1) {
  auto l = std::make_shared<Layer>();
  l->id = id;
  l->props.blend = b, l->props.opacity = opacity, l->props.fill = fill;
  l->pixels = std::move(px);
  return l;
}
LayerPtr group(LayerId id, std::vector<LayerPtr> children, Blend b = Blend::PassThrough, float opacity = 1) {
  auto l = std::make_shared<Layer>();
  l->id = id, l->kind = Kind::Group;
  l->props.blend = b, l->props.opacity = opacity;
  l->children = std::move(children);
  return l;
}
LayerPtr hidden(const LayerPtr& l) {
  auto h = std::make_shared<Layer>(*l);
  h->props.visible = false;
  return h;
}
DocPtr doc(std::vector<LayerPtr> layers, Depth depth = Depth::U16, Mode mode = Mode::Rgb, int w = 4 * S, int h = 4 * S) {
  auto root = std::make_shared<Layer>();
  root->kind = Kind::Group;
  root->children = std::move(layers);
  auto d = std::make_shared<DocState>();
  d->width = w, d->height = h, d->mode = mode, d->depth = depth, d->root = std::move(root);
  return d;
}
// A composite tile's pixels, 4 samples each row by row (zeros for null).
std::vector<uint16_t> pixels_of(const TilePtr& t) {
  std::vector<uint16_t> px(kPixels * 4);
  if (!t) return px;
  CHECK(t->format() == kRgba16);
  TileGrid g(kRgba16);
  g.set(0, 0, t);
  g.read_rect(0, 0, S, S, reinterpret_cast<uint8_t*>(px.data()), S * 8);
  return px;
}
std::vector<uint16_t> composite(const DocPtr& d, int tx = 0, int ty = 0, std::optional<StrokeOverlay> stroke = {}) {
  return pixels_of(composite_tile({d, std::move(stroke)}, 0, tx, ty));
}
int max_diff(const std::vector<uint16_t>& a, const std::vector<uint16_t>& b) {
  int d = 0;
  for (size_t i = 0; i < a.size(); i++) d = std::max(d, std::abs(a[i] - b[i]));
  return d;
}

}  // namespace

// Each mode's formula over random pixels, the edges 0, 0.5, 1 and alpha 0 and 1 among them, within 1e-6 of W3C's
// (Photoshop's Soft Light, Linear Light and dodge and burn edges).
TEST(test_composite_formulas) {
  std::mt19937 rng(7);
  const auto unit = [&] {
    const unsigned r = rng() % 8;
    return r == 0 ? 0.f : r == 1 ? 1.f : r == 2 ? 0.5f : float(rng() >> 8) * 0x1p-24f;
  };
  for (const Blend m : kModes) {
    double worst = 0;
    for (int i = 0; i < 100'000; i++) {
      const float ab = unit(), as = unit(), cs = unit(), cb_p = unit() * ab;  // the backdrop as accumulated
      const float cb = ab > 0 ? cb_p / ab : 0;                                // and its colour
      const auto [co, ao] = ref_over(m, cb, ab, cs, as);
      worst = std::max({worst, std::abs(blend::composite(m, cb_p, ab, cs, as) - co),
                        std::abs(blend::over_alpha(ab, as) - ao)});
    }
    CHECK(worst <= 1e-6);
  }
}

// composite_tile() in each mode over a random backdrop at random opacity and fill: W3C's result to the 16-bit level;
// the dodge and burn edges exactly.
TEST(test_composite_modes) {
  std::mt19937 rng(3);
  for (const Blend m : kModes) {
    const TilePtr below = make_tile(kRgba16, [&](size_t) { return random_px(rng, kRgba16); });
    const TilePtr above = make_tile(kRgba16, [&](size_t) { return random_px(rng, kRgba16); });
    const float opacity = 0.2f + 0.8f * float(rng() % 1024) / 1023, fill = 0.2f + 0.8f * float(rng() % 1024) / 1023;
    const auto got = composite(doc({pixel(1, at00(below)), pixel(2, at00(above), m, opacity, fill)}));
    const Pin pb = below->pin(), pa = above->pin();
    const uint16_t *b = samples(pb), *a = samples(pa);
    for (size_t i = 0; i < kPixels * 4; i += 4) {
      const double ab = b[i + 3] / 65535., as = a[i + 3] / 65535. * opacity * fill, ao = as + ab * (1 - as);
      CHECK(std::abs(got[i + 3] - ao * 65535) <= 1);
      if (!got[i + 3]) CHECK(!got[i] && !got[i + 1] && !got[i + 2]);  // transparent is all 0
      if (ao * 65535 < 1) continue;                                  // too faint for a colour
      for (int c = 0; c < 3; c++) {
        const auto level = [&](double cb, double cs) {
          return ref_over(m, std::clamp(cb, 0., 1.), ab, std::clamp(cs, 0., 1.), as)[0] / ao * 65535;
        };
        // Where float32 cannot pin the colour down (a dodge or burn by its edge moves a lot for an ulp of Cb or Cs), as
        // much slack as a few ulps of either move it; 0 and 1 stay exact.
        const double cb = b[i + c] / 65535., cs = a[i + c] / 65535.;
        const auto ulps = [](double x, double n) { return x > 0 && x < 1 ? x * (1 + n * 1e-7) : x; };
        const double slack = std::abs(level(ulps(cb, 4), cs) - level(ulps(cb, -4), cs)) +
                             std::abs(level(cb, ulps(cs, 4)) - level(cb, ulps(cs, -4)));
        CHECK(std::abs(got[i + c] - level(cb, cs)) <= 1 + slack);
      }
    }
  }
  // White burned by black stays white, black dodged by white stays black; a level off, the formula takes over.
  CHECK(composite(doc({pixel(1, grey(65535)), pixel(2, grey(0), Blend::ColorBurn)}))[0] == 65535);
  CHECK(composite(doc({pixel(1, grey(65534)), pixel(2, grey(0), Blend::ColorBurn)}))[0] == 0);
  CHECK(composite(doc({pixel(1, grey(0)), pixel(2, grey(65535), Blend::ColorDodge)}))[0] == 0);
  CHECK(composite(doc({pixel(1, grey(1)), pixel(2, grey(65535), Blend::ColorDodge)}))[0] == 65535);
}

// Normal at 100 % over a solid backdrop gives the layer above, every 16-bit value as it was; solid in, solid out;
// nothing visible, null; only the canvas shows; mip levels are 0 to 31.
TEST(test_composite_normal) {
  std::mt19937 rng(11);
  const TilePtr base = Tile::solid(kRgba16, {1000, 30000, 60000, 65535});
  const TilePtr above = make_tile(kRgba16, [&](size_t i) { return Pixel{uint16_t(i), uint16_t(~i), uint16_t(rng()), 65535}; });
  const Pin p = above->pin();
  CHECK(std::memcmp(composite(doc({pixel(1, at00(base)), pixel(2, at00(above))})).data(), p.data(), kPixels * 8) == 0);
  const Format rgba8{Depth::U8, 4};
  const TilePtr base8 = Tile::solid(rgba8, {10, 120, 250, 255});
  const TilePtr above8 = make_tile(rgba8, [&](size_t i) { return Pixel{uint16_t(i & 255), uint16_t(i >> 8), uint16_t(rng() & 255), 255}; });
  const auto got8 = composite(doc({pixel(1, at00(base8)), pixel(2, at00(above8))}, Depth::U8));
  const Pin p8 = above8->pin();
  for (size_t i = 0; i < kPixels * 4; i++) CHECK(got8[i] == p8.data()[i] * 257);

  const TilePtr flat = composite_tile({doc({pixel(1, at00(base))})}, 0, 0, 0);
  CHECK(flat && flat->is_solid() && flat->value() == base->value());
  const TilePtr flat8 = composite_tile({doc({pixel(1, at00(base8))}, Depth::U8)}, 0, 0, 0);
  CHECK(flat8 && flat8->is_solid() && flat8->value() == (Pixel{10 * 257, 120 * 257, 250 * 257, 65535}));
  CHECK(!composite_tile({doc({})}, 0, 0, 0));
  CHECK(!composite_tile({doc({pixel(1, at00(base), Blend::Normal, 0), hidden(pixel(2, at00(base))),
                              pixel(3, at00(base), Blend::Multiply, 1, 0)})},
                        0, 0, 0));

  // A 300 x 200 canvas: of tile (1, 0), 44 x 200 pixels; past the canvas nothing, though the layer spills there.
  std::vector<std::pair<TileXY, TilePtr>> spill;
  for (int y = -1; y <= 1; y++)
    for (int x = -1; x <= 2; x++) spill.push_back({{x, y}, base});
  const DocPtr small = doc({pixel(1, grid(kRgba16, spill))}, Depth::U16, Mode::Rgb, 300, 200);
  const auto edge = composite(small, 1, 0);
  for (int y = 0; y < S; y++)
    for (int x = 0; x < S; x++) CHECK(edge[(size_t(y) * S + size_t(x)) * 4 + 3] == (x < 44 && y < 200 ? 65535 : 0));
  for (const TileXY t : {TileXY{-1, 0}, TileXY{2, 0}, TileXY{0, 1}, TileXY{0, -1}}) CHECK(!composite_tile({small}, 0, t.x, t.y));
  for (const int level : {-1, 32}) {
    bool threw = false;
    try {
      composite_tile({small}, level, 0, 0);
    } catch (const std::invalid_argument&) {
      threw = true;
    }
    CHECK(threw);
  }
}

// Layers without pixels at the tile, hidden ones, ones at opacity or fill 0 and groups of none change nothing there.
TEST(test_composite_skips) {
  std::mt19937 rng(13);
  const auto rnd = [&] { return at00(make_tile(kRgba16, [&](size_t) { return random_px(rng, kRgba16); })); };
  const auto elsewhere = grid(kRgba16, {{{3, 1}, Tile::solid(kRgba16, {9, 9, 9, 65535})}, {{-1, 0}, Tile::solid(kRgba16, {1, 2, 3, 400})}});
  const auto a = rnd(), b = rnd(), c = rnd();
  const auto want = composite(doc({pixel(1, a), pixel(2, b, Blend::Multiply, 0.7f), pixel(3, c, Blend::Screen, 0.5f, 0.8f)}));
  CHECK(composite(doc({pixel(10, elsewhere, Blend::Difference), pixel(1, a), hidden(pixel(11, rnd(), Blend::Difference)),
                       pixel(12, nullptr, Blend::ColorBurn),
                       group(13, {pixel(14, elsewhere, Blend::Multiply), group(15, {})}, Blend::Normal),
                       pixel(2, b, Blend::Multiply, 0.7f), group(16, {pixel(17, elsewhere)}, Blend::PassThrough, 0.5f),
                       hidden(group(18, {pixel(19, rnd(), Blend::Difference)}, Blend::Normal)),
                       pixel(20, rnd(), Blend::Difference, 0), pixel(3, c, Blend::Screen, 0.5f, 0.8f),
                       pixel(21, rnd(), Blend::Overlay, 1, 0), pixel(22, elsewhere, Blend::LinearLight)})) == want);
  CHECK(!composite_tile({doc({pixel(1, elsewhere), group(2, {pixel(3, elsewhere)}, Blend::Multiply)})}, 0, 0, 0));
}

// Pass Through at full opacity is its layers ungrouped; below it, the backdrop mixed with what they made of it. An
// isolated group composites its layers apart, then blends as one layer.
TEST(test_composite_groups) {
  std::mt19937 rng(17);
  const auto rnd = [&] {  // alpha 0.46 up: colours keep their precision through the 16-bit results mixed below
    return at00(make_tile(kRgba16, [&](size_t) {
      Pixel v = random_px(rng, kRgba16);
      v[3] = uint16_t(30000 + rng() % 35536);
      return v;
    }));
  };
  const auto bg = rnd(), a = rnd(), b = rnd(), c = rnd();
  CHECK(composite(doc({pixel(1, bg), group(5, {pixel(2, a, Blend::Normal, 0.6f), group(6, {pixel(3, b, Blend::Normal, 1, 0.7f)})}), pixel(4, c)})) ==
        composite(doc({pixel(1, bg), pixel(2, a, Blend::Normal, 0.6f), pixel(3, b, Blend::Normal, 1, 0.7f), pixel(4, c)})));

  const auto before = composite(doc({pixel(1, bg)}));
  const auto after = composite(doc({pixel(1, bg), pixel(2, a, Blend::Multiply), pixel(3, b, Blend::Screen, 0.5f)}));
  const auto got = composite(doc({pixel(1, bg), group(5, {pixel(2, a, Blend::Multiply), pixel(3, b, Blend::Screen, 0.5f)}, Blend::PassThrough, 0.4f)}));
  for (size_t i = 0; i < kPixels * 4; i += 4) {
    const double a0 = before[i + 3] / 65535., a1 = after[i + 3] / 65535., am = 0.6 * a0 + 0.4 * a1;
    CHECK(std::abs(got[i + 3] - am * 65535) <= 1);
    for (int k = 0; k < 3; k++) CHECK(std::abs(got[i + k] - (0.6 * before[i + k] * a0 + 0.4 * after[i + k] * a1) / am) <= 2);
  }

  // A Multiply layer in a group over grey 0.6: Pass Through multiplies the backdrop by its 0.5; isolated, the layer has
  // nothing below it in the group, which then shows its 0.5 as it is.
  const auto multiply = [&](Blend g) { return composite(doc({pixel(1, grey(39321)), group(2, {pixel(3, grey(32768), Blend::Multiply)}, g)}))[0]; };
  CHECK(multiply(Blend::PassThrough) == 19661);  // 39321 x 32768 / 65535 = 19660.8
  CHECK(multiply(Blend::Normal) == 32768);
  // As one layer: in its own mode, at its opacity, as its layer would be; two deep, a Pass Through at half opacity
  // in an isolated group halves its layer's alpha there.
  CHECK(max_diff(composite(doc({pixel(1, bg), group(3, {pixel(2, a)}, Blend::Screen, 0.6f)})),
                 composite(doc({pixel(1, bg), pixel(2, a, Blend::Screen, 0.6f)}))) <= 1);
  CHECK(max_diff(composite(doc({pixel(1, bg), group(3, {group(4, {pixel(2, a)}, Blend::PassThrough, 0.5f), pixel(5, b, Blend::Multiply)}, Blend::Normal)})),
                 composite(doc({pixel(1, bg), group(3, {pixel(2, a, Blend::Normal, 0.5f), pixel(5, b, Blend::Multiply)}, Blend::Normal)}))) <= 1);
}

// The stroke on its layer: painting lays its colour at opacity x coverage over the layer's pixels, erasing takes
// their alpha down by as much; then the layer's own mode and opacity, at any depth.
TEST(test_composite_stroke) {
  std::mt19937 rng(19);
  const TilePtr pixels = make_tile(kRgba16, [&](size_t) { return random_px(rng, kRgba16); });
  const TilePtr cover = make_tile(kCover, [&](size_t) { return Pixel{sample(rng, Depth::U16)}; });
  StrokeOverlay s{7, at00(cover), {0.2f, 0.9f, 0.4f}, 0.8f, false};
  const DocPtr d = doc({pixel(7, at00(pixels))});
  const Pin pp = pixels->pin(), pc = cover->pin();
  const uint16_t *l = samples(pp), *cv = samples(pc);
  for (const bool erase : {false, true}) {
    s.erase = erase;
    const auto got = composite(d, 0, 0, s);
    for (size_t i = 0; i < kPixels; i++) {
      const double k = double(s.opacity) * cv[i] / 65535, a = l[4 * i + 3] / 65535.;
      const double ao = erase ? a * (1 - k) : k + a * (1 - k);
      CHECK(std::abs(got[4 * i + 3] - ao * 65535) <= 1);
      if (ao * 65535 < 1) continue;
      for (int c = 0; c < 3; c++)
        CHECK(std::abs(got[4 * i + c] - (erase ? l[4 * i + c] : (k * s.color[c] * 65535 + a * (1 - k) * l[4 * i + c]) / ao)) <= 1);
    }
  }
  // Where the layer has no pixels: painting gives the colour at opacity x coverage, erasing nothing.
  s.erase = false;
  const auto onto = composite(doc({pixel(7, nullptr)}), 0, 0, s);
  for (size_t i = 0; i < kPixels; i++) {
    const double k = double(s.opacity) * cv[i] / 65535;
    CHECK(std::abs(onto[4 * i + 3] - k * 65535) <= 1);
    for (int c = 0; c < 3; c++)
      if (onto[4 * i + 3]) CHECK(std::abs(onto[4 * i + c] - s.color[c] * 65535) <= 1);
  }
  s.erase = true;
  CHECK(!composite_tile({doc({pixel(7, nullptr)}), s}, 0, 0, 0));
  // Another layer's stroke is not this one's. In grey, the colour is color[0].
  s.layer = 8;
  CHECK(composite(d, 0, 0, s) == composite(d));
  s.layer = 7, s.erase = false;
  const auto in_grey = composite(doc({pixel(7, nullptr)}, Depth::U16, Mode::Gray), 0, 0, s);
  s.color = {0.2f, 0.2f, 0.2f};
  CHECK(in_grey == composite(doc({pixel(7, nullptr)}), 0, 0, s));
  // Under the layer's mode and opacity in a group: as the layer with the stroke merged into its pixels.
  const TilePtr painted = composite_tile({d, s}, 0, 0, 0);
  const auto bg = at00(make_tile(kRgba16, [&](size_t) { return Pixel{sample(rng, Depth::U16), sample(rng, Depth::U16), sample(rng, Depth::U16), 65535}; }));
  const auto in_group = [&](const TilePtr& px) { return doc({pixel(1, bg), group(2, {pixel(7, at00(px), Blend::Multiply, 0.7f)}, Blend::Normal)}); };
  CHECK(max_diff(composite(in_group(pixels), 0, 0, s), composite(in_group(painted))) <= 2);
}

// The same document in 8 and 16 bits (values x 257), and in grey and in RGB with R = G = B: every mode at random
// opacities over random pixels composites alike (G1 allows a level of 8 bits).
TEST(test_composite_depths) {
  std::mt19937 rng(23);
  for (const bool is_grey : {false, true}) {
    std::vector<LayerPtr> l8, l16, rgb;
    for (int j = 0; j <= 11; j++) {
      std::vector<Pixel> v(kPixels);
      for (Pixel& p : v) {
        p = random_px(rng, {Depth::U8, 4});
        if (is_grey) p[1] = p[2] = p[0];
      }
      const auto px = [&](Format f, uint16_t scale) {
        return at00(make_tile(f, [&](size_t i) {
          Pixel p = v[i];
          for (uint16_t& x : p) x = uint16_t(x * scale);
          return f.channels == 2 ? Pixel{p[0], p[3]} : p;
        }));
      };
      const Blend m = j ? kModes[j - 1] : Blend::Normal;
      const float o = 0.3f + 0.7f * float(rng() % 100) / 99;
      l8.push_back(pixel(LayerId(j + 1), px({Depth::U8, is_grey ? 2 : 4}, 1), m, o));
      l16.push_back(pixel(LayerId(j + 1), px({Depth::U16, is_grey ? 2 : 4}, 257), m, o));
      rgb.push_back(pixel(LayerId(j + 1), px({Depth::U8, 4}, 1), m, o));
    }
    const Mode mode = is_grey ? Mode::Gray : Mode::Rgb;
    const auto c8 = composite(doc(l8, Depth::U8, mode));
    CHECK(c8 == composite(doc(l16, Depth::U16, mode)));
    if (is_grey) CHECK(c8 == composite(doc(rgb, Depth::U8)));
  }
}

// The plan the GPU composite runs too (composite.h): the layers with pixels at the tile in order, groups bracketed
// unless Pass Through at full opacity, the layers' tiles at the level, the tile's part of the canvas at that level.
TEST(test_composite_plan) {
  const TilePtr t = Tile::solid(kRgba16, {1, 2, 3, 65535});
  const auto here = at00(t), elsewhere = grid(kRgba16, {{{4, 0}, t}});
  const DocPtr d = doc({pixel(1, here), group(2, {pixel(3, here, Blend::Multiply, 0.5f, 0.5f)}, Blend::Screen),
                        group(4, {pixel(5, here)}), group(6, {pixel(7, here), hidden(pixel(8, here))}, Blend::PassThrough, 0.5f),
                        pixel(9, elsewhere), group(10, {pixel(11, elsewhere)}, Blend::Normal)},
                       Depth::U16, Mode::Rgb, 1100, 700);
  using C = CompositeStep;
  const std::vector<std::tuple<C::Type, LayerId, bool, Blend, float>> want = {
      {C::Layer, 1, false, Blend::Normal, 1}, {C::Push, 2, false, Blend::Normal, 1},
      {C::Layer, 3, false, Blend::Multiply, 0.25f}, {C::Pop, 2, false, Blend::Screen, 1},
      {C::Layer, 5, false, Blend::Normal, 1}, {C::Push, 6, true, Blend::Normal, 1},
      {C::Layer, 7, false, Blend::Normal, 1}, {C::Pop, 6, true, Blend::PassThrough, 0.5f}};
  const CompositePlan p = composite_plan({d}, 0, 0, 0);
  CHECK(p.steps.size() == want.size() && p.depth == 1);
  for (size_t i = 0; i < want.size(); i++) {
    const C& s = p.steps[i];
    CHECK(std::make_tuple(s.type, s.layer, s.pass, s.blend, s.alpha) == want[i]);
    CHECK(s.type == C::Layer ? s.tile == t : !s.tile);
  }
  CHECK(p.x0 == 0 && p.x1 == S && p.y0 == 0 && p.y1 == S);
  // At level 2 the canvas is 275 x 175 (1100 / 4, 700 / 4): tile (1, 0) has 19 x 175 of it, and the mip tiles there.
  const CompositePlan p2 = composite_plan({d}, 2, 1, 0);
  CHECK(p2.x0 == 0 && p2.x1 == 19 && p2.y0 == 0 && p2.y1 == 175 && p2.steps.size() == 4);
  CHECK(p2.steps[0].layer == 9 && p2.steps[0].tile == mip(*elsewhere, 2, 1, 0) && p2.steps[2].tile == p2.steps[0].tile);
  CHECK(composite_plan({d}, 2, 2, 0).steps.empty());  // past the canvas
}

namespace {

// Every pixel grid as its tiles at `level` (mip.h), where they lie at that level.
std::shared_ptr<const TileGrid> shrink(const std::shared_ptr<const TileGrid>& g, int level) {
  if (!g) return g;
  auto out = std::make_shared<TileGrid>(g->format(), g->fill());
  const TileRect b = g->bounds();
  for (int y = b.y0 >> level; y <= (b.y1 - 1) >> level; y++)
    for (int x = b.x0 >> level; x <= (b.x1 - 1) >> level; x++) out->set(x, y, mip(*g, level, x, y));
  return out;
}
LayerPtr shrink(const LayerPtr& l, int level) {
  auto c = std::make_shared<Layer>(*l);
  c->pixels = shrink(l->pixels, level);
  for (LayerPtr& child : c->children) child = shrink(child, level);
  return c;
}

}  // namespace

// Levels 1 to 6. Reference: the document shrunk 2^L times, its grids and the stroke buffer as their tiles at level L
// (mip.h) and its canvas ceil(size / 2^L), composited at level 0; tolerance 0.
TEST(test_composite_levels) {
  std::mt19937 rng(29);
  const auto rnd = [&](const std::vector<TileXY>& where, bool opaque) {
    std::vector<std::pair<TileXY, TilePtr>> tiles;
    for (const TileXY xy : where)
      tiles.push_back({xy, make_tile(kRgba16, [&](size_t) {
                         Pixel v = random_px(rng, kRgba16);
                         if (opaque) v[3] = 65535;
                         return v;
                       })});
    return grid(kRgba16, tiles);
  };
  std::vector<TileXY> canvas;  // 1100 x 700 in 5 x 3 tiles
  for (int y = 0; y < 3; y++)
    for (int x = 0; x < 5; x++) canvas.push_back({x, y});
  const auto coverage = [&] { return make_tile(kCover, [&](size_t) { return Pixel{sample(rng, Depth::U16)}; }); };
  const StrokeOverlay s{8, grid(kCover, {{{0, 0}, coverage()}, {{2, 0}, coverage()}, {{1, 1}, Tile::solid(kCover, {40000})}}),
                        {0.9f, 0.1f, 0.3f}, 0.7f, false};
  const DocPtr d = doc({pixel(1, rnd(canvas, true)),
                        pixel(2, rnd({{-1, 0}, {1, 1}, {4, 2}, {5, 2}}, false), Blend::Multiply, 0.8f),  // past the canvas too
                        group(3, {pixel(4, rnd({{1, 1}, {2, 2}}, false)),
                                  group(5, {pixel(6, rnd({{2, 1}}, false), Blend::Overlay)}, Blend::PassThrough, 0.6f)},
                              Blend::Screen, 0.7f),
                        group(7, {pixel(8, rnd({{0, 0}, {1, 0}}, false), Blend::SoftLight)}),
                        hidden(pixel(9, rnd({{0, 0}}, false), Blend::Difference)),
                        pixel(10, grid(kRgba16, {{{3, 2}, Tile::solid(kRgba16, {50000, 1000, 20000, 30000})}}), Blend::Difference)},
                       Depth::U16, Mode::Rgb, 1100, 700);
  for (int level = 1; level <= 6; level++) {
    auto shrunk = std::make_shared<DocState>(*d);
    shrunk->root = shrink(d->root, level);
    shrunk->width = (1100 + (1 << level) - 1) >> level, shrunk->height = (700 + (1 << level) - 1) >> level;
    StrokeOverlay ss = s;
    ss.buffer = shrink(s.buffer, level);
    CHECK(composite_tile({d, s}, level, 0, 0));
    for (int ty = -1; ty <= (shrunk->height - 1) / S + 1; ty++)
      for (int tx = -1; tx <= (shrunk->width - 1) / S + 1; tx++)
        CHECK(pixels_of(composite_tile({d, s}, level, tx, ty)) == pixels_of(composite_tile({shrunk, ss}, 0, tx, ty)));
  }
}

// A level-L pixel stands for the 2^L x 2^L full-size pixels under it. Reference: their average by alpha (as mip.h
// averages) over the level-0 composite, transparent past the canvas, in double; one layer within the canvas, smooth up
// to its edge. Tolerance L / 2 + 1 16-bit levels: K2 rounds each level to the nearest, the composite once.
TEST(test_composite_downsampled) {
  constexpr int W = 700, H = 500;  // in 3 x 2 tiles
  std::vector<std::pair<TileXY, TilePtr>> tiles;
  for (int ty = 0; ty < 2; ty++)
    for (int tx = 0; tx < 3; tx++)
      tiles.push_back({{tx, ty}, make_tile(kRgba16, [&](size_t i) {
                         const int x = tx * S + int(i % S), y = ty * S + int(i / S);
                         if (x >= W || y >= H) return Pixel{};
                         return Pixel{uint16_t(65535 * x / W), uint16_t(65535 * y / H), uint16_t(65535 - 65535 * (x + y) / (W + H)),
                                      uint16_t(20000 + 45535 * x / W * y / H)};
                       })});
  const DocPtr d = doc({pixel(1, grid(kRgba16, tiles))}, Depth::U16, Mode::Rgb, W, H);
  std::vector<uint16_t> full(size_t(3 * S) * 2 * S * 4);  // the level-0 composite of those tiles
  for (int ty = 0; ty < 2; ty++)
    for (int tx = 0; tx < 3; tx++) {
      const auto px = composite(d, tx, ty);
      for (int y = 0; y < S; y++)
        std::copy_n(&px[size_t(y) * S * 4], S * 4, &full[(size_t(ty * S + y) * 3 * S + size_t(tx) * S) * 4]);
    }
  for (int level = 1; level <= 6; level++) {
    const int n = 1 << level;
    const double tol = level / 2. + 1;
    std::vector<std::vector<uint16_t>> at(4);  // the level's tiles (0, 0), (1, 0), (0, 1), (1, 1): all it takes
    for (int k = 0; k < 4; k++) at[k] = pixels_of(composite_tile({d}, level, k & 1, k >> 1));
    for (int j = 0; j < 2 * S / n; j++)
      for (int i = 0; i < 3 * S / n; i++) {
        double a = 0, c[3] = {};
        for (int y = j * n; y < (j + 1) * n; y++)
          for (int x = i * n; x < (i + 1) * n; x++) {
            const uint16_t* p = &full[(size_t(y) * 3 * S + size_t(x)) * 4];
            a += p[3];
            for (int k = 0; k < 3; k++) c[k] += double(p[3]) * p[k];
          }
        const uint16_t* g = &at[size_t(i / S + 2 * (j / S))][(size_t(j % S) * S + size_t(i % S)) * 4];
        CHECK(std::abs(g[3] - a / (n * n)) <= tol);
        if (a / (n * n) >= 1)
          for (int k = 0; k < 3; k++) CHECK(std::abs(g[k] - c[k] / a) <= tol);
      }
  }
}
