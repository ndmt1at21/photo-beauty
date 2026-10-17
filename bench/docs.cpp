// The benchmark documents (docs.h): a photo, layers made tile by tile on the pool, strokes through C2's Stroke. Random
// numbers come straight from std::mt19937, whose sequence the standard fixes (its distributions differ between
// standard libraries), and are drawn one to a statement where the order of evaluation would be unspecified.
#include "docs.h"

#include <turbojpeg.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "color.h"
#include "jobs.h"
#include "open.h"
#include "profile.h"
#include "stroke.h"

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
// after windows.h; its 2nd version is kernel32's, nothing more to link
#define PSAPI_VERSION 2
#include <psapi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#else
#include <unistd.h>
#endif

namespace pb::bench {
namespace {

constexpr int S = kTileSize;
constexpr const char* kRaw = "Nikon_D850_Nikon-D850-14bit-lossless-compressed.NEF";
constexpr double kPi = 3.14159265358979323846;
using Grid = std::shared_ptr<const TileGrid>;
using Rng = std::mt19937;

double uni(Rng& r, double a, double b) { return a + (b - a) * (double(r()) / 4294967296.0); }  // a..b
int pick(Rng& r, int a, int b) { return a + int(r() % uint32_t(b - a + 1)); }                    // a..b, a <= b

std::shared_ptr<const std::vector<uint8_t>> srgb() {
  static const auto p = std::make_shared<const std::vector<uint8_t>>(icc_profile(ColorSpace::Srgb));
  return p;
}

// An 8-bit RGB picture, rows packed.
struct Photo {
  int w = 0, h = 0;
  std::vector<uint8_t> px;
  const uint8_t* at(int x, int y) const { return &px[(size_t(y) * size_t(w) + size_t(x)) * 3]; }
};

// The largest JPEG in a file, decoded at 1/scale (TurboJPEG's DCT scaling): a camera raw's full-size preview. Each SOI
// marker in the file is tried with TurboJPEG's header reader, which those the raw data holds by chance fail (each with
// a handle of its own: one that failed reads no more headers).
Photo embedded_jpeg(const std::filesystem::path& path, int scale) {
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  std::vector<uint8_t> data(f ? size_t(f.tellg()) : 0);
  f.seekg(0);
  f.read(reinterpret_cast<char*>(data.data()), std::streamsize(data.size()));
  size_t at = 0, area = 0;
  for (size_t i = 0; f && i + 3 <= data.size(); i++)
    if (data[i] == 0xff && data[i + 1] == 0xd8 && data[i + 2] == 0xff) {
      tjhandle t = tj3Init(TJINIT_DECOMPRESS);
      if (tj3DecompressHeader(t, &data[i], data.size() - i) == 0) {
        const size_t a = size_t(tj3Get(t, TJPARAM_JPEGWIDTH)) * size_t(tj3Get(t, TJPARAM_JPEGHEIGHT));
        if (a > area) area = a, at = i;
      }
      tj3Destroy(t);
    }
  tjhandle t = tj3Init(TJINIT_DECOMPRESS);
  Photo p;
  const tjscalingfactor k{1, scale};
  bool ok = area && tj3DecompressHeader(t, &data[at], data.size() - at) == 0 && tj3SetScalingFactor(t, k) == 0;
  if (ok) {
    p.w = TJSCALED(tj3Get(t, TJPARAM_JPEGWIDTH), k), p.h = TJSCALED(tj3Get(t, TJPARAM_JPEGHEIGHT), k);
    p.px.resize(size_t(p.w) * size_t(p.h) * 3);
    // A warning (cameras' JPEGs have some) leaves the picture decoded.
    ok = tj3Decompress8(t, &data[at], data.size() - at, p.px.data(), 0, TJPF_RGB) == 0 || tj3GetErrorCode(t) == TJERR_WARNING;
  }
  const std::string why = ok ? "" : area ? tj3GetErrorStr(t) : "no JPEG in it";
  tj3Destroy(t);
  if (!ok) throw std::runtime_error("pb-bench: " + path.string() + ": " + why);
  return p;
}

// For machines without the samples (CI): smooth colours with grain, as bench/open.cpp's; the same picture at any size.
Photo synthetic(int w, int h) {
  Photo p{w, h, std::vector<uint8_t>(size_t(w) * size_t(h) * 3)};
  const double k = 8256.0 / w;  // in pixels of the full size
  parallel_for(h, [&](int y0, int y1) {
    for (int y = y0; y < y1; y++) {
      std::minstd_rand rng(uint32_t(y) + 1);
      uint8_t* q = &p.px[size_t(y) * size_t(w) * 3];
      for (int x = 0; x < w; x++, q += 3) {
        const double X = x * k, Y = y * k;
        const double v[3] = {128 + 90 * std::sin(X / 300 + Y / 470), 128 + 90 * std::sin(X / 210 - Y / 330 + 1),
                             128 + 90 * std::cos(X / 150 + Y / 520)};
        for (int c = 0; c < 3; c++) q[c] = uint8_t(std::clamp(v[c] + double(rng() % 17) - 8, 0., 255.));
      }
    }
  });
  return p;
}

// The raw's JPEG at 1/scale, else a synthetic photo of that size; says which, for `doc`.
Photo load_photo(const std::filesystem::path& samples, int scale, const char* doc) {
  const std::filesystem::path raw = samples / kRaw;
  if (!std::filesystem::exists(raw)) {
    std::printf("%s: photo: synthetic, there is no %s\n", doc, raw.string().c_str());
    return synthetic(8256 / scale, 5504 / scale);
  }
  Photo p = embedded_jpeg(raw, scale);
  std::printf("%s: photo: the JPEG in %s at 1/%d, %d x %d\n", doc, raw.string().c_str(), scale, p.w, p.h);
  return p;
}

// A grid of format f with a tile at each of `at`, made on the pool: paint(xy, pixels) fills a fresh transparent one,
// which is kept compacted (empty when it stays transparent).
Grid made(Format f, const std::vector<TileXY>& at, const std::function<void(TileXY, uint8_t*)>& paint) {
  std::vector<TilePtr> t(at.size());
  parallel_for(int(at.size()), [&](int b, int e) {
    for (int i = b; i < e; i++) {
      PROFILE_ZONE("bench_doc_tile");
      const std::shared_ptr<Tile> x = Tile::make(f);
      {
        const WritePin p = x->write();
        paint(at[size_t(i)], p.data());
      }
      t[size_t(i)] = compact(x, {});
    }
  });
  auto g = std::make_shared<TileGrid>(f);
  for (size_t i = 0; i < at.size(); i++) g->set(at[i].x, at[i].y, std::move(t[i]));
  return g;
}

std::vector<TileXY> canvas_tiles(int w, int h) {
  std::vector<TileXY> at;
  for (int y = 0; y * S < h; y++)
    for (int x = 0; x * S < w; x++) at.push_back({x, y});
  return at;
}

uint32_t hash(int x, int y, int c) {  // murmur3's finalizer over the place
  uint32_t h = uint32_t(x) * 0x9e3779b1u ^ uint32_t(y) * 0x85ebca77u ^ uint32_t(c) * 0xc2b2ae3du;
  h ^= h >> 16, h *= 0x85ebca6bu, h ^= h >> 13, h *= 0xc2b2ae35u, h ^= h >> 16;
  return h;
}

// The photo at 16 bits: v x 257 plus noise of -128..128, which rounds back to v: K1 measured a 16-bit photo's tiles
// compressing 1.27x, which zero low bytes would beat by far.
Grid lifted(const Photo& p) {
  return made({Depth::U16, 4}, canvas_tiles(p.w, p.h), [&](TileXY xy, uint8_t* out) {
    auto* q = reinterpret_cast<uint16_t*>(out);
    for (int y = xy.y * S; y < std::min((xy.y + 1) * S, p.h); y++)
      for (int x = xy.x * S; x < std::min((xy.x + 1) * S, p.w); x++) {
        uint16_t* d = q + (size_t(y - xy.y * S) * S + size_t(x - xy.x * S)) * 4;
        for (int c = 0; c < 3; c++)
          d[c] = uint16_t(std::clamp(p.at(x, y)[c] * 257 + int(hash(x, y, c) % 257) - 128, 0, 65535));
        d[3] = 65535;
      }
  });
}

// D1's Low: base (opaque) blurred by a Gaussian of sigma kLowSigma, kLowRadius px each way, its edges extended; one
// pass across and one down, in floats, a tile at a time from the base's pixels about it.
Grid low_pass(const TileGrid& base, int w, int h) {
  constexpr int R = kLowRadius;
  std::array<float, 2 * R + 1> k;
  double sum = 0;
  for (int i = -R; i <= R; i++) sum += std::exp(-i * i / (2 * kLowSigma * kLowSigma));
  for (int i = -R; i <= R; i++) k[size_t(i + R)] = float(std::exp(-i * i / (2 * kLowSigma * kLowSigma)) / sum);
  return made({Depth::U16, 4}, canvas_tiles(w, h), [&](TileXY xy, uint8_t* out) {
    const int x0 = xy.x * S, y0 = xy.y * S, tw = std::min(S, w - x0), th = std::min(S, h - y0);
    const int rx0 = std::max(0, x0 - R), ry0 = std::max(0, y0 - R);
    const int rw = std::min(w, x0 + tw + R) - rx0, rh = std::min(h, y0 + th + R) - ry0, ph = th + 2 * R;
    std::vector<uint16_t> in(size_t(rw) * size_t(rh) * 4);
    base.read_rect(rx0, ry0, rw, rh, reinterpret_cast<uint8_t*>(in.data()), size_t(rw) * 8);
    std::vector<float> row(size_t(tw + 2 * R)), across(size_t(tw) * size_t(ph)), down(static_cast<size_t>(tw));
    auto* q = reinterpret_cast<uint16_t*>(out);
    for (int c = 0; c < 3; c++) {
      for (int j = 0; j < ph; j++) {  // rows y0 - R on, across
        const size_t sy = size_t(std::clamp(y0 - R + j, 0, h - 1) - ry0);
        for (int i = 0; i < tw + 2 * R; i++)
          row[size_t(i)] = in[(sy * size_t(rw) + size_t(std::clamp(x0 - R + i, 0, w - 1) - rx0)) * 4 + size_t(c)];
        float* o = &across[size_t(j) * size_t(tw)];
        std::fill(o, o + tw, 0.f);
        for (int t = 0; t <= 2 * R; t++)
          for (int i = 0; i < tw; i++) o[i] += k[size_t(t)] * row[size_t(i + t)];
      }
      for (int j = 0; j < th; j++) {
        std::fill(down.begin(), down.end(), 0.f);
        for (int t = 0; t <= 2 * R; t++)
          for (int i = 0; i < tw; i++) down[size_t(i)] += k[size_t(t)] * across[size_t(j + t) * size_t(tw) + size_t(i)];
        for (int i = 0; i < tw; i++)
          q[(size_t(j) * S + size_t(i)) * 4 + size_t(c)] = uint16_t(std::min(down[size_t(i)] + 0.5f, 65535.f));
      }
    }
    for (int j = 0; j < th; j++)
      for (int i = 0; i < tw; i++) q[(size_t(j) * S + size_t(i)) * 4 + 3] = 65535;
  });
}

// D1's High: (base - low) / 2 + 1/2, rounded.
Grid high_pass(const TileGrid& base, const TileGrid& low, int w, int h) {
  return made({Depth::U16, 4}, canvas_tiles(w, h), [&](TileXY xy, uint8_t* out) {
    std::vector<uint16_t> b(size_t(S) * S * 4), l(b.size());
    base.read_rect(xy.x * S, xy.y * S, S, S, reinterpret_cast<uint8_t*>(b.data()), size_t(S) * 8);
    low.read_rect(xy.x * S, xy.y * S, S, S, reinterpret_cast<uint8_t*>(l.data()), size_t(S) * 8);
    auto* q = reinterpret_cast<uint16_t*>(out);
    for (int j = 0; j < std::min(S, h - xy.y * S); j++)
      for (int i = 0; i < std::min(S, w - xy.x * S); i++) {
        const size_t p = (size_t(j) * S + size_t(i)) * 4;
        for (size_t c = 0; c < 3; c++) q[p + c] = uint16_t((float(b[p + c]) - float(l[p + c])) * 0.5f + 32768.f);
        q[p + 3] = 65535;
      }
  });
}

// Pixels of tile xy of g in the w x h canvas with alpha above 0.
size_t covered_tile(const TileGrid& g, TileXY xy, int w, int h) {
  const Tile* t = g.get(xy.x, xy.y);
  const int x0 = std::max(xy.x * S, 0), x1 = std::min((xy.x + 1) * S, w);
  const int y0 = std::max(xy.y * S, 0), y1 = std::min((xy.y + 1) * S, h);
  if (!t || x0 >= x1 || y0 >= y1) return 0;
  const Format f = g.format();
  const size_t a = size_t(f.channels - 1);
  if (t->is_solid()) return t->value()[a] ? size_t(x1 - x0) * size_t(y1 - y0) : 0;
  const Pin p = t->pin();
  const auto* p16 = reinterpret_cast<const uint16_t*>(p.data());
  size_t n = 0;
  for (int y = y0; y < y1; y++)
    for (int x = x0; x < x1; x++) {
      const size_t i = (size_t(y - xy.y * S) * S + size_t(x - xy.x * S)) * size_t(f.channels) + a;
      n += (f.depth == Depth::U16 ? p16[i] : p.data()[i]) != 0;
    }
  return n;
}

// Strokes through C2's Stroke onto an empty layer of a w x h canvas at 16 bits, each merged at pen-up, until they cover
// `cover` of the canvas (pixels with alpha above 0) or `most` are done; next() gives each one's brush and events.
Grid painted(int w, int h, double cover, int most,
             const std::function<std::pair<BrushParams, std::vector<PenEvent>>()>& next) {
  DocPtr d = run(*new_document(w, h, Mode::Rgb, Depth::U16, nullptr), AddLayer{});
  const LayerId id = d->selected[0];
  std::map<TileXY, size_t> count;  // covered pixels, by tile
  size_t covered = 0;
  for (int n = 0; n < most && double(covered) < cover * w * h; n++) {
    PROFILE_ZONE("bench_doc_stroke");
    const std::pair<BrushParams, std::vector<PenEvent>> s = next();
    Stroke stroke(d, id, s.first);
    for (const PenEvent& e : s.second) stroke.add(e);
    stroke.finish();
    const std::shared_ptr<const StrokeState> st = stroke.wait();
    if (!st->pixels) throw std::runtime_error("pb-bench: a stroke failed");
    d = run(*d, SetPixels{id, st->pixels});
    for (const auto& [xy, t] : st->buffer->tiles()) {
      size_t& c = count[xy];
      covered -= c;
      covered += c = covered_tile(*st->pixels, xy, w, h);
    }
  }
  return find_layer(*d, id)->pixels;
}

// n pen events at 200 Hz from (x, y), `step` px apart, the line bending smoothly (by up to `turn` radians an event, as a
// hand does) and the pressure drifting over 0.2..1.
std::vector<PenEvent> wander(Rng& rng, double x, double y, int n, double step, double turn) {
  std::vector<PenEvent> ev;
  double dir = uni(rng, 0, 2 * kPi), bend = 0, p = uni(rng, 0.4, 1);
  for (int i = 0; i < n; i++) {
    ev.push_back({i / 200., float(x), float(y), float(p)});
    bend = 0.7 * bend + uni(rng, -turn, turn);
    dir += bend;
    x += step * std::cos(dir), y += step * std::sin(dir);
    p = std::clamp(p + uni(rng, -0.08, 0.08), 0.2, 1.);
  }
  return ev;
}

// A layer of strokes with the photo's pixels from (dx, dy) away wherever they cover: what a clone source lays.
Grid cloned(const TileGrid& strokes, const TileGrid& photo, int dx, int dy) {
  std::vector<TileXY> at;
  for (const auto& [xy, t] : strokes.tiles()) at.push_back(xy);
  return made(strokes.format(), at, [&](TileXY xy, uint8_t* out) {
    std::vector<uint16_t> from(size_t(S) * S * 4);
    strokes.read_rect(xy.x * S, xy.y * S, S, S, out, size_t(S) * 8);
    photo.read_rect(xy.x * S + dx, xy.y * S + dy, S, S, reinterpret_cast<uint8_t*>(from.data()), size_t(S) * 8);
    auto* q = reinterpret_cast<uint16_t*>(out);
    for (size_t i = 0; i < from.size(); i += 4)
      if (q[i + 3]) std::copy(&from[i], &from[i + 3], &q[i]);
  });
}

// A piece of the photo on a layer of D2: canvas pixels x, y, w, h showing those from (sx, sy) on of picture `src` (0:
// the photo at 1/2, 1: at 1/4), its edges faded in over `feather` px.
struct Piece {
  int x = 0, y = 0, w = 0, h = 0, src = 0, sx = 0, sy = 0, feather = 0;
};

// A layer of pieces apart from each other, at 8 bits: the picture's pixels, at alpha 1 but in the fade.
Grid pieces_layer(const std::vector<Piece>& pieces, const Photo* src) {
  std::map<TileXY, std::vector<const Piece*>> at;
  for (const Piece& p : pieces)
    for (int ty = p.y >> kTileLog2; ty <= (p.y + p.h - 1) >> kTileLog2; ty++)
      for (int tx = p.x >> kTileLog2; tx <= (p.x + p.w - 1) >> kTileLog2; tx++) at[{tx, ty}].push_back(&p);
  std::vector<TileXY> xy;
  for (const auto& [t, v] : at) xy.push_back(t);
  const auto& which = at;
  return made({Depth::U8, 4}, xy, [&](TileXY t, uint8_t* out) {
    for (const Piece* p : which.at(t)) {
      const int x0 = std::max(p->x, t.x * S), x1 = std::min(p->x + p->w, (t.x + 1) * S);
      const int y0 = std::max(p->y, t.y * S), y1 = std::min(p->y + p->h, (t.y + 1) * S);
      for (int y = y0; y < y1; y++) {
        uint8_t* q = out + (size_t(y - t.y * S) * S + size_t(x0 - t.x * S)) * 4;
        const uint8_t* s = src[p->src].at(p->sx + x0 - p->x, p->sy + y - p->y);
        const int ey = std::min(y - p->y, p->y + p->h - 1 - y);
        for (int x = x0; x < x1; x++, q += 4, s += 3) {
          const int e = std::min({ey, x - p->x, p->x + p->w - 1 - x});
          q[0] = s[0], q[1] = s[1], q[2] = s[2];
          q[3] = uint8_t(e >= p->feather ? 255 : (e + 1) * 255 / (p->feather + 1));
        }
      }
    }
  });
}

// Layer i of D4, of `kind` (0 type, 1 shape, 2 smart object, 3 painted), from a seed of its own, somewhere on the
// cw x ch canvas, a little past its edges at times.
Grid d4_layer(int kind, int i, const Photo& photo, int cw, int ch) {
  Rng rng(uint32_t(1000 + i));
  const std::array<uint8_t, 3> colour{uint8_t(rng()), uint8_t(rng()), uint8_t(rng())};
  int w = 0, h = 0;
  std::vector<uint8_t> px;
  const auto size = [&](int ww, int hh) { w = ww, h = hh, px.assign(size_t(w) * size_t(h) * 4, 0); };
  const auto put = [&](int x, int y, const uint8_t* rgb, double alpha) {
    uint8_t* q = &px[(size_t(y) * size_t(w) + size_t(x)) * 4];
    q[0] = rgb[0], q[1] = rgb[1], q[2] = rgb[2], q[3] = uint8_t(alpha * 255 + 0.5);
  };
  const auto fill = [&](int x0, int y0, int x1, int y1) {
    for (int y = std::max(y0, 0); y < std::min(y1, h); y++)
      for (int x = std::max(x0, 0); x < std::min(x1, w); x++) put(x, y, colour.data(), 1);
  };
  switch (kind) {
    case 0: {  // lines of glyph-like marks: stems and bars, words apart
      const bool para = rng() % 5 == 0;
      const int lh = para ? pick(rng, 14, 30) : pick(rng, 16, 90);
      const int lines = para ? pick(rng, 5, 20) : 1;
      const int chars = para ? pick(rng, 30, 90) : pick(rng, 6, 36);
      const int cw = std::max(4, lh * 11 / 20), stem = std::max(1, lh / 8);
      size(chars * cw, lines * lh);
      for (int l = 0; l < lines; l++)
        for (int c = 0; c < chars; c++) {
          if (rng() % 6 == 0) continue;  // a space
          const int x = c * cw, y = l * lh, marks = pick(rng, 1, 3);
          for (int m = 0; m < marks; m++)
            if (rng() % 2) {
              const int sx = x + pick(rng, cw / 10, cw * 7 / 10), top = y + pick(rng, lh / 10, lh * 3 / 10);
              fill(sx, top, sx + stem, y + lh * 4 / 5);
            } else {
              const int by = y + pick(rng, lh / 4, lh * 3 / 4);
              fill(x + cw / 10, by, x + cw * 4 / 5, by + stem);
            }
        }
      break;
    }
    case 1: {  // an ellipse or a rounded rectangle, anti-aliased; one in ten a band across much of the canvas
      const bool band = rng() % 10 == 0;
      const double sw = band ? uni(rng, 1500, cw) : 40 * std::pow(60., uni(rng, 0, 1));
      const double sh = band ? uni(rng, 200, 1200) : 40 * std::pow(60., uni(rng, 0, 1));
      const bool ellipse = rng() % 3 == 0;
      const double corner = uni(rng, 0, 0.5);
      size(int(sw), int(sh));
      const double a = w / 2., b = h / 2., r = ellipse ? 0 : std::min(a, b) * corner;
      for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
          const double dx = std::abs(x + 0.5 - a), dy = std::abs(y + 0.5 - b);
          const double qx = dx - (a - r), qy = dy - (b - r);
          const double d = ellipse ? (std::hypot(dx / a, dy / b) - 1) * std::min(a, b)  // distance outside, about
                                   : std::hypot(std::max(qx, 0.), std::max(qy, 0.)) + std::min(std::max(qx, qy), 0.) - r;
          if (d < 0.5) put(x, y, colour.data(), std::min(0.5 - d, 1.));
        }
      break;
    }
    case 2: {  // a piece of the photo, as placed
      const int pw = pick(rng, 200, std::min(1500, photo.w));
      const int ph = std::min(photo.h, pw * pick(rng, 2, 4) / 4);
      const int sx = pick(rng, 0, photo.w - pw), sy = pick(rng, 0, photo.h - ph);
      size(pw, ph);
      for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) put(x, y, photo.at(sx + x, sy + y), 1);
      break;
    }
    default: {  // a soft blob
      const int bw = pick(rng, 50, 700), bh = pick(rng, 50, 700);
      size(bw, bh);
      for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
          const double u = (x + 0.5) / w * 2 - 1, v = (y + 0.5) / h * 2 - 1, r2 = u * u + v * v;
          if (r2 < 1) put(x, y, colour.data(), (1 - r2) * (1 - r2));
        }
    }
  }
  const int x = pick(rng, -w / 8, cw - w * 7 / 8), y = pick(rng, -h / 8, ch - h * 7 / 8);
  auto g = std::make_shared<TileGrid>(Format{Depth::U8, 4});
  g->write_rect(x, y, w, h, px.data(), size_t(w) * 4);
  return g;
}

}  // namespace

DocPtr make_d1(const std::filesystem::path& samples) {
  Grid base;
  int w = 0, h = 0;
  auto icc = srgb();
  if (const std::filesystem::path tif = samples / "photo16.tif"; std::filesystem::exists(tif)) {
    const DocPtr d = open_image(tif);
    if (d->mode == Mode::Rgb && d->depth == Depth::U16) {
      base = d->root->children[0]->pixels, w = d->width, h = d->height, icc = d->icc;
      std::printf("d1: photo: %s, %d x %d\n", tif.string().c_str(), w, h);
    } else {
      std::printf("d1: %s is not 16-bit RGB: not taken\n", tif.string().c_str());
    }
  }
  if (!base) {
    const Photo p = load_photo(samples, 1, "d1");
    base = lifted(p), w = p.w, h = p.h;
  }
  const Grid low = low_pass(*base, w, h);
  const Grid high = high_pass(*base, *low, w, h);

  Rng rng(1);
  const auto face = [&] {  // a point of the face: an ellipse about the centre, 40% of the canvas
    for (;;) {
      const double u = uni(rng, -1, 1), v = uni(rng, -1, 1);
      if (u * u + v * v <= 1) return std::array<double, 2>{w * (0.5 + 0.3 * u), h * (0.45 + 0.42 * v)};
    }
  };
  // Strokes from points of the face till they cover `cover`: 10 to 50 events of 0.3 of the size apart, sizes
  // size0..size1 px at full pressure; their colour the photo's where they start when `sample`.
  const auto retouch = [&](double cover, double size0, double size1, float hardness, float flow,
                           std::array<float, 3> color, bool sample) {
    return painted(w, h, cover, 5000, [&] {
      BrushParams b;
      b.size = float(uni(rng, size0, size1)), b.hardness = hardness, b.flow = flow, b.color = color;
      const std::array<double, 2> at = face();
      if (sample) {
        uint16_t px[4];
        base->read_rect(int(at[0]), int(at[1]), 1, 1, reinterpret_cast<uint8_t*>(px), sizeof px);
        for (size_t c = 0; c < 3; c++) b.color[c] = px[c] / 65535.f;
      }
      return std::make_pair(b, wander(rng, at[0], at[1], pick(rng, 10, 50), b.size * 0.3, 0.3));
    });
  };
  // Large soft strokes anywhere till they cover `cover`, as adjustment layers' masks are painted.
  const auto mask = [&](double cover, std::array<float, 3> color) {
    return painted(w, h, cover, 50, [&] {
      BrushParams b;
      b.size = float(uni(rng, 700, 1600)), b.hardness = 0, b.flow = 0.6f, b.color = color;
      const double x = uni(rng, 0, w), y = uni(rng, 0, h);
      return std::make_pair(b, wander(rng, x, y, pick(rng, 8, 24), b.size * 0.4, 0.2));
    });
  };
  const Grid heal = cloned(*retouch(0.2, 30, 150, 0.6f, 1, {1, 1, 1}, false), *base, 70, -45);
  const Grid clone = cloned(*retouch(0.2, 60, 250, 0.3f, 1, {1, 1, 1}, false), *base, -420, 260);
  const Grid dodge = retouch(0.2, 120, 420, 0, 0.1f, {1, 1, 1}, false);
  const Grid burn = retouch(0.2, 120, 420, 0, 0.1f, {0, 0, 0}, false);
  const Grid skin = retouch(0.2, 80, 300, 0.2f, 0.3f, {}, true);
  const Grid curves = mask(0.4, {0.35f, 0.35f, 0.35f});
  const Grid hue = mask(0.25, {0.62f, 0.47f, 0.40f});
  const Grid balance = mask(0.35, {0.70f, 0.56f, 0.42f});
  const Grid levels = mask(0.3, {0.92f, 0.90f, 0.86f});
  const Grid vignette = painted(w, h, 1, 1, [&] {  // one stroke round the edges
    BrushParams b;
    b.size = 1400, b.hardness = 0, b.flow = 0.5f, b.pressure_size = false, b.color = {0.45f, 0.42f, 0.40f};
    const auto edge = [&](double s) -> std::array<double, 2> {  // s px clockwise from the top left
      if (s < w) return {s, 0};
      if ((s -= w) < h) return {double(w), s};
      if ((s -= h) < w) return {w - s, double(h)};
      return {0, std::max(h - (s - w), 0.)};
    };
    std::vector<PenEvent> ev;
    for (int i = 0; i * 100. < 2. * (w + h); i++) ev.push_back({i / 200., float(edge(i * 100.)[0]), float(edge(i * 100.)[1]), 1});
    ev.push_back({ev.size() / 200., 0, 0, 1});  // back at the start
    return std::make_pair(b, ev);
  });
  const Grid text = painted(w, h, 1, 9, [&] {  // a signature's strokes in the bottom right corner
    BrushParams b;
    b.size = 12, b.hardness = 1, b.color = {0.95f, 0.95f, 0.95f};
    const double x0 = w - 2300., y0 = h - 650.;
    const double x = x0 + uni(rng, 0, 1800), y = y0 + uni(rng, 0, 380);
    std::vector<PenEvent> ev = wander(rng, x, y, pick(rng, 60, 120), 5, 0.9);
    for (PenEvent& e : ev)
      e.x = float(std::clamp(double(e.x), x0, x0 + 1800)), e.y = float(std::clamp(double(e.y), y0, y0 + 380));
    return std::make_pair(b, ev);
  });

  DocPtr d = new_document(w, h, Mode::Rgb, Depth::U16, icc);
  const auto add = [&](LayerId parent, const char* name, Kind kind, Blend blend, float opacity, Grid px) {
    LayerProps p;
    p.name = name, p.blend = blend, p.opacity = opacity;
    d = run(*d, AddLayer{parent, int(find_layer(*d, parent)->children.size()), kind, p, std::move(px)});
    return d->selected[0];
  };
  add(0, "Background", Kind::Pixel, Blend::Normal, 1, base);
  const LayerId fs = add(0, "Frequency Separation", Kind::Group, Blend::Normal, 1, nullptr);
  add(fs, "Low", Kind::Pixel, Blend::Normal, 1, low);
  add(fs, "High", Kind::Pixel, Blend::LinearLight, 1, high);
  add(0, "Heal", Kind::Pixel, Blend::Normal, 1, heal);
  add(0, "Clone", Kind::Pixel, Blend::Normal, 1, clone);
  add(0, "Dodge", Kind::Pixel, Blend::SoftLight, 1, dodge);
  const LayerId mid = add(0, "Burn", Kind::Pixel, Blend::SoftLight, 1, burn);
  add(0, "Skin", Kind::Pixel, Blend::Normal, 0.5f, skin);
  add(0, "Curves", Kind::Pixel, Blend::SoftLight, 1, curves);
  add(0, "Hue/Saturation", Kind::Pixel, Blend::Overlay, 0.6f, hue);
  add(0, "Color Balance", Kind::Pixel, Blend::SoftLight, 0.7f, balance);
  add(0, "Levels", Kind::Pixel, Blend::Multiply, 1, levels);
  add(0, "Vignette", Kind::Pixel, Blend::Multiply, 0.8f, vignette);
  add(0, "Text", Kind::Pixel, Blend::Normal, 0.9f, text);
  return run(*d, SelectLayers{{mid}});
}

DocPtr make_d2(bool mini, const std::filesystem::path& samples) {
  const char* name = mini ? "d2mini" : "d2";
  const Photo src[2] = {load_photo(samples, 2, name), load_photo(samples, 4, name)};
  constexpr int kW = 20000, kH = 15000, kLayers = 40, kCellW = 4000, kCellH = 2500;
  if (src[0].w < kCellW || src[0].h < kCellH) throw std::runtime_error("pb-bench: d2: the photo at 1/2 is under 4000 x 2500");
  Rng rng(2);
  std::vector<std::vector<Piece>> layers(kLayers);
  for (int y = 0; y < kH; y += kCellH)  // the mosaic
    for (int x = 0; x < kW; x += kCellW) {
      const int sx = pick(rng, 0, src[0].w - kCellW), sy = pick(rng, 0, src[0].h - kCellH);
      layers[0].push_back({x, y, kCellW, kCellH, 0, sx, sy, 0});
    }
  // The others' shares of the canvas, 10 to 46%, scaled so that with the mosaic's 100% they make 30% on average; their
  // pieces at random, 3 in 10 from the photo at 1/4, until they cover their share, the last cut short to it.
  std::vector<double> share(kLayers);
  double sum = 0;
  for (size_t l = 1; l < kLayers; l++) sum += share[l] = uni(rng, 0.1, 0.46);
  for (size_t l = 1; l < kLayers; l++) {
    const double want = share[l] * (0.3 * kLayers - 1) / sum * kW * kH;
    double area = 0;
    for (int tries = 0; area < want && tries < 100000; tries++) {
      Piece n;
      n.src = rng() % 10 < 7 ? 0 : 1;
      const Photo& p = src[n.src];
      n.w = pick(rng, 400, p.w), n.h = pick(rng, 300, p.h);
      n.x = pick(rng, 0, kW - n.w), n.y = pick(rng, 0, kH - n.h);
      n.sx = pick(rng, 0, p.w - n.w), n.sy = pick(rng, 0, p.h - n.h), n.feather = pick(rng, 0, 40);
      if (std::any_of(layers[l].begin(), layers[l].end(), [&](const Piece& o) {
            return n.x < o.x + o.w && o.x < n.x + n.w && n.y < o.y + o.h && o.y < n.y + n.h;
          }))
        continue;
      n.h = std::min(n.h, int(std::ceil((want - area) / n.w)));
      layers[l].push_back(n);
      area += double(n.w) * n.h;
    }
  }
  const int k = mini ? 4 : 1;  // D2-mini: a quarter across and down, from the same pictures
  for (std::vector<Piece>& l : layers) {
    for (Piece& p : l) {
      const int x1 = (p.x + p.w) / k, y1 = (p.y + p.h) / k;
      p.x /= k, p.y /= k, p.w = x1 - p.x, p.h = y1 - p.y, p.feather /= k;
    }
    l.erase(std::remove_if(l.begin(), l.end(), [](const Piece& p) { return p.w < 1 || p.h < 1; }), l.end());
  }

  constexpr Blend kModes[] = {Blend::Normal, Blend::Normal,   Blend::Normal, Blend::Normal,  Blend::Normal,
                              Blend::Normal, Blend::Multiply, Blend::Screen, Blend::Overlay, Blend::SoftLight};
  DocPtr d = new_document(kW / k, kH / k, Mode::Rgb, Depth::U8, srgb());
  for (int l = 0; l < kLayers; l++) {
    LayerProps p;
    p.name = l ? "Layer " + std::to_string(l + 1) : "Background";
    const Blend mode = kModes[rng() % 10];
    const bool faint = rng() % 4 == 0;
    const double opacity = uni(rng, 0.5, 0.9);
    p.blend = l ? mode : Blend::Normal;
    p.opacity = l && faint ? float(opacity) : 1;
    d = run(*d, AddLayer{0, l, Kind::Pixel, p, pieces_layer(layers[size_t(l)], src)});
  }
  return d;
}

DocPtr make_d4(const std::filesystem::path& samples) {
  constexpr int kW = 6000, kH = 4000, kLayers = 1000;
  const Photo photo = load_photo(samples, 4, "d4");
  Rng rng(4);
  std::vector<int> kinds;  // 300 type, 300 shapes, 100 smart objects, 300 painted, shuffled (Fisher-Yates)
  for (const int kind : {0, 1, 2, 3}) kinds.insert(kinds.end(), kind == 2 ? 100 : 300, kind);
  for (size_t i = kinds.size() - 1; i > 0; i--) std::swap(kinds[i], kinds[rng() % (i + 1)]);
  std::vector<Grid> px(kLayers);
  parallel_for(kLayers, [&](int b, int e) {
    for (int i = b; i < e; i++) px[size_t(i)] = d4_layer(kinds[size_t(i)], i, photo, kW, kH);
  });

  // The tree: a chain of groups 6 deep, then each layer into a group at random, one time in 6 into a new group there
  // while that is no deeper than 6.
  DocPtr d = new_document(kW, kH, Mode::Rgb, Depth::U8, srgb());
  std::vector<LayerId> groups{0};
  std::vector<int> depth{0};
  const char* const kNames[] = {"Text", "Shape", "Smart Object", "Layer", "Group"};
  int named[5] = {};
  constexpr Blend kOthers[] = {Blend::Multiply, Blend::Screen, Blend::Overlay, Blend::SoftLight};
  const auto add = [&](size_t g, int kind, Grid pixels) {  // kind 4: a group
    LayerProps p;
    p.name = std::string(kNames[kind]) + " " + std::to_string(++named[kind]);
    if (kind == 4) {
      p.blend = rng() % 5 ? Blend::PassThrough : Blend::Normal;
    } else {
      const uint32_t mode = rng() % 100;
      p.blend = mode < 85 ? Blend::Normal : kOthers[mode % 4];
      p.opacity = rng() % 5 ? 1 : float(uni(rng, 0.3, 0.95));
      p.visible = rng() % 20 != 0;
    }
    const int index = pick(rng, 0, int(find_layer(*d, groups[g])->children.size()));
    d = run(*d, AddLayer{groups[g], index, kind == 4 ? Kind::Group : Kind::Pixel, p, std::move(pixels)});
    return d->selected[0];
  };
  for (int i = 1; i <= 6; i++) groups.push_back(add(groups.size() - 1, 4, nullptr)), depth.push_back(i);
  for (size_t i = 0; i < kLayers; i++) {
    size_t g = rng() % groups.size();
    if (depth[g] < 6 && rng() % 6 == 0) {
      groups.push_back(add(g, 4, nullptr)), depth.push_back(depth[g] + 1);
      g = groups.size() - 1;
    }
    add(g, kinds[i], px[i]);
  }
  return d;
}

size_t resident_bytes() {
#ifdef _WIN32
  PROCESS_MEMORY_COUNTERS m{};
  return GetProcessMemoryInfo(GetCurrentProcess(), &m, sizeof m) ? m.WorkingSetSize : 0;
#elif defined(__APPLE__)
  mach_task_basic_info info{};
  mach_msg_type_number_t n = MACH_TASK_BASIC_INFO_COUNT;
  return task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &n) == KERN_SUCCESS
             ? size_t(info.resident_size)
             : 0;
#else
  long pages = 0, resident = 0;
  FILE* f = std::fopen("/proc/self/statm", "r");
  if (!f) return 0;
  const bool ok = std::fscanf(f, "%ld %ld", &pages, &resident) == 2;
  std::fclose(f);
  return ok ? size_t(resident) * size_t(sysconf(_SC_PAGESIZE)) : 0;
#endif
}

size_t peak_resident_bytes() {
#ifdef _WIN32
  PROCESS_MEMORY_COUNTERS m{};
  return GetProcessMemoryInfo(GetCurrentProcess(), &m, sizeof m) ? m.PeakWorkingSetSize : 0;
#elif defined(__APPLE__)
  mach_task_basic_info info{};
  mach_msg_type_number_t n = MACH_TASK_BASIC_INFO_COUNT;
  return task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &n) == KERN_SUCCESS
             ? size_t(info.resident_size_max)
             : 0;
#else
  FILE* f = std::fopen("/proc/self/status", "r");
  if (!f) return 0;
  char line[256];
  size_t kb = 0;
  while (std::fgets(line, sizeof line, f) && std::sscanf(line, "VmHWM: %zu kB", &kb) != 1) {
  }
  std::fclose(f);
  return kb * 1024;
#endif
}

}  // namespace pb::bench
