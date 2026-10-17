// View of render.h: the same formulas as ui/qml/View.js (docs/ui-tasks.md, Một hàm đổi tọa độ), so the canvas,
// the mock canvas and every overlay put a document point at one place. s = zoom / dpr logical pixels per document
// pixel; R(a) turns by a clockwise, y pointing down: to_canvas = centre + s R(a) (p - (x, y)),
// to_doc = (x, y) + R(-a) (q - centre) / s.
#include <algorithm>
#include <cmath>

#include "render.h"

namespace pb {
namespace {
constexpr double kDegree = 3.14159265358979323846 / 180;  // radians; GCC 9 has no <numbers>

// The document pixel under the centre of device pixel (i, j), as pattern.slang's under(): float, in its order.
std::array<int32_t, 2> under(const PatternConsts& k, int i, int j) {
  const float fi = float(i), fj = float(j);
  const float u = k.origin[0] + fi * k.across[0] + fj * k.down[0];
  const float v = k.origin[1] + fi * k.across[1] + fj * k.down[1];
  const auto pixel = [](float f) { return int32_t(std::clamp(std::floor(f), -1073741824.f, 1073741824.f)); };
  return {pixel(u), pixel(v)};
}
}  // namespace

std::array<double, 2> View::to_canvas(double doc_x, double doc_y) const {
  const double s = zoom / dpr, a = angle * kDegree, c = std::cos(a), n = std::sin(a);
  const double dx = (doc_x - x) * s, dy = (doc_y - y) * s;
  return {width / 2 + dx * c - dy * n, height / 2 + dx * n + dy * c};
}

std::array<double, 2> View::to_doc(double canvas_x, double canvas_y) const {
  const double s = zoom / dpr, a = angle * kDegree, c = std::cos(a), n = std::sin(a);
  const double dx = canvas_x - width / 2, dy = canvas_y - height / 2;
  return {x + (dx * c + dy * n) / s, y + (dy * c - dx * n) / s};
}

int View::level() const { return std::max(0, int(std::floor(-std::log2(zoom)))); }

View::Pixels View::pixels() const {
  Pixels p;
  p.width = int(std::lround(width * dpr)), p.height = int(std::lround(height * dpr));
  if (p.width <= 0 || p.height <= 0) return {};
  const double sx = width / p.width, sy = height / p.height;  // logical pixels a device pixel: about 1 / dpr
  const double s = zoom / dpr, a = angle * kDegree, c = std::cos(a), n = std::sin(a);
  p.origin = to_doc(sx / 2, sy / 2);
  p.across = {c * sx / s, -n * sx / s};  // to_doc()'s derivatives
  p.down = {n * sy / s, c * sy / s};
  return p;
}

PatternConsts pattern_consts(const View& view) {
  const View::Pixels p = view.pixels();
  PatternConsts k{};
  for (int i = 0; i < 2; i++)
    k.origin[i] = float(p.origin[i]), k.across[i] = float(p.across[i]), k.down[i] = float(p.down[i]);
  while (k.grid < 20 && std::ldexp(view.zoom, k.grid) < 4) k.grid++;
  k.width = p.width, k.height = p.height;
  return k;
}

uint32_t pattern_pixel(const PatternConsts& k, int i, int j) {
  const auto p = under(k, i, j), l = under(k, i - 1, j), a = under(k, i, j - 1);
  const int32_t cx = p[0] >> k.grid, cy = p[1] >> k.grid;
  const bool line = cx != l[0] >> k.grid || cy != l[1] >> k.grid || cx != a[0] >> k.grid || cy != a[1] >> k.grid;
  uint32_t r = uint32_t(p[0] >> 4) & 255, g = uint32_t(p[1] >> 4) & 255, b = ((cx >> 3) + (cy >> 3)) & 1 ? 224 : 96;
  if (line) r = 255 - r, g = 255 - g, b = 255 - b;
  return r | g << 8 | b << 16 | 0xff000000u;
}

}  // namespace pb
