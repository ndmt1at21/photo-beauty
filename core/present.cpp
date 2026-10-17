// The Presenter (present.h): which tiles of which mip level each part of a frame shows, the composite of them (the
// GPU's, GpuComposite; or composite_tile() on the CPU), then present.slang over the frame, or shade() below, the same
// line by line on the CPU.
#include "present.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <mutex>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

#include "composite.h"
#include "gpu_composite.h"
#include "jobs.h"
#include "lru.h"
#include "mip.h"
#include "present.spv.h"
#include "profile.h"

namespace pb {
namespace {

constexpr int S = kTileSize;
constexpr uint32_t kBilinear = 1, kIdentity = 2;
constexpr uint32_t kNone = 0xffffffff, kEmpty = 0xfffffffe, kNoChoice = 0xffffffff;  // present.slang's
// The colour table: the matrix's rows from 0, the checkerboard's light and dark squares at 9 and 12, the pasteboard at
// 15 (R, G, B 0..1); the document's curves (Source::linear) from kLin, the screen's (Display::curve) from kOut.
constexpr int kLut = Source::kLut, kCurve = Display::kCurve;
constexpr int kLin = 32, kOut = kLin + 3 * kLut, kColors = kOut + 3 * kCurve;
constexpr size_t kChunk = 16;  // tiles a Visible job makes between two calls of on_ready()

// present.slang's push constants: see there.
struct Consts {
  float ox, oy, axx, axy, ayx, ayy;
  float lox, loy, hix, hiy;
  float base, scale, offx, offy;
  int32_t minx, miny, maxx, maxy;
  uint32_t log2, cw, ch, tw, th, table, pass, flags;
  float inv_cell, phasex, phasey;
  uint32_t width, height;
};
static_assert(sizeof(Consts) == 31 * 4, "present.slang's layout");

// ---- present.slang on the CPU, line by line ----

// A composite tile on the CPU: its pixels (composite_tile()'s RGBA U16 rows), or one value throughout.
struct Src {
  const uint16_t* px = nullptr;
  Pixel value{};
};
struct Bufs {  // what present.slang binds
  const uint32_t* tables;
  const float* color;
  const Src* tiles;
};
using F4 = std::array<float, 4>;
using F3 = std::array<float, 3>;

float unit16(unsigned v) { return float(v) / 65535.f; }  // blend.slang's unit16(): the nearest float

uint32_t slot(const Consts& k, const uint32_t* tables, int x, int y) {
  const int tx = x >> k.log2, ty = y >> k.log2;
  if (tx < 0 || ty < 0 || tx >= int(k.tw) || ty >= int(k.th)) return kNone;
  return tables[k.table + uint32_t(ty) * k.tw + uint32_t(tx)];
}

F4 texel(const Consts& k, const Bufs& b, int x, int y, int ownx, int owny) {
  uint32_t e = slot(k, b.tables, x, y);
  if (e == kNone) {
    const int last = (1 << k.log2) - 1;
    x = std::clamp(x, ownx, ownx + last);
    y = std::clamp(y, owny, owny + last);
    e = slot(k, b.tables, x, y);
  }
  if (e >= kEmpty) return {0, 0, 0, 0};
  const Src& s = b.tiles[e];
  const unsigned mask = (1u << k.log2) - 1;
  const uint16_t* p = s.px ? s.px + (((unsigned(y) & mask) << k.log2) + (unsigned(x) & mask)) * 4 : s.value.data();
  return {unit16(p[0]), unit16(p[1]), unit16(p[2]), unit16(p[3])};
}

F4 premultiplied(const F4& t) { return {t[0] * t[3], t[1] * t[3], t[2] * t[3], t[3]}; }

F4 sample(const Consts& k, const Bufs& b, float px, float py) {
  const float u = px * k.scale + k.offx, v = py * k.scale + k.offy;
  const int iu = std::clamp(int(std::floor(u)), k.minx, k.maxx), iv = std::clamp(int(std::floor(v)), k.miny, k.maxy);
  const int ownx = (iu >> k.log2) << k.log2, owny = (iv >> k.log2) << k.log2;
  if (!(k.flags & kBilinear)) return texel(k, b, iu, iv, ownx, owny);
  const float fu = u - 0.5f, fv = v - 0.5f;
  const int x0 = int(std::floor(fu)), y0 = int(std::floor(fv));
  const float wx = fu - float(x0), wy = fv - float(y0);
  const int xa = std::clamp(x0, k.minx, k.maxx), xb = std::clamp(x0 + 1, k.minx, k.maxx);
  const int ya = std::clamp(y0, k.miny, k.maxy), yb = std::clamp(y0 + 1, k.miny, k.maxy);
  const F4 t00 = premultiplied(texel(k, b, xa, ya, ownx, owny)), t10 = premultiplied(texel(k, b, xb, ya, ownx, owny));
  const F4 t01 = premultiplied(texel(k, b, xa, yb, ownx, owny)), t11 = premultiplied(texel(k, b, xb, yb, ownx, owny));
  const float w00 = (1 - wx) * (1 - wy), w10 = wx * (1 - wy), w01 = (1 - wx) * wy, w11 = wx * wy;
  F4 p;
  for (int c = 0; c < 4; c++) p[c] = t00[c] * w00 + t10[c] * w10 + t01[c] * w01 + t11[c] * w11;
  if (!(p[3] > 0)) return {0, 0, 0, 0};
  return {p[0] / p[3], p[1] / p[3], p[2] / p[3], p[3]};
}

float lut(const float* color, int at, int n, float pos) {
  const int i = std::min(int(pos), n - 2);
  const float f = pos - float(i);
  return color[at + i] + f * (color[at + i + 1] - color[at + i]);
}

F3 shown(const float* c, const F3& e) {
  const float l[3] = {lut(c, kLin, kLut, std::clamp(e[0], 0.f, 1.f) * float(kLut - 1)),
                      lut(c, kLin + kLut, kLut, std::clamp(e[1], 0.f, 1.f) * float(kLut - 1)),
                      lut(c, kLin + 2 * kLut, kLut, std::clamp(e[2], 0.f, 1.f) * float(kLut - 1))};
  float d[3];
  for (int r = 0; r < 3; r++) d[r] = std::clamp(c[3 * r] * l[0] + c[3 * r + 1] * l[1] + c[3 * r + 2] * l[2], 0.f, 1.f);
  return {lut(c, kOut, kCurve, std::sqrt(d[0]) * float(kCurve - 1)),
          lut(c, kOut + kCurve, kCurve, std::sqrt(d[1]) * float(kCurve - 1)),
          lut(c, kOut + 2 * kCurve, kCurve, std::sqrt(d[2]) * float(kCurve - 1))};
}

const float* checker(const Consts& k, const float* color, float s, float t) {
  const float a = (s - k.phasex) * k.inv_cell, b = (t - k.phasey) * k.inv_cell;
  return color + (((int(std::floor(a)) + int(std::floor(b))) & 1) != 0 ? 12 : 9);
}

uint32_t pack(const float* v) {
  uint32_t o[3];
  for (int c = 0; c < 3; c++) o[c] = uint32_t(std::clamp(v[c], 0.f, 1.f) * 255.f + 0.5f);
  return o[0] | o[1] << 8 | o[2] << 16 | 0xff000000u;
}

// Pixel (x, y) of the frame: present.slang's present() with the pass its tile chose (passes share all but their
// level's constants).
uint32_t shade(const std::vector<Consts>& passes, const Bufs& b, uint32_t x, uint32_t y) {
  const Consts& k0 = passes[0];
  const float s = float(x), t = float(y);
  const float px = k0.ox + s * k0.axx + t * k0.ayx, py = k0.oy + s * k0.axy + t * k0.ayy;
  if (!(px >= k0.lox && px < k0.hix && py >= k0.loy && py < k0.hiy)) return pack(b.color + 15);
  const float qx = px * k0.base, qy = py * k0.base;
  const int cx = int(std::floor(qx)) >> k0.log2, cy = int(std::floor(qy)) >> k0.log2;
  const uint32_t choice = cx >= 0 && cy >= 0 && cx < int(k0.cw) && cy < int(k0.ch)
                              ? b.tables[uint32_t(cy) * k0.cw + uint32_t(cx)]
                              : kNoChoice;
  const float* bg = checker(k0, b.color, s, t);
  if (choice == kNoChoice) return pack(bg);
  const Consts& k = passes[choice];
  const F4 c = sample(k, b, px, py);
  const F3 e = (k.flags & kIdentity) ? F3{c[0], c[1], c[2]} : shown(b.color, {c[0], c[1], c[2]});
  float o[3];
  for (int i = 0; i < 3; i++) o[i] = e[i] * c[3] + bg[i] * (1 - c[3]);
  return pack(o);
}

// ---- planning ----

using GridPtr = std::shared_ptr<const TileGrid>;

// The pixel layers' grids that a composite reads (composite.cpp's Planner): visible, opacity x fill above 0, in such
// groups.
void grids_of(const Layer& group, std::vector<GridPtr>& out) {
  for (const LayerPtr& l : group.children) {
    const LayerProps& p = l->props;
    if (!p.visible || !(std::min(p.opacity * p.fill, 1.f) > 0)) continue;
    if (l->kind == Kind::Group)
      grids_of(*l, out);
    else if (l->pixels)
      out.push_back(l->pixels);
  }
}

// The CPU's cache of composite tiles: the plan they come of (as GpuComposite keys its own): each step's kind, mode,
// alpha, tile and stroke cover, then the stroke where it paints.
std::vector<uint64_t> key_of(const CompositePlan& p, const RenderInput& in) {
  std::vector<uint64_t> key{uint64_t(p.x0) | uint64_t(p.x1) << 16 | uint64_t(p.y0) << 32 | uint64_t(p.y1) << 48};
  bool stroke = false;
  for (const CompositeStep& s : p.steps) {
    uint32_t a;
    std::memcpy(&a, &s.alpha, 4);
    key.push_back(uint64_t(s.type) | uint64_t(s.pass) << 8 | uint64_t(s.blend) << 16 | uint64_t(a) << 32);
    key.push_back(s.tile ? s.tile->id() : 0);
    key.push_back(s.cover ? s.cover->id() : 0);
    stroke = stroke || s.cover;
  }
  if (stroke) {
    const StrokeOverlay& o = *in.stroke;
    uint32_t v[4];
    std::memcpy(v, o.color.data(), 12);
    std::memcpy(v + 3, &o.opacity, 4);
    key.push_back(uint64_t(v[0]) | uint64_t(v[1]) << 32);
    key.push_back(uint64_t(v[2]) | uint64_t(v[3]) << 32 | uint64_t(o.erase) << 63);
  }
  return key;
}

// What the frames and the Visible job share; the job holds it while it runs.
struct Shared {
  std::atomic<uint64_t> made{0};  // chunks of tiles the job made: frames count again what they lacked
  std::mutex m;
  std::function<void()> on_ready;  // null once the presenter goes
};

// The sRGB profile's, for documents without one (or with one lcms does not take).
const Source& srgb_source() {
  static const std::shared_ptr<const Source> s = source_profile(icc_profile(ColorSpace::Srgb));
  return *s;
}

}  // namespace

struct Presenter::Impl {
  gpu::Device* dev = nullptr;
  std::unique_ptr<GpuComposite> gc;
  gpu::KernelPtr kernel;
  gpu::BufferPtr tables, colors;
  size_t tables_bytes = 0;
  Lru<std::vector<uint64_t>, TilePtr> cpu{size_t(256) << 20};  // composite tiles, about 2 frames of 4K at 100 %

  // The colour table, for the document's profile, the screen and the look's colours.
  bool colored = false, uploaded = false, identity = false;
  std::shared_ptr<const std::vector<uint8_t>> icc;
  std::shared_ptr<const Display> screen;
  std::array<uint8_t, 9> shades{};
  std::vector<float> color;

  // How many mip tiles the cache lacks for a tile of a level (mip_missing() over `grids`, the visible layers'), counted
  // up to `most` + 1, as of `made` (the job's chunks then): a count above 0 may drop once the job made more.
  struct Known {
    int n, most;
    uint64_t made;
  };
  std::vector<GridPtr> grids;
  std::map<std::array<int, 3>, Known> known;
  uint64_t made = 0;

  // The Visible job making mips: each frame lacking some asks anew (Latest: the one running stops at its next chunk).
  std::shared_ptr<Shared> shared = std::make_shared<Shared>();
  Latest job{Priority::Visible};

  int missing(int level, int tx, int ty, int most) {
    const std::array<int, 3> at{level, tx, ty};
    if (const auto it = known.find(at); it != known.end()) {
      const Known& k = it->second;
      if ((k.n == 0 || k.made == made) && (k.n <= k.most || k.most >= most)) return std::min(k.n, most + 1);
    }
    int n = 0;
    for (size_t i = 0; i < grids.size() && n <= most; i++) n += mip_missing(*grids[i], level, tx, ty, most - n);
    known[at] = {n, most, made};
    return n;
  }

  // Has the job make the mips of `tiles` at `level`, most urgent first.
  void request(int level, std::vector<TileXY> tiles) {
    job.submit([sh = shared, gs = grids, level, tiles = std::move(tiles)](const CancelToken& token) {
      PROFILE_ZONE("present_mips");
      for (size_t at = 0; at < tiles.size();) {
        if (cancelled(token)) return;
        const size_t n = std::min(kChunk, tiles.size() - at);
        parallel_for(int(n * gs.size()), [&](int begin, int end) {
          for (int i = begin; i < end; i++) {
            const TileXY t = tiles[at + size_t(i) % n];
            mip(*gs[size_t(i) / n], level, t.x, t.y);
          }
        });
        at += n;
        sh->made++;
        const std::lock_guard l(sh->m);
        if (sh->on_ready) sh->on_ready();
      }
    });
  }

  void set_colors(const DocState& d, const Look& look) {
    const std::array<uint8_t, 9> s{look.light[0], look.light[1], look.light[2], look.dark[0],      look.dark[1],
                                   look.dark[2],  look.pasteboard[0], look.pasteboard[1], look.pasteboard[2]};
    if (colored && d.icc == icc && look.screen == screen && s == shades) return;
    PROFILE_ZONE("present_colors");
    colored = true, uploaded = false, icc = d.icc, screen = look.screen, shades = s;
    std::shared_ptr<const Source> own;
    try {
      if (icc && !icc->empty()) own = source_profile(*icc);
    } catch (const std::runtime_error&) {  // a profile lcms does not take shows as sRGB
    }
    const Source& src = own ? *own : srgb_source();
    const Display& to = screen ? *screen : srgb_display();
    color.assign(kColors, 0.f);
    for (int r = 0; r < 3; r++)
      for (int c = 0; c < 3; c++) {
        double v = 0;
        for (int j = 0; j < 3; j++) v += double(to.from_rec2020[size_t(3 * r + j)]) * src.to_rec2020[size_t(3 * j + c)];
        color[size_t(3 * r + c)] = float(v);
      }
    for (int i = 0; i < 9; i++) color[size_t(9 + i)] = float(s[size_t(i)]) / 255.f;
    std::copy(&src.linear[0][0], &src.linear[0][0] + 3 * kLut, color.begin() + kLin);
    std::copy(&to.curve[0][0], &to.curve[0][0] + 3 * kCurve, color.begin() + kOut);
    // Colours as they are where the screen shows every one within a quarter level of that: sRGB on sRGB, bit for bit.
    identity = true;
    for (int r = 0; r <= 8 && identity; r++)
      for (int g = 0; g <= 8 && identity; g++)
        for (int b = 0; b <= 8 && identity; b++) {
          const F3 e{r / 8.f, g / 8.f, b / 8.f}, o = shown(color.data(), e);
          for (int c = 0; c < 3; c++) identity = identity && std::abs(o[size_t(c)] - e[size_t(c)]) <= 0.25f / 255;
        }
  }
};

Presenter::Presenter(gpu::Device* device, std::function<void()> on_ready) : impl_(std::make_unique<Impl>()) {
  Impl& m = *impl_;
  m.shared->on_ready = std::move(on_ready);
  if (!device) return;
  m.dev = device;
  m.gc = std::make_unique<GpuComposite>(*device);
  m.kernel = device->kernel("present", present_spv, present_spv_sizeInBytes, 4, sizeof(Consts));
  m.colors = device->buffer(size_t(kColors) * 4);
}

Presenter::~Presenter() {
  {
    const std::lock_guard l(impl_->shared->m);
    impl_->shared->on_ready = nullptr;
  }
  impl_->job.submit([](const CancelToken&) {});  // the running job stops at its next chunk
}

Presenter::Frame Presenter::frame(const RenderInput& in, const View& view, const Target& target, const Look& look) {
  PROFILE_ZONE("present");
  Impl& m = *impl_;
  if (!in.doc) throw std::invalid_argument("pb: Presenter: no document");
  if (m.dev ? !target.buffer : !target.pixels) throw std::invalid_argument("pb: Presenter: a target it cannot fill");
  Frame f;
  // Pixel (i, j)'s centre -> document point o + i ax + j ay, as the canvas has it.
  const View::Pixels px = view.pixels();
  const int W = px.width, H = px.height;
  if (W <= 0 || H <= 0) return f;
  const DocState& d = *in.doc;
  m.set_colors(d, look);
  const std::array<double, 2> o = px.origin, ax = px.across, ay = px.down;
  const double det = ax[0] * ay[1] - ay[0] * ax[1];
  const auto index = [&](double x, double y) {  // a document point's pixel (i, j), centres at whole numbers
    return std::array<double, 2>{((x - o[0]) * ay[1] - (y - o[1]) * ay[0]) / det,
                                 ((y - o[1]) * ax[0] - (x - o[0]) * ax[1]) / det};
  };
  const int L = view.level();
  // The frame's corners' box on the document.
  double x0 = std::numeric_limits<double>::max(), y0 = x0, x1 = -x0, y1 = -x0;
  for (const double s : {-0.5, W - 0.5})
    for (const double t : {-0.5, H - 0.5}) {
      const double x = o[0] + s * ax[0] + t * ay[0], y = o[1] + s * ax[1] + t * ay[1];
      x0 = std::min(x0, x), x1 = std::max(x1, x), y0 = std::min(y0, y), y1 = std::max(y1, y);
    }
  const auto texels = [&](int l, int size) { return int((int64_t(size) + (int64_t(1) << l) - 1) >> l); };  // at level l
  // The tiles of level l within a texel of that box, on the canvas.
  const auto window = [&](int l) {
    const double k = std::ldexp(1.0, -l);
    const int nx = (texels(l, d.width) + S - 1) / S, ny = (texels(l, d.height) + S - 1) / S;
    const auto tile = [&](double v, int n) { return int(std::clamp(std::floor(v / S), 0., double(n - 1))); };
    return TileRect{tile(x0 * k - 1, nx), tile(y0 * k - 1, ny), tile(x1 * k + 1, nx) + 1, tile(y1 * k + 1, ny) + 1};
  };
  // Whether tile (tx, ty) of level l, a texel around it with, meets the frame: the boxes, then the frame's own axes.
  const auto seen = [&](int l, int tx, int ty) {
    const double k = std::ldexp(1.0, l);
    const double rx0 = (double(tx) * S - 1) * k, rx1 = (double(tx + 1) * S + 1) * k;
    const double ry0 = (double(ty) * S - 1) * k, ry1 = (double(ty + 1) * S + 1) * k;
    if (rx1 < x0 || rx0 > x1 || ry1 < y0 || ry0 > y1) return false;
    double s0 = std::numeric_limits<double>::max(), t0 = s0, s1 = -s0, t1 = -s0;
    for (const double x : {rx0, rx1})
      for (const double y : {ry0, ry1}) {
        const std::array<double, 2> c = index(x, y);
        s0 = std::min(s0, c[0]), s1 = std::max(s1, c[0]), t0 = std::min(t0, c[1]), t1 = std::max(t1, c[1]);
      }
    return s1 >= -0.5 && s0 <= W - 0.5 && t1 >= -0.5 && t0 <= H - 0.5;
  };

  // The frame's level's tiles it reads, from its centre out.
  const TileRect w0 = window(L);
  std::vector<TileXY> want;
  for (int ty = w0.y0; ty < w0.y1; ty++)
    for (int tx = w0.x0; tx < w0.x1; tx++)
      if (seen(L, tx, ty)) want.push_back({tx, ty});
  {
    const double k = std::ldexp(1.0, -L) / S;
    const double s = (W - 1) / 2., t = (H - 1) / 2.;
    const double cx = (o[0] + s * ax[0] + t * ay[0]) * k - 0.5, cy = (o[1] + s * ax[1] + t * ay[1]) * k - 0.5;
    const auto far = [&](TileXY t) {
      return std::make_tuple((t.x - cx) * (t.x - cx) + (t.y - cy) * (t.y - cy), t.y, t.x);
    };
    std::sort(want.begin(), want.end(), [&](TileXY a, TileXY b) { return far(a) < far(b); });
  }
  f.tiles = int(want.size());

  // Which level shows each: its own where the cache has the mips, or the frame makes them (within kFrameMips, where no
  // coarser level has them); else the finest coarser level that has them, or none; those last two the job makes.
  std::vector<GridPtr> grids;
  if (d.root) grids_of(*d.root, grids);
  if (grids != m.grids || m.known.size() > (size_t(1) << 16)) m.grids = std::move(grids), m.known.clear();
  m.made = m.shared->made.load();
  int top = 0;  // the level the canvas fits a tile at
  while (((int64_t(d.width) - 1) >> top) >= S || ((int64_t(d.height) - 1) >> top) >= S) top++;
  std::vector<int> choice(want.size(), 0);  // level above L, -1 none
  std::vector<TileXY> build, ask;
  for (size_t i = 0; i < want.size(); i++) {
    const TileXY t = want[i];
    const int n = m.missing(L, t.x, t.y, kFrameMips);
    if (!n) continue;
    int c = 1;
    while (L + c <= top && m.missing(L + c, t.x >> c, t.y >> c, 0)) c++;
    if (L + c <= top) {
      choice[i] = c, f.coarser++;
    } else if (f.built + n <= kFrameMips) {
      f.built += n;
      build.push_back(t);
      continue;
    } else {
      choice[i] = -1, f.missing++;
    }
    ask.push_back(t);
  }
  if (!build.empty()) {
    PROFILE_ZONE("present_build");
    const int n = int(build.size()), g = int(m.grids.size());
    parallel_for(n * g, [&](int begin, int end) {
      for (int i = begin; i < end; i++) mip(*m.grids[size_t(i / n)], L, build[size_t(i % n)].x, build[size_t(i % n)].y);
    });
    for (const TileXY t : build) m.known[{L, t.x, t.y}] = {0, 0, m.made};
  }
  if ((f.pending = !ask.empty())) m.request(L, std::move(ask));

  // A pass a level shown: its tiles, its window of them (the first's is the frame's level's, the tiles' choices).
  struct Pass {
    int level;
    TileRect win;
    std::vector<TileXY> tiles;
  };
  std::vector<Pass> passes{{L, w0, {}}};
  std::vector<uint32_t> table(size_t(w0.x1 - w0.x0) * size_t(w0.y1 - w0.y0), kNoChoice);  // then each pass's slots
  for (size_t i = 0; i < want.size(); i++) {
    if (choice[i] < 0) continue;
    const int level = L + choice[i];
    size_t p = 0;
    while (p < passes.size() && passes[p].level != level) p++;
    if (p == passes.size()) passes.push_back({level, window(level), {}});
    const TileXY t{want[i].x >> choice[i], want[i].y >> choice[i]};
    if (std::find(passes[p].tiles.begin(), passes[p].tiles.end(), t) == passes[p].tiles.end())
      passes[p].tiles.push_back(t);
    table[size_t(want[i].y - w0.y0) * size_t(w0.x1 - w0.x0) + size_t(want[i].x - w0.x0)] = uint32_t(p);
  }

  // Their constants: the document point relative to the first pass's window's corner, so that floats keep fractions
  // of a pixel at any zoom; the checkerboard from where the document's top left is on screen.
  const double R[2] = {std::ldexp(double(w0.x0) * S, L), std::ldexp(double(w0.y0) * S, L)};
  const int cell = std::max(look.cell, 1);
  const std::array<double, 2> corner = index(0, 0);
  const auto phase = [&](double v) { return float(v - std::floor(v / (2 * cell)) * 2 * cell); };
  std::vector<Consts> consts;
  for (size_t p = 0; p < passes.size(); p++) {
    const Pass& ps = passes[p];
    Consts c{};
    c.ox = float(o[0] - R[0]), c.oy = float(o[1] - R[1]);
    c.axx = float(ax[0]), c.axy = float(ax[1]), c.ayx = float(ay[0]), c.ayy = float(ay[1]);
    c.lox = float(-R[0]), c.loy = float(-R[1]), c.hix = float(d.width - R[0]), c.hiy = float(d.height - R[1]);
    c.base = std::ldexp(1.f, -L);
    c.scale = std::ldexp(1.f, -ps.level);
    c.offx = float(std::ldexp(R[0], -ps.level) - double(ps.win.x0) * S);
    c.offy = float(std::ldexp(R[1], -ps.level) - double(ps.win.y0) * S);
    c.minx = -ps.win.x0 * S, c.miny = -ps.win.y0 * S;
    c.maxx = texels(ps.level, d.width) - 1 - ps.win.x0 * S, c.maxy = texels(ps.level, d.height) - 1 - ps.win.y0 * S;
    c.log2 = kTileLog2;
    c.cw = uint32_t(w0.x1 - w0.x0), c.ch = uint32_t(w0.y1 - w0.y0);
    c.tw = uint32_t(ps.win.x1 - ps.win.x0), c.th = uint32_t(ps.win.y1 - ps.win.y0), c.table = uint32_t(table.size());
    c.pass = uint32_t(p);
    c.flags = (view.zoom < 1 ? kBilinear : 0) | (m.identity ? kIdentity : 0);
    c.inv_cell = 1.f / float(cell);
    c.phasex = phase(corner[0]), c.phasey = phase(corner[1]);
    c.width = uint32_t(W), c.height = uint32_t(H);
    consts.push_back(c);
    table.resize(table.size() + size_t(c.tw) * c.th, kNone);
  }
  const auto at = [&](size_t p, TileXY t) {
    const Consts& c = consts[p];
    return c.table + size_t(t.y - passes[p].win.y0) * c.tw + size_t(t.x - passes[p].win.x0);
  };

  if (m.dev) {  // each pass's tiles composited, then its dispatch: their slots hold until the next composite()
    gpu::Device& dev = *m.dev;
    const auto reserve = [&](gpu::BufferPtr& b, size_t& cap, size_t bytes) {
      if (b && cap >= bytes) return;
      cap = std::max(bytes, 2 * cap);
      b = dev.buffer(cap);
    };
    reserve(m.tables, m.tables_bytes, table.size() * 4);
    if (!m.uploaded) dev.upload(*m.colors, m.color.data(), m.color.size() * 4), m.uploaded = true;
    dev.upload(*m.tables, table.data(), consts[0].table * 4);
    for (size_t p = 0; p < passes.size(); p++) {
      if (!passes[p].tiles.empty()) {
        const std::vector<int> slots = m.gc->composite(in, passes[p].level, passes[p].tiles);
        for (size_t i = 0; i < slots.size(); i++)
          table[at(p, passes[p].tiles[i])] = slots[i] < 0 ? kEmpty : uint32_t(slots[i]);
      }
      const Consts& c = consts[p];
      dev.upload(*m.tables, table.data() + c.table, size_t(c.tw) * c.th * 4, size_t(c.table) * 4);
      dev.dispatch(*m.kernel, {&m.gc->output(), m.tables.get(), m.colors.get(), target.buffer}, &c, sizeof c,
                   uint32_t(W + 7) / 8, uint32_t(H + 7) / 8);
    }
    f.submission = dev.submit();
    return f;
  }

  // The CPU: each pass's composite tiles, from the cache or composite_tile(), pinned while the frame reads them.
  std::vector<Src> srcs;
  std::vector<Pin> pins;
  for (size_t p = 0; p < passes.size(); p++) {
    const Pass& ps = passes[p];
    const size_t n = ps.tiles.size();
    std::vector<std::vector<uint64_t>> keys(n);
    std::vector<TilePtr> got(n);
    std::vector<size_t> todo;
    for (size_t i = 0; i < n; i++) {
      const CompositePlan plan = composite_plan(in, ps.level, ps.tiles[i].x, ps.tiles[i].y);
      if (plan.steps.empty()) continue;  // transparent
      keys[i] = key_of(plan, in);
      if (m.cpu.find(keys[i]))
        got[i] = m.cpu.get(keys[i]);
      else
        todo.push_back(i);
    }
    parallel_for(int(todo.size()), [&](int begin, int end) {
      for (int j = begin; j < end; j++)
        got[todo[size_t(j)]] = composite_tile(in, ps.level, ps.tiles[todo[size_t(j)]].x, ps.tiles[todo[size_t(j)]].y);
    });
    for (const size_t i : todo)
      m.cpu.put(keys[i], got[i], got[i] && !got[i]->is_solid() ? size_t(S) * S * 8 : 64);
    for (size_t i = 0; i < n; i++) {
      uint32_t& e = table[at(p, ps.tiles[i])];
      if (!got[i]) {
        e = kEmpty;
        continue;
      }
      e = uint32_t(srcs.size());
      srcs.push_back({nullptr, got[i]->value()});
      if (!got[i]->is_solid()) {
        pins.push_back(got[i]->pin());
        srcs.back().px = reinterpret_cast<const uint16_t*>(pins.back().data());
      }
    }
  }
  const Bufs b{table.data(), m.color.data(), srcs.data()};
  PROFILE_ZONE("present_cpu");
  parallel_for(H, [&](int begin, int end) {
    for (int y = begin; y < end; y++)
      for (int x = 0; x < W; x++)
        target.pixels[size_t(y) * size_t(W) + size_t(x)] = shade(consts, b, uint32_t(x), uint32_t(y));
  });
  return f;
}

}  // namespace pb
