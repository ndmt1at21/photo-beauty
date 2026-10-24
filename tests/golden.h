#pragma once
// Photoshop's images for photo-beauty's tests (B30), and how a test compares with them. tests/golden/ps/ holds B4's
// base.png, top.png and what tools/ps-golden/blend.jsx made of them; P1's inputs in p1/in/ (pb-bench ps-inputs writes
// them: inputs:: below says what each holds); and a set per Photoshop script of P1, <set>/ with index.json and the
// images: p1/blend (tools/ps-golden/p1/blend.jsx), p1/adjust, p1/gradient (D32), p1/filters, p1/styles (F31), strokes
// (C41), select (S39), resample (T38). Photoshop makes them once, at B33 (tools/ps-golden/README.md); until then a test
// says what it skips and passes.
//
// A test against a set, as test_blend_ps (B31) would be:
//   golden::Set ps("p1/blend");
//   if (!ps.ready("test_blend_ps")) return;            // no index.json yet: says so
//   for (const golden::Entry& e : ps.entries()) {
//     const golden::Image want = ps.image(e);          // Photoshop's <name>.png; empty when not there, counted
//     if (!want) continue;
//     const golden::Image got = ...;                   // photo-beauty's, of the scene ps.layers(e)
//     const golden::Diff d = golden::compare(got, want, "p1/blend/" + e.name);
//     golden::print(e.name, d);
//     CHECK(d.levels <= 1);                            // a public formula; else ΔE2000: d.mean < 1 && d.p99 < 3
//   }
//   ps.skipped("test_blend_ps");                       // what image() did not find, if anything
// PB_GOLDEN_DIFF=<dir> makes compare() write <dir>/<name>.png: ours, Photoshop's, ΔE2000 in bands.
// Header only, for tests and for pb-bench (ps-inputs writes the inputs with it; Set needs PB_SOURCE_DIR, pb-tests').
#include <spng.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <vector>

#include "doc.h"
#include "open.h"
#include "tile.h"

namespace pb::golden {

// ---- Images ----------------------------------------------------------------------------------------------------

// An image as RGBA with 16-bit samples, alpha straight (as PNG keeps it): a file's grey comes in as R = G = B, no
// alpha as 65535, 8 bits as v * 257. bits and channels are the PNG's: read from a file, written to one.
struct Image {
  int w = 0, h = 0;
  int bits = 16;     // 8 or 16
  int channels = 4;  // 1 grey, 2 grey and alpha, 3 RGB, 4 RGBA
  std::vector<uint16_t> px;  // w * h * 4
  Image() = default;
  Image(int width, int height, int chans = 4, int depth = 16)
      : w(width), h(height), bits(depth), channels(chans), px(size_t(width) * size_t(height) * 4) {}
  explicit operator bool() const { return w > 0; }
  uint16_t* at(int x, int y) { return &px[(size_t(y) * size_t(w) + size_t(x)) * 4]; }
  const uint16_t* at(int x, int y) const { return &px[(size_t(y) * size_t(w) + size_t(x)) * 4]; }
};

// The PNG at path: empty (w 0) when there is no such file. Throws std::runtime_error for one spng cannot read.
inline Image read_png(const std::filesystem::path& path) {
  Image im;
  const std::unique_ptr<FILE, int (*)(FILE*)> f(std::fopen(path.string().c_str(), "rb"), std::fclose);
  if (!f) return im;
  const std::unique_ptr<spng_ctx, void (*)(spng_ctx*)> ctx(spng_ctx_new(0), spng_ctx_free);
  const auto check = [&](int rc) {
    if (rc) throw std::runtime_error("golden: " + path.string() + ": " + spng_strerror(rc));
  };
  spng_ihdr ihdr{};
  size_t bytes = 0;
  check(spng_set_png_file(ctx.get(), f.get()));
  check(spng_get_ihdr(ctx.get(), &ihdr));
  check(spng_decoded_image_size(ctx.get(), SPNG_FMT_RGBA16, &bytes));
  im.w = int(ihdr.width), im.h = int(ihdr.height), im.px.resize(bytes / 2);
  check(spng_decode_image(ctx.get(), im.px.data(), bytes, SPNG_FMT_RGBA16, SPNG_DECODE_TRNS));  // host-endian
  spng_trns trns{};
  const bool keyed = spng_get_trns(ctx.get(), &trns) == 0;
  switch (ihdr.color_type) {
    case SPNG_COLOR_TYPE_GRAYSCALE: im.channels = keyed ? 2 : 1; break;
    case SPNG_COLOR_TYPE_GRAYSCALE_ALPHA: im.channels = 2; break;
    case SPNG_COLOR_TYPE_TRUECOLOR: im.channels = keyed ? 4 : 3; break;
    case SPNG_COLOR_TYPE_INDEXED: im.channels = keyed ? 4 : 3; break;
    default: im.channels = 4;
  }
  im.bits = ihdr.bit_depth == 16 ? 16 : 8;  // 1, 2 and 4 bits come scaled as 8 would
  return im;
}

// im as a PNG of its bits and channels (grey from R; 8 bits rounded from 16), no colour chunk: sRGB values that
// Photoshop takes as they are. Makes the folder. Throws std::runtime_error when it cannot write.
inline void write_png(const std::filesystem::path& path, const Image& im) {
  if (im.channels < 1 || im.channels > 4 || (im.bits != 8 && im.bits != 16) || im.px.size() != size_t(im.w) * size_t(im.h) * 4)
    throw std::runtime_error("golden: " + path.string() + ": not an image to write");
  if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
  const int n = im.channels;
  const int take[4][4] = {{0}, {0, 3}, {0, 1, 2}, {0, 1, 2, 3}};  // samples of RGBA per channel count
  std::vector<uint16_t> wide;
  std::vector<uint8_t> narrow;
  for (size_t p = 0; p < im.px.size(); p += 4)
    for (int c = 0; c < n; c++) {
      const uint16_t v = im.px[p + size_t(take[n - 1][c])];
      if (im.bits == 16)
        wide.push_back(v);
      else
        narrow.push_back(uint8_t((v + 128) / 257));
    }
  const std::unique_ptr<FILE, int (*)(FILE*)> f(std::fopen(path.string().c_str(), "wb"), std::fclose);
  if (!f) throw std::runtime_error("golden: cannot write " + path.string());
  const std::unique_ptr<spng_ctx, void (*)(spng_ctx*)> ctx(spng_ctx_new(SPNG_CTX_ENCODER), spng_ctx_free);
  const auto check = [&](int rc) {
    if (rc) throw std::runtime_error("golden: " + path.string() + ": " + spng_strerror(rc));
  };
  const uint8_t types[4] = {SPNG_COLOR_TYPE_GRAYSCALE, SPNG_COLOR_TYPE_GRAYSCALE_ALPHA, SPNG_COLOR_TYPE_TRUECOLOR,
                            SPNG_COLOR_TYPE_TRUECOLOR_ALPHA};
  spng_ihdr ihdr{uint32_t(im.w), uint32_t(im.h), uint8_t(im.bits), types[n - 1], 0, 0, 0};
  check(spng_set_png_file(ctx.get(), f.get()));
  check(spng_set_ihdr(ctx.get(), &ihdr));
  if (im.bits == 16)  // host-endian, as spng takes them
    check(spng_encode_image(ctx.get(), wide.data(), wide.size() * 2, SPNG_FMT_PNG, SPNG_ENCODE_FINALIZE));
  else
    check(spng_encode_image(ctx.get(), narrow.data(), narrow.size(), SPNG_FMT_PNG, SPNG_ENCODE_FINALIZE));
}

// im turned clockwise by 90, 180 or 270 degrees (Photoshop's Image Rotation), 0 as it is.
inline Image rotate(const Image& im, int degrees) {
  const int turns = ((degrees / 90) % 4 + 4) % 4;
  if (degrees % 90) throw std::invalid_argument("golden: a turn of " + std::to_string(degrees) + " degrees");
  if (!turns) return im;
  Image out(turns == 2 ? im.w : im.h, turns == 2 ? im.h : im.w, im.channels, im.bits);
  for (int y = 0; y < out.h; y++)
    for (int x = 0; x < out.w; x++) {
      const int sx = turns == 1 ? y : turns == 2 ? im.w - 1 - x : im.w - 1 - y;
      const int sy = turns == 1 ? im.h - 1 - x : turns == 2 ? im.h - 1 - y : x;
      std::copy_n(im.at(sx, sy), 4, out.at(x, y));
    }
  return out;
}

// ---- Photoshop's levels ----------------------------------------------------------------------------------------

// Photoshop keeps 16-bit samples as 0..32768: the level it reads a 16-bit sample s as, round(s * 32768 / 65535), and
// the sample for x in 0..1 that it reads back exactly (2v for level v, 65535 for 32768; B4's). Its 16-bit PNGs hold
// round(v * 65535 / 32768): ps15() of them is v again, so compare Photoshop's 16-bit samples through ps15().
inline int ps15(uint16_t s) { return int((uint32_t(s) * 32768u + 32767u) / 65535u); }
inline uint16_t ps_level(double x) {
  const long v = std::lround(std::clamp(x, 0., 1.) * 32768);
  return uint16_t(v < 32768 ? 2 * v : 65535);
}
inline bool is_ps_level(uint16_t s) { return s == 65535 || s % 2 == 0; }
// Opacity and fill as Photoshop keeps them: a byte (50% is 128 / 255). index.json has them as set (0.5).
inline double ps_byte(double x) { return double(std::lround(std::clamp(x, 0., 1.) * 255)) / 255; }

// ---- Colour ------------------------------------------------------------------------------------------------------

// sRGB's curve (IEC 61966-2-1): encoded 0..1 to linear light.
inline double srgb_linear(double s) { return s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4); }
inline const double* srgb_linear16() {  // of every 16-bit sample
  static const std::vector<double> t = [] {
    std::vector<double> v(65536);
    for (size_t i = 0; i < v.size(); i++) v[i] = srgb_linear(double(i) / 65535);
    return v;
  }();
  return t.data();
}

struct Lab {
  double L = 0, a = 0, b = 0;
};
// CIELAB, D65 white, of linear sRGB (Lindbloom's sRGB to XYZ matrix; white is 0.95047, 1, 1.08883).
inline Lab lab_linear(double r, double g, double b) {
  const double x = (0.4124564 * r + 0.3575761 * g + 0.1804375 * b) / 0.95047;
  const double y = 0.2126729 * r + 0.7151522 * g + 0.0721750 * b;
  const double z = (0.0193339 * r + 0.1191920 * g + 0.9503041 * b) / 1.08883;
  const auto f = [](double t) { return t > 216. / 24389 ? std::cbrt(t) : (24389. / 27 * t + 16) / 116; };
  const double fx = f(x), fy = f(y), fz = f(z);
  return {116 * fy - 16, 500 * (fx - fy), 200 * (fy - fz)};
}
// Of sRGB as encoded, 0..1.
inline Lab lab(double r, double g, double b) { return lab_linear(srgb_linear(r), srgb_linear(g), srgb_linear(b)); }

// CIEDE2000 (Sharma, Wu, Dalal 2005: their notes on hue angles, kL = kC = kH = 1).
inline double delta_e2000(const Lab& x, const Lab& y) {
  constexpr double kDeg = 3.14159265358979323846 / 180, k25 = 6103515625.;  // 25^7
  const double c1 = std::hypot(x.a, x.b), c2 = std::hypot(y.a, y.b), cm7 = std::pow((c1 + c2) / 2, 7);
  const double g = 0.5 * (1 - std::sqrt(cm7 / (cm7 + k25)));
  const double a1 = (1 + g) * x.a, a2 = (1 + g) * y.a, c1p = std::hypot(a1, x.b), c2p = std::hypot(a2, y.b);
  const auto hue = [&](double b, double a) {
    if (a == 0 && b == 0) return 0.;
    const double h = std::atan2(b, a) / kDeg;
    return h < 0 ? h + 360 : h;
  };
  const double h1 = hue(x.b, a1), h2 = hue(y.b, a2);
  const bool achromatic = c1p * c2p == 0;
  double dh = h2 - h1, hm = h1 + h2;  // achromatic: no hue difference, the sum as the mean hue
  if (achromatic)
    dh = 0;
  else if (dh > 180)
    dh -= 360;
  else if (dh < -180)
    dh += 360;
  if (!achromatic) hm = std::abs(h1 - h2) <= 180 ? hm / 2 : hm < 360 ? (hm + 360) / 2 : (hm - 360) / 2;
  const double dl = y.L - x.L, dc = c2p - c1p, dhh = 2 * std::sqrt(c1p * c2p) * std::sin(dh / 2 * kDeg);
  const double lm = (x.L + y.L) / 2, cm = (c1p + c2p) / 2, cmp7 = std::pow(cm, 7);
  const double t = 1 - 0.17 * std::cos((hm - 30) * kDeg) + 0.24 * std::cos(2 * hm * kDeg) +
                   0.32 * std::cos((3 * hm + 6) * kDeg) - 0.20 * std::cos((4 * hm - 63) * kDeg);
  const double theta = 30 * std::exp(-((hm - 275) / 25) * ((hm - 275) / 25));
  const double rt = -std::sin(2 * theta * kDeg) * 2 * std::sqrt(cmp7 / (cmp7 + k25));
  const double sl = 1 + 0.015 * (lm - 50) * (lm - 50) / std::sqrt(20 + (lm - 50) * (lm - 50));
  const double l = dl / sl, c = dc / (1 + 0.045 * cm), h = dhh / (1 + 0.015 * cm * t);
  return std::sqrt(l * l + c * c + h * h + rt * c * h);
}

// ---- Comparing ---------------------------------------------------------------------------------------------------

struct Rect {
  int x = 0, y = 0, w = 0, h = 0;  // w or h 0: the whole image
};

// How far apart two images are. Where either has alpha below 1 both are put over white and over black and the larger
// difference counts, so a difference of alpha shows as one of colour.
struct Diff {
  size_t n = 0;                       // pixels compared
  double mean = 0, p99 = 0, max = 0;  // ΔE2000
  int x = 0, y = 0;                   // where max is
  double levels = 0;                  // the largest difference of a colour sample, in levels of 8 bits (1 is 1 / 255)
  int lx = 0, ly = 0, lc = 0;         // where, which channel: 0 R, 1 G, 2 B
  double over1 = 0;                   // share of pixels with a sample more than a level of 8 bits off
  bool alpha = false;                 // put over white and black
};

namespace detail {

// ΔE2000 as a colour for the diff image: under 0.5 black, under 1 dark blue, under 2 green, under 3 yellow, under 5
// orange, then red.
inline void heat(double de, uint16_t* p) {
  static const double bands[][4] = {{.5, 0, 0, 0}, {1, 0, 0, .5}, {2, 0, .6, 0}, {3, 1, 1, 0}, {5, 1, .5, 0}};
  double r = 1, g = 0, b = 0;
  for (const auto& band : bands)
    if (de < band[0]) {
      r = band[1], g = band[2], b = band[3];
      break;
    }
  p[0] = uint16_t(r * 65535), p[1] = uint16_t(g * 65535), p[2] = uint16_t(b * 65535), p[3] = 65535;
}

// Ours, Photoshop's (over a checkerboard where they have alpha) and the ΔE2000 bands, side by side: <dir>/<name>.png.
inline void write_diff(const std::string& name, const Image& a, const Image& b, const Rect& r,
                       const std::vector<double>& de) {
  const char* dir = std::getenv("PB_GOLDEN_DIFF");
  if (!dir || !*dir || name.empty()) return;
  Image out(3 * r.w, r.h, 3, 8);
  for (int y = 0; y < r.h; y++)
    for (int x = 0; x < r.w; x++) {
      const double check = ((x / 8 + y / 8) % 2) ? .8 : .6;
      for (int k = 0; k < 2; k++) {
        const uint16_t* s = (k ? b : a).at(r.x + x, r.y + y);
        uint16_t* d = out.at(k * r.w + x, y);
        const double al = s[3] / 65535.;
        for (int c = 0; c < 3; c++) d[c] = uint16_t(std::lround((s[c] / 65535. * al + check * (1 - al)) * 65535));
        d[3] = 65535;
      }
      heat(de[size_t(y) * size_t(r.w) + size_t(x)], out.at(2 * r.w + x, y));
    }
  write_png(std::filesystem::path(dir) / (name + ".png"), out);
}

}  // namespace detail

// How far ours is from Photoshop's (theirs) over r, which must be inside both, of the same size. With a name and
// PB_GOLDEN_DIFF set, writes $PB_GOLDEN_DIFF/<name>.png (name may hold folders). Throws std::invalid_argument for
// images of different sizes.
inline Diff compare(const Image& ours, const Image& theirs, const std::string& name = "", Rect r = {}) {
  if (ours.w != theirs.w || ours.h != theirs.h)
    throw std::invalid_argument("golden: " + (name.empty() ? std::string("compare") : name) + ": " +
                                std::to_string(ours.w) + " x " + std::to_string(ours.h) + " here, " +
                                std::to_string(theirs.w) + " x " + std::to_string(theirs.h) + " in Photoshop's");
  if (!r.w || !r.h) r = {0, 0, ours.w, ours.h};
  if (r.x < 0 || r.y < 0 || r.w < 0 || r.h < 0 || r.x + r.w > ours.w || r.y + r.h > ours.h)
    throw std::invalid_argument("golden: " + name + ": the area is not inside the image");
  Diff d;
  for (int y = r.y; y < r.y + r.h && !d.alpha; y++)
    for (int x = r.x; x < r.x + r.w; x++)
      if (ours.at(x, y)[3] != 65535 || theirs.at(x, y)[3] != 65535) {
        d.alpha = true;
        break;
      }
  const double* lin = srgb_linear16();
  std::vector<double> de(size_t(r.w) * size_t(r.h));
  double sum = 0;
  size_t over1 = 0;
  for (int y = r.y; y < r.y + r.h; y++)
    for (int x = r.x; x < r.x + r.w; x++) {
      const uint16_t *p = ours.at(x, y), *q = theirs.at(x, y);
      double e = 0, worst = 0;
      int channel = 0;
      for (int bg = 0; bg < (d.alpha ? 2 : 1); bg++) {  // over black, then white
        double u[3], v[3];
        uint16_t iu[3], iv[3];
        for (int c = 0; c < 3; c++) {
          const double pa = p[3] / 65535., qa = q[3] / 65535.;
          u[c] = p[c] / 65535. * pa + bg * (1 - pa), v[c] = q[c] / 65535. * qa + bg * (1 - qa);
          iu[c] = uint16_t(std::lround(u[c] * 65535)), iv[c] = uint16_t(std::lround(v[c] * 65535));
          if (std::abs(u[c] - v[c]) * 255 > worst) worst = std::abs(u[c] - v[c]) * 255, channel = c;
        }
        if (iu[0] != iv[0] || iu[1] != iv[1] || iu[2] != iv[2])
          e = std::max(e, delta_e2000(lab_linear(lin[iu[0]], lin[iu[1]], lin[iu[2]]),
                                      lab_linear(lin[iv[0]], lin[iv[1]], lin[iv[2]])));
      }
      de[size_t(y - r.y) * size_t(r.w) + size_t(x - r.x)] = e;
      sum += e;
      if (e > d.max || d.n == 0) d.max = e, d.x = x, d.y = y;
      if (worst > d.levels || d.n == 0) d.levels = worst, d.lx = x, d.ly = y, d.lc = channel;
      over1 += worst > 1;
      d.n++;
    }
  d.mean = d.n ? sum / double(d.n) : 0;
  d.over1 = d.n ? double(over1) / double(d.n) : 0;
  detail::write_diff(name, ours, theirs, r, de);
  if (!de.empty()) {
    const auto k = de.begin() + std::ptrdiff_t(0.99 * double(de.size() - 1));  // nearest rank below
    std::nth_element(de.begin(), k, de.end());
    d.p99 = *k;
  }
  return d;
}

// "dE2000 mean 0.120, p99 0.800, max 2.310 at (12, 40); 1.40 levels of 8 bits at most (G at (12, 40)), 0.30% of
// pixels over 1", and "over white and black" where it put them so.
inline std::string str(const Diff& d) {
  char s[256];
  std::snprintf(s, sizeof s,
                "dE2000 mean %.3f, p99 %.3f, max %.3f at (%d, %d); %.2f levels of 8 bits at most (%c at (%d, %d)), "
                "%.2f%% of pixels over 1%s",
                d.mean, d.p99, d.max, d.x, d.y, d.levels, "RGB"[d.lc], d.lx, d.ly, d.over1 * 100,
                d.alpha ? ", over white and black" : "");
  return s;
}
inline void print(const std::string& name, const Diff& d) {
  std::printf("ps golden %s: %s\n", name.c_str(), str(d).c_str());
  std::fflush(stdout);
}

// ---- Layers as grids ---------------------------------------------------------------------------------------------

// The image as tiles of format f from (0, 0): RGBA; grey and alpha from R and alpha; one channel from alpha when the
// image has it, else from R (a mask, a selection). 8 bits rounded from 16.
inline std::shared_ptr<TileGrid> grid(const Image& im, Format f) {
  const int n = f.channels;
  const int from = n == 1 && (im.channels == 2 || im.channels == 4) ? 3 : 0;
  std::vector<uint8_t> buf(size_t(im.w) * size_t(im.h) * size_t(f.bytes()));
  for (size_t i = 0, p = 0; p < im.px.size(); p += 4, i++)
    for (int c = 0; c < n; c++) {
      const uint16_t v = im.px[p + size_t(n == 4 ? c : n == 2 ? (c ? 3 : 0) : from)];
      if (f.depth == Depth::U16)
        reinterpret_cast<uint16_t*>(buf.data())[i * size_t(n) + size_t(c)] = v;
      else
        buf[i * size_t(n) + size_t(c)] = uint8_t((v + 128) / 257);
    }
  auto g = std::make_shared<TileGrid>(f);
  g->write_rect(0, 0, im.w, im.h, buf.data(), size_t(im.w) * size_t(f.bytes()));
  return g;
}

// Pixels (0, 0) to (w, h) of a grid as an image: RGBA (4 channels), grey and alpha (2), or a value (1) as grey; 16
// bits, or 8 as v * 257.
inline Image image(const TileGrid& g, int w, int h) {
  const Format f = g.format();
  const int n = f.channels;
  std::vector<uint8_t> buf(size_t(w) * size_t(h) * size_t(f.bytes()));
  g.read_rect(0, 0, w, h, buf.data(), size_t(w) * size_t(f.bytes()));
  Image im(w, h, n, f.depth == Depth::U16 ? 16 : 8);
  for (size_t i = 0; i < size_t(w) * size_t(h); i++) {
    uint16_t s[4] = {0, 0, 0, 65535};
    for (int c = 0; c < n; c++)
      s[c] = f.depth == Depth::U16 ? reinterpret_cast<const uint16_t*>(buf.data())[i * size_t(n) + size_t(c)]
                                   : uint16_t(buf[i * size_t(n) + size_t(c)] * 257);
    uint16_t* p = &im.px[i * 4];
    if (n == 4)
      std::copy_n(s, 4, p);
    else
      p[0] = p[1] = p[2] = s[0], p[3] = n == 2 ? s[1] : 65535;
  }
  return im;
}

// ---- P1's inputs: tests/golden/ps/p1/in/, as pb-bench ps-inputs writes them ---------------------------------------

// Every sample a level Photoshop reads back exactly (ps_level()), 16 bits, no colour chunk (sRGB values: the scripts
// assign sRGB, nothing converts). B4's base.png and top.png are there too, the same bytes as tests/golden/ps/'s.
namespace inputs {

inline constexpr int kSize = 256;  // photo, ramp, impulse, shapes and alpha are kSize x kSize

// B4's layers, 256 x 384, RGBA. Rows 0-255: base ramps across (x / 255, 1 - x / 255, x / 255), top down (y / 255,
// y / 255, 1 - y / 255), both opaque: every pair of 256 levels in each channel; 256-319: 8 colours of base across
// under 8 of top down, in 32 x 8 patches; 320-383: base's alpha ramps across, top's down, on fixed colours.
inline void base_top(Image& base, Image& top) {
  constexpr int W = 256, H = 384;
  struct Rgba {
    double r, g, b, a;
  };
  const Rgba below[8] = {{0, 0, 0, 1}, {1, 1, 1, 1}, {.5, .5, .5, 1},    {1, 0, 0, 1},
                         {0, 1, 0, 1}, {0, 0, 1, 1}, {.87, .67, .55, 1}, {.1, .35, .4, 1}};
  const Rgba above[8] = {{1, 1, 1, 1}, {0, 0, 0, 1}, {.5, .5, .5, 1},    {0, 1, 1, 1},
                         {1, 0, 1, 1}, {1, 1, 0, 1}, {.75, .75, .75, 1}, {.4, .25, .1, 1}};
  base = Image(W, H), top = Image(W, H);
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      const double u = x / 255., v = y / 255.;
      Rgba b, t;
      if (y < 256)
        b = {u, 1 - u, u, 1}, t = {v, v, 1 - v, 1};
      else if (y < 320)
        b = below[x / 32], t = above[(y - 256) / 8];
      else  // B(Cb, Cs) differs from Cs in some channel in every mode
        b = {.8, .25, .55, u}, t = {.3, .6, .9, (y - 320) / 63.};
      uint16_t *p = base.at(x, y), *q = top.at(x, y);
      p[0] = ps_level(b.r), p[1] = ps_level(b.g), p[2] = ps_level(b.b), p[3] = ps_level(b.a);
      q[0] = ps_level(t.r), q[1] = ps_level(t.g), q[2] = ps_level(t.b), q[3] = ps_level(t.a);
    }
}

// photo.png, RGB: a kSize square of ui/mock/photos/monan.jpg (food on wood: chilli red, leaf green, meat, onion
// rings, cream, near black bowl rims, grey plates) averaged down kPhotoScale times, so its 16 bits hold more than the
// JPEG's 8, from (kPhotoX, kPhotoY) of the smaller image.
inline constexpr int kPhotoScale = 4, kPhotoX = 97, kPhotoY = 44;
inline Image photo(const std::filesystem::path& jpeg) {
  const DocPtr doc = open_image(jpeg);
  const pb::Layer& layer = *doc->root->children.at(0);
  const int w = doc->width, h = doc->height, s = kPhotoScale;
  if (w < (kPhotoX + kSize) * s || h < (kPhotoY + kSize) * s || doc->depth != Depth::U8 || doc->mode != Mode::Rgb)
    throw std::runtime_error("golden: " + jpeg.string() + " is not the photo photo.png is cut from");
  const int x0 = kPhotoX * s, y0 = kPhotoY * s, side = kSize * s;
  std::vector<uint8_t> rgba(size_t(side) * size_t(side) * 4);
  layer.pixels->read_rect(x0, y0, side, side, rgba.data(), size_t(side) * 4);
  Image im(kSize, kSize, 3);
  for (int y = 0; y < kSize; y++)
    for (int x = 0; x < kSize; x++)
      for (int c = 0; c < 3; c++) {
        int sum = 0;
        for (int j = 0; j < s; j++)
          for (int i = 0; i < s; i++) sum += rgba[((size_t(y) * s + size_t(j)) * size_t(side) + size_t(x) * s + size_t(i)) * 4 + size_t(c)];
        im.at(x, y)[c] = ps_level(sum / (255. * s * s));
      }
  for (size_t p = 3; p < im.px.size(); p += 4) im.px[p] = 65535;
  return im;
}

// cube.png, RGB, kCubeSide square: the kCubeN³ colours of a lattice, k / (kCubeN - 1) in each channel, red fastest,
// then green, then blue, row by row from the top left (colour i at (i % kCubeSide, i / kCubeSide)); black past them.
inline constexpr int kCubeN = 33, kCubeSide = 190;
inline int cube_index(int r, int g, int b) { return r + kCubeN * (g + kCubeN * b); }
inline Image cube() {
  Image im(kCubeSide, kCubeSide, 3);
  for (int i = 0; i < kCubeSide * kCubeSide; i++) {
    uint16_t* p = &im.px[size_t(i) * 4];
    p[3] = 65535;
    if (i >= kCubeN * kCubeN * kCubeN) continue;
    const int k[3] = {i % kCubeN, i / kCubeN % kCubeN, i / (kCubeN * kCubeN)};
    for (int c = 0; c < 3; c++) p[c] = ps_level(k[c] / double(kCubeN - 1));
  }
  return im;
}

// hsv(h, s, v): RGB 0..1 of hue h degrees, saturation and value 0..1.
inline void hsv(double h, double s, double v, double* rgb) {
  const double c = v * s, hp = std::fmod(h, 360) / 60, x = c * (1 - std::abs(std::fmod(hp, 2) - 1)), m = v - c;
  const int sector = int(hp);
  const double r[6] = {c, x, 0, 0, x, c}, g[6] = {x, c, c, x, 0, 0}, b[6] = {0, 0, x, c, c, x};
  rgb[0] = r[sector] + m, rgb[1] = g[sector] + m, rgb[2] = b[sector] + m;
}

// ramp.png, RGB: kRampBands bands of kRampRows rows, each a ramp across, t = x / 255: 0 grey t; 1, 2, 3 red, green,
// blue t (others 0); then hue 360 x / 256 degrees at (saturation, value) 4 (1, 1), 5 (1, 0.5), 6 (0.5, 1), 7 (0.25,
// 0.75). Rows of a band alike; bands meet in hard edges.
inline constexpr int kRampBands = 8, kRampRows = 32;
inline Image ramp() {
  const double sv[4][2] = {{1, 1}, {1, .5}, {.5, 1}, {.25, .75}};
  Image im(kSize, kSize, 3);
  for (int y = 0; y < kSize; y++)
    for (int x = 0; x < kSize; x++) {
      const int band = y / kRampRows;
      const double t = x / 255.;
      double rgb[3] = {t, t, t};
      if (band >= 1 && band <= 3)
        rgb[0] = rgb[1] = rgb[2] = 0, rgb[band - 1] = t;
      else if (band >= 4)
        hsv(360. * x / 256, sv[band - 4][0], sv[band - 4][1], rgb);
      uint16_t* p = im.at(x, y);
      for (int c = 0; c < 3; c++) p[c] = ps_level(rgb[c]);
      p[3] = 65535;
    }
  return im;
}

// impulse.png, RGB: single white pixels kImpulseStep apart from (kImpulseFirst, kImpulseFirst), on black in the top
// half and on 50% grey (32768: level 16384, an exact half) in the bottom one, where a kernel's negative lobes show:
// a linear filter's output around each is its kernel (to kImpulseStep / 2 out).
inline constexpr int kImpulseStep = 32, kImpulseFirst = 16;
inline Image impulse() {
  Image im(kSize, kSize, 3);
  for (int y = 0; y < kSize; y++)
    for (int x = 0; x < kSize; x++) {
      const bool dot = x % kImpulseStep == kImpulseFirst && y % kImpulseStep == kImpulseFirst;
      const uint16_t v = dot ? 65535 : y < kSize / 2 ? 0 : ps_level(.5);
      uint16_t* p = im.at(x, y);
      p[0] = p[1] = p[2] = v, p[3] = 65535;
    }
  return im;
}

// shapes.png, RGBA: shapes on transparent black, edges anti-aliased (16 x 16 samples a pixel), each a colour of its
// own: a disc at (64, 64) r 48; a rectangle x 144-240, y 24-104 with corners of r 20; a ring at (64, 192), r 24 to 48;
// a line 1 px high on row 136, x 144-239 (whole pixels); a line 1 px wide from (144, 150) to (240, 172); a soft dot at
// (192, 216), alpha a Gaussian of sigma 12 (to 3 sigma). For layer styles, masks, clipping.
inline Image shapes() {
  // Each shape's test of a point, in squared distances: sums of squares of multiples of 1/32, exact, so the same
  // samples are in on every platform.
  struct Shape {
    double r, g, b;
    int x0, y0, x1, y1;                   // the pixels it may cover
    bool (*in)(double x, double y);
  };
  const auto disc = [](double x, double y) { return (x - 64) * (x - 64) + (y - 64) * (y - 64) <= 48 * 48; };
  const auto rounded = [](double x, double y) {
    const double dx = std::max({144 + 20 - x, x - (240 - 20), 0.}), dy = std::max({24 + 20 - y, y - (104 - 20), 0.});
    return x >= 144 && x <= 240 && y >= 24 && y <= 104 && dx * dx + dy * dy <= 20 * 20;
  };
  const auto ring = [](double x, double y) {
    const double d2 = (x - 64) * (x - 64) + (y - 192) * (y - 192);
    return d2 <= 48 * 48 && d2 >= 24 * 24;
  };
  const auto row = [](double x, double y) { return y >= 136 && y < 137 && x >= 144 && x < 240; };
  const auto slant = [](double x, double y) {  // within half a pixel of the segment
    const double ax = 144, ay = 150, bx = 240, by = 172;
    const double t = std::clamp(((x - ax) * (bx - ax) + (y - ay) * (by - ay)) / ((bx - ax) * (bx - ax) + (by - ay) * (by - ay)), 0., 1.);
    const double dx = x - ax - t * (bx - ax), dy = y - ay - t * (by - ay);
    return dx * dx + dy * dy <= .25;
  };
  const Shape all[] = {{.86, .36, .2, 16, 16, 112, 112, disc},      {.2, .5, .86, 144, 24, 240, 104, rounded},
                       {.3, .72, .36, 16, 144, 112, 240, ring},     {.96, .92, .3, 144, 136, 240, 137, row},
                       {.96, .92, .3, 143, 149, 241, 173, slant}};
  Image im(kSize, kSize, 4);
  for (const Shape& s : all)
    for (int y = s.y0; y < s.y1; y++)
      for (int x = s.x0; x < s.x1; x++) {
        int in = 0;
        for (int j = 0; j < 16; j++)
          for (int i = 0; i < 16; i++) in += s.in(x + (i + .5) / 16, y + (j + .5) / 16);
        uint16_t* p = im.at(x, y);
        if (in) p[0] = ps_level(s.r), p[1] = ps_level(s.g), p[2] = ps_level(s.b), p[3] = ps_level(in / 256.);
      }
  for (int y = 216 - 36; y <= 216 + 36; y++)  // the soft dot, at pixel centres
    for (int x = 192 - 36; x <= 192 + 36; x++) {
      const double d2 = (x + .5 - 192) * (x + .5 - 192) + (y + .5 - 216) * (y + .5 - 216);
      uint16_t* p = im.at(x, y);
      if (d2 <= 36 * 36) p[0] = ps_level(.6), p[1] = ps_level(.3), p[2] = ps_level(.75), p[3] = ps_level(std::exp(-d2 / 288));
    }
  return im;
}

// alpha.png, RGBA: alpha x / 255 across; colour (y / 255, 1 - y / 255, (x + y) / 510). blend.jsx stacks it on itself
// turned 90 degrees (alpha down): every pair of alphas, transparent over transparent.
inline Image alpha() {
  Image im(kSize, kSize, 4);
  for (int y = 0; y < kSize; y++)
    for (int x = 0; x < kSize; x++) {
      uint16_t* p = im.at(x, y);
      p[0] = ps_level(y / 255.), p[1] = ps_level(1 - y / 255.), p[2] = ps_level((x + y) / 510.), p[3] = ps_level(x / 255.);
    }
  return im;
}

struct File {
  std::string name;
  Image image;
};
// Every file of p1/in/: B4's base.png and top.png, then P1's. jpeg: ui/mock/photos/monan.jpg.
inline std::vector<File> all(const std::filesystem::path& jpeg) {
  std::vector<File> files(2);
  files[0].name = "base.png", files[1].name = "top.png";
  base_top(files[0].image, files[1].image);
  files.push_back({"photo.png", photo(jpeg)});
  files.push_back({"cube.png", cube()});
  files.push_back({"ramp.png", ramp()});
  files.push_back({"impulse.png", impulse()});
  files.push_back({"shapes.png", shapes()});
  files.push_back({"alpha.png", alpha()});
  return files;
}

}  // namespace inputs

// ---- Sets: a script's images and its index.json ----------------------------------------------------------------
#ifdef PB_SOURCE_DIR

// tests/golden/ps of the source tree; for a Windows .exe (its PB_SOURCE_DIR a Linux path), tests/golden/ps copied
// beside it.
inline const std::filesystem::path kRoot = [] {
  const std::filesystem::path in_source = PB_SOURCE_DIR "/tests/golden/ps";
  return std::filesystem::exists(in_source) ? in_source : std::filesystem::path("tests/golden/ps");
}();

// An image of a set: its entry in index.json, as the script wrote it. lib.jsx's scripts write "name", "canvas" [w, h],
// "bits" (the PNG's; the document was 16 bits), "layers" (the scene, see SceneLayer) and what else the case had.
struct Entry {
  std::string name;  // the image is <name>.png next to index.json
  nlohmann::json json;
};

// A layer of an entry's scene ("layers", bottom first, as DocState's children): as photo-beauty has it.
struct SceneLayer {
  std::string kind;               // "pixel", "group", "adj" (Mock.KINDS' names: also "fill", "text", "shape", "so")
  std::string props;              // LayerProps as JSON, "version" added when missing: props_from_json() reads it
  const Image* pixels = nullptr;  // a pixel layer's: its "src" input, turned clockwise by "rotate" degrees
  const Image* mask = nullptr;    // its layer mask: the alpha of input "mask" (Photoshop: loaded as a selection,
                                  // Reveal Selection); null for none
  std::vector<SceneLayer> children;  // a group's, bottom first
  const nlohmann::json* json = nullptr;  // the layer as index.json has it, for keys of later tasks
};

class Set {
 public:
  // tests/golden/ps/<dir> (an absolute dir as it is); reads index.json there if it is. Throws std::runtime_error for
  // one that is not JSON.
  explicit Set(const std::filesystem::path& dir) : dir_(kRoot / dir) {
    std::ifstream f(dir_ / "index.json");
    if (!f) return;
    index_ = nlohmann::json::parse(f, nullptr, false);
    if (!index_.is_object() || !index_.contains("images") || !index_["images"].is_array())
      throw std::runtime_error("golden: " + (dir_ / "index.json").string() + " is not an index of images");
    for (const auto& e : index_["images"]) {
      if (!e.is_object() || !e.contains("name") || !e["name"].is_string())
        throw std::runtime_error("golden: " + (dir_ / "index.json").string() + ": an image without a name");
      entries_.push_back({e["name"].get<std::string>(), e});
    }
    inputs_dir_ = dir_ / index_.value("inputs", std::string("in"));
  }

  const std::filesystem::path& dir() const { return dir_; }
  bool has_index() const { return !index_.is_null(); }
  const nlohmann::json& index() const { return index_; }  // the whole index.json: "script", "photoshop"...
  const std::vector<Entry>& entries() const { return entries_; }
  const Entry* find(const std::string& name) const {
    for (const Entry& e : entries_)
      if (e.name == name) return &e;
    return nullptr;
  }

  // Whether there is an index.json: if not, says "<test>: skipped..." and the script that makes it. Notes a script
  // that stopped (its index has the images it made).
  bool ready(const char* test) const {
    const std::string where = name();
    if (!has_index()) {
      if (in_tree())
        std::printf("%s: skipped, no Photoshop images yet: %s/index.json comes from tools/ps-golden/%s.jsx at B33 "
                    "(tools/ps-golden/README.md)\n", test, where.c_str(), relative().c_str());
      else
        std::printf("%s: skipped, no %s/index.json\n", test, where.c_str());
      return false;
    }
    if (!index_.value("complete", true))
      std::printf("%s: %s: the script stopped (%s), %zu images\n", test, where.c_str(),
                  index_.value("error", std::string("no reason given")).c_str(), entries_.size());
    const nlohmann::json failed = index_.value("failed", nlohmann::json::array());
    if (failed.is_array() && !failed.empty())
      std::printf("%s: %s: %zu cases failed in Photoshop (index.json, photoshop.txt), first %s\n", test, where.c_str(),
                  failed.size(), failed[0].dump().c_str());
    return true;
  }
  // tests/golden/ps/<set> for a set of the tree, else its folder as given.
  std::string name() const { return in_tree() ? "tests/golden/ps/" + relative() : dir_.generic_string(); }

  // Photoshop's image of e: <name>.png. Empty when it is not there (skipped() lists it).
  Image image(const Entry& e) {
    Image im = read_png(dir_ / (e.name + ".png"));
    if (!im) missing_.push_back(e.name);
    return im;
  }
  // Says what image() did not find, if anything: "<test>: skipped n of m images, not in ... yet: names".
  void skipped(const char* test) const {
    if (missing_.empty()) return;
    std::string names;
    for (size_t i = 0; i < missing_.size() && i < 8; i++) names += " " + missing_[i];
    std::printf("%s: skipped %zu of %zu images, not in %s yet:%s%s\n", test, missing_.size(), entries_.size(),
                name().c_str(), names.c_str(), missing_.size() > 8 ? " ..." : "");
  }

  // An input (index.json's "inputs" folder, from this one; in/ when it does not say), turned clockwise by `rotate`
  // degrees; read once. Throws std::runtime_error when it is not there.
  const Image& input(const std::string& file, int rotate = 0) {
    const std::string key = file + "@" + std::to_string(rotate);
    auto it = inputs_.find(key);
    if (it == inputs_.end()) {
      Image im = read_png(inputs_dir_ / file);
      if (!im) throw std::runtime_error("golden: no input " + (inputs_dir_ / file).string());
      it = inputs_.emplace(key, golden::rotate(im, rotate)).first;  // a map: references stay valid
    }
    return it->second;
  }

  // e's scene, bottom first. Throws std::runtime_error for a layer it cannot read (see problems()).
  std::vector<SceneLayer> layers(const Entry& e) {
    std::vector<SceneLayer> out;
    if (e.json.contains("layers"))
      for (const auto& l : e.json["layers"]) out.push_back(layer(l, e.name));
    return out;
  }

  // What is wrong with the set, empty when nothing: an image index.json lists missing, a scene it cannot read (no
  // such input, a kind photo-beauty has not, props props_from_json() does not take, a group without children).
  std::vector<std::string> problems() {
    std::vector<std::string> out;
    for (const Entry& e : entries_) {
      if (!std::filesystem::exists(dir_ / (e.name + ".png"))) out.push_back(e.name + ": no " + e.name + ".png");
      try {
        layers(e);
      } catch (const std::exception& x) {
        out.push_back(x.what());
      }
    }
    return out;
  }

 private:
  std::string relative() const { return std::filesystem::relative(dir_, kRoot).generic_string(); }
  bool in_tree() const {
    const std::string rel = relative();
    return !rel.empty() && rel.compare(0, 2, "..") != 0;
  }

  SceneLayer layer(const nlohmann::json& j, const std::string& entry) {
    const auto bad = [&](const std::string& why) { return std::runtime_error("golden: " + entry + ": " + why); };
    if (!j.is_object()) throw bad("a layer is not an object");
    SceneLayer l;
    l.json = &j;
    l.kind = j.value("kind", std::string("pixel"));
    static const char* const kKinds[] = {"pixel", "group", "adj", "fill", "text", "shape", "so"};  // Mock.KINDS
    if (std::find(std::begin(kKinds), std::end(kKinds), l.kind) == std::end(kKinds)) throw bad("no kind " + l.kind);
    nlohmann::json props = j.value("props", nlohmann::json::object());
    if (!props.is_object()) throw bad("props are not an object");
    if (!props.contains("version")) props["version"] = kLayerSchemaVersion;
    l.props = props.dump();
    if (!props_from_json(l.props)) throw bad("props photo-beauty does not read: " + l.props);
    if (j.contains("src")) l.pixels = &input(j["src"].get<std::string>(), j.value("rotate", 0));
    if (j.contains("mask")) l.mask = &input(j["mask"].get<std::string>());
    if (l.kind == "pixel" && !l.pixels) throw bad("a pixel layer without src");
    if (l.kind == "group") {
      if (!j.contains("children") || !j["children"].is_array()) throw bad("a group without children");
      for (const auto& c : j["children"]) l.children.push_back(layer(c, entry));
    }
    return l;
  }

  std::filesystem::path dir_, inputs_dir_;
  nlohmann::json index_;
  std::vector<Entry> entries_;
  std::map<std::string, Image> inputs_;
  std::vector<std::string> missing_;
};

#endif  // PB_SOURCE_DIR

}  // namespace pb::golden
