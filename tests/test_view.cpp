// View of render.h (core/view.cpp): the cases of test_view_* in ui/tests/tst_u6.qml, so View and ui/qml/View.js
// put a document point at the same place; the canvas's test card, on the CPU and the GPU.
#include <array>
#include <cmath>
#include <optional>
#include <vector>

#include "check.h"
#include "pattern.spv.h"
#include "render.h"

namespace {
using pb::View;
using P = std::array<double, 2>;
constexpr double kDegree = 3.14159265358979323846 / 180;

bool near(P a, P b, double eps) { return std::abs(a[0] - b[0]) <= eps && std::abs(a[1] - b[1]) <= eps; }
P canvas(const View& v, P p) { return v.to_canvas(p[0], p[1]); }
P doc(const View& v, P p) { return v.to_doc(p[0], p[1]); }
}  // namespace

TEST(test_view) {
  // Known values: chân dung.psd (4.400 x 5.600) at 12,5 % in a 1.034 x 750 frame, as the prototype lays it out.
  const View v{2200, 2800, 0.125, 0, 1034, 750, 1};
  CHECK(near(canvas(v, {0, 0}), {242, 25}, 1e-12));
  CHECK(near(canvas(v, {4400, 5600}), {792, 725}, 1e-12));
  CHECK(near(doc(v, {517, 375}), {2200, 2800}, 1e-12));
  CHECK(near(canvas({2200, 2800, 0.125, 90, 1034, 750, 1}, {0, 0}), {867, 100}, 1e-9));  // clockwise: top left → right
  CHECK(near(canvas({2200, 2800, 0.125, -90, 1034, 750, 1}, {0, 0}), {167, 650}, 1e-9));
  CHECK(near(canvas({2200, 2800, 1, 0, 1034, 750, 2}, {2300, 2900}), {567, 425}, 1e-12));  // 100 % at dpr 2

  // Document → canvas → document, and canvas → document → canvas, for any view.
  const P sizes[] = {{1034, 750}, {640, 480}, {1052, 768}};
  const double dprs[] = {1, 2, 1.25};
  const double views[][4] = {{2200, 2800, 0.125, 0}, {1000.5, -20, 2, 30}, {0, 0, 128, -45}, {3.25, 7, 0.01, 180},
                             {900, 600, 0.5522, 90}};
  const P points[] = {{0, 0}, {4400, 5600}, {-123.5, 77.25}, {1e5, -3e4}};
  for (int i = 0; i < 3; i++)
    for (const auto& w : views)
      for (const P& p : points) {
        const View u{w[0], w[1], w[2], w[3], sizes[i][0], sizes[i][1], dprs[i]};
        CHECK(near(doc(u, canvas(u, p)), p, 1e-6 * (1 + std::abs(p[0]) + std::abs(p[1]))));
        const P q{p[0] / 100, p[1] / 100};
        CHECK(near(canvas(u, doc(u, q)), q, 1e-6));
      }

  // Zoomed about canvas point p as View.js's zoomAt: the document point under p stays there.
  const double about[][4] = {{2200, 2800, 0.125, 0}, {10, 20, 3, 30}, {-50, 900, 0.5, -135}};
  for (const auto& w : about)
    for (const P& p : {P{0, 0}, P{300, 200}, P{1034, 750}, P{517, 375}})
      for (double zoom : {0.01, 0.0833, 1., 6.25, 128.}) {
        const View before{w[0], w[1], w[2], w[3], 1034, 750, 1.5};
        View after = before;
        after.zoom = zoom;
        const P d = doc(before, p), q = doc(after, p);
        after.x += d[0] - q[0];
        after.y += d[1] - q[1];
        CHECK(near(doc(after, p), d, 1e-6));
        CHECK(near(canvas(after, d), p, 1e-6));
      }

  // Turned about the frame's centre: the point there stays, the rest turns about it, clockwise.
  const P c{517, 375};
  const View flat{2200, 2800, 0.25, 0, 1034, 750, 1};
  for (double a : {30., 90., -45., 180., 270., 359.}) {
    View turned = flat;
    turned.angle = a;
    CHECK(near(canvas(turned, {2200, 2800}), c, 1e-9));
    const double r = a * kDegree;
    for (const P& p : {P{0, 0}, P{4400, 0}, P{1234, 4321}}) {
      const P o = canvas(flat, p), t = canvas(turned, p), d{o[0] - c[0], o[1] - c[1]};
      CHECK(near(t, {c[0] + d[0] * std::cos(r) - d[1] * std::sin(r), c[1] + d[0] * std::sin(r) + d[1] * std::cos(r)}, 1e-6));
    }
  }

  // Frames in device pixels: the canvas's size times dpr, rounded; each pixel shows to_doc() of its centre.
  for (int i = 0; i < 3; i++)
    for (const auto& w : views) {
      const View u{w[0], w[1], w[2], w[3], sizes[i][0] + 0.3, sizes[i][1], dprs[i]};
      const View::Pixels px = u.pixels();
      CHECK(px.width == int(std::lround((sizes[i][0] + 0.3) * dprs[i])) && px.height == int(sizes[i][1] * dprs[i]));
      const double sx = u.width / px.width, sy = u.height / px.height;
      for (const P& at : {P{0, 0}, P{px.width - 1., 0}, P{17, px.height - 1.}, P{px.width - 1., px.height - 1.}}) {
        const P want = doc(u, {(at[0] + 0.5) * sx, (at[1] + 0.5) * sy});
        const P got{px.origin[0] + at[0] * px.across[0] + at[1] * px.down[0],
                    px.origin[1] + at[0] * px.across[1] + at[1] * px.down[1]};
        CHECK(near(got, want, 1e-9 * (1 + std::abs(want[0]) + std::abs(want[1]))));
      }
    }
  const View empty{0, 0, 1, 0, 0, 100, 1}, tiny{0, 0, 1, 0, 0.2, 100, 2};
  CHECK(empty.pixels().width == 0 && tiny.pixels().height == 0);

  // The mip level: floor(log2(1 / zoom)), not below 0; the device pixels of zoom, whatever the dpr.
  const struct {
    double zoom;
    int level;
  } levels[] = {{128, 0}, {1, 0}, {0.5001, 0}, {0.5, 1}, {0.4999, 1}, {0.3, 1}, {0.25, 2}, {0.125, 3},
                {0.0833, 3}, {0.0157, 5}, {1. / 64, 6}, {0.0156, 6}, {0.01, 6}};
  for (const auto& l : levels) {
    const View flat1{0, 0, l.zoom, 0, 100, 100, 1}, turned2{0, 0, l.zoom, 30, 100, 100, 2};
    CHECK(flat1.level() == l.level && turned2.level() == l.level);
  }
}

namespace {
// What the test card shows at document point d of a frame with grid lines 2^grid document pixels apart, as render.h
// says, in double precision; d's neighbours left (l) and above (a) say where the lines are. Null where one of them lies
// within eps of a pixel's edge, where float may round the other way.
std::optional<uint32_t> card(P d, P l, P a, int grid, double eps) {
  for (const P& q : {d, l, a})
    for (double c : q)
      if (std::abs(c - std::round(c)) < eps) return std::nullopt;
  const auto cell = [&](P q) {
    return std::array<int64_t, 2>{int64_t(std::floor(q[0])) >> grid, int64_t(std::floor(q[1])) >> grid};
  };
  const auto c = cell(d);
  uint32_t r = uint32_t(int64_t(std::floor(d[0])) >> 4) & 255, g = uint32_t(int64_t(std::floor(d[1])) >> 4) & 255,
           b = ((c[0] >> 3) + (c[1] >> 3)) & 1 ? 224 : 96;
  if (c != cell(l) || c != cell(a)) r = 255 - r, g = 255 - g, b = 255 - b;
  return r | g << 8 | b << 16 | 0xff000000u;
}
}  // namespace

TEST(test_pattern) {
  const View views[] = {{2200.3, 2800.6, 0.125, 0, 1034, 750, 1}, {1000.5, -20, 2, 30, 333, 217, 1.25},
                        {0.3, 0.7, 128, -45, 64, 48, 2},        {3.25, 7.4, 0.01, 180, 300, 200, 1.5},
                        {900.1, 600.2, 0.5522, 90, 257, 129, 1}, {-40.4, 77.7, 8, 0, 200, 120, 1},
                        {1e4 + 0.1, -3e3, 6.25, -135, 101, 99, 1.75}};
  auto dev = pb::test::gpu_device();
  const pb::gpu::KernelPtr kernel =
      dev ? dev->kernel("pattern", pattern_spv, pattern_spv_sizeInBytes, 1, sizeof(pb::PatternConsts)) : nullptr;
  int checked = 0;
  for (const View& v : views) {
    const pb::PatternConsts k = pb::pattern_consts(v);
    const View::Pixels px = v.pixels();
    CHECK(k.width == px.width && k.height == px.height);
    CHECK(std::ldexp(v.zoom, k.grid) >= 4 && (k.grid == 0 || std::ldexp(v.zoom, k.grid - 1) < 4));
    // What it shows: the document pixel under each device pixel's centre, by View::to_doc() in double precision.
    const double sx = v.width / px.width, sy = v.height / px.height;
    const auto at = [&](int i, int j) { return doc(v, {(i + 0.5) * sx, (j + 0.5) * sy}); };
    const double eps = 1e-6 * (1 + std::abs(px.origin[0]) + std::abs(px.origin[1]) +
                               (px.width + px.height) * (std::abs(px.across[0]) + std::abs(px.across[1]) +
                                                         std::abs(px.down[0]) + std::abs(px.down[1])));
    for (int j = 0; j < px.height; j += 3)
      for (int i = 0; i < px.width; i += 3)
        if (const auto want = card(at(i, j), at(i - 1, j), at(i, j - 1), k.grid, eps)) {
          CHECK(pb::pattern_pixel(k, i, j) == *want);
          checked++;
        }
    if (!dev) continue;
    // shaders/pattern.slang, to the bit.
    const size_t n = size_t(px.width) * px.height;
    const auto out = dev->buffer(n * 4);
    dev->dispatch(*kernel, {out.get()}, &k, sizeof k, uint32_t(px.width + 7) / 8, uint32_t(px.height + 7) / 8);
    std::vector<uint32_t> got(n);
    dev->download(*out, got.data(), n * 4);
    dev->flush();
    for (int j = 0; j < px.height; j++)
      for (int i = 0; i < px.width; i++) CHECK(got[size_t(j) * px.width + i] == pb::pattern_pixel(k, i, j));
  }
  CHECK(checked > 100000);  // of about 125k
}
