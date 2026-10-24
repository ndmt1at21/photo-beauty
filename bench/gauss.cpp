// pb-bench gauss [radius...]: Feather Selection (gauss.h) on a selection the size of D1 (8256 x 5504, 16 bits as D1's
// depth): an elliptical marquee about the face, anti-aliased (2 x 2 samples a pixel), with a rectangle from the canvas's
// top left corner added; solid tiles inside, none outside, data tiles along the edges, as S31 rasterises one. Feathers
// it once by each radius (50 px, the task's number, unless given), Apply effect at canvas bounds off then on, and
// prints the milliseconds and the data tiles the result keeps.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "bench.h"
#include "gauss.h"
#include "tile.h"

using namespace pb;

namespace {

constexpr int S = kTileSize, kW = 8256, kH = 5504;

// Inside the ellipse or the rectangle.
bool selected(double x, double y) {
  const double u = (x - kW * 0.5) / (kW * 0.3), v = (y - kH * 0.45) / (kH * 0.4);
  return u * u + v * v < 1 || (x < 1200 && y < 900);
}

TileGrid selection() {
  const Format f{Depth::U16, 1};
  TileGrid g(f);
  std::vector<uint16_t> px(size_t(S) * S);
  for (int ty = 0; ty < (kH + S - 1) / S; ty++)
    for (int tx = 0; tx < (kW + S - 1) / S; tx++) {
      // The ellipse's point nearest the tile in its own scale tells a tile wholly outside it.
      const double x0 = tx * S, y0 = ty * S, x1 = x0 + S, y1 = y0 + S;
      const double nx = std::clamp(kW * 0.5, x0, x1), ny = std::clamp(kH * 0.45, y0, y1);
      const double u = (nx - kW * 0.5) / (kW * 0.3), v = (ny - kH * 0.45) / (kH * 0.4);
      if (u * u + v * v >= 1 && !(x0 < 1200 && y0 < 900)) continue;
      int n = 0;
      for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) {
          int c = 0;
          for (int k = 0; k < 4; k++) c += selected(x0 + x + (k % 2 + 0.5) / 2, y0 + y + (k / 2 + 0.5) / 2);
          px[size_t(y) * S + size_t(x)] = uint16_t((c * 65535 + 2) / 4);
          n += c;
        }
      if (n == 4 * S * S)
        g.set(tx, ty, Tile::solid(f, Pixel{65535}));
      else if (n)
        g.write_rect(tx * S, ty * S, S, S, reinterpret_cast<const uint8_t*>(px.data()), size_t(S) * 2);
    }
  return g;
}

size_t data_tiles(const TileGrid& g) {
  size_t n = 0;
  for (const auto& [xy, t] : g.tiles()) n += !t->is_solid();
  return n;
}

}  // namespace

BENCH(gauss) {
  using pb::bench::report;
  std::vector<double> radii;
  for (const std::string& a : args) radii.push_back(std::strtod(a.c_str(), nullptr));
  if (radii.empty()) radii.push_back(50);
  const TileGrid sel = selection();
  report("gauss_selection_tiles", double(sel.tiles().size()), "tiles");
  report("gauss_selection_data_tiles", double(data_tiles(sel)), "tiles");
  for (const double r : radii)
    for (const bool bounds : {false, true}) {
      const auto t0 = pb::bench::Clock::now();
      const TileGrid out = feather_selection(sel, r, kW, kH, bounds);
      const double ms = pb::bench::ms_since(t0);
      char name[64];
      std::snprintf(name, sizeof name, "gauss_feather%g%s_d1", r, bounds ? "_bounds" : "");
      report(name, ms, "ms");
      std::snprintf(name, sizeof name, "gauss_feather%g%s_data_tiles", r, bounds ? "_bounds" : "");
      report(name, double(data_tiles(out)), "tiles");
    }
}
