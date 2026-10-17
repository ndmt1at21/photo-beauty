// composite_tile() (render.h) on the CPU: the reference the GPU composite (B2) matches. The layers with pixels at the
// tile become a list of steps, run row by row over float accumulators of premultiplied R, G, B and alpha with
// blend.h's formulas.
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "blend.h"
#include "profile.h"  // snapone's PROFILE_ZONE
#include "render.h"

namespace pb {
namespace {

constexpr int S = kTileSize;

// One step of a tile's composite. Push and Pop bracket a group's layers when they do not go straight onto the
// backdrop: an isolated group's (onto transparency, then blended as one layer) and a Pass Through's below full
// opacity (onto a copy of the backdrop, then mixed with it).
struct Step {
  enum Type : uint8_t { Layer, Push, Pop } type = Layer;
  bool pass = false;             // Push, Pop: a Pass Through group's
  Blend blend = Blend::Normal;   // Layer, Pop
  float alpha = 1;               // Layer, Pop: opacity x fill
  const Tile* tile = nullptr;    // Layer: its pixels here, null where it has none
  const Tile* stroke = nullptr;  // Layer: the stroke buffer's tile here, when the stroke is on this layer
  Pin pixels, coverage;          // theirs, for as long as the composite runs
};

// The steps of tile (tx, ty), bottom up, leaving out hidden layers, layers without pixels there and groups of none.
struct Plan {
  const RenderInput& in;
  int tx, ty;
  std::vector<Step> steps;
  int depth = 0, max_depth = 0;  // of nested Push

  void add(const Layer& group) {
    for (const LayerPtr& l : group.children) {
      const LayerProps& p = l->props;
      // ponytail: fill scales alpha as opacity does; Photoshop weighs fill otherwise in Color Burn, Color Dodge, Linear
      // Light, Difference and four P1 modes: match it once B4 compares fill.
      const float alpha = std::min(p.opacity * p.fill, 1.f);
      const bool pass = p.blend == Blend::PassThrough;
      if (!p.visible || !(alpha > 0)) continue;
      if (l->kind == Kind::Pixel) {
        add_pixels(*l, alpha);
      } else if (pass && alpha == 1) {
        add(*l);  // its layers straight onto the backdrop
      } else {
        const size_t at = steps.size();
        steps.push_back({Step::Push, pass});
        max_depth = std::max(max_depth, ++depth);
        add(*l);
        depth--;
        if (steps.size() == at + 1)
          steps.pop_back();
        else
          steps.push_back({Step::Pop, pass, p.blend, alpha});
      }
    }
  }

  void add_pixels(const Layer& l, float alpha) {
    const Tile* t = l.pixels ? l.pixels->get(tx, ty) : nullptr;
    const bool stroked = in.stroke && in.stroke->layer == l.id && in.stroke->buffer;
    const Tile* st = stroked ? in.stroke->buffer->get(tx, ty) : nullptr;
    if (!t && (!st || in.stroke->erase)) return;  // erasing where there is nothing leaves nothing
    Step s{Step::Layer, false, l.props.blend, alpha, t, st};
    if (t) s.pixels = t->pin();
    if (st) s.coverage = st->pin();
    steps.push_back(std::move(s));
  }
};

// v / max_value() as to_unit() rounds it, without its division: the double product rounds to the same float for every
// 8- and 16-bit v, so the same values in either depth composite alike.
inline float unit(unsigned v, double k) { return float(double(v) * k); }

// n pixels of a layer as R, G, B (grey thrice) and alpha, not premultiplied.
template <class T>
void unpack(const T* in, int channels, double k, int n, float* out) {
  const int g = channels == 4 ? 1 : 0, b = channels == 4 ? 2 : 0, a = channels - 1;
  for (int i = 0; i < n; i++, in += channels, out += 4) {
    out[0] = unit(in[0], k), out[1] = unit(in[g], k), out[2] = unit(in[b], k), out[3] = unit(in[a], k);
  }
}

// Pixels x0 to x0 + n of row y of a Layer step's tile; transparent where it has none.
void load(const Step& s, int y, int x0, int n, float* out) {
  if (!s.tile) return void(std::fill(out, out + 4 * n, 0.f));
  const Format f = s.tile->format();
  const double k = 1.0 / max_value(f.depth);
  const uint8_t* d = s.pixels.data();
  if (!d) {  // solid
    const Pixel v = s.tile->value();
    unpack(v.data(), f.channels, k, 1, out);
    for (int i = 1; i < n; i++) std::copy(out, out + 4, out + 4 * i);
    return;
  }
  const size_t at = (size_t(y) * S + size_t(x0)) * size_t(f.channels);
  if (f.depth == Depth::U16)
    unpack(reinterpret_cast<const uint16_t*>(d) + at, f.channels, k, n, out);
  else
    unpack(d + at, f.channels, k, n, out);
}

// The stroke over the same pixels of its layer, `color` in R, G, B.
void apply_stroke(const StrokeOverlay& o, const float color[3], const Step& s, int y, int x0, int n, float* px) {
  constexpr double scale = 1.0 / 65535;
  const uint16_t* cov = reinterpret_cast<const uint16_t*>(s.coverage.data());
  if (cov) cov += size_t(y) * S + size_t(x0);
  const float solid = unit(s.stroke->value()[0], scale);
  for (int i = 0; i < n; i++, px += 4) {
    const float k = o.opacity * (cov ? unit(cov[i], scale) : solid);
    if (o.erase) {
      px[3] = blend::erase_alpha(px[3], k);
      continue;
    }
    const float a = blend::paint_alpha(px[3], k);
    for (int c = 0; c < 3; c++) px[c] = blend::paint(px[c], color[c], k, a);
    px[3] = a;
  }
}

// n source pixels (not premultiplied, alpha times `alpha`) over the accumulator's in mode M.
template <Blend M>
void over_row(float* acc, const float* src, int n, float alpha) {
  for (int i = 0; i < n; i++, acc += 4, src += 4) {
    const float ab = acc[3], as = src[3] * alpha;
    for (int c = 0; c < 3; c++) acc[c] = blend::composite(M, acc[c], ab, src[c], as);
    acc[3] = blend::over_alpha(ab, as);
  }
}
void over_row(Blend m, float* acc, const float* src, int n, float alpha) {
  switch (m) {
    case Blend::Darken: return over_row<Blend::Darken>(acc, src, n, alpha);
    case Blend::Multiply: return over_row<Blend::Multiply>(acc, src, n, alpha);
    case Blend::ColorBurn: return over_row<Blend::ColorBurn>(acc, src, n, alpha);
    case Blend::Lighten: return over_row<Blend::Lighten>(acc, src, n, alpha);
    case Blend::Screen: return over_row<Blend::Screen>(acc, src, n, alpha);
    case Blend::ColorDodge: return over_row<Blend::ColorDodge>(acc, src, n, alpha);
    case Blend::Overlay: return over_row<Blend::Overlay>(acc, src, n, alpha);
    case Blend::SoftLight: return over_row<Blend::SoftLight>(acc, src, n, alpha);
    case Blend::LinearLight: return over_row<Blend::LinearLight>(acc, src, n, alpha);
    case Blend::Difference: return over_row<Blend::Difference>(acc, src, n, alpha);
    default: return over_row<Blend::Normal>(acc, src, n, alpha);  // and the modes P1 adds, as blend::separable()
  }
}

}  // namespace

TilePtr composite_tile(const RenderInput& in, int level, int tx, int ty) {
  PROFILE_ZONE("composite_tile");
  if (!in.doc) throw std::invalid_argument("pb: composite_tile: no document");
  // ponytail: full size only; the mip levels read K2's tiles once it lands.
  if (level != 0) throw std::invalid_argument("pb: composite_tile: no mip levels before K2");
  if (in.stroke && in.stroke->buffer && in.stroke->buffer->format() != Format{Depth::U16, 1})
    throw std::invalid_argument("pb: composite_tile: a stroke buffer is 1 channel U16");
  const DocState& d = *in.doc;
  // The canvas's part of the tile, columns x0 to x1 and rows y0 to y1; the rest stays transparent.
  const int64_t left = int64_t(tx) * S, top = int64_t(ty) * S;
  const int x0 = int(std::clamp<int64_t>(-left, 0, S)), x1 = int(std::clamp<int64_t>(d.width - left, 0, S));
  const int y0 = int(std::clamp<int64_t>(-top, 0, S)), y1 = int(std::clamp<int64_t>(d.height - top, 0, S));
  if (x0 >= x1 || y0 >= y1 || !d.root) return nullptr;
  Plan plan{in, tx, ty, {}};
  plan.add(*d.root);
  if (plan.steps.empty()) return nullptr;

  const int n = x1 - x0;
  float color[3] = {};  // the stroke's in R, G, B
  if (in.stroke) {
    const auto& c = in.stroke->color;
    const bool grey = d.mode == Mode::Gray;
    color[0] = c[0], color[1] = grey ? c[0] : c[1], color[2] = grey ? c[0] : c[2];
  }
  // A row of accumulator per Push deep, then a row of source pixels.
  std::vector<float> rows(size_t(plan.max_depth + 2) * S * 4);
  float* const src = rows.data() + size_t(plan.max_depth + 1) * S * 4;
  const std::shared_ptr<Tile> out = Tile::make({Depth::U16, 4});
  {
    const WritePin w = out->write();
    for (int y = y0; y < y1; y++) {
      float* acc = rows.data();
      std::fill(acc, acc + 4 * n, 0.f);
      for (const Step& s : plan.steps) {
        if (s.type == Step::Layer) {
          load(s, y, x0, n, src);
          if (s.stroke) apply_stroke(*in.stroke, color, s, y, x0, n, src);
          over_row(s.blend, acc, src, n, s.alpha);
        } else if (s.type == Step::Push) {
          float* next = acc + 4 * S;
          if (s.pass)
            std::copy(acc, acc + 4 * n, next);
          else
            std::fill(next, next + 4 * n, 0.f);
          acc = next;
        } else {
          float* below = acc - 4 * S;
          if (s.pass) {
            for (int i = 0; i < 4 * n; i++) below[i] = blend::mix(below[i], acc[i], s.alpha);
          } else {  // the group as a layer
            for (int i = 0; i < 4 * n; i += 4) {
              for (int c = 0; c < 3; c++) src[i + c] = blend::unpremultiply(acc[i + c], acc[i + 3]);
              src[i + 3] = acc[i + 3];
            }
            over_row(s.blend, below, src, n, s.alpha);
          }
          acc = below;
        }
      }
      uint16_t* o = reinterpret_cast<uint16_t*>(w.data()) + (size_t(y) * S + size_t(x0)) * 4;
      for (int i = 0; i < 4 * n; i += 4) {
        const uint16_t a = from_unit(acc[i + 3], Depth::U16);
        for (int c = 0; c < 3; c++) o[i + c] = a ? from_unit(blend::unpremultiply(acc[i + c], acc[i + 3]), Depth::U16) : 0;
        o[i + 3] = a;
      }
    }
  }
  return compact(out, {});
}

}  // namespace pb
