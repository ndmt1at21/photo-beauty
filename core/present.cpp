// The Presenter (present.h): which tiles of which mip level each part of a frame shows, the composite of them (the
// GPU's, GpuComposite; or composite_tile() on the CPU), then present.slang over the frame, or shade() below, the same
// line by line on the CPU.
#include "present.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
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

F4 read(const Consts& k, const Bufs& b, uint32_t e, int x, int y) {
  if (e >= kEmpty) return {0, 0, 0, 0};
  const Src& s = b.tiles[e];
  const unsigned mask = (1u << k.log2) - 1;
  const uint16_t* p = s.px ? s.px + (((unsigned(y) & mask) << k.log2) + (unsigned(x) & mask)) * 4 : s.value.data();
  return {unit16(p[0]), unit16(p[1]), unit16(p[2]), unit16(p[3])};
}

F4 texel(const Consts& k, const Bufs& b, int x, int y, int ownx, int owny) {
  uint32_t e = slot(k, b.tables, x, y);
  if (e == kNone) {
    const int last = (1 << k.log2) - 1;
    x = std::clamp(x, ownx, ownx + last);
    y = std::clamp(y, owny, owny + last);
    e = slot(k, b.tables, x, y);
  }
  return read(k, b, e, x, y);
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
  const uint32_t e = (xa >> k.log2) == (xb >> k.log2) && (ya >> k.log2) == (yb >> k.log2) ? slot(k, b.tables, xa, ya) : kNone;
  F4 t00, t10, t01, t11;
  if (e != kNone) {
    t00 = premultiplied(read(k, b, e, xa, ya)), t10 = premultiplied(read(k, b, e, xb, ya));
    t01 = premultiplied(read(k, b, e, xa, yb)), t11 = premultiplied(read(k, b, e, xb, yb));
  } else {
    t00 = premultiplied(texel(k, b, xa, ya, ownx, owny)), t10 = premultiplied(texel(k, b, xb, ya, ownx, owny));
    t01 = premultiplied(texel(k, b, xa, yb, ownx, owny)), t11 = premultiplied(texel(k, b, xb, yb, ownx, owny));
  }
  const float w00 = (1 - wx) * (1 - wy), w10 = wx * (1 - wy), w01 = (1 - wx) * wy, w11 = wx * wy;
  F4 p;
  for (int c = 0; c < 4; c++) p[c] = t00[c] * w00 + t10[c] * w10 + t01[c] * w01 + t11[c] * w11;
  if (!(p[3] > 0)) return {0, 0, 0, 0};
  if (p[3] == 1) return p;
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
  // Tiles (level, x, y) to read ahead whose layer tiles the job brought back into RAM (zstd, scratch) since a frame
  // asked: those a frame uploads without stalling on that. Under m.
  std::set<std::array<int, 3>> warm;
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
  bool warming = false;  // the job brings tiles to read ahead into RAM: a stroke's frames stop it, the pool theirs

  // The last frame: its document, its level and the tiles it showed at that level (sorted), so that a frame of a view
  // change tells the tiles it shows newly.
  int fixed = -1;  // set_upload_budget()'s, -1: the budget's
  UploadBudget uploads;
  // GPU frames whose times the budget has yet to take, oldest first: CPU ms until the GPU starts (its first
  // submission), of them staging the frame's level's uploads; marks at the start, after the frame's level
  // composited, before and after reading ahead (its last command); the uploads.
  struct Took {
    double head = -1, sent = 0;
    uint64_t a = 0, b = 0, c = 0, d = 0;
    int n = 0;
  };
  std::deque<Took> took;
  std::weak_ptr<const DocState> last_doc;
  int last_level = -1;
  std::vector<TileXY> last;
  View::Pixels last_px;  // its view, whatever it showed
  // Tiles (level, x, y) composited for its document since it came: shown, standing in or read ahead, in the cache
  // unless it let them go since; what stands in for tiles waiting, or need not be read ahead.
  std::set<std::array<int, 3>> done;

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

  // Gives the budget the times of the frames before whose marks are in (their submissions waited for), forgetting
  // those still out past a few frames: a frame's, as its waiter sees it, the CPU's until the GPU starts then the GPU's
  // until it is done; of it the uploads', their copies and composites on the GPU and the staging before it started
  // (the CPU stages reading ahead while the GPU runs the frame).
  void measured() {
    for (; !took.empty(); took.pop_front()) {
      const Took& t = took.front();
      const double a = dev->marked(t.a), b = dev->marked(t.b), c = dev->marked(t.c), d = dev->marked(t.d);
      if (std::isnan(d) && took.size() < 8) return;
      if (std::isnan(a + b + c + d)) continue;
      const double up = (b - a) + (d - c) + t.sent;
      uploads.took(t.head + (d - a) - up, up, t.n);
    }
  }

  // Has the job make the mips of `tiles` at `level`, most urgent first, calling on_ready() after each chunk; then the
  // top level's (`top`, -1: none), the canvas in one tile, which may stand in for any; then bring back into RAM (zstd,
  // scratch) the layer tiles of `waiting`, tiles of `in` at `level` that wait to composite, about two frames' `budget`
  // of them, and call on_ready() for the next frame to go on; then as many of `ahead`'s, at level `al`, the tiles the
  // next frames read ahead.
  void request(int level, std::vector<TileXY> tiles, int top, const RenderInput& in, std::vector<TileXY> waiting,
               int al, std::vector<TileXY> ahead, int budget) {
    job.submit([sh = shared, gs = grids, level, tiles = std::move(tiles), top, in, waiting = std::move(waiting), al,
                ahead = std::move(ahead), most = 2 * budget](const CancelToken& token) {
      PROFILE_ZONE("present_mips");
      const auto ready = [&] {
        const std::lock_guard l(sh->m);
        if (sh->on_ready) sh->on_ready();
      };
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
        ready();
      }
      if (top >= 0 && !cancelled(token)) {
        parallel_for(int(gs.size()), [&](int begin, int end) {
          for (int i = begin; i < end; i++) mip(*gs[size_t(i)], top, 0, 0);
        });
        sh->made++;
      }
      // The layer tiles of ts back into RAM, tile after tile, in lots; `mark`: each tile into `warm` once its are.
      const auto warm = [&](int l, const std::vector<TileXY>& ts, bool mark) {  // false once cancelled
        PROFILE_ZONE("present_warm");
        std::vector<TilePtr> cold;
        std::vector<size_t> ends;  // of each tile's in cold
        for (size_t i = 0; i < ts.size() && int(cold.size()) < most; i++) {
          for (const CompositeStep& s : composite_plan(in, l, ts[i].x, ts[i].y).steps)
            for (const TilePtr& t : {s.tile, s.cover})
              if (t && !t->is_solid()) cold.push_back(t);
          ends.push_back(cold.size());
        }
        for (size_t at = 0, done = 0; done < ends.size();) {
          if (cancelled(token)) return false;
          const size_t n = std::min(4 * kChunk, cold.size() - at);
          parallel_for(int(n), [&](int begin, int end) {
            for (int i = begin; i < end; i++) cold[at + size_t(i)]->pin();
          });
          at += n;
          const std::lock_guard lock(sh->m);
          for (; done < ends.size() && ends[done] <= at; done++)
            if (mark) sh->warm.insert({l, ts[done].x, ts[done].y});
        }
        return !cancelled(token);
      };
      if (!waiting.empty()) {
        if (!warm(level, waiting, false)) return;
        ready();
      }
      warm(al, ahead, true);
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

void Presenter::set_upload_budget(int tiles) { impl_->fixed = tiles; }

int UploadBudget::next() const {
  return int(std::clamp((kFrameMs - other_) / each_, double(kMin), double(kMax)));
}

void UploadBudget::took(double other_ms, double upload_ms, int uploads) {
  constexpr double k = 0.25;  // the newest frame's weight; one that took long (a store growing) counts twice the mean
  if (std::isfinite(other_ms)) other_ += k * (std::clamp(other_ms, 0., 2 * other_ + 1) - other_);
  if (uploads >= kMin && std::isfinite(upload_ms) && upload_ms > 0)
    each_ += k * (std::min(upload_ms / uploads, 2 * each_) - each_);
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
  // The budget, and this frame's times for it (GPU).
  const auto begun = std::chrono::steady_clock::now();
  int budget = m.fixed >= 0 ? m.fixed : kFrameUploads;
  Impl::Took took;
  if (m.dev) {
    m.measured();
    if (m.fixed < 0) budget = m.uploads.next();
    took.a = m.dev->mark();
  }
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

  // Tiles of level l in the order to composite them, from document point `at` out within `box` (x0, y0, x1, y1,
  // document pixels) about it: first the tile a zoom in about `at` keeps in view longest (the smaller the scale of the
  // box about `at` at which it still meets the tile), then the nearest; rings of the box's shape about it, so that what
  // a zoom in shows next is there first (V2d), and a pan's or turn's centre.
  const auto order = [&](std::vector<TileXY>& ts, int l, const std::array<double, 2>& at, const std::array<double, 4>& box) {
    const double side = std::ldexp(double(S), l);
    std::vector<std::pair<std::array<double, 2>, TileXY>> keyed;
    for (const TileXY t : ts) {
      double scale = 0, near = 0;
      for (size_t c = 0; c < 2; c++) {
        const double lo = (c ? t.y : t.x) * side, hi = lo + side, a = at[c];
        if (lo > a) scale = std::max(scale, (lo - a) / std::max(box[c + 2] - a, 1e-9));
        if (hi < a) scale = std::max(scale, (a - hi) / std::max(a - box[c], 1e-9));
        near += ((lo + hi) / 2 - a) * ((lo + hi) / 2 - a);
      }
      keyed.push_back({{scale, near}, t});
    }
    std::sort(keyed.begin(), keyed.end(), [](const auto& a, const auto& b) {
      return a.first != b.first ? a.first < b.first : a.second < b.second;
    });
    for (size_t i = 0; i < ts.size(); i++) ts[i] = keyed[i].second;
  };

  // The frame's level's tiles it reads, from its anchor out: the pixel a zoom or turn since the last frame keeps in
  // place (the pointer's, as Photoshop zooms), else its centre.
  const View::Pixels before = m.last_px;
  const TileRect w0 = window(L);
  std::vector<TileXY> want;
  for (int ty = w0.y0; ty < w0.y1; ty++)
    for (int tx = w0.x0; tx < w0.x1; tx++)
      if (seen(L, tx, ty)) want.push_back({tx, ty});
  std::array<double, 2> anchor;  // on the document
  {
    double s = (W - 1) / 2., t = (H - 1) / 2.;
    const View::Pixels& q = before;  // o + i ax + j ay = q.origin + i q.across + j q.down there
    const double a = ax[0] - q.across[0], b = ay[0] - q.down[0], c = ax[1] - q.across[1], e = ay[1] - q.down[1];
    const double turn = a * e - b * c, rx = q.origin[0] - o[0], ry = q.origin[1] - o[1];
    if (q.width && std::abs(turn) > 1e-6 * std::abs(det))
      s = std::clamp((rx * e - b * ry) / turn, 0., W - 1.), t = std::clamp((a * ry - rx * c) / turn, 0., H - 1.);
    m.last_px = px;
    anchor = {o[0] + s * ax[0] + t * ay[0], o[1] + s * ax[1] + t * ay[1]};
    order(want, L, anchor, {x0, y0, x1, y1});
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
  // Which of the frame's level's tiles may wait (V2c): while the view moves over the document the last frame showed,
  // those that frame did not show at this level (newly in view, a zoom past a level), once the top level can stand in
  // for any. They composite within the budget, those composited before first (free), the others show meanwhile from
  // the level below where it has them composited (the level a zoom out comes from), else the finest above that has,
  // the top's at last. What changed under the view (an edit, the stroke) composites in this frame, as does all of a
  // frame of another document.
  const bool top_ready = L < top && m.missing(top, 0, 0, 0) == 0;
  const bool same = m.last_doc.lock() == in.doc, moving = top_ready && same;
  if (!same || m.done.size() > (size_t(1) << 14)) m.done.clear();
  {
    const std::lock_guard lock(m.shared->m);
    if (!same || m.shared->warm.size() > (size_t(1) << 14)) m.shared->warm.clear();
  }
  const auto fresh = [&](TileXY t) {
    return !(m.last_level == L && std::binary_search(m.last.begin(), m.last.end(), t));
  };

  // A pass a level shown: its tiles, its window of them (the first's is the frame's level's, the tiles' choices).
  struct Pass {
    int level;
    TileRect win;
    std::vector<TileXY> tiles;
  };
  std::vector<Pass> passes{{L, w0, {}}};
  std::vector<uint32_t> table(size_t(w0.x1 - w0.x0) * size_t(w0.y1 - w0.y0), kNoChoice);  // then each pass's slots
  const auto pick = [&](TileXY t) -> uint32_t& {
    return table[size_t(t.y - w0.y0) * size_t(w0.x1 - w0.x0) + size_t(t.x - w0.x0)];
  };
  std::vector<TileXY> later;  // the frame's level's that may wait, after those that may not
  for (size_t i = 0; i < want.size(); i++) {
    if (choice[i] < 0) continue;
    if (choice[i] == 0) {
      (moving && fresh(want[i]) ? later : passes[0].tiles).push_back(want[i]);
      pick(want[i]) = 0;
      continue;
    }
    const int level = L + choice[i];
    size_t p = 1;
    while (p < passes.size() && passes[p].level != level) p++;
    if (p == passes.size()) passes.push_back({level, window(level), {}});
    const TileXY t{want[i].x >> choice[i], want[i].y >> choice[i]};
    if (std::find(passes[p].tiles.begin(), passes[p].tiles.end(), t) == passes[p].tiles.end())
      passes[p].tiles.push_back(t);
    pick(want[i]) = uint32_t(p);
  }
  const size_t optional = passes[0].tiles.size();
  std::stable_partition(later.begin(), later.end(), [&](TileXY t) { return m.done.count({L, t.x, t.y}) != 0; });  // free
  passes[0].tiles.insert(passes[0].tiles.end(), later.begin(), later.end());

  // Each pass's constants: the document point relative to the first pass's window's corner, so that floats keep
  // fractions of a pixel at any zoom; the checkerboard from where the document's top left is on screen.
  const double R[2] = {std::ldexp(double(w0.x0) * S, L), std::ldexp(double(w0.y0) * S, L)};
  const int cell = std::max(look.cell, 1);
  const std::array<double, 2> corner = index(0, 0);
  const auto phase = [&](double v) { return float(v - std::floor(v / (2 * cell)) * 2 * cell); };
  std::vector<Consts> consts;
  const auto constants = [&](size_t p) {  // pass p's, its slots in the table after the others'
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
  };
  for (size_t p = 0; p < passes.size(); p++) constants(p);
  const auto at = [&](size_t p, TileXY t) {
    const Consts& c = consts[p];
    return c.table + size_t(t.y - passes[p].win.y0) * c.tw + size_t(t.x - passes[p].win.x0);
  };

  // Pass p's tiles composited, from `optional` on only within `most` layer tiles, into its slots: -2 for those that
  // wait, as GpuComposite's. On the CPU from the cache or composite_tile(), pinned while the frame reads them.
  std::vector<Src> srcs;
  std::vector<Pin> pins;
  const auto run = [&](size_t p, size_t optional, int most) {
    const Pass& ps = passes[p];
    const size_t n = ps.tiles.size();
    std::vector<int> r;
    const bool budgeted = most > 0;  // once spent, the rest wait unplanned: plans cost too
    bool spent = false;
    if (m.dev) {
      r = m.gc->composite(in, ps.level, ps.tiles, optional, most);
    } else {
      r.assign(n, -1);
      std::vector<std::vector<uint64_t>> keys(n);
      std::vector<TilePtr> got(n);
      std::vector<size_t> todo;
      for (size_t i = 0; i < n; i++) {
        if (i >= optional && budgeted && most <= 0) {
          r[i] = -2;
          continue;
        }
        const CompositePlan plan = composite_plan(in, ps.level, ps.tiles[i].x, ps.tiles[i].y);
        if (plan.steps.empty()) continue;  // transparent
        keys[i] = key_of(plan, in);
        r[i] = 0;
        if (m.cpu.find(keys[i])) {
          got[i] = m.cpu.get(keys[i]);
          continue;
        }
        if (i >= optional) {  // the layer tiles it reads, as the GPU's uploads
          int k = 0;
          for (const CompositeStep& s : plan.steps) k += (s.tile && !s.tile->is_solid()) + (s.cover && !s.cover->is_solid());
          if (k > 0 && (most <= 0 || (k > most && spent))) {  // as GpuComposite's
            r[i] = -2, most = 0;
            continue;
          }
          most -= k, spent = spent || k > 0;
        }
        todo.push_back(i);
      }
      parallel_for(int(todo.size()), [&](int begin, int end) {
        for (int j = begin; j < end; j++)
          got[todo[size_t(j)]] = composite_tile(in, ps.level, ps.tiles[todo[size_t(j)]].x, ps.tiles[todo[size_t(j)]].y);
      });
      for (const size_t i : todo)
        m.cpu.put(keys[i], got[i], got[i] && !got[i]->is_solid() ? size_t(S) * S * 8 : 64);
      for (size_t i = 0; i < n; i++) {
        if (r[i] != 0) continue;
        if (!got[i]) {
          r[i] = -1;
          continue;
        }
        r[i] = int(srcs.size());
        srcs.push_back({nullptr, got[i]->value()});
        if (!got[i]->is_solid()) {
          pins.push_back(got[i]->pin());
          srcs.back().px = reinterpret_cast<const uint16_t*>(pins.back().data());
        }
      }
    }
    for (size_t i = 0; i < n; i++) {
      if (r[i] == -2) continue;
      table[at(p, ps.tiles[i])] = r[i] < 0 ? kEmpty : uint32_t(r[i]);
      m.done.insert({ps.level, ps.tiles[i].x, ps.tiles[i].y});
    }
    return r;
  };
  // Pass p's dispatch on the GPU, the choices as they are by then: right after its composite(), whose slots hold until
  // the next. A tile a later pass takes over shows the checkerboard from the first pass until that pass draws it.
  const auto draw = [&](size_t p) {
    if (!m.dev) return;
    gpu::Device& dev = *m.dev;
    if (!m.tables || m.tables_bytes < table.size() * 4) {
      m.tables_bytes = std::max(table.size() * 4, 2 * m.tables_bytes);
      m.tables = dev.buffer(m.tables_bytes);
    }
    if (!m.uploaded) dev.upload(*m.colors, m.color.data(), m.color.size() * 4), m.uploaded = true;
    const Consts& c = consts[p];
    dev.upload(*m.tables, table.data(), consts[0].table * 4);
    dev.upload(*m.tables, table.data() + c.table, size_t(c.tw) * c.th * 4, size_t(c.table) * 4);
    dev.dispatch(*m.kernel, {&m.gc->output(), m.tables.get(), m.colors.get(), target.buffer}, &c, sizeof c,
                 uint32_t(W + 7) / 8, uint32_t(H + 7) / 8);
  };

  // What was recorded to the GPU, which runs it while the CPU goes on; the first time, the CPU's time until then.
  const auto start = [&] {
    if (took.head < 0) took.head = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begun).count();
    m.dev->submit();
  };
  std::vector<TileXY> waiting, late;  // the frame's level's tiles that wait: those a pass below takes, all of them
  int left = 0;                       // of the budget, what the frame's level did not take
  {
    const std::vector<int> r = run(0, optional, budget);
    if (m.dev) {
      const GpuComposite::Stats s = m.gc->stats();
      left = budget - s.uploaded, took.n = s.uploaded, took.sent = s.sent_ms, took.b = m.dev->mark();
      if (s.uploaded && !in.stroke) start();  // the GPU composites while the CPU records the passes (a stroke's: few)
    }
    m.last.clear();
    for (size_t i = 0; i < r.size(); i++)
      if (r[i] == -2)
        waiting.push_back(passes[0].tiles[i]), pick(passes[0].tiles[i]) = kNoChoice;
      else
        m.last.push_back(passes[0].tiles[i]);
    std::sort(m.last.begin(), m.last.end());
    m.last_doc = in.doc, m.last_level = L;
  }
  draw(0);
  for (size_t p = 1; p < passes.size(); p++) run(p, SIZE_MAX, 0), draw(p);
  const bool waited = !waiting.empty();
  late = waiting;
  // A pass of tiles at `level` standing in for those waiting: cover(t) puts in `ps` the tiles of the level t needs
  // there, false where the level cannot show it (tiles not composited since the document came: `done`); it shows
  // those whose tiles the cache still has (all of them).
  const auto stand_in = [&](int level, const std::function<bool(TileXY, std::vector<TileXY>&)>& cover, int& count) {
    Pass ps{level, window(level), {}};
    std::vector<std::vector<TileXY>> need(waiting.size());
    for (size_t i = 0; i < waiting.size(); i++)
      if (!cover(waiting[i], need[i])) need[i].clear();
    for (const std::vector<TileXY>& n : need)
      for (const TileXY t : n)
        if (std::find(ps.tiles.begin(), ps.tiles.end(), t) == ps.tiles.end()) ps.tiles.push_back(t);
    if (ps.tiles.empty()) return;
    passes.push_back(std::move(ps));
    const size_t p = passes.size() - 1;
    constants(p);
    const std::vector<int> r = run(p, level == top ? SIZE_MAX : 0, 0);  // only the top composites, if it must
    const auto has = [&](TileXY t) {
      return r[size_t(std::find(passes[p].tiles.begin(), passes[p].tiles.end(), t) - passes[p].tiles.begin())] != -2;
    };
    std::vector<TileXY> still;
    for (size_t i = 0; i < waiting.size(); i++)
      if (!need[i].empty() && std::all_of(need[i].begin(), need[i].end(), has))
        pick(waiting[i]) = uint32_t(p), count++;
      else
        still.push_back(waiting[i]);
    if (still.size() < waiting.size()) draw(p);  // else a pass that shows nothing
    waiting.swap(still);
  };
  if (L > 0 && waited) {
    const TileRect wb = window(L - 1);
    const int nx = (texels(L - 1, d.width) + S - 1) / S, ny = (texels(L - 1, d.height) + S - 1) / S;
    stand_in(L - 1, [&](TileXY t, std::vector<TileXY>& out) {
      for (int k = 0; k < 4; k++) {
        const TileXY c{2 * t.x + (k & 1), 2 * t.y + (k >> 1)};
        if (c.x >= nx || c.y >= ny || c.x < wb.x0 || c.y < wb.y0 || c.x >= wb.x1 || c.y >= wb.y1) continue;
        if (!m.done.count({L - 1, c.x, c.y}) || m.missing(L - 1, c.x, c.y, 0)) return false;
        out.push_back(c);
      }
      return !out.empty();
    }, f.finer);
  }
  for (int l = L + 1; l <= top && !waiting.empty(); l++)
    stand_in(l, [&](TileXY t, std::vector<TileXY>& out) {
      const TileXY c{t.x >> (l - L), t.y >> (l - L)};
      if ((l < top && !m.done.count({l, c.x, c.y})) || m.missing(l, c.x, c.y, 0)) return false;
      out.push_back(c);
      return true;
    }, f.coarser);
  f.missing += int(waiting.size());  // none: the top stands in for any

  // Read ahead: with budget left and nothing waiting, the tiles not composited yet, from the middle out, of the view
  // where the motion since the last frame goes: a zoom in the view about the anchor once it gets to the next level (as
  // all of it shows there unless the zoom goes on); else the view kAhead frames on, at its level (the next column of a
  // pan, the next ring of a zoom out). Into the GPU's cache, those whose layer tiles the job brought into RAM; a view
  // at rest has the job bring those of a zoom in about its centre, for a zoom to start from. ponytail: the motion
  // extrapolated linearly, and the CPU (slow anyway) reads nothing ahead.
  if (m.dev) took.c = m.dev->mark();
  int al = 0;
  std::vector<TileXY> next;  // to read ahead next, at level al
  const bool still = before.origin == o && before.across == ax && before.down == ay;
  if (m.dev && moving && !waited && !in.stroke && (left > 0 || still) && before.width == W && before.height == H) {
    constexpr double kAhead = 10;  // frames
    std::array<double, 2> o2, ax2, ay2, mid = anchor;
    const double now = std::hypot(ax[0], ax[1]), then = std::hypot(before.across[0], before.across[1]);
    if (L > 0 && (still || now < then * (1 - 1e-9))) {  // in: its level L - 1 begins below 2^L document pixels a device pixel
      const double f = std::ldexp(1.0, L) * (1 - 1e-6) / now;
      for (size_t c = 0; c < 2; c++)
        o2[c] = anchor[c] + (o[c] - anchor[c]) * f, ax2[c] = ax[c] * f, ay2[c] = ay[c] * f;
    } else {
      for (size_t c = 0; c < 2; c++)
        o2[c] = o[c] + kAhead * (o[c] - before.origin[c]), ax2[c] = ax[c] + kAhead * (ax[c] - before.across[c]),
        ay2[c] = ay[c] + kAhead * (ay[c] - before.down[c]);
      mid = {o2[0] + (W - 1) / 2. * ax2[0] + (H - 1) / 2. * ay2[0], o2[1] + (W - 1) / 2. * ax2[1] + (H - 1) / 2. * ay2[1]};
    }
    const double unit = std::hypot(ax2[0], ax2[1]);  // document pixels a device pixel there
    if ((o2 != o || ax2 != ax || ay2 != ay) && unit > 0) {
      int l = 0;
      while (l < top && unit >= std::ldexp(1.0, l + 1)) l++;
      double bx0 = std::numeric_limits<double>::max(), by0 = bx0, bx1 = -bx0, by1 = -bx0;
      for (const double s : {-0.5, W - 0.5})
        for (const double t : {-0.5, H - 0.5}) {
          const double x = o2[0] + s * ax2[0] + t * ay2[0], y = o2[1] + s * ax2[1] + t * ay2[1];
          bx0 = std::min(bx0, x), bx1 = std::max(bx1, x), by0 = std::min(by0, y), by1 = std::max(by1, y);
        }
      const double k = std::ldexp(1.0, -l) / S;
      const int nx = (texels(l, d.width) + S - 1) / S, ny = (texels(l, d.height) + S - 1) / S;
      const int tx0 = std::max(0, int(std::floor(bx0 * k))), ty0 = std::max(0, int(std::floor(by0 * k)));
      const int tx1 = std::min(nx, int(std::floor(bx1 * k)) + 1), ty1 = std::min(ny, int(std::floor(by1 * k)) + 1);
      std::vector<TileXY> ahead, warm;
      for (int ty = ty0; ty < ty1; ty++)
        for (int tx = tx0; tx < tx1; tx++)
          if (!m.done.count({l, tx, ty}) && !m.missing(l, tx, ty, 0)) ahead.push_back({tx, ty});
      order(ahead, l, mid, {bx0, by0, bx1, by1});
      {  // those the job brought into RAM go up now, the others once it did: zstd and scratch would stall the frame
        const std::lock_guard lock(m.shared->m);
        for (const TileXY t : ahead)
          if (m.shared->warm.count({l, t.x, t.y})) warm.push_back(t);
      }
      std::vector<int> r;
      if (still) warm.clear();  // the job's alone
      if (!warm.empty()) {
        start();  // the GPU runs the frame while the CPU stages these
        r = m.gc->composite(in, l, warm, 0, left);
        took.n += m.gc->stats().uploaded;
      }
      for (size_t i = 0, j = 0; i < ahead.size(); i++)  // the others to read ahead next, in order
        if (j < warm.size() && warm[j] == ahead[i] && r[j++] != -2)
          m.done.insert({l, ahead[i].x, ahead[i].y});
        else
          next.push_back(ahead[i]);
      al = l;
    }
  }
  if (m.dev) took.d = m.dev->mark();

  const bool need_top = L < top && !top_ready;
  if ((f.pending = !ask.empty() || waited) || need_top || !next.empty()) {
    m.warming = !next.empty();
    m.request(L, std::move(ask), need_top ? top : -1, in, std::move(late), al, std::move(next), budget);
  } else if (m.warming && in.stroke) {
    m.job.submit([](const CancelToken&) {});  // the running one stops at its next lot
    m.warming = false;
  }
  // At rest, room in the GPU's cache for the composite tiles of every level of the document (a motion's frames
  // would else grow it: an allocation, a copy, the frame late).
  if (m.dev && still && same && !in.stroke) {
    int all = 0;
    for (int l = 0; l <= top && all < (1 << 20); l++) all += ((texels(l, d.width) + S - 1) / S) * ((texels(l, d.height) + S - 1) / S);
    m.gc->reserve(all);
  }

  if (m.dev) {
    if (took.head < 0) took.head = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begun).count();
    f.submission = m.dev->submit();
    m.took.push_back(took);
    return f;
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
