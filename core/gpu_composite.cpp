// GpuComposite (gpu_composite.h): each output tile's plan (composite_plan(), the one composite_tile() runs) becomes a
// list of steps over atlas slots and solid values, the tiles made resident in the atlas, and composite.slang runs over
// all the tiles of a frame in one dispatch. Where the stroke paints, the accumulators below its layer go to a cache
// for the next frames to start from.
#include "gpu_composite.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <list>
#include <map>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "composite.h"
#include "composite.spv.h"
#include "composite_copy.spv.h"
#include "jobs.h"
#include "profile.h"

namespace pb {
namespace {

constexpr int S = kTileSize;
constexpr size_t kPixels = size_t(S) * S;
constexpr size_t kOutBytes = kPixels * 8;     // an RGBA U16 tile
constexpr size_t kBelowBytes = kPixels * 16;  // an accumulator: premultiplied float R, G, B, alpha
constexpr int kMaxDepth = 10;                 // composite.slang's: groups open at once on the GPU
constexpr size_t kLot = size_t(32) << 20;     // staging between two submits, as Device::upload() keeps it

// composite.slang's push constants and buffers.
struct Consts {
  uint32_t log2, channels, u16, erase;
  float color[3], opacity;
};
struct Job {
  uint32_t first, count, out, x, y;
};
struct Entry {
  uint32_t op, slot, blend;
  float alpha;
  float solid[4];
  uint32_t cover;
  float cover_solid;
  uint32_t level, pad;
};
static_assert(sizeof(Consts) == 32 && sizeof(Job) == 20 && sizeof(Entry) == 48, "composite.slang's layout");
constexpr uint32_t kSolid = ~0u, kNone = ~0u - 1;
constexpr uint32_t kLayer = 0, kPush = 1, kPop = 2, kSave = 3, kLoad = 4, kPass = 8;  // Entry::op

// composite_copy.slang's.
struct CopyConsts {
  uint32_t n, row;
};
constexpr uint32_t kCopyRow = 64 * 1024;

// The modes P0 composites, as blend.slang numbers them (Blend's values); composite.cpp takes the others as Normal.
Blend p0_mode(Blend b) {
  switch (b) {
    case Blend::Darken: case Blend::Multiply: case Blend::ColorBurn: case Blend::Lighten: case Blend::Screen:
    case Blend::ColorDodge: case Blend::Overlay: case Blend::SoftLight: case Blend::LinearLight: case Blend::Difference:
      return b;
    default: return Blend::Normal;
  }
}
static_assert(int(Blend::Normal) == 1 && int(Blend::Darken) == 3 && int(Blend::Multiply) == 4 &&
                  int(Blend::ColorBurn) == 5 && int(Blend::Lighten) == 8 && int(Blend::Screen) == 9 &&
                  int(Blend::ColorDodge) == 10 && int(Blend::Overlay) == 13 && int(Blend::SoftLight) == 14 &&
                  int(Blend::LinearLight) == 17 && int(Blend::Difference) == 20,
              "blend.slang's kNormal...");

uint32_t bits(float f) {
  uint32_t u;
  std::memcpy(&u, &f, 4);
  return u;
}

// n slots of a device buffer keyed by Index::key_type, least recently used out first. A slot used at the current
// stamp (by a dispatch not yet recorded, or by the current call) is not taken.
template <class Index>
class Slots {
 public:
  using Key = typename Index::key_type;
  int size() const { return int(pos_.size()); }
  void grow(int n) {  // to n slots, the new ones least recently used
    for (int s = size(); s < n; s++) {
      pos_.push_back(order_.insert(order_.end(), s));
      stamp_.push_back(0), keys_.emplace_back(), held_.push_back(false);
    }
  }
  bool has(const Key& key) const { return index_.count(key) != 0; }
  void drop(int s) {  // s holds nothing from now on
    if (held_[size_t(s)]) index_.erase(keys_[size_t(s)]), held_[size_t(s)] = false;
  }
  int held() const { return int(index_.size()); }  // slots holding a key
  int find(const Key& key, uint64_t stamp) {  // the slot holding key, used now; -1 if none
    const auto it = index_.find(key);
    if (it == index_.end()) return -1;
    touch(it->second, stamp);
    return it->second;
  }
  int take(uint64_t stamp) {  // the least recently used slot, emptied and used now; -1 when all are in use now
    const int s = order_.empty() ? -1 : order_.back();
    if (s < 0 || stamp_[size_t(s)] == stamp) return -1;
    if (held_[size_t(s)]) index_.erase(keys_[size_t(s)]), held_[size_t(s)] = false;
    touch(s, stamp);
    return s;
  }
  void put(int s, const Key& key) {  // s, just taken, holds key from now on, instead of any slot that did
    const auto [it, fresh] = index_.try_emplace(key, s);
    if (!fresh) held_[size_t(it->second)] = false, it->second = s;
    keys_[size_t(s)] = key, held_[size_t(s)] = true;
  }

 private:
  void touch(int s, uint64_t stamp) {
    order_.splice(order_.begin(), order_, pos_[size_t(s)]);
    stamp_[size_t(s)] = stamp;
  }
  std::list<int> order_;  // most recently used first
  std::vector<std::list<int>::iterator> pos_;
  std::vector<uint64_t> stamp_;
  std::vector<Key> keys_;
  std::vector<bool> held_;  // keys_ is in index_
  Index index_;
};

// A device buffer of slots of `size` bytes, LRU, that takes memory as frames need it, up to `cap` slots.
template <class Index>
struct Store {
  size_t size = 0;
  int cap = 0;
  gpu::BufferPtr buf;  // null while it has no slots
  Slots<Index> slots;
  size_t bytes() const { return size_t(slots.size()) * size; }
};
using Keys = std::map<std::vector<uint64_t>, int>;

// A buffer of at least `bytes`, grown by doubling.
void at_least(gpu::Device& dev, gpu::BufferPtr& buf, size_t& cap, size_t bytes) {
  if (buf && cap >= bytes) return;
  cap = std::max<size_t>(cap, 4096);
  while (cap < bytes) cap *= 2;
  buf = dev.buffer(cap, false);
}

// An output tile to composite: its plan, the output cache's key, and where the cache below the stroke's layer comes in.
struct Todo {
  size_t i = 0;  // in the call's tiles
  CompositePlan plan;
  std::vector<uint64_t> key;
  size_t at = 0;    // the stroke's layer's step, when a layer lies below it
  int levels = 0;   // accumulators open there: one, and one for each group around the layer; 0: no cache
  std::vector<uint64_t> below;  // the cache's key of steps before `at`, an accumulator's level to append
  std::vector<int> load;        // their slots in the cache when it has them all, bottom first
};

}  // namespace

struct GpuComposite::Impl {
  gpu::Device& dev;
  size_t budget = 0;
  gpu::KernelPtr kernel, copy;
  gpu::BufferPtr none;  // bound in place of a store without a buffer yet
  Store<Keys> out;      // composite tiles, by the steps that make them
  Store<Keys> below;    // accumulators below the stroke's layer, by the steps before it and the level
  // Tiles of the document's format, and the stroke buffer's (smaller), by id. ponytail: a frame needing more tiles
  // than its cap uploads them all every frame (least recently used out first, in the same order each time); keep the
  // slots of its last batch if that ever shows.
  Store<std::unordered_map<uint64_t, int>> atlas;
  Format format;  // the atlas's
  gpu::BufferPtr job_buf, entry_buf;
  size_t job_cap = 0, entry_cap = 0;
  Consts consts{};
  std::vector<uint64_t> stroke_key;  // the stroke's colour, opacity and mode, in the key of a tile it paints
  // The batch: tiles to composite at the next dispatch, their steps, and the cache keys of their slots.
  std::vector<Job> jobs;
  std::vector<Entry> entries;
  std::vector<std::pair<int, std::vector<uint64_t>>> keys, below_keys;
  struct Up {  // a layer tile going into atlas slot `slot`, `bytes` of it
    TilePtr tile;
    int slot;
    size_t bytes;
  };
  std::vector<Up> ups;           // the batch's, sent before its dispatch
  uint64_t batch = 1, call = 0;  // stamps
  Stats stats;
  // The cache's keys of the tiles planned for `planned_doc` (by level and place; empty: transparent), so that a tile the
  // cache has needs no plan: a view shows the same document frame after frame (V2d). Frames with a stroke, a filter's
  // preview or layers being transformed (RenderInput's, A30) plan anew: the document alone does not make their tiles.
  // A mip tile built again since (the mip cache let it go) has a new id but the same pixels: the key stays good.
  static constexpr size_t kPlanned = 8192;
  std::weak_ptr<const DocState> planned_doc;
  std::unordered_map<uint64_t, std::vector<uint64_t>> planned;

  explicit Impl(gpu::Device& d) : dev(d) {}

  size_t bytes() const { return out.bytes() + atlas.bytes() + below.bytes() + job_cap + entry_cap; }

  // Room in s for the `need` slots a call uses: as many, or twice what it had, up to its cap, what it held copied
  // over; otherwise its least recently used slots go. ponytail: each time a copy and an allocation the size of the
  // whole, a few times a session (as the view shows more tiles); separate chunks if that hitch shows.
  template <class Index>
  void fit(Store<Index>& s, int need) {
    const int n = std::min(s.cap, std::max(need, 2 * s.slots.size()));
    if (need <= s.slots.size() || n <= s.slots.size()) return;
    PROFILE_ZONE("gpu_composite_grow");
    gpu::BufferPtr to = dev.buffer(size_t(n) * s.size, false);  // the one before freed once copied
    if (s.buf) {
      const CopyConsts c{uint32_t(s.bytes() / 16), kCopyRow};
      dev.dispatch(*copy, {s.buf.get(), to.get()}, &c, sizeof c, kCopyRow / 64, (c.n + kCopyRow - 1) / kCopyRow);
    }
    s.buf = std::move(to);
    s.slots.grow(n);
  }

  // The atlas for tiles of f, the one of another format going (one document shows at a time).
  void use(Format f) {
    if (atlas.size && format == f) return;
    atlas.buf = nullptr;
    atlas.slots = {};
    atlas.size = kPixels * size_t(f.bytes());  // a stroke buffer's tile, 2 bytes a pixel, fits any
    atlas.cap = int(std::max<size_t>(1, std::min(budget / 2, dev.max_buffer()) / atlas.size));
    format = f;
  }

  // The stroke's push constants, and what it adds to the key of a tile it paints.
  void set_stroke(const RenderInput& in) {
    consts = {uint32_t(kTileLog2), uint32_t(format.channels), format.depth == Depth::U16 ? 1u : 0u, 0, {}, 0};
    stroke_key.clear();
    if (!in.stroke) return;
    const StrokeOverlay& o = *in.stroke;
    const bool grey = in.doc->mode == Mode::Gray;  // composite.cpp's colour: R, G, B
    consts.erase = o.erase;
    consts.color[0] = o.color[0], consts.color[1] = grey ? o.color[0] : o.color[1];
    consts.color[2] = grey ? o.color[0] : o.color[2], consts.opacity = o.opacity;
    for (const float f : consts.color) stroke_key.push_back(bits(f));
    stroke_key.push_back(bits(o.opacity) | uint64_t(o.erase) << 32);
  }

  // A cache key: steps [begin, end) of a plan over the tile's part of the canvas, the stroke's colour if it paints.
  std::vector<uint64_t> key(const CompositePlan& p, size_t begin, size_t end) const {
    std::vector<uint64_t> k{uint64_t(p.x0) | uint64_t(p.x1) << 16 | uint64_t(p.y0) << 32 | uint64_t(p.y1) << 48};
    bool stroked = false;
    for (size_t i = begin; i < end; i++) {
      const CompositeStep& s = p.steps[i];
      k.push_back(uint64_t(s.type) | uint64_t(s.pass) << 8 | uint64_t(p0_mode(s.blend)) << 16 | uint64_t(bits(s.alpha)) << 32);
      if (s.type != CompositeStep::Layer) continue;
      k.push_back(s.tile ? s.tile->id() : 0);
      k.push_back(s.cover ? s.cover->id() : 0);
      stroked |= bool(s.cover);
    }
    if (stroked) k.insert(k.end(), stroke_key.begin(), stroke_key.end());
    return k;
  }

  // Where the cache below the stroke's layer comes in: at the layer's step, when a layer lies below it; loaded when
  // the cache has every accumulator open there.
  void split(Todo& t) {
    const std::vector<CompositeStep>& steps = t.plan.steps;
    int depth = 0;
    bool layers = false;
    size_t at = 0;
    for (; at < steps.size() && !steps[at].cover; at++) {
      layers |= steps[at].type == CompositeStep::Layer;
      depth += steps[at].type == CompositeStep::Push ? 1 : steps[at].type == CompositeStep::Pop ? -1 : 0;
    }
    if (at == steps.size() || !layers || !below.cap) return;
    t.at = at, t.levels = depth + 1, t.below = key(t.plan, 0, at);
    for (int j = 0; j < t.levels; j++) {
      t.below.push_back(uint64_t(j));
      const int s = below.slots.find(t.below, call);
      t.below.pop_back();
      if (s < 0) return t.load.clear();
      t.load.push_back(s);
    }
  }

  // The layer tiles composite() would upload for t: those it reads that the atlas lacks.
  int uploads(const Todo& t) const {
    int n = 0;
    for (size_t i = t.load.empty() ? 0 : t.at; i < t.plan.steps.size(); i++)
      for (const Tile* tile : {t.plan.steps[i].tile.get(), t.plan.steps[i].cover.get()})
        n += tile && !tile->is_solid() && !atlas.slots.has(tile->id());
    return n;
  }

  // The atlas slot holding t (`bytes` of it), to upload with the batch if it was not there; -1 when the batch uses every
  // slot.
  int resident(const TilePtr& t, size_t bytes) {
    int s = atlas.slots.find(t->id(), batch);
    if (s >= 0 || (s = atlas.slots.take(batch)) < 0) return s;
    atlas.slots.put(s, t->id());
    ups.push_back({t, s, bytes});
    stats.uploaded++;
    return s;
  }

  // The batch's uploads: staged, then filled on the pool's threads (each tile back in RAM first: zstd, scratch), some
  // 32 MB at a time, each lot but the last submitted, so that the GPU copies it while the next one fills. Slots that
  // did not fill hold nothing.
  void send() {
    PROFILE_ZONE("gpu_composite_upload");
    const auto t0 = std::chrono::steady_clock::now();
    for (size_t at = 0; at < ups.size();) {
      const size_t from = at;
      std::vector<void*> to;
      for (size_t bytes = 0; at < ups.size() && (at == from || bytes + ups[at].bytes <= kLot); bytes += ups[at++].bytes)
        to.push_back(dev.stage(*atlas.buf, ups[at].bytes, size_t(ups[at].slot) * atlas.size));
      try {
        parallel_for(int(to.size()), [&](int begin, int end) {
          for (int i = begin; i < end; i++) {
            const Up& u = ups[from + size_t(i)];
            const Pin pin = u.tile->pin();
            std::memcpy(to[size_t(i)], pin.data(), u.bytes);
          }
        });
      } catch (...) {
        for (const Up& u : ups) atlas.slots.drop(u.slot);
        ups.clear();
        throw;
      }
      if (at < ups.size()) dev.submit();
    }
    ups.clear();
    stats.sent_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  }

  // The entries of the steps the GPU runs for t, their tiles resident in the atlas: from the accumulators the cache
  // has below the stroke's layer, else from the start, saving them into `save` (bottom first) on the way. False,
  // entries as they were, when the atlas lacks room beside the tiles the batch uses.
  bool add_entries(const Todo& t, const std::vector<int>& save) {
    const size_t first = entries.size();
    const double unit = 1.0 / max_value(format.depth);  // composite.cpp's unit()
    for (size_t j = 0; j < t.load.size(); j++) entries.push_back({kLoad, uint32_t(t.load[j]), 0, 0, {}, kNone, 0, uint32_t(j), 0});
    for (size_t i = t.load.empty() ? 0 : t.at; i < t.plan.steps.size(); i++) {
      const CompositeStep& step = t.plan.steps[i];
      for (size_t j = 0; i == t.at && j < save.size(); j++)
        entries.push_back({kSave, uint32_t(save[j]), 0, 0, {}, kNone, 0, uint32_t(j), 0});
      Entry e{kLayer, kSolid, uint32_t(p0_mode(step.blend)), step.alpha, {}, kNone, 0, 0, 0};
      if (step.type != CompositeStep::Layer) {
        e.op = (step.type == CompositeStep::Push ? kPush : kPop) | (step.pass ? kPass : 0);
      } else if (step.tile && step.tile->is_solid()) {  // R, G, B (grey thrice), alpha
        const Pixel v = step.tile->value();
        const bool rgb = format.channels == 4;
        const int at[4] = {0, rgb ? 1 : 0, rgb ? 2 : 0, format.channels - 1};
        for (int c = 0; c < 4; c++) e.solid[c] = float(double(v[size_t(at[c])]) * unit);
      } else if (step.tile) {
        const int s = resident(step.tile, atlas.size);
        if (s < 0) return entries.resize(first), false;
        e.slot = uint32_t(s);
      }  // else transparent, where the stroke paints
      if (step.cover && step.cover->is_solid()) {
        e.cover = kSolid, e.cover_solid = float(double(step.cover->value()[0]) * (1.0 / 65535));
      } else if (step.cover) {
        const int s = resident(step.cover, kPixels * 2);
        if (s < 0) return entries.resize(first), false;
        e.cover = uint32_t(s);
      }
      entries.push_back(e);
    }
    return true;
  }

  // Records the batch's dispatch: every tile of it at once.
  void flush() {
    if (jobs.empty()) return;
    send();
    at_least(dev, job_buf, job_cap, jobs.size() * sizeof(Job));
    at_least(dev, entry_buf, entry_cap, entries.size() * sizeof(Entry));
    dev.upload(*job_buf, jobs.data(), jobs.size() * sizeof(Job));
    dev.upload(*entry_buf, entries.data(), entries.size() * sizeof(Entry));
    dev.dispatch(*kernel,
                 {job_buf.get(), entry_buf.get(), atlas.buf ? atlas.buf.get() : none.get(), out.buf.get(),
                  below.buf ? below.buf.get() : none.get()},
                 &consts, sizeof consts, uint32_t(kPixels / 64), uint32_t(jobs.size()));
    for (const auto& [s, key] : keys) out.slots.put(s, key);
    for (const auto& [s, key] : below_keys) below.slots.put(s, key);
    stats.composited += int(jobs.size());
    stats.dispatches++;
    jobs.clear(), entries.clear(), keys.clear(), below_keys.clear();
    batch++;
  }

  // composite_tile()'s tile into slot o of the output.
  void upload_cpu(const TilePtr& t, int o) {
    std::vector<uint16_t> px(kPixels * 4);
    if (t && t->is_solid()) {
      for (size_t i = 0; i < px.size(); i++) px[i] = t->value()[i % 4];
    } else if (t) {
      std::memcpy(px.data(), t->pin().data(), kOutBytes);
    }
    dev.upload(*out.buf, px.data(), kOutBytes, size_t(o) * kOutBytes);
  }
};

GpuComposite::GpuComposite(gpu::Device& dev, size_t bytes) : impl_(std::make_unique<Impl>(dev)) {
  Impl& m = *impl_;
  m.budget = bytes ? bytes : std::min<size_t>(size_t(1) << 30, dev.memory() / 4);
  const size_t quarter = std::min(m.budget / 4, dev.max_buffer());
  m.out.size = kOutBytes, m.out.cap = int(std::max<size_t>(1, quarter / kOutBytes));
  m.below.size = kBelowBytes, m.below.cap = int(quarter / kBelowBytes);
  m.kernel = dev.kernel("composite", composite_spv, composite_spv_sizeInBytes, 5, sizeof(Consts));
  m.copy = dev.kernel("composite_copy", composite_copy_spv, composite_copy_spv_sizeInBytes, 2, sizeof(CopyConsts));
  m.none = dev.buffer(16, false);
}
GpuComposite::~GpuComposite() = default;

gpu::Buffer& GpuComposite::output() const { return impl_->out.buf ? *impl_->out.buf : *impl_->none; }
int GpuComposite::output_tiles() const { return impl_->out.slots.size(); }
void GpuComposite::reserve(int tiles) { impl_->fit(impl_->out, std::min(tiles, impl_->out.cap)); }
size_t GpuComposite::bytes() const { return impl_->bytes(); }
GpuComposite::Stats GpuComposite::stats() const { return impl_->stats; }

std::vector<int> GpuComposite::composite(const RenderInput& in, int level, const std::vector<TileXY>& tiles,
                                         size_t optional, int most) {
  PROFILE_ZONE("gpu_composite");
  Impl& m = *impl_;
  if (!in.doc) throw std::invalid_argument("pb: GpuComposite: no document");
  m.use(pixel_format(*in.doc));
  m.set_stroke(in);
  m.stats = {};
  m.call++;
  for (const Impl::Up& u : m.ups) m.atlas.slots.drop(u.slot);  // what a call that threw left
  m.batch++, m.jobs.clear(), m.entries.clear(), m.keys.clear(), m.below_keys.clear(), m.ups.clear();

  // Each tile's plan, and the keys of what the caches have; what the stores lack for the rest.
  std::vector<int> slots(tiles.size(), -1);
  std::vector<Todo> todo;
  int waiting = 0;                 // optional tiles past `most`
  const bool budgeted = most > 0;  // once spent, the rest wait unplanned: plans cost too
  bool spent = false;              // some tile took of `most`: a tile of more layers than it still comes in turn
  const bool reuse = !in.stroke && !in.preview && !in.warp;  // keys planned before stand
  if (m.planned_doc.lock() != in.doc || m.planned.size() > Impl::kPlanned) m.planned.clear(), m.planned_doc = in.doc;
  for (size_t i = 0; i < tiles.size(); i++) {
    const uint64_t at = uint64_t(level) << 56 | uint64_t(uint32_t(tiles[i].y)) << 28 | uint32_t(tiles[i].x);
    const auto known = reuse ? m.planned.find(at) : m.planned.end();
    if (known != m.planned.end()) {
      if (known->second.empty()) continue;  // transparent
      if ((slots[i] = m.out.slots.find(known->second, m.call)) >= 0) {
        m.stats.cached++;
        continue;
      }
    }
    if (i >= optional && budgeted && most <= 0) {
      slots[i] = -2, waiting++;
      continue;
    }
    Todo t{i, composite_plan(in, level, tiles[i].x, tiles[i].y)};  // the tiles composite_tile() reads
    if (t.plan.steps.empty()) {
      if (reuse) m.planned[at].clear();
      continue;
    }
    t.key = m.key(t.plan, 0, t.plan.steps.size());
    if (reuse) m.planned[at] = t.key;
    if ((slots[i] = m.out.slots.find(t.key, m.call)) >= 0) {
      m.stats.cached++;
      continue;
    }
    if (t.plan.depth <= kMaxDepth) m.split(t);
    if (i >= optional) {  // in order while they fit `most`; the first that does not ends it, but for the first of all
      const int n = m.uploads(t);
      if (n > 0 && (most <= 0 || (n > most && spent))) {
        slots[i] = -2, waiting++, most = 0;
        continue;
      }
      most -= n, spent = spent || n > 0;
    }
    todo.push_back(std::move(t));
  }
  int levels = 0;                      // accumulators the cache below the stroke's layer gives or takes
  std::unordered_set<uint64_t> reads;  // the atlas's tiles the GPU reads
  for (const Todo& t : todo) {
    if (t.plan.depth > kMaxDepth) continue;
    levels += t.levels;
    for (size_t i = t.load.empty() ? 0 : t.at; i < t.plan.steps.size(); i++)
      for (const Tile* tile : {t.plan.steps[i].tile.get(), t.plan.steps[i].cover.get()})
        if (tile && !tile->is_solid()) reads.insert(tile->id());
  }
  // Room for those waiting too, as they come; and in a call where some wait (a view change, V2c) for what it holds:
  // it grows to its cap before any tile goes, so that the levels a frame stands in from and the views it comes back
  // to stay, as the CPU's cache does.
  const int held = waiting ? m.out.slots.held() : 0;
  m.fit(m.out, std::max(m.stats.cached + int(todo.size()) + waiting, held + int(todo.size())));
  m.fit(m.atlas, int(reads.size()));
  m.fit(m.below, levels);
  if (int(reads.size()) <= m.atlas.slots.size())  // one batch: those there stay while the others go up
    for (const uint64_t id : reads) m.atlas.slots.find(id, m.batch);

  for (Todo& t : todo) {
    const int o = slots[t.i] = m.out.slots.take(m.call);
    if (o < 0) throw std::length_error("pb: GpuComposite: more tiles than output_tiles()");
    if (t.plan.depth > kMaxDepth) {  // ponytail: groups nested deeper than Photoshop's go through the CPU
      m.upload_cpu(composite_tile(in, level, tiles[t.i].x, tiles[t.i].y), o);
      m.out.slots.put(o, t.key);  // its upload recorded already
      m.stats.cpu++;
      continue;
    }
    std::vector<int> save;  // the accumulators below the stroke's layer, into the cache: all or none
    for (int j = 0; t.load.empty() && j < t.levels; j++) {
      const int s = m.below.slots.take(m.call);
      if (s < 0) {
        save.clear();
        break;
      }
      save.push_back(s);
    }
    size_t first = m.entries.size();
    if (!m.add_entries(t, save)) {
      m.flush();  // the batch so far, so that its tiles can go
      first = 0;
      if (!m.add_entries(t, save)) throw std::length_error("pb: GpuComposite: a tile has more layers than the atlas holds");
    }
    for (size_t i = first; i < m.entries.size(); i++) m.stats.layers += m.entries[i].op == kLayer;
    m.jobs.push_back({uint32_t(first), uint32_t(m.entries.size() - first), uint32_t(o),
                      uint32_t(t.plan.x0 | t.plan.x1 << 16), uint32_t(t.plan.y0 | t.plan.y1 << 16)});
    m.keys.emplace_back(o, std::move(t.key));
    for (size_t j = 0; j < save.size(); j++) {
      t.below.push_back(uint64_t(j));
      m.below_keys.emplace_back(save[j], t.below);
      t.below.pop_back();
    }
    m.stats.below += !t.load.empty();
  }
  m.flush();
  return slots;
}

}  // namespace pb
