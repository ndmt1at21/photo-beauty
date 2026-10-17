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

}  // namespace pb
