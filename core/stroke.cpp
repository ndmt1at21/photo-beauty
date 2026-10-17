// Stroke (stroke.h): pen events applied in order by one Interactive job at a time; their dabs (dab.h) stamped into a
// 1-channel U16 coverage buffer tile by tile, tiles on the pool and each tile's dabs in order, so that what a stroke
// makes never depends on the threads or on how its events batch; a copy published after each batch, with the tail for
// frames; at pen-up the buffer merged into the layer by blend.h's formulas, as composite_tile() lays the overlay.
#ifdef __clang__
#pragma clang fp contract(off)  // the merge rounds alike everywhere, as dab.cpp: C4 compares hashes across platforms
#endif
#include "stroke.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <exception>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "blend.h"
#include "dab.h"
#include "jobs.h"
#include "profile.h"

namespace pb {
namespace {

constexpr int S = kTileSize;
constexpr size_t kPixels = size_t(S) * S;
constexpr Format kCover{Depth::U16, 1};
constexpr size_t kMaskBytes = 64 << 20;  // masks held at once while stamping (a 5,000 px dab's is 50 MB)

std::atomic<int> g_threads{0};

float unit01(float v) { return v > 0 ? (v < 1 ? v : 1) : 0; }  // NaN gives 0, as DabPlacer clamps
// v / max as composite_tile() rounds it, so that the merge and the overlay agree to the bit before rounding.
inline float unit(unsigned v, double k) { return float(double(v) * k); }
// 0..1 to 0..65535; the double product is exact, so a fused multiply-add rounds it no differently.
uint32_t fixed(float v) { return uint32_t(double(unit01(v)) * 65535 + 0.5); }

// fn(i) for i in 0..n on the pool, or here alone after set_stroke_threads(1).
void each(int n, const std::function<void(int)>& fn) {
  const auto run = [&](int b, int e) {
    for (int i = b; i < e; i++) fn(i);
  };
  if (g_threads.load(std::memory_order_relaxed) == 1) return run(0, n);
  parallel_for(n, run);
}

// A dab to stamp: its mask, and its flow and cap 0..65535.
struct Mark {
  DabMask mask;
  uint32_t flow = 0, cap = 0;
};

// Tile xy's dabs, in order, into its coverage b, within the canvas: each takes B toward its cap by its mask times its
// flow in integers, as every platform rounds alike,
//   a = (mask x flow + 32767) / 65535,  B += (a (cap - B) + 65534) / 65535 where B < cap:
// rounded up, so that B reaches the cap (to the nearest, at a flow under 1/2 it stops a level short: an eraser would
// leave alpha 1 of 65535) and never passes it.
void stamp_tile(uint16_t* b, TileXY xy, const std::vector<Mark>& marks, const std::vector<uint32_t>& order, int width,
                int height) {
  const int ox = xy.x << kTileLog2, oy = xy.y << kTileLog2;
  for (const uint32_t i : order) {
    const Mark& k = marks[i];
    const DabMask& m = k.mask;
    const int x0 = std::max({m.x, ox, 0}), x1 = std::min({m.x + m.w, ox + S, width});
    const int y0 = std::max({m.y, oy, 0}), y1 = std::min({m.y + m.h, oy + S, height});
    for (int y = y0; y < y1; y++) {
      const uint16_t* src = m.px->data() + size_t(y - m.y) * size_t(m.w) + size_t(x0 - m.x);
      uint16_t* dst = b + size_t(y - oy) * S + size_t(x0 - ox);
      for (int x = 0; x < x1 - x0; x++) {
        const uint32_t v = dst[x];
        if (v >= k.cap) continue;
        const uint32_t a = (uint32_t(src[x]) * k.flow + 32767) / 65535;
        dst[x] = uint16_t(v + (a * (k.cap - v) + 65534) / 65535);
      }
    }
  }
}

// A layer tile's pixels (T samples, C channels, alpha last) with the stroke laid over in place, as composite_tile()
// lays the overlay (render.h): k = opacity x B, blend.h's paint() or erase_alpha(), then rounded to the depth. Pixels
// at B 0 keep their samples (what those formulas give them).
template <class T, int C>
void lay(T* px, const uint16_t* cov, const std::array<float, 3>& color, float opacity, bool erase, Depth depth) {
  const double k1 = 1.0 / max_value(depth);
  for (size_t i = 0; i < kPixels; i++, px += C) {
    if (!cov[i]) continue;
    const float k = opacity * unit(cov[i], 1.0 / 65535);
    const float a = unit(px[C - 1], k1);
    if (erase) {
      px[C - 1] = T(from_unit(blend::erase_alpha(a, k), depth));
      continue;
    }
    const float ao = blend::paint_alpha(a, k);
    for (int c = 0; c < C - 1; c++) px[c] = T(from_unit(blend::paint(unit(px[c], k1), color[size_t(c)], k, ao), depth));
    px[C - 1] = T(from_unit(ao, depth));
  }
}

}  // namespace

struct Stroke::Impl {
  // Set at the start, read by any thread.
  DocPtr doc;
  LayerId layer;
  BrushParams params;
  std::function<void()> on_publish;
  std::shared_ptr<const TileGrid> pixels;  // the layer's
  float opacity;                           // the stroke's, and its colour: clamped as the overlay has them
  std::array<float, 3> color;

  // The stroke's job's (one at a time).
  DabPlacer placer;
  TileGrid buffer{kCover};
  std::shared_ptr<const TileGrid> published;  // the last copy of buffer
  size_t applied = 0;

  // Under m.
  std::mutex m;
  std::condition_variable cv;  // a state published, or the job failed
  std::vector<PenEvent> queue;
  size_t added = 0;
  bool finished = false, merged = false, running = false, failed = false;
  std::atomic<bool> dead{false};  // the Stroke is gone: the job stops at the next part of a batch
  std::shared_ptr<const StrokeState> state;

  Impl(DocPtr d, LayerId id, const BrushParams& p, std::function<void()> f)
      : doc(std::move(d)), layer(id), params(p), on_publish(std::move(f)), placer(p) {
    const Layer* l = doc ? find_layer(*doc, id) : nullptr;
    if (!l || l->kind != Kind::Pixel) throw std::invalid_argument("pb: Stroke: no pixel layer " + std::to_string(id));
    pixels = l->pixels ? l->pixels : std::make_shared<const TileGrid>(pixel_format(*doc));
    opacity = unit01(p.opacity);
    for (size_t c = 0; c < 3; c++) color[c] = unit01(p.color[c]);
    published = std::make_shared<const TileGrid>(buffer);
    auto s = std::make_shared<StrokeState>();
    s->buffer = published;
    s->overlay = {layer, published, color, opacity, p.erase};
    state = std::move(s);
  }

  // The job: batches of what was queued, until none is left.
  void run() {
    for (;;) {
      std::vector<PenEvent> events;
      bool last;
      {
        const std::lock_guard<std::mutex> l(m);
        last = finished && !merged;
        if (dead || (queue.empty() && !last)) return void(running = false);
        events.swap(queue);
      }
      try {
        batch(events, last);
      } catch (const std::exception& e) {  // no hang in wait(): what is published stays
        std::fprintf(stderr, "pb: stroke failed: %s\n", e.what());
        const std::lock_guard<std::mutex> l(m);
        failed = true, running = false;
        cv.notify_all();
        return;
      }
    }
  }

  void batch(const std::vector<PenEvent>& events, bool last) {
    PROFILE_ZONE("stroke_batch");
    std::vector<Dab> dabs;
    for (const PenEvent& e : events) {
      const std::vector<Dab> d = placer.add(e);
      dabs.insert(dabs.end(), d.begin(), d.end());
    }
    if (last) {
      const std::vector<Dab> d = placer.finish();
      dabs.insert(dabs.end(), d.begin(), d.end());
    }
    applied += events.size();
    if (!dabs.empty()) {
      stamp(buffer, dabs);
      published = std::make_shared<const TileGrid>(buffer);
    }
    if (dead) return;
    auto s = std::make_shared<StrokeState>();
    s->buffer = published;
    s->events = applied;
    s->overlay = {layer, published, color, opacity, params.erase};
    if (last) {
      s->pixels = merge(*published);
    } else if (const std::vector<Dab> tail = DabPlacer(placer).finish(); !tail.empty()) {
      PROFILE_ZONE("stroke_tail");
      TileGrid preview(*published);  // its tiles shared: stamping copies them
      stamp(preview, tail);
      s->overlay.buffer = std::make_shared<const TileGrid>(std::move(preview));
    }
    {
      const std::lock_guard<std::mutex> l(m);
      if (dead) return;
      state = std::move(s);
      merged = merged || last;
      cv.notify_all();
    }
    if (on_publish) on_publish();
  }

  // Dabs into g in order, a part of them at a time so that their masks stay within kMaskBytes.
  void stamp(TileGrid& g, const std::vector<Dab>& dabs) const {
    PROFILE_ZONE("stroke_stamp");
    for (size_t at = 0; at < dabs.size() && !dead;) {
      size_t end = at;
      for (double bytes = 0; end < dabs.size() && bytes < kMaskBytes; end++)
        bytes += (double(dabs[end].size) + 2) * (double(dabs[end].size) + 2) * 2;
      std::vector<Mark> marks(end - at);
      each(int(marks.size()), [&](int i) {
        const Dab& d = dabs[at + size_t(i)];
        // The dab's opacity as a share of the stroke's: 1 unless the pen's pressure sets it.
        marks[size_t(i)] = {dab_mask(d, params.hardness), fixed(d.flow), opacity > 0 ? fixed(d.opacity / opacity) : 65535};
      });
      std::map<TileXY, std::vector<uint32_t>> tiles;  // each tile's dabs, in order
      for (uint32_t i = 0; i < marks.size(); i++) {
        const DabMask& k = marks[i].mask;
        const int x0 = std::max(k.x, 0), x1 = std::min(k.x + k.w, doc->width);
        const int y0 = std::max(k.y, 0), y1 = std::min(k.y + k.h, doc->height);
        if (!k.px || x0 >= x1 || y0 >= y1) continue;
        for (int ty = y0 >> kTileLog2; ty <= (y1 - 1) >> kTileLog2; ty++)
          for (int tx = x0 >> kTileLog2; tx <= (x1 - 1) >> kTileLog2; tx++) tiles[{tx, ty}].push_back(i);
      }
      struct Work {
        TileXY xy;
        WritePin pin;  // held while stamping
        const std::vector<uint32_t>* order;
      };
      std::vector<Work> work;
      work.reserve(tiles.size());
      for (const auto& [xy, order] : tiles) work.push_back({xy, g.write(xy.x, xy.y), &order});  // one thread writes g
      each(int(work.size()), [&](int i) {
        const Work& w = work[size_t(i)];
        stamp_tile(reinterpret_cast<uint16_t*>(w.pin.data()), w.xy, marks, *w.order, doc->width, doc->height);
      });
      at = end;
    }
  }

  // The layer's pixels with the coverage merged: a new tile where the stroke covers any pixel, compacted; the layer's
  // own grid when it changed nothing.
  std::shared_ptr<const TileGrid> merge(const TileGrid& cover) const {
    PROFILE_ZONE("stroke_merge");
    std::vector<std::pair<TileXY, const Tile*>> in;
    for (const auto& [xy, t] : cover.tiles()) in.push_back({xy, t.get()});
    std::vector<TilePtr> out(in.size());
    each(int(in.size()), [&](int i) { out[size_t(i)] = merge_tile(in[size_t(i)].first, *in[size_t(i)].second); });
    TileGrid g(*pixels);
    bool changed = false;
    for (size_t i = 0; i < in.size(); i++) {
      const TileXY xy = in[i].first;
      if (out[i].get() == pixels->get(xy.x, xy.y)) continue;
      g.set(xy.x, xy.y, out[i]);
      changed = true;
    }
    return changed ? std::make_shared<const TileGrid>(std::move(g)) : pixels;
  }

  TilePtr merge_tile(TileXY xy, const Tile& cover) const {
    const auto it = pixels->tiles().find(xy);
    const TilePtr was = it == pixels->tiles().end() ? nullptr : it->second;
    const Pin cp = cover.pin();  // a data tile: buffers are written, never compacted
    const auto* cov = reinterpret_cast<const uint16_t*>(cp.data());
    if ((params.erase && !was) || !(opacity > 0) || std::all_of(cov, cov + kPixels, [](uint16_t v) { return v == 0; }))
      return was;
    const Format f = pixels->format();
    const std::shared_ptr<Tile> t = Tile::make(f, was && was->is_solid() ? was->value() : Pixel{});
    {
      const WritePin w = t->write();
      if (was && !was->is_solid()) std::memcpy(w.data(), was->pin().data(), kPixels * size_t(f.bytes()));
      const bool rgb = f.channels == 4;
      if (f.depth == Depth::U16) {
        auto* px = reinterpret_cast<uint16_t*>(w.data());
        rgb ? lay<uint16_t, 4>(px, cov, color, opacity, params.erase, f.depth)
            : lay<uint16_t, 2>(px, cov, color, opacity, params.erase, f.depth);
      } else {
        rgb ? lay<uint8_t, 4>(w.data(), cov, color, opacity, params.erase, f.depth)
            : lay<uint8_t, 2>(w.data(), cov, color, opacity, params.erase, f.depth);
      }
    }
    return compact(t, pixels->fill());
  }
};

Stroke::Stroke(DocPtr doc, LayerId layer, const BrushParams& p, std::function<void()> on_publish)
    : impl_(std::make_shared<Impl>(std::move(doc), layer, p, std::move(on_publish))) {}

Stroke::~Stroke() {
  const std::lock_guard<std::mutex> l(impl_->m);
  impl_->dead = true;  // the job stops soon; on_publish may run once more if it was under way
  impl_->queue.clear();
}

void Stroke::add(const PenEvent& e) {
  Impl& s = *impl_;
  {
    const std::lock_guard<std::mutex> l(s.m);
    if (s.finished || s.failed) return;  // after finish(): nothing to add to
    s.queue.push_back(e);
    s.added++;
    if (std::exchange(s.running, true)) return;  // the running job takes it
  }
  submit(Priority::Interactive, [impl = impl_] { impl->run(); });
}

void Stroke::finish() {
  Impl& s = *impl_;
  {
    const std::lock_guard<std::mutex> l(s.m);
    if (std::exchange(s.finished, true) || s.failed || std::exchange(s.running, true)) return;
  }
  submit(Priority::Interactive, [impl = impl_] { impl->run(); });
}

std::shared_ptr<const StrokeState> Stroke::state() const {
  const std::lock_guard<std::mutex> l(impl_->m);
  return impl_->state;
}

std::shared_ptr<const StrokeState> Stroke::wait() const {  // not from a pool job: the stroke's would queue behind it
  Impl& s = *impl_;
  std::unique_lock<std::mutex> l(s.m);
  const size_t events = s.added;
  const bool merge = s.finished;
  s.cv.wait(l, [&] { return s.failed || (s.state->events >= events && (!merge || s.state->pixels)); });
  return s.state;
}

void set_stroke_threads(int n) { g_threads.store(n, std::memory_order_relaxed); }

}  // namespace pb
