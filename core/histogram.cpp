// histogram() (histogram.h): each chunk of tiles sums integer weights by channel and level, the chunks added up
// exactly, so that the counts are the same however the pool splits the work.
#include "histogram.h"

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <stdexcept>

#include "jobs.h"
#include "mip.h"
#include "profile.h"

namespace pb {
namespace {

constexpr int S = kTileSize;

// Weights by channel and level (channel c's level v at c * 256 + v) and their sum, in units of one over alpha's
// maximum times the selection's.
struct Sums {
  std::array<uint64_t, 4 * 256> w{};
  uint64_t total = 0;
};

// A sample's 8-bit level: a 16-bit one's nearest.
inline unsigned level8(unsigned v, bool u16) { return u16 ? (v * 255 + 32767) / 65535 : v; }

// Sample c of pixel i: of the pixels d, or the solid value v where there are none.
template <class T>
inline unsigned sample(const T* d, const Pixel& v, size_t i, int channels, int c) {
  return d ? d[i * size_t(channels) + size_t(c)] : v[size_t(c)];
}

// Pixels x0..x1 - 1 by y0..y1 - 1 of tile t of the level (null: the grid's fill), each weighing its alpha times the
// selection's tile st there (null: the selection's fill; no selection: 1), samples of type T.
template <class T>
void count_tile(const TileGrid& grid, const Tile* t, const TileGrid* sel, const Tile* st, int x0, int x1, int y0,
                int y1, Sums& s) {
  const Format f = grid.format();
  const int ch = f.channels, g = ch == 4 ? 1 : 0, b = ch == 4 ? 2 : 0, a = f.alpha() ? ch - 1 : -1;
  const bool u16 = f.depth == Depth::U16, grey = ch < 4;
  const Pin p = t && !t->is_solid() ? t->pin() : Pin();
  const T* d = reinterpret_cast<const T*>(p.data());
  const Pixel v = t ? t->value() : grid.fill();
  const Pin sp = st && !st->is_solid() ? st->pin() : Pin();
  const bool s16 = sel && sel->format().depth == Depth::U16;
  const uint8_t* s8 = sp.data();
  const uint16_t* s16d = reinterpret_cast<const uint16_t*>(sp.data());
  const unsigned sv = !sel ? 1 : st ? st->value()[0] : sel->fill()[0];  // where it is one value
  const auto add = [&](size_t i, uint64_t times) {
    const unsigned ws = !s8 ? sv : s16 ? s16d[i] : s8[i];
    const uint64_t wt = uint64_t(a < 0 ? 1 : sample(d, v, i, ch, a)) * ws * times;
    if (!wt) return;
    const unsigned r = sample(d, v, i, ch, 0), gr = sample(d, v, i, ch, g), bl = sample(d, v, i, ch, b);
    const unsigned l = grey ? r : (kLumPercent[0] * r + kLumPercent[1] * gr + kLumPercent[2] * bl + 50) / 100;
    s.w[level8(r, u16)] += wt;
    s.w[256 + level8(gr, u16)] += wt;
    s.w[512 + level8(bl, u16)] += wt;
    s.w[768 + level8(l, u16)] += wt;
    s.total += wt;
  };
  if (!d && !s8) return add(0, uint64_t(x1 - x0) * uint64_t(y1 - y0));  // one value, one weight
  for (int y = y0; y < y1; y++)
    for (int x = x0; x < x1; x++) add(size_t(y) * S + size_t(x), 1);
}

}  // namespace

Histogram histogram(const TileGrid& grid, int level, int x, int y, int w, int h, const TileGrid* selection) {
  PROFILE_ZONE("histogram");
  if (level < 0 || level > 31) throw std::invalid_argument("pb: histogram: no such mip level");
  if (selection && selection->format().channels != 1)
    throw std::invalid_argument("pb: histogram: a selection is 1 channel");
  Histogram out;
  if (w <= 0 || h <= 0) return out;
  // The region at the level, a pixel partly in it counting, and its tiles.
  const int64_t step = int64_t(1) << level;
  const int64_t px0 = int64_t(x) >> level, py0 = int64_t(y) >> level;
  const int64_t px1 = (int64_t(x) + w + step - 1) >> level, py1 = (int64_t(y) + h + step - 1) >> level;
  const int64_t tx0 = px0 >> kTileLog2, ty0 = py0 >> kTileLog2;
  const int cols = int(((px1 - 1) >> kTileLog2) + 1 - tx0), rows = int(((py1 - 1) >> kTileLog2) + 1 - ty0);
  const Format f = grid.format();
  const bool transparent = f.alpha() && grid.fill()[size_t(f.channels - 1)] == 0;
  Sums all;
  std::mutex m;
  parallel_for(cols * rows, [&](int begin, int end) {
    PROFILE_ZONE("histogram tiles");
    Sums s;
    for (int i = begin; i < end; i++) {
      const int tx = int(tx0) + i % cols, ty = int(ty0) + i / cols;
      const int64_t left = int64_t(tx) * S, top = int64_t(ty) * S;
      const int x0 = int(std::max<int64_t>(px0 - left, 0)), x1 = int(std::min<int64_t>(px1 - left, S));
      const int y0 = int(std::max<int64_t>(py0 - top, 0)), y1 = int(std::min<int64_t>(py1 - top, S));
      const TilePtr st = selection ? mip(*selection, level, tx, ty) : nullptr;
      if (selection && !st && selection->fill()[0] == 0) continue;  // nothing selected here
      const TilePtr t = mip(grid, level, tx, ty);
      if (!t && transparent) continue;
      if (f.depth == Depth::U16)
        count_tile<uint16_t>(grid, t.get(), selection, st.get(), x0, x1, y0, y1, s);
      else
        count_tile<uint8_t>(grid, t.get(), selection, st.get(), x0, x1, y0, y1, s);
    }
    std::lock_guard l(m);
    for (size_t k = 0; k < s.w.size(); k++) all.w[k] += s.w[k];
    all.total += s.total;
  });
  const double unit = double(f.alpha() ? max_value(f.depth) : 1) *
                      double(selection ? max_value(selection->format().depth) : 1);
  for (size_t c = 0; c < 4; c++)
    for (size_t v = 0; v < 256; v++) out.counts[c][v] = double(all.w[c * 256 + v]) / unit;
  out.total = double(all.total) / unit;
  return out;
}

}  // namespace pb
