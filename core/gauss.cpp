#include "gauss.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <functional>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include "jobs.h"
#include "profile.h"

namespace pb {
namespace {

constexpr int S = kTileSize;
constexpr int kLanes = 8;      // inner loops run over blocks of this many values, which GCC vectorises at -O2
constexpr int kAcross = 16;    // values per line of the boxes' across pass: 16 / channels rows of a region at once
constexpr int kBatch = 16;     // rows of a region the direct kernel reads at once
constexpr double kOne = 256;   // a box's weight inside; its two end taps weigh `end` of it
constexpr double kRound = 4503599627370496.;  // 2^52: x + kRound - kRound is x to the nearest integer, 0 <= x < 2^51

size_t padded(size_t n) { return (n + kLanes - 1) / kLanes * kLanes; }

void check_format(Format f) {
  if ((f.channels != 1 && f.channels != 2 && f.channels != 4) || (f.depth != Depth::U8 && f.depth != Depth::U16))
    throw std::invalid_argument("pb: gauss: a format has 1, 2 or 4 channels, U8 or U16");
}

// Entries past the channels 0 and samples within the depth, as tile.cpp keeps pixels, so that they compare with ==.
Pixel normal(Format f, Pixel v) {
  for (int c = 0; c < 4; c++) v[c] = c < f.channels ? std::min(v[c], uint16_t(max_value(f.depth))) : 0;
  return v;
}
bool transparent(Format f, const Pixel& v) { return f.alpha() && v[f.channels - 1] == 0; }
bool alike(Format f, const Pixel& a, const Pixel& b) { return a == b || (transparent(f, a) && transparent(f, b)); }

// What a Gaussian of a sigma convolves with.
struct Kernel {
  double sigma = -1;
  int margin = 0;           // px read past each side
  bool boxes = false;
  std::vector<float> taps;  // direct: 2 margin + 1 of them, summing to 1
  int r = 0;                // boxes: each sums 2r + 1 values at weight kOne and the next one on either side at `end`
  double end = 0, inv = 0;  // inv = 1 / (kOne (2r + 1) + 2 end), its total weight
};

Kernel make_kernel(double sigma) {
  if (!(sigma >= 0 && sigma <= kMaxSigma)) throw std::invalid_argument("pb: gauss: sigma outside 0 to 4000");
  Kernel k;
  k.sigma = sigma;
  if (sigma == 0) return k;
  // The sampled Gaussian out to ceil(3 sigma), renormalised.
  const int m = int(std::ceil(3 * sigma));
  std::vector<double> g(size_t(2 * m + 1));
  double sum = 0;
  for (int i = -m; i <= m; i++) sum += g[size_t(i + m)] = std::exp(-double(i) * i / (2 * sigma * sigma));
  if (sigma <= kDirectSigma) {
    k.margin = m;
    for (const double v : g) k.taps.push_back(float(v / sum));
    return k;
  }
  // kBoxes boxes sharing its variance: each the widest plain box within a kBoxes-th of it, its end taps weighted for
  // the rest (radius r with end taps a: (r (r + 1)(2r + 1) / 3 + 2 a (r + 1)^2) / (2r + 1 + 2a)).
  double var = 0;
  for (int i = -m; i <= m; i++) var += g[size_t(i + m)] / sum * i * i;
  const double v = var / kBoxes;
  int r = int((std::sqrt(12 * v + 1) - 1) / 2);
  while (r > 0 && r * (r + 1.) / 3 > v) r--;
  while ((r + 1.) * (r + 2) / 3 <= v) r++;
  const double a = (v * (2 * r + 1) - r * (r + 1.) * (2 * r + 1) / 3) / (2 * (r + 1.) * (r + 1) - 2 * v);
  k.boxes = true;
  k.r = r;
  k.end = std::round(std::clamp(a, 0., 1.) * kOne);
  k.inv = 1 / (kOne * (2 * r + 1) + 2 * k.end);
  k.margin = kBoxes * (r + 1);
  return k;
}

// The last kernel this thread made, for gauss_tile() and gauss_pixels() called tile after tile with one sigma.
const Kernel& kernel(double sigma) {
  thread_local Kernel k;
  if (!(k.sigma == sigma && sigma >= 0)) k = make_kernel(sigma);
  return k;
}

// A thread's buffers, kept from tile to tile.
struct Work {
  std::vector<uint8_t> bytes, edge;
  std::vector<float> row, across, down;
  std::vector<double> lines0, lines1, h0, h1, sum;
};

// Fills rows j0 to j0 + n of the region a kernel reads into dst, rows `stride` bytes apart.
using Rows = std::function<void(int j0, int n, uint8_t* dst, size_t stride)>;

// --- Direct: floats 0..1, colours premultiplied by alpha.

template <class T>
void load_unit(const uint8_t* src, int n, int C, float* dst) {
  const T* s = reinterpret_cast<const T*>(src);
  const float inv = 1.f / float(sizeof(T) == 1 ? 255 : 65535);
  if (C == 1) {
    for (int i = 0; i < n; i++) dst[i] = float(s[i]) * inv;
    return;
  }
  for (int i = 0; i < n; i++, s += C, dst += C) {
    const float a = float(s[C - 1]) * inv;
    for (int c = 0; c < C - 1; c++) dst[c] = float(s[c]) * inv * a;
    dst[C - 1] = a;
  }
}

template <class T>
void store_unit(const float* src, int n, int C, uint8_t* dst) {
  T* d = reinterpret_cast<T*>(dst);
  const Depth depth = sizeof(T) == 1 ? Depth::U8 : Depth::U16;
  if (C == 1) {
    for (int i = 0; i < n; i++) d[i] = T(from_unit(src[i], depth));
    return;
  }
  for (int i = 0; i < n; i++, src += C, d += C) {
    const float a = src[C - 1];
    const T alpha = T(from_unit(a, depth));
    for (int c = 0; c < C - 1; c++) d[c] = alpha ? T(from_unit(src[c] / a, depth)) : 0;  // a >= half a level
    d[C - 1] = alpha;
  }
}

// out[j] = the sum over t of taps[t] row[j + t C], j < len, t in order.
void direct_across(const float* __restrict row, size_t len, int C, const std::vector<float>& taps,
                   float* __restrict out) {
  std::fill(out, out + len, 0.f);
  for (size_t t = 0; t < taps.size(); t++) {
    const float k = taps[t];
    const float* __restrict in = row + t * size_t(C);
    for (size_t x = 0; x < len; x += kLanes)
      for (int q = 0; q < kLanes; q++) out[x + q] += k * in[x + q];
  }
}

// Line i of out (n of them, len values each) = the sum over t of taps[t] line i + t of in, t in order.
void direct_down(const float* __restrict in, int n, size_t len, const std::vector<float>& taps, float* __restrict out) {
  for (int i = 0; i < n; i++) {
    float* __restrict o = out + size_t(i) * len;
    std::fill(o, o + len, 0.f);
    for (size_t t = 0; t < taps.size(); t++) {
      const float k = taps[t];
      const float* __restrict l = in + (size_t(i) + t) * len;
      for (size_t x = 0; x < len; x += kLanes)
        for (int q = 0; q < kLanes; q++) o[x + q] += k * l[x + q];
    }
  }
}

void blur_direct(Format f, const Kernel& k, const Rows& rows, int w, int h, uint8_t* out, size_t out_stride, Work& wk) {
  PROFILE_ZONE("gauss_direct");
  const int m = k.margin, C = f.channels, rw = w + 2 * m, rh = h + 2 * m;
  const size_t bpp = size_t(f.bytes()), len = padded(size_t(w) * C);
  const bool u8 = f.depth == Depth::U8;
  wk.bytes.resize(size_t(kBatch) * rw * bpp);
  wk.row.assign(size_t(rw) * C + kLanes, 0.f);  // direct_across reads up to len - 1 + 2 m C
  wk.across.resize(size_t(rh) * len);
  for (int j0 = 0; j0 < rh; j0 += kBatch) {
    const int n = std::min(kBatch, rh - j0);
    rows(j0, n, wk.bytes.data(), size_t(rw) * bpp);
    for (int j = 0; j < n; j++) {
      const uint8_t* src = wk.bytes.data() + size_t(j) * rw * bpp;
      u8 ? load_unit<uint8_t>(src, rw, C, wk.row.data()) : load_unit<uint16_t>(src, rw, C, wk.row.data());
      direct_across(wk.row.data(), len, C, k.taps, wk.across.data() + size_t(j0 + j) * len);
    }
  }
  wk.down.resize(size_t(h) * len);
  direct_down(wk.across.data(), h, len, k.taps, wk.down.data());
  for (int y = 0; y < h; y++) {
    const float* src = wk.down.data() + size_t(y) * len;
    uint8_t* dst = out + size_t(y) * out_stride;
    u8 ? store_unit<uint8_t>(src, w, C, dst) : store_unit<uint16_t>(src, w, C, dst);
  }
}

// --- Boxes: integers in doubles, exact, about 2^32 to the full range: a value without alpha, and alpha itself, is
// v s1 (s1 (2^n - 1) = 2^32 - 1), a colour v a s2. Sums of up to 2^13 of them, 256 times, stay below 2^53. Between
// the pass across and the pass down they round to 16 bits (kHalf units), as gauss() keeps them in tiles of the store
// between its two phases: however a pixel is computed, the same number.

struct Scale {
  double s1, s2;
};
Scale scale(Depth d) { return d == Depth::U8 ? Scale{16843009, 66051} : Scale{65537, 1}; }

constexpr double kHalf = 65537;  // 2^32 units in one of 16 bits
double to16(double x) { return x * (1 / kHalf) + kRound - kRound; }  // to the nearest 16-bit unit

// Pixel i's samples at dst + i stride.
template <class T>
void load_fixed(const uint8_t* src, int n, int C, Scale k, double* dst, size_t stride) {
  const T* s = reinterpret_cast<const T*>(src);
  for (int i = 0; i < n; i++, s += C, dst += stride) {
    if (C == 1) {
      dst[0] = double(s[0]) * k.s1;
      continue;
    }
    const double a = double(s[C - 1]);
    for (int c = 0; c < C - 1; c++) dst[c] = double(s[c]) * a * k.s2;
    dst[C - 1] = a * k.s1;
  }
}

template <class T>
void store_fixed(const double* src, int n, int C, Scale k, uint8_t* dst) {
  T* d = reinterpret_cast<T*>(dst);
  const double top = sizeof(T) == 1 ? 255 : 65535, inv = 1 / k.s1, ratio = k.s1 / k.s2;
  const auto level = [top](double v) { return T(v > 0 ? (v < top ? v + 0.5 : top) : 0.); };  // v in levels
  if (C == 1) {
    for (int i = 0; i < n; i++) d[i] = level(src[i] * inv);
    return;
  }
  for (int i = 0; i < n; i++, src += C, d += C) {
    const double a = src[C - 1];
    const T alpha = level(a * inv);
    for (int c = 0; c < C - 1; c++) d[c] = alpha ? level(src[c] * ratio / a) : 0;
    d[C - 1] = alpha;
  }
}

// One box along n lines of len values each: line i of out (n - 2r - 2 of them) from lines i to i + 2r + 2 of in,
// rounded to an integer; the 2r + 1 inside a running sum, exact.
void box_lines(const double* __restrict in, int n, size_t len, const Kernel& k, double* __restrict out,
               double* __restrict sum) {
  const int w = 2 * k.r + 1;
  std::fill(sum, sum + len, 0.);
  for (int j = 1; j <= w; j++) {
    const double* __restrict l = in + size_t(j) * len;
    for (size_t x = 0; x < len; x += kLanes)
      for (int q = 0; q < kLanes; q++) sum[x + q] += l[x + q];
  }
  const double one = kOne, end = k.end, inv = k.inv;
  for (int i = 0; i + w + 1 < n; i++) {
    const double* __restrict lo = in + size_t(i) * len;
    const double* __restrict first = lo + len;
    const double* __restrict hi = in + size_t(i + w + 1) * len;
    double* __restrict o = out + size_t(i) * len;
    for (size_t x = 0; x < len; x += kLanes)
      for (int q = 0; q < kLanes; q++) {
        const size_t p = x + q;
        o[p] = (one * sum[p] + end * (lo[p] + hi[p])) * inv + kRound - kRound;
        sum[p] += hi[p] - first[p];
      }
  }
}

// kBoxes boxes along n lines in a (b the other buffer, as large); returns where the n - 2 margin lines out lie.
double* boxes_along(double* a, double* b, int n, size_t len, const Kernel& k, double* sum) {
  for (int p = 0; p < kBoxes; p++, n -= 2 * (k.r + 1)) {
    box_lines(a, n, len, k, b, sum);
    std::swap(a, b);
  }
  return a;
}

// Across: the region's rows kAcross / C at a time, turned into lines of a column each (pixel x of row j at line x,
// values j C to j C + C - 1), so that the running sums of those rows go side by side; then down, a line a row.
void blur_boxes(Format f, const Kernel& k, const Rows& rows, int w, int h, uint8_t* out, size_t out_stride, Work& wk) {
  PROFILE_ZONE("gauss_boxes");
  const int m = k.margin, C = f.channels, rw = w + 2 * m, rh = h + 2 * m, batch = kAcross / C;
  const size_t bpp = size_t(f.bytes()), len = padded(size_t(w) * C);
  const bool u8 = f.depth == Depth::U8;
  const Scale sc = scale(f.depth);
  wk.bytes.resize(size_t(batch) * rw * bpp);
  wk.lines0.resize(size_t(rw) * kAcross);
  wk.lines1.resize(size_t(rw) * kAcross);
  wk.sum.resize(std::max(len, size_t(kAcross)));
  wk.h0.resize(size_t(rh) * len);
  wk.h1.resize(size_t(rh) * len);
  for (int j0 = 0; j0 < rh; j0 += batch) {
    const int n = std::min(batch, rh - j0);
    rows(j0, n, wk.bytes.data(), size_t(rw) * bpp);
    std::fill(wk.lines0.begin(), wk.lines0.end(), 0.);  // rows past n stay 0
    for (int j = 0; j < n; j++) {
      const uint8_t* src = wk.bytes.data() + size_t(j) * rw * bpp;
      double* dst = wk.lines0.data() + size_t(j) * C;
      u8 ? load_fixed<uint8_t>(src, rw, C, sc, dst, kAcross) : load_fixed<uint16_t>(src, rw, C, sc, dst, kAcross);
    }
    const double* a = boxes_along(wk.lines0.data(), wk.lines1.data(), rw, kAcross, k, wk.sum.data());
    for (int j = 0; j < n; j++) {
      double* row = wk.h0.data() + size_t(j0 + j) * len;
      for (int x = 0; x < w; x++)
        for (int c = 0; c < C; c++) row[size_t(x) * C + c] = to16(a[size_t(x) * kAcross + size_t(j) * C + c]) * kHalf;
    }
  }
  for (int j = 0; j < rh; j++)  // the padding at the end of each line: any finite value
    std::fill(wk.h0.begin() + ptrdiff_t(size_t(j) * len + size_t(w) * C), wk.h0.begin() + ptrdiff_t((j + 1) * len), 0.);
  const double* a = boxes_along(wk.h0.data(), wk.h1.data(), rh, len, k, wk.sum.data());
  for (int y = 0; y < h; y++) {
    const double* src = a + size_t(y) * len;
    uint8_t* dst = out + size_t(y) * out_stride;
    u8 ? store_fixed<uint8_t>(src, w, C, sc, dst) : store_fixed<uint16_t>(src, w, C, sc, dst);
  }
}

void blur(Format f, const Kernel& k, const Rows& rows, int w, int h, uint8_t* out, size_t out_stride, Work& wk) {
  (k.boxes ? blur_boxes : blur_direct)(f, k, rows, w, h, out, out_stride, wk);
}

// --- Grids.

bool has_canvas(const GaussEdges& e) { return e.width > 0 && e.height > 0; }

// Pixels x, y, w, h of the plane the blur reads into dst, rows `stride` bytes apart: the grid's, or within the canvas,
// past it the nearest pixel on it or `outside`.
void read_region(const TileGrid& g, const GaussEdges& e, int x, int y, int w, int h, uint8_t* dst, size_t stride,
                 std::vector<uint8_t>& tmp) {
  const Format f = g.format();
  const size_t bpp = size_t(f.bytes());
  if (!has_canvas(e) || (x >= 0 && y >= 0 && x + w <= e.width && y + h <= e.height))
    return g.read_rect(x, y, w, h, dst, stride);
  if (!e.extend) {
    const Pixel v = normal(f, e.outside);
    uint8_t px[8];
    for (int c = 0; c < f.channels; c++)
      f.depth == Depth::U16 ? void(std::memcpy(px + 2 * c, &v[size_t(c)], 2)) : void(px[c] = uint8_t(v[size_t(c)]));
    for (int j = 0; j < h; j++)
      for (int i = 0; i < w; i++) std::memcpy(dst + size_t(j) * stride + size_t(i) * bpp, px, bpp);
    const int x0 = std::max(x, 0), y0 = std::max(y, 0);
    const int x1 = int(std::min<int64_t>(int64_t(x) + w, e.width)), y1 = int(std::min<int64_t>(int64_t(y) + h, e.height));
    if (x0 < x1 && y0 < y1)
      g.read_rect(x0, y0, x1 - x0, y1 - y0, dst + size_t(y0 - y) * stride + size_t(x0 - x) * bpp, stride);
    return;
  }
  // The nearest pixel on the canvas: the rows and columns there read once, then repeated.
  const int cx0 = std::clamp(x, 0, e.width - 1), cy0 = std::clamp(y, 0, e.height - 1);
  const int cx1 = int(std::clamp<int64_t>(int64_t(x) + w - 1, 0, e.width - 1));
  const int cy1 = int(std::clamp<int64_t>(int64_t(y) + h - 1, 0, e.height - 1));
  const int cw = cx1 - cx0 + 1, ch = cy1 - cy0 + 1;
  const size_t crow = size_t(cw) * bpp;
  tmp.resize(size_t(ch) * crow);
  g.read_rect(cx0, cy0, cw, ch, tmp.data(), crow);
  const int left = int(std::clamp<int64_t>(-int64_t(x), 0, w));                     // columns left of the canvas
  const int right = int(std::clamp<int64_t>(int64_t(x) + w - e.width, 0, w - left));  // right of it
  const int mid = w - left - right;
  for (int j = 0; j < h; j++) {
    const uint8_t* src = tmp.data() + size_t(std::clamp<int64_t>(int64_t(y) + j, 0, e.height - 1) - cy0) * crow;
    uint8_t* d = dst + size_t(j) * stride;
    for (int i = 0; i < left; i++) std::memcpy(d + size_t(i) * bpp, src, bpp);
    if (mid > 0) std::memcpy(d + size_t(left) * bpp, src + size_t(x + left - cx0) * bpp, size_t(mid) * bpp);
    for (int i = left + mid; i < w; i++) std::memcpy(d + size_t(i) * bpp, src + crow - bpp, bpp);
  }
}

// Whether every pixel the blur of tile (tx, ty) reads, mx past it across and my down, is alike (one value, or
// transparent throughout), and which: no data tile under it, and what the canvas puts past its edges.
bool uniform(const TileGrid& g, const GaussEdges& e, int mx, int my, int tx, int ty, Pixel& v) {
  const Format f = g.format();
  int64_t x0 = int64_t(tx) * S - mx, x1 = (int64_t(tx) + 1) * S + mx;
  int64_t y0 = int64_t(ty) * S - my, y1 = (int64_t(ty) + 1) * S + my;
  bool any = false;
  const auto take = [&](const Pixel& p) {
    if (any) return alike(f, v, p);
    any = true, v = p;
    return true;
  };
  if (has_canvas(e)) {
    if (e.extend) {  // the nearest pixels on the canvas
      x0 = std::clamp<int64_t>(x0, 0, e.width - 1), x1 = std::clamp<int64_t>(x1 - 1, 0, e.width - 1) + 1;
      y0 = std::clamp<int64_t>(y0, 0, e.height - 1), y1 = std::clamp<int64_t>(y1 - 1, 0, e.height - 1) + 1;
    } else {
      if (x0 < 0 || y0 < 0 || x1 > e.width || y1 > e.height) take(normal(f, e.outside));
      x0 = std::max<int64_t>(x0, 0), x1 = std::min<int64_t>(x1, e.width);
      y0 = std::max<int64_t>(y0, 0), y1 = std::min<int64_t>(y1, e.height);
      if (x0 >= x1 || y0 >= y1) return true;
    }
  }
  for (int64_t y = y0 >> kTileLog2; y <= (y1 - 1) >> kTileLog2; y++)
    for (int64_t x = x0 >> kTileLog2; x <= (x1 - 1) >> kTileLog2; x++) {
      const Tile* t = g.get(int(x), int(y));
      if (t && !t->is_solid()) return false;
      if (!take(t ? t->value() : g.fill())) return false;
    }
  if (transparent(f, v)) v = {};
  return true;
}

// Tile (tx, ty) out where all the blur of it reads is alike, without filtering: null where that is fill(), the
// grid's own tile where it is as alike, else a solid tile.
bool shortcut(const TileGrid& g, const Kernel& k, const GaussEdges& e, int tx, int ty, TilePtr& out) {
  const auto it = g.tiles().find({tx, ty});
  const TilePtr own = it == g.tiles().end() ? nullptr : it->second;
  const Format f = g.format();
  Pixel v;
  if (k.margin == 0) return out = own, true;
  if (!uniform(g, e, k.margin, k.margin, tx, ty, v)) return false;
  if (alike(f, v, g.fill()))
    out = nullptr;
  else if (own && own->is_solid() && alike(f, own->value(), v))
    out = own;
  else
    out = Tile::solid(f, v);
  return true;
}

TilePtr blur_tile(const TileGrid& g, const Kernel& k, const GaussEdges& e, int tx, int ty, Work& wk) {
  TilePtr out;
  if (shortcut(g, k, e, tx, ty, out)) return out;
  PROFILE_ZONE("gauss_tile");
  const Format f = g.format();
  const std::shared_ptr<Tile> t = Tile::make(f);
  {
    const WritePin p = t->write();
    const int m = k.margin, x = tx * S - m, y = ty * S - m;
    const Rows rows = [&](int j0, int n, uint8_t* dst, size_t stride) {
      read_region(g, e, x, y + j0, S + 2 * m, n, dst, stride, wk.edge);
    };
    blur(f, k, rows, S, S, p.data(), size_t(S) * size_t(f.bytes()), wk);
  }
  return compact(t, g.fill());
}

// --- gauss() with boxes: two phases over strips of tiles, so that no row is filtered again for each tile under its
// margin. Phase 1, the pass across along tile rows, for the rows the tiles to filter read, into H: 16-bit tiles of
// the store (the values premultiplied, as the boxes round them between passes); phase 2, the pass down along tile
// columns, from H. Each a run of tiles at a time, with the margin once at its ends.

// Tiles a to b of tile row `line` (phase 1) or tile column `line` (phase 2).
struct Run {
  int line, a, b;
};

// Runs of positions (line, position, sorted), joined over gaps shorter than `join` tiles, at most `most` tiles long.
std::vector<Run> runs(const std::vector<std::pair<int, int>>& at, int join, int most) {
  std::vector<Run> out;
  for (const auto& [line, pos] : at)
    if (!out.empty() && out.back().line == line && pos - out.back().b <= join && pos - out.back().a < most)
      out.back().b = pos;
    else
      out.push_back({line, pos, pos});
  return out;
}

// What the pass across gives a row of v, in H's format: its samples premultiplied, to 16 bits.
Pixel across_value(Format f, const Pixel& v) {
  uint8_t px[8] = {};
  for (int c = 0; c < f.channels; c++)
    f.depth == Depth::U16 ? void(std::memcpy(px + 2 * c, &v[size_t(c)], 2)) : void(px[c] = uint8_t(v[size_t(c)]));
  double d[4];
  f.depth == Depth::U8 ? load_fixed<uint8_t>(px, 1, f.channels, scale(f.depth), d, 4)
                       : load_fixed<uint16_t>(px, 1, f.channels, scale(f.depth), d, 4);
  Pixel h{};
  for (int c = 0; c < f.channels; c++) h[size_t(c)] = uint16_t(to16(d[c]));
  return h;
}

// Phase 1: H's tiles a to b of tile row `line`, compacted (fill: H's).
void across_run(const TileGrid& g, const Kernel& k, const GaussEdges& e, const Run& run, Pixel hfill,
                std::vector<TilePtr>& out, Work& wk) {
  PROFILE_ZONE("gauss_across");
  const Format f = g.format(), hf{Depth::U16, f.channels};
  const int m = k.margin, C = f.channels, n = run.b - run.a + 1, w = n * S, rw = w + 2 * m, batch = kAcross / C;
  const size_t bpp = size_t(f.bytes());
  const bool u8 = f.depth == Depth::U8;
  const Scale sc = scale(f.depth);
  std::vector<std::shared_ptr<Tile>> tiles;
  std::vector<WritePin> pins;
  for (int i = 0; i < n; i++) {
    tiles.push_back(Tile::make(hf));
    pins.push_back(tiles.back()->write());
  }
  wk.bytes.resize(size_t(batch) * rw * bpp);
  wk.lines0.resize(size_t(rw) * kAcross);
  wk.lines1.resize(size_t(rw) * kAcross);
  wk.sum.resize(kAcross);
  for (int j0 = 0; j0 < S; j0 += batch) {
    read_region(g, e, run.a * S - m, run.line * S + j0, rw, batch, wk.bytes.data(), size_t(rw) * bpp, wk.edge);
    for (int j = 0; j < batch; j++) {
      const uint8_t* src = wk.bytes.data() + size_t(j) * rw * bpp;
      double* dst = wk.lines0.data() + size_t(j) * C;
      u8 ? load_fixed<uint8_t>(src, rw, C, sc, dst, kAcross) : load_fixed<uint16_t>(src, rw, C, sc, dst, kAcross);
    }
    const double* a = boxes_along(wk.lines0.data(), wk.lines1.data(), rw, kAcross, k, wk.sum.data());
    for (int j = 0; j < batch; j++)
      for (int x = 0; x < w; x++) {
        uint16_t* d = reinterpret_cast<uint16_t*>(pins[size_t(x / S)].data()) + (size_t(j0 + j) * S + size_t(x % S)) * C;
        for (int c = 0; c < C; c++) d[c] = uint16_t(to16(a[size_t(x) * kAcross + size_t(j) * C + c]));
      }
  }
  pins.clear();
  out.resize(size_t(n));
  for (int i = 0; i < n; i++) out[size_t(i)] = compact(tiles[size_t(i)], hfill);
}

// Rows y to y + n of H at columns x to x + w into dst: H's own, or past the canvas's top and bottom the nearest row on
// it (`extend`) or a row of `outside` across (hout).
void read_h(const TileGrid& h, const GaussEdges& e, const Pixel& hout, int x, int y, int w, int n, uint8_t* dst,
            size_t stride) {
  if (!has_canvas(e) || (y >= 0 && y + n <= e.height)) return h.read_rect(x, y, w, n, dst, stride);
  const int y0 = std::max(y, 0), y1 = int(std::min<int64_t>(int64_t(y) + n, e.height));  // rows on the canvas
  if (y0 < y1) h.read_rect(x, y0, w, y1 - y0, dst + size_t(y0 - y) * stride, stride);
  const size_t bpp = size_t(h.format().bytes()), row = size_t(w) * bpp;
  for (int j = 0; j < n; j++) {
    const int at = y + j;
    if (at >= y0 && at < y1) continue;
    uint8_t* d = dst + size_t(j) * stride;
    if (e.extend && y0 < y1) {
      std::memcpy(d, dst + size_t(std::clamp(at, y0, y1 - 1) - y) * stride, row);
    } else {
      for (int i = 0; i < w; i++) std::memcpy(d + size_t(i) * bpp, hout.data(), bpp);
    }
  }
}

// Phase 2: tiles a to b of tile column `line` out, from H, kAcross / C columns at a time; compacted.
void down_run(const TileGrid& h, Format f, Pixel fill, const Kernel& k, const GaussEdges& e, const Pixel& hout,
              const Run& run, std::vector<TilePtr>& out, Work& wk) {
  PROFILE_ZONE("gauss_down");
  const int m = k.margin, C = f.channels, n = run.b - run.a + 1, rows = n * S, rh = rows + 2 * m, cols = kAcross / C;
  const size_t bpp = size_t(f.bytes()), hb = size_t(2 * C);
  const Scale sc = scale(f.depth);
  const bool u8 = f.depth == Depth::U8;
  std::vector<std::shared_ptr<Tile>> tiles;
  std::vector<WritePin> pins;
  for (int i = 0; i < n; i++) {
    tiles.push_back(Tile::make(f));
    pins.push_back(tiles.back()->write());
  }
  wk.bytes.resize(size_t(rh) * cols * hb);
  wk.h0.resize(size_t(rh) * kAcross);
  wk.h1.resize(size_t(rh) * kAcross);
  wk.sum.resize(kAcross);
  for (int c0 = 0; c0 < S; c0 += cols) {
    read_h(h, e, hout, run.line * S + c0, run.a * S - m, cols, rh, wk.bytes.data(), size_t(cols) * hb);
    for (int i = 0; i < rh; i++) {
      const uint16_t* s = reinterpret_cast<const uint16_t*>(wk.bytes.data() + size_t(i) * cols * hb);
      double* l = wk.h0.data() + size_t(i) * kAcross;
      for (int q = 0; q < kAcross; q++) l[q] = double(s[q]) * kHalf;
    }
    const double* a = boxes_along(wk.h0.data(), wk.h1.data(), rh, kAcross, k, wk.sum.data());
    for (int y = 0; y < rows; y++) {
      uint8_t* dst = pins[size_t(y / S)].data() + (size_t(y % S) * S + size_t(c0)) * bpp;
      u8 ? store_fixed<uint8_t>(a + size_t(y) * kAcross, cols, C, sc, dst)
         : store_fixed<uint16_t>(a + size_t(y) * kAcross, cols, C, sc, dst);
    }
  }
  pins.clear();
  out.resize(size_t(n));
  for (int i = 0; i < n; i++) out[size_t(i)] = compact(tiles[size_t(i)], fill);
}

// The tiles to filter (where work[i], i over the rect of tiles r row by row) through the two phases, into done[i].
void blur_strips(const TileGrid& g, const Kernel& k, const GaussEdges& e, TileRect r, const std::vector<char>& work,
                 std::vector<TilePtr>& done) {
  const Format f = g.format(), hf{Depth::U16, f.channels};
  const int m = k.margin, join = (2 * m + S - 1) / S, most = std::max(8, 2 * join), w = r.x1 - r.x0;
  // Phase 2's runs, column by column; the tiles of H they read, row by row.
  std::vector<std::pair<int, int>> at;
  for (int x = r.x0; x < r.x1; x++)
    for (int y = r.y0; y < r.y1; y++)
      if (work[size_t(y - r.y0) * w + size_t(x - r.x0)]) at.push_back({x, y});
  const std::vector<Run> down = runs(at, join, most);
  int top = INT_MIN, bottom = INT_MAX;  // H's tile rows on the canvas
  if (has_canvas(e)) top = 0, bottom = (e.height - 1) >> kTileLog2;
  std::vector<std::pair<int, int>> rows;
  for (const Run& d : down) {
    const int y0 = int((int64_t(d.a) * S - m) >> kTileLog2), y1 = int(((int64_t(d.b) + 1) * S + m - 1) >> kTileLog2);
    for (int y = std::max(y0, top); y <= std::min(y1, bottom); y++) rows.push_back({y, d.line});
  }
  std::sort(rows.begin(), rows.end());
  rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
  // Phase 1: tiles of H whose rows read alike are that value without filtering, the others in runs along their rows.
  TileGrid h(hf, across_value(f, g.fill()));
  std::vector<std::pair<int, int>> todo;
  for (const auto& [y, x] : rows) {
    Pixel v;
    if (!uniform(g, e, m, 0, x, y, v)) {
      todo.push_back({y, x});
    } else if (const Pixel hv = across_value(f, v); !alike(hf, hv, h.fill())) {
      h.set(x, y, Tile::solid(hf, hv));
    }
  }
  const std::vector<Run> across = runs(todo, join, most);
  std::vector<std::vector<TilePtr>> made(across.size());
  parallel_for(int(across.size()), [&](int begin, int end) {
    Work wk;
    for (int i = begin; i < end; i++) across_run(g, k, e, across[size_t(i)], h.fill(), made[size_t(i)], wk);
  });
  for (size_t i = 0; i < across.size(); i++)
    for (int x = across[i].a; x <= across[i].b; x++) h.set(x, across[i].line, std::move(made[i][size_t(x - across[i].a)]));
  // Phase 2.
  const Pixel hout = across_value(f, normal(f, e.outside));
  made.assign(down.size(), {});
  parallel_for(int(down.size()), [&](int begin, int end) {
    Work wk;
    for (int i = begin; i < end; i++) down_run(h, f, g.fill(), k, e, hout, down[size_t(i)], made[size_t(i)], wk);
  });
  for (size_t i = 0; i < down.size(); i++)
    for (int y = down[i].a; y <= down[i].b; y++) {
      const size_t at = size_t(y - r.y0) * w + size_t(down[i].line - r.x0);
      if (work[at]) done[at] = std::move(made[i][size_t(y - down[i].a)]);  // the others kept their shortcut
    }
}

}  // namespace

int gauss_margin(double sigma) { return kernel(sigma).margin; }

void gauss_pixels(Format f, const uint8_t* in, size_t in_stride, int w, int h, double sigma, uint8_t* out,
                  size_t out_stride) {
  check_format(f);
  const Kernel& k = kernel(sigma);
  if (w <= 0 || h <= 0) return;
  const int m = k.margin;
  const size_t row = (size_t(w) + 2 * size_t(m)) * size_t(f.bytes());
  if (m == 0) {
    for (int y = 0; y < h; y++) std::memcpy(out + size_t(y) * out_stride, in + size_t(y) * in_stride, row);
    return;
  }
  Work wk;
  const Rows rows = [&](int j0, int n, uint8_t* dst, size_t stride) {
    for (int j = 0; j < n; j++) std::memcpy(dst + size_t(j) * stride, in + size_t(j0 + j) * in_stride, row);
  };
  blur(f, k, rows, w, h, out, out_stride, wk);
}

TilePtr gauss_tile(const TileGrid& g, double sigma, int tx, int ty, const GaussEdges& e) {
  const Kernel& k = kernel(sigma);
  Work wk;
  return blur_tile(g, k, e, tx, ty, wk);
}

TileGrid gauss(const TileGrid& g, double sigma, const GaussEdges& e) {
  PROFILE_ZONE("gauss");
  const Kernel k = make_kernel(sigma);
  TileGrid out(g.format(), g.fill());
  TileRect r;  // the tiles out that may hold anything: over the canvas, or as far as the blur reaches
  if (has_canvas(e)) {
    r = {0, 0, ((e.width - 1) >> kTileLog2) + 1, ((e.height - 1) >> kTileLog2) + 1};
  } else if (!g.tiles().empty()) {
    const int grow = (k.margin + S - 1) / S;
    r = g.bounds();
    r = {r.x0 - grow, r.y0 - grow, r.x1 + grow, r.y1 + grow};
  }
  const int w = r.x1 - r.x0, n = w * (r.y1 - r.y0);
  std::vector<TilePtr> done(size_t(std::max(n, 0)));
  std::vector<char> work(done.size());
  parallel_for(n, [&](int begin, int end) {
    Work wk;
    for (int i = begin; i < end; i++) {
      const int tx = r.x0 + i % w, ty = r.y0 + i / w;
      if (k.boxes)
        work[size_t(i)] = !shortcut(g, k, e, tx, ty, done[size_t(i)]);
      else
        done[size_t(i)] = blur_tile(g, k, e, tx, ty, wk);
    }
  });
  if (std::find(work.begin(), work.end(), 1) != work.end()) blur_strips(g, k, e, r, work, done);
  for (int i = 0; i < n; i++)
    if (done[size_t(i)]) out.set(r.x0 + i % w, r.y0 + i / w, std::move(done[size_t(i)]));
  return out;
}

TileGrid feather_selection(const TileGrid& selection, double radius, int width, int height, bool at_canvas_bounds) {
  if (selection.format().channels != 1)
    throw std::invalid_argument("pb: feather_selection: a selection is a grid of one channel");
  if (!(radius >= 0 && radius <= kFeatherMax))
    throw std::invalid_argument("pb: feather_selection: Feather Radius outside 0 to 1,000 px");
  if (width < 1 || height < 1) throw std::invalid_argument("pb: feather_selection: a canvas without pixels");
  GaussEdges e;
  e.width = width, e.height = height, e.extend = !at_canvas_bounds;  // outside: 0, not selected
  return gauss(selection, sigma_of_radius(radius), e);
}

}  // namespace pb
