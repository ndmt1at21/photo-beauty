// GpuComposite (gpu_composite.h): each output tile's plan (composite_plan(), the one composite_tile() runs) becomes a
// list of atlas slots and solid values, the tiles made resident in the atlas, and composite.slang runs over all the
// tiles of a frame in one dispatch.
#include "gpu_composite.h"

#include <algorithm>
#include <cstring>
#include <list>
#include <map>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include "composite.h"
#include "composite.spv.h"
#include "profile.h"

namespace pb {
namespace {

constexpr int S = kTileSize;
constexpr size_t kPixels = size_t(S) * S;
constexpr size_t kOutBytes = kPixels * 8;  // an RGBA U16 tile
constexpr uint32_t kSolid = ~0u;

// composite.slang's push constants and buffers.
struct Consts {
  uint32_t log2, channels, u16;
};
struct Job {
  uint32_t first, count, out, x, y;
};
struct Entry {
  uint32_t slot, blend;
  float alpha;
  uint32_t pad;
  float solid[4];
};
static_assert(sizeof(Job) == 20 && sizeof(Entry) == 32, "composite.slang's layout");

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

// Whether composite.slang runs the plan: Layer steps only, without the stroke; groups (Push, Pop) and the stroke wait for
// B3.
bool flat(const CompositePlan& plan) {
  for (const CompositeStep& s : plan.steps)
    if (s.type != CompositeStep::Layer || s.cover) return false;
  return true;
}

// n slots of a device buffer keyed by Index::key_type, least recently used out first. A slot used at the current
// stamp (by a dispatch not yet recorded, or by the current call) is not taken.
template <class Index>
class Slots {
 public:
  using Key = typename Index::key_type;
  explicit Slots(int n = 0) : pos_(size_t(n)), stamp_(size_t(n)), keys_(size_t(n)), held_(size_t(n)) {
    for (int i = 0; i < n; i++) pos_[size_t(i)] = order_.insert(order_.end(), i);
  }
  int size() const { return int(pos_.size()); }
  int find(const Key& key, uint64_t stamp) {  // the slot holding key, used now; -1 if none
    const auto it = index_.find(key);
    if (it == index_.end()) return -1;
    touch(it->second, stamp);
    return it->second;
  }
  int take(uint64_t stamp) {  // the least recently used slot, emptied and used now; -1 when all are in use now
    const int s = order_.empty() ? -1 : order_.back();
    if (s < 0 || stamp_[size_t(s)] == stamp) return -1;
    if (held_[size_t(s)]) index_.erase(keys_[size_t(s)]);
    held_[size_t(s)] = false;
    touch(s, stamp);
    return s;
  }
  void put(int s, const Key& key) {  // s holds key from now on, instead of any slot that did
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

// A buffer of at least `bytes`, grown by doubling.
void reserve(gpu::Device& dev, gpu::BufferPtr& buf, size_t& cap, size_t bytes) {
  if (buf && cap >= bytes) return;
  cap = std::max<size_t>(cap, 4096);
  while (cap < bytes) cap *= 2;
  buf = dev.buffer(cap);
}

}  // namespace

struct GpuComposite::Impl {
  gpu::Device& dev;
  size_t budget = 0;
  gpu::KernelPtr kernel;
  gpu::BufferPtr out;  // composite tiles
  Slots<std::map<std::vector<uint64_t>, int>> out_slots;
  Format format{Depth::U8, 0};  // the atlas's; none yet
  gpu::BufferPtr atlas;
  size_t tile_bytes = 0;
  // By tile id. ponytail: a frame needing more tiles than the atlas holds uploads them all every frame (least recently
  // used out first, in the same order each time); keep the slots of its last batch if that ever shows.
  Slots<std::unordered_map<uint64_t, int>> atlas_slots;
  gpu::BufferPtr job_buf, entry_buf;
  size_t job_cap = 0, entry_cap = 0;
  // The batch: tiles to composite at the next dispatch, their layers, and the cache keys of their slots.
  std::vector<Job> jobs;
  std::vector<Entry> entries;
  std::vector<std::pair<int, std::vector<uint64_t>>> keys;
  uint64_t batch = 1, call = 0;  // stamps
  Stats stats;

  explicit Impl(gpu::Device& d) : dev(d) {}

  // The atlas for tiles of f, the one of another format going (one document shows at a time).
  void use(Format f) {
    if (atlas && format == f) return;
    atlas = nullptr;
    tile_bytes = kPixels * size_t(f.bytes());
    const size_t room = budget > out_slots.size() * kOutBytes ? budget - out_slots.size() * kOutBytes : 0;
    const size_t n = std::max<size_t>(1, std::min(room, dev.max_buffer()) / tile_bytes);
    atlas = dev.buffer(n * tile_bytes);
    atlas_slots = decltype(atlas_slots)(int(n));
    format = f;
  }

  // The entries of a flat plan's layers, their tiles resident in the atlas; false, entries as they were, when the atlas
  // lacks room beside the tiles the batch uses.
  bool add_entries(const CompositePlan& plan) {
    const size_t first = entries.size();
    const double unit = 1.0 / max_value(format.depth);
    for (const CompositeStep& step : plan.steps) {
      const Tile& t = *step.tile;
      Entry e{kSolid, uint32_t(p0_mode(step.blend)), step.alpha, 0, {}};
      if (t.is_solid()) {  // composite.cpp's unit(): R, G, B (grey thrice), alpha
        const Pixel v = t.value();
        const bool rgb = format.channels == 4;
        const int at[4] = {0, rgb ? 1 : 0, rgb ? 2 : 0, format.channels - 1};
        for (int c = 0; c < 4; c++) e.solid[c] = float(double(v[size_t(at[c])]) * unit);
      } else {
        int s = atlas_slots.find(t.id(), batch);
        if (s < 0) {
          if ((s = atlas_slots.take(batch)) < 0) {
            entries.resize(first);
            return false;
          }
          PROFILE_ZONE("gpu_composite_upload");
          const Pin pin = t.pin();
          dev.upload(*atlas, pin.data(), tile_bytes, size_t(s) * tile_bytes);
          atlas_slots.put(s, t.id());
          stats.uploaded++;
        }
        e.slot = uint32_t(s);
      }
      entries.push_back(e);
    }
    return true;
  }

  // Records the batch's dispatch: every tile of it at once.
  void flush() {
    if (jobs.empty()) return;
    reserve(dev, job_buf, job_cap, jobs.size() * sizeof(Job));
    reserve(dev, entry_buf, entry_cap, entries.size() * sizeof(Entry));
    dev.upload(*job_buf, jobs.data(), jobs.size() * sizeof(Job));
    dev.upload(*entry_buf, entries.data(), entries.size() * sizeof(Entry));
    const Consts c{uint32_t(kTileLog2), uint32_t(format.channels), format.depth == Depth::U16 ? 1u : 0u};
    dev.dispatch(*kernel, {job_buf.get(), entry_buf.get(), atlas.get(), out.get()}, &c, sizeof c,
                 uint32_t(kPixels / 64), uint32_t(jobs.size()));
    for (const auto& [s, key] : keys) out_slots.put(s, key);
    stats.composited += int(jobs.size());
    stats.dispatches++;
    jobs.clear(), entries.clear(), keys.clear();
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
    dev.upload(*out, px.data(), kOutBytes, size_t(o) * kOutBytes);
  }
};

GpuComposite::GpuComposite(gpu::Device& dev, size_t bytes) : impl_(std::make_unique<Impl>(dev)) {
  Impl& m = *impl_;
  m.budget = bytes ? bytes : std::min<size_t>(size_t(1) << 30, dev.memory() / 4);
  const size_t n = std::max<size_t>(1, std::min(m.budget / 4, dev.max_buffer()) / kOutBytes);
  m.out = dev.buffer(n * kOutBytes);
  m.out_slots = decltype(m.out_slots)(int(n));
  m.kernel = dev.kernel("composite", composite_spv, composite_spv_sizeInBytes, 4, sizeof(Consts));
}
GpuComposite::~GpuComposite() = default;

gpu::Buffer& GpuComposite::output() const { return *impl_->out; }
int GpuComposite::output_tiles() const { return impl_->out_slots.size(); }
size_t GpuComposite::bytes() const {
  const Impl& m = *impl_;
  return size_t(m.out_slots.size()) * kOutBytes + (m.atlas ? size_t(m.atlas_slots.size()) * m.tile_bytes : 0) +
         m.job_cap + m.entry_cap;
}
GpuComposite::Stats GpuComposite::stats() const { return impl_->stats; }

std::vector<int> GpuComposite::composite(const RenderInput& in, int level, const std::vector<TileXY>& tiles) {
  PROFILE_ZONE("gpu_composite");
  Impl& m = *impl_;
  if (!in.doc) throw std::invalid_argument("pb: GpuComposite: no document");
  m.use(pixel_format(*in.doc));
  m.stats = {};
  m.call++;
  m.batch++, m.jobs.clear(), m.entries.clear(), m.keys.clear();  // what a call that threw left
  std::vector<int> slots(tiles.size(), -1);
  for (size_t i = 0; i < tiles.size(); i++) {
    const int tx = tiles[i].x, ty = tiles[i].y;
    const CompositePlan plan = composite_plan(in, level, tx, ty);  // the tiles composite_tile() reads
    if (plan.steps.empty()) continue;
    const bool gpu = flat(plan);
    std::vector<uint64_t> key;
    if (gpu) {
      key.push_back(uint64_t(plan.x0) | uint64_t(plan.x1) << 16 | uint64_t(plan.y0) << 32 | uint64_t(plan.y1) << 48);
      for (const CompositeStep& s : plan.steps) {
        uint32_t a;
        std::memcpy(&a, &s.alpha, 4);
        key.push_back(s.tile->id());
        key.push_back(uint64_t(p0_mode(s.blend)) << 32 | a);
      }
      if ((slots[i] = m.out_slots.find(key, m.call)) >= 0) {
        m.stats.cached++;
        continue;
      }
    }
    const int o = slots[i] = m.out_slots.take(m.call);
    if (o < 0) throw std::length_error("pb: GpuComposite: more tiles than output_tiles()");
    if (!gpu) {  // ponytail: B3 composites groups and the stroke on the GPU
      m.upload_cpu(composite_tile(in, level, tx, ty), o);
      m.stats.cpu++;
      continue;
    }
    if (!m.add_entries(plan)) {
      m.flush();  // the batch so far, so that its tiles can go
      if (!m.add_entries(plan)) throw std::length_error("pb: GpuComposite: a tile has more layers than the atlas holds");
    }
    m.jobs.push_back({uint32_t(m.entries.size() - plan.steps.size()), uint32_t(plan.steps.size()), uint32_t(o),
                      uint32_t(plan.x0 | plan.x1 << 16), uint32_t(plan.y0 | plan.y1 << 16)});
    m.keys.emplace_back(o, std::move(key));
  }
  m.flush();
  return slots;
}

}  // namespace pb
