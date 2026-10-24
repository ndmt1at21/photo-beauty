// composite_tile() (render.h) on the CPU: the reference the GPU composite (B2) matches. The tile's plan
// (composite_plan(), composite.h) runs row by row over float accumulators of premultiplied R, G, B and alpha with
// blend.h's formulas.
#include "composite.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "blend.h"
#include "mip.h"
#include "profile.h"

namespace pb {
namespace {

constexpr int S = kTileSize;

// The steps of a tile, bottom up, from the layer tree.
struct Planner {
  const RenderInput& in;
  int level, tx, ty;
  CompositePlan& plan;
  int depth = 0;

  void add(const Layer& group) {
    std::vector<CompositeStep>& steps = plan.steps;
    for (const LayerPtr& l : group.children) {
      const LayerProps& p = l->props;
      // ponytail: fill scales alpha as opacity does; Photoshop weighs fill otherwise in Color Burn, Color Dodge, Linear
      // Light, Difference and four P1 modes: match it once B4 compares fill.
      const float alpha = std::min(p.opacity * p.fill, 1.f);
      const bool pass = p.blend == Blend::PassThrough;
      if (!p.visible || !(alpha > 0)) continue;
      if (has_pixels(l->kind)) {  // text and shape layers too
        add_pixels(*l, alpha);
      } else if (l->kind != Kind::Group) {
        continue;  // adjustment and fill layers: D40; masks, clipping, Blend If, Channels: B34 (composite.h)
      } else if (pass && alpha == 1) {
        add(*l);  // its layers straight onto the backdrop
      } else {
        const size_t at = steps.size();
        steps.push_back({CompositeStep::Push, l->id, pass});
        plan.depth = std::max(plan.depth, ++depth);
        add(*l);
        depth--;
        if (steps.size() == at + 1)
          steps.pop_back();
        else
          steps.push_back({CompositeStep::Pop, l->id, pass, p.blend, alpha});
      }
    }
  }

  // The layer's tile here: the preview's where it has one, else its pixels' at the level.
  TilePtr tile_of(const Layer& l) const {
    const std::optional<LayerPreview>& v = in.preview;
    if (v && v->layer == l.id && v->level == level && v->tiles) {
      const auto& tiles = v->tiles->tiles();
      if (const auto it = tiles.find({tx, ty}); it != tiles.end()) {
        const TilePtr& t = it->second;
        return t->is_solid() && !t->value()[size_t(t->format().channels - 1)] ? nullptr : t;  // cleared
      }
    }
    return l.pixels ? mip(*l.pixels, level, tx, ty) : nullptr;
  }

  void add_pixels(const Layer& l, float alpha) {
    TilePtr t = tile_of(l);
    const bool stroked = in.stroke && in.stroke->layer == l.id && in.stroke->buffer;
    TilePtr cover = stroked ? mip(*in.stroke->buffer, level, tx, ty) : nullptr;
    if (!t && (!cover || in.stroke->erase)) return;  // erasing where there is nothing leaves nothing
    plan.steps.push_back({CompositeStep::Layer, l.id, false, l.props.blend, alpha, std::move(t), std::move(cover)});
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

// Pixels x0 to x0 + n of row y of a layer's tile t (pinned as p); transparent where it is null.
void load(const Tile* t, const Pin& p, int y, int x0, int n, float* out) {
  if (!t) return void(std::fill(out, out + 4 * n, 0.f));
  const Format f = t->format();
  const double k = 1.0 / max_value(f.depth);
  const uint8_t* d = p.data();
  if (!d) {  // solid
    const Pixel v = t->value();
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

// The stroke over the same pixels of its layer, its coverage tile `cover` pinned as p, `color` in R, G, B.
void apply_stroke(const StrokeOverlay& o, const float color[3], const Tile& cover, const Pin& p, int y, int x0, int n,
                  float* px) {
  constexpr double scale = 1.0 / 65535;
  const uint16_t* cov = reinterpret_cast<const uint16_t*>(p.data());
  if (cov) cov += size_t(y) * S + size_t(x0);
  const float solid = unit(cover.value()[0], scale);
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

CompositePlan composite_plan(const RenderInput& in, int level, int tx, int ty) {
  PROFILE_ZONE("composite_plan");
  if (!in.doc) throw std::invalid_argument("pb: composite_plan: no document");
  if (level < 0 || level > 31) throw std::invalid_argument("pb: composite_plan: no such mip level");
  if (in.stroke && in.stroke->buffer && in.stroke->buffer->format() != Format{Depth::U16, 1})
    throw std::invalid_argument("pb: composite_plan: a stroke buffer is 1 channel U16");
  const DocState& d = *in.doc;
  if (in.preview && in.preview->tiles && in.preview->tiles->format() != pixel_format(d))
    throw std::invalid_argument("pb: composite_plan: a preview's tiles are in the document's pixel format");
  // The canvas at the level. A pixel partly on it averages in what lies past the edge (mip.h): transparent for a layer
  // within the canvas. ponytail: a layer spilling past the canvas tints that last row and column at levels > 0; mask
  // such layers at the edge before averaging if it shows.
  const int64_t w = (int64_t(d.width) + (int64_t(1) << level) - 1) >> level;
  const int64_t h = (int64_t(d.height) + (int64_t(1) << level) - 1) >> level;
  const int64_t left = int64_t(tx) * S, top = int64_t(ty) * S;
  CompositePlan plan;
  plan.x0 = int(std::clamp<int64_t>(-left, 0, S)), plan.x1 = int(std::clamp<int64_t>(w - left, 0, S));
  plan.y0 = int(std::clamp<int64_t>(-top, 0, S)), plan.y1 = int(std::clamp<int64_t>(h - top, 0, S));
  if (plan.x0 >= plan.x1 || plan.y0 >= plan.y1 || !d.root) return plan;
  Planner{in, level, tx, ty, plan}.add(*d.root);
  return plan;
}

TilePtr composite_tile(const RenderInput& in, int level, int tx, int ty) {
  PROFILE_ZONE("composite_tile");
  const CompositePlan plan = composite_plan(in, level, tx, ty);
  if (plan.steps.empty()) return nullptr;
  // The Layer steps' pixels, held while the composite runs.
  std::vector<std::array<Pin, 2>> pins(plan.steps.size());
  for (size_t i = 0; i < plan.steps.size(); i++) {
    if (plan.steps[i].tile) pins[i][0] = plan.steps[i].tile->pin();
    if (plan.steps[i].cover) pins[i][1] = plan.steps[i].cover->pin();
  }
  const int x0 = plan.x0, n = plan.x1 - plan.x0;
  float color[3] = {};  // the stroke's in R, G, B
  if (in.stroke) {
    const auto& c = in.stroke->color;
    const bool grey = in.doc->mode == Mode::Gray;
    color[0] = c[0], color[1] = grey ? c[0] : c[1], color[2] = grey ? c[0] : c[2];
  }
  // A row of accumulator per Push deep, then a row of source pixels.
  std::vector<float> rows(size_t(plan.depth + 2) * S * 4);
  float* const src = rows.data() + size_t(plan.depth + 1) * S * 4;
  const std::shared_ptr<Tile> out = Tile::make({Depth::U16, 4});
  {
    const WritePin w = out->write();
    for (int y = plan.y0; y < plan.y1; y++) {
      float* acc = rows.data();
      std::fill(acc, acc + 4 * n, 0.f);
      for (size_t i = 0; i < plan.steps.size(); i++) {
        const CompositeStep& s = plan.steps[i];
        if (s.type == CompositeStep::Layer) {
          load(s.tile.get(), pins[i][0], y, x0, n, src);
          if (s.cover) apply_stroke(*in.stroke, color, *s.cover, pins[i][1], y, x0, n, src);
          over_row(s.blend, acc, src, n, s.alpha);
        } else if (s.type == CompositeStep::Push) {
          float* next = acc + 4 * S;
          if (s.pass)
            std::copy(acc, acc + 4 * n, next);
          else
            std::fill(next, next + 4 * n, 0.f);
          acc = next;
        } else {
          float* below = acc - 4 * S;
          if (s.pass) {
            for (int j = 0; j < 4 * n; j++) below[j] = blend::mix(below[j], acc[j], s.alpha);
          } else {  // the group as a layer
            for (int j = 0; j < 4 * n; j += 4) {
              for (int c = 0; c < 3; c++) src[j + c] = blend::unpremultiply(acc[j + c], acc[j + 3]);
              src[j + 3] = acc[j + 3];
            }
            over_row(s.blend, below, src, n, s.alpha);
          }
          acc = below;
        }
      }
      uint16_t* o = reinterpret_cast<uint16_t*>(w.data()) + (size_t(y) * S + size_t(x0)) * 4;
      for (int j = 0; j < 4 * n; j += 4) {
        const uint16_t a = from_unit(acc[j + 3], Depth::U16);
        for (int c = 0; c < 3; c++) o[j + c] = a ? from_unit(blend::unpremultiply(acc[j + c], acc[j + 3]), Depth::U16) : 0;
        o[j + 3] = a;
      }
    }
  }
  return compact(out, {});
}

}  // namespace pb
