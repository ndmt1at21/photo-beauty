#include "dab.h"

#include <algorithm>
#include <cmath>
#include <mutex>

#include "lru.h"
#include "profile.h"

// Bit for bit the same on every compiler and CPU, as C4 compares replays across platforms: doubles, and only + - * /
// and sqrt, which IEEE 754 rounds exactly everywhere (no libm function, whose last bits differ between libraries); no
// fused multiply-add (GCC fuses only where FMA is enabled, which no preset does; clang would on ARM).
#ifdef __clang__
#pragma clang fp contract(off)
#endif

namespace pb {
namespace {

constexpr double kMaxCoord = 1 << 20;     // document pixels; PSB documents reach 300,000
constexpr double kMaxSize = 5000;         // px, as Photoshop
constexpr double kSame = 1e-3;            // px: an event this close to the last point is the same point
constexpr int kMaxPieces = 1 << 16;       // a span is walked as chords of about 1 px, at most this many
constexpr double kMaxSpanDabs = 1 << 20;  // past this many in a span (a 1 px brush at 1% over 10,000 px), sparser

constexpr int kFalloffPower = 4;  // the fall-off past the hard centre: (1 - t^2)^kFalloffPower (dab.h has why)
constexpr int kSub = 8;           // samples a side over a pixel near a hard edge or a fall-off narrower than kFine
constexpr double kFine = 2;       // px
constexpr double kHalfDiagonal = 0.7072;  // of a pixel, rounded up
constexpr size_t kCacheBytes = 64 << 20;

double clamp(double v, double lo, double hi) { return v > lo ? (v < hi ? v : hi) : lo; }  // NaN gives lo

// What a mask depends on: the size in quarter pixels, the hardness in percent, the centre in quarter pixels.
struct Key {
  int size4 = 0, hard = 0;
  long cx4 = 0, cy4 = 0;
  uint64_t id() const {
    return uint64_t(size4) << 16 | uint64_t(hard) << 4 | uint64_t(cx4 & 3) << 2 | uint64_t(cy4 & 3);
  }
};
Key key(const Dab& d, float hardness) {
  return {int(std::lround(clamp(d.size, 0, kMaxSize) * 4)), int(std::lround(clamp(hardness, 0, 1) * 100)),
          std::lround(clamp(d.x, -kMaxCoord, kMaxCoord) * 4), std::lround(clamp(d.y, -kMaxCoord, kMaxCoord) * 4)};
}

// The mask of k with its centre in pixel (0, 0), at a quarter-pixel offset in it. Distances to pixel and sample centres
// are exact (multiples of 1/16 px: their squares and sums are exact in a double) and samples add up as integers, so a
// mirrored dab gets the mirrored mask exactly.
DabMask shape(const Key& k) {
  PROFILE_ZONE("dab_mask");
  const double r = k.size4 / 8., fx = (k.cx4 & 3) / 4., fy = (k.cy4 & 3) / 4.;
  const double c0 = r * k.hard / 100, w = r - c0;  // the fall-off, from radius c0 to r
  const bool hard = w < 1. / kSub;                 // under a sample: as hard as 100%
  const double edge = hard ? r : c0;
  const auto coverage = [&](double d2) {  // of the fall-off, at squared distance d2
    if (d2 <= c0 * c0) return 1.;
    if (d2 >= r * r) return 0.;
    const double t = (std::sqrt(d2) - c0) / w;
    double u = 1 - t * t, v = 1;
    for (int i = 0; i < kFalloffPower; i++) v *= u;
    return v;
  };
  DabMask m;
  m.x = int(std::floor(fx - r)), m.y = int(std::floor(fy - r));
  m.w = int(std::ceil(fx + r)) - m.x, m.h = int(std::ceil(fy + r)) - m.y;
  auto px = std::make_shared<std::vector<uint16_t>>(size_t(m.w) * m.h);
  // Pixels within half a diagonal of the edge (or of a fall-off narrower than kFine) take kSub samples a side; others
  // their centre's value.
  const bool fine = hard || w < kFine;
  const double lo = edge > kHalfDiagonal ? (edge - kHalfDiagonal) * (edge - kHalfDiagonal) : -1;
  const double hi = (r + kHalfDiagonal) * (r + kHalfDiagonal);
  // A hard edge: each pixel's share of the disc, exact across each of kSub rows of it, averaged with the same down kSub
  // columns so that the mask is as symmetric as the disc: the half-chords of every sample row and column.
  std::vector<double> rows, cols;
  if (hard) {
    const auto half = [&](double t) { return t * t < r * r ? std::sqrt(r * r - t * t) : 0.; };
    for (int j = 0; j < m.h * kSub; j++) rows.push_back(half(m.y + (j + 0.5) / kSub - fy));
    for (int i = 0; i < m.w * kSub; i++) cols.push_back(half(m.x + (i + 0.5) / kSub - fx));
  }
  for (int j = 0; j < m.h; j++) {
    const double dy = m.y + j + 0.5 - fy;
    uint16_t* row = px->data() + size_t(j) * m.w;
    for (int i = 0; i < m.w; i++) {
      const double dx = m.x + i + 0.5 - fx, d2 = dx * dx + dy * dy;
      if (!fine || d2 <= lo || d2 >= hi) {
        row[i] = uint16_t((hard ? d2 <= lo : coverage(d2)) * 65535 + 0.5);
      } else if (hard) {
        const double x0 = dx - 0.5, y0 = dy - 0.5;  // the pixel's corner
        int64_t sum = 0;                            // 2 kSub overlaps of 2^24 at most: 2^28
        for (int s = 0; s < kSub; s++) {
          const double a = rows[size_t(j) * kSub + s], b = cols[size_t(i) * kSub + s];
          sum += int64_t(std::max(0., std::min(x0 + 1, a) - std::max(x0, -a)) * (1 << 24) + 0.5);
          sum += int64_t(std::max(0., std::min(y0 + 1, b) - std::max(y0, -b)) * (1 << 24) + 0.5);
        }
        row[i] = uint16_t((sum * 65535 + (int64_t(1) << 27)) >> 28);
      } else {
        int64_t sum = 0;  // kSub^2 samples of 2^24 at most: 2^30
        for (int b = 0; b < kSub; b++) {
          const double sy = dy + (b + 0.5) / kSub - 0.5;
          for (int a = 0; a < kSub; a++) {
            const double sx = dx + (a + 0.5) / kSub - 0.5;
            sum += int64_t(coverage(sx * sx + sy * sy) * (1 << 24) + 0.5);
          }
        }
        row[i] = uint16_t((sum * 65535 + (int64_t(1) << 29)) >> 30);
      }
    }
  }
  m.px = std::move(px);
  return m;
}

DabMask positioned(DabMask m, const Key& k) {
  m.x += int(k.cx4 >> 2), m.y += int(k.cy4 >> 2);  // floor: C++20 shifts negative values arithmetically
  return m;
}

}  // namespace

DabPlacer::DabPlacer(const BrushParams& p) : params_(p) {
  params_.size = float(clamp(p.size, 0, kMaxSize));
  params_.opacity = float(clamp(p.opacity, 0, 1));
  params_.flow = float(clamp(p.flow, 0, 1));
  params_.spacing = float(clamp(p.spacing, 0.01, 10));
}

DabPlacer::Point DabPlacer::mirror(const Point& b, const Point& c) { return {2 * b.x - c.x, 2 * b.y - c.y, 0}; }

std::vector<Dab> DabPlacer::add(const PenEvent& e) {
  std::vector<Dab> out;
  if (n_ < 0 || !std::isfinite(e.x) || !std::isfinite(e.y)) return out;
  const Point q{clamp(e.x, -kMaxCoord, kMaxCoord), clamp(e.y, -kMaxCoord, kMaxCoord), clamp(e.pressure, 0, 1)};
  if (n_ > 0) {
    const double dx = q.x - pts_[3].x, dy = q.y - pts_[3].y;
    if (dx * dx + dy * dy < kSame * kSame) {  // the pen pressed or eased in place: no curve to walk
      pts_[3].pressure = q.pressure;
      return out;
    }
  }
  std::copy(pts_ + 1, pts_ + 4, pts_);
  pts_[3] = q;
  if (++n_ == 1)
    carry_ = place(q.x, q.y, q.pressure, out);
  else if (n_ >= 3)  // the span before the newest point: its curve leaves toward it
    span(n_ == 3 ? mirror(pts_[1], pts_[2]) : pts_[0], pts_[1], pts_[2], pts_[3], out);
  return out;
}

std::vector<Dab> DabPlacer::finish() {
  std::vector<Dab> out;
  if (n_ >= 2) span(n_ == 2 ? mirror(pts_[2], pts_[3]) : pts_[1], pts_[2], pts_[3], mirror(pts_[3], pts_[2]), out);
  n_ = -1;
  return out;
}

// The curve from b to c, a and d the points around them (b, c mirrored at the stroke's ends: the curve leaves along the
// chord), as a cubic Hermite in u 0..1 with the tangents of the Catmull-Rom spline on centripetal knots.
void DabPlacer::span(const Point& a, const Point& b, const Point& c, const Point& d, std::vector<Dab>& out) {
  PROFILE_ZONE("place_dabs");
  const auto knot = [](const Point& p, const Point& q) {  // the square root of the chord's length
    return std::sqrt(std::sqrt((q.x - p.x) * (q.x - p.x) + (q.y - p.y) * (q.y - p.y)));
  };
  const double t0 = knot(a, b), t1 = knot(b, c), t2 = knot(c, d);
  const double m1x = ((b.x - a.x) / t0 - (c.x - a.x) / (t0 + t1) + (c.x - b.x) / t1) * t1;
  const double m1y = ((b.y - a.y) / t0 - (c.y - a.y) / (t0 + t1) + (c.y - b.y) / t1) * t1;
  const double m2x = ((c.x - b.x) / t1 - (d.x - b.x) / (t1 + t2) + (d.x - c.x) / t2) * t1;
  const double m2y = ((c.y - b.y) / t1 - (d.y - b.y) / (t1 + t2) + (d.y - c.y) / t2) * t1;
  // b + u (m1 + u (k2 + u k3)): the Hermite cubic's coefficients.
  const double k3x = 2 * (b.x - c.x) + m1x + m2x, k3y = 2 * (b.y - c.y) + m1y + m2y;
  const double k2x = 3 * (c.x - b.x) - 2 * m1x - m2x, k2y = 3 * (c.y - b.y) - 2 * m1y - m2y;
  // Chords of about 1 px: the Bezier control polygon is at least as long as the curve.
  const double ex = c.x - b.x - (m1x + m2x) / 3, ey = c.y - b.y - (m1y + m2y) / 3;
  const double polygon =
      (std::sqrt(m1x * m1x + m1y * m1y) + std::sqrt(m2x * m2x + m2y * m2y)) / 3 + std::sqrt(ex * ex + ey * ey);
  const int n = int(clamp(std::ceil(polygon), 1, kMaxPieces));
  const double least = polygon / kMaxSpanDabs;  // bounds a span's dabs, and memory, whatever its length
  double px = b.x, py = b.y;
  for (int k = 1; k <= n; k++) {
    const double u = double(k) / n;
    const double qx = k == n ? c.x : b.x + u * (m1x + u * (k2x + u * k3x));
    const double qy = k == n ? c.y : b.y + u * (m1y + u * (k2y + u * k3y));
    const double len = std::sqrt((qx - px) * (qx - px) + (qy - py) * (qy - py));
    while (carry_ <= len) {  // never for len 0: carry_ > 0
      const double f = carry_ / len, pressure = b.pressure + (c.pressure - b.pressure) * ((k - 1 + f) / n);
      carry_ += std::max(place(px + (qx - px) * f, py + (qy - py) * f, pressure, out), least);
    }
    carry_ -= len;
    px = qx, py = qy;
  }
}

// Appends the dab there; returns the arc length to the next one.
double DabPlacer::place(double x, double y, double pressure, std::vector<Dab>& out) const {
  const float p = float(pressure);
  const BrushParams& b = params_;
  out.push_back({float(x), float(y), b.pressure_size ? b.size * p : b.size,
                 b.pressure_opacity ? b.opacity * p : b.opacity, b.pressure_flow ? b.flow * p : b.flow});
  return double(b.spacing) * std::max(double(out.back().size), 1.);
}

DabMask make_dab_mask(const Dab& d, float hardness) {
  const Key k = key(d, hardness);
  return k.size4 > 0 ? positioned(shape(k), k) : DabMask{};
}

DabMask dab_mask(const Dab& d, float hardness) {
  // Never destroyed: at exit the pool's threads finish the jobs they hold (a dropped stroke's too) after statics made
  // later than the pool, as these are, have gone.
  static auto& mutex = *new std::mutex;
  static auto& cache = *new Lru<uint64_t, DabMask>(kCacheBytes);
  const Key k = key(d, hardness);
  if (k.size4 == 0) return {};
  DabMask m;
  {
    const std::lock_guard<std::mutex> lock(mutex);
    m = cache.get(k.id());
  }
  if (!m.px) {  // made outside the lock: two threads may both make it, alike
    m = shape(k);
    const std::lock_guard<std::mutex> lock(mutex);
    cache.put(k.id(), m, m.px->size() * sizeof(uint16_t) + sizeof m);
  }
  return positioned(std::move(m), k);
}

}  // namespace pb
