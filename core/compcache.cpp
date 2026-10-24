// The tools' composite (compcache.h): a cache of composite_tile()'s tiles keyed by their plans, least recently used out
// first, within a quarter of the tile budget; grids of them composited on the GPU (a GpuComposite of their own, its
// output downloaded into tiles) or on the pool's threads; and the job that fills it ahead while the document stands
// still.
#include "compcache.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iterator>
#include <list>
#include <map>
#include <shared_mutex>
#include <stdexcept>
#include <utility>
#include <vector>

#include "composite.h"
#include "gpu_composite.h"
#include "profile.h"
#include "tilecache.h"

namespace pb {
namespace {

constexpr int S = kTileSize;
constexpr size_t kPixels = size_t(S) * S;
constexpr size_t kOutBytes = kPixels * 8;  // a composite tile's pixels: RGBA U16
// The GpuComposite's budget: a quarter for kGridBatch output tiles, an atlas of 2 x kGridBatch U16 RGBA tiles (more of
// smaller ones), the stroke's accumulators.
constexpr size_t kGpuBytes = 4 * size_t(kGridBatch) * kOutBytes;
constexpr size_t kLot = 64;               // tiles the prefill composites at a time
constexpr size_t kMost = kOutBytes + 1024;  // a cache entry's cost at most, but for keys of hundreds of steps

uint32_t bits(float f) {
  uint32_t u;
  std::memcpy(&u, &f, 4);
  return u;
}

// A tile's key: where it is, the canvas's part of it, and each step's layer, kind, mode, alpha and tiles (by id, new
// whenever pixels change), so that one key means one composite.
using Key = std::vector<uint64_t>;
Key key(const CompositePlan& plan, int level, TileXY at) {
  // Every member by name: one more in the plan or its steps (masks, B34) stops this from building until it is in the
  // key.
  [[maybe_unused]] const auto& [steps, x0, y0, x1, y1, depth] = plan;
  const auto u16 = [](int v) { return uint64_t(uint16_t(v)); };
  Key k{uint64_t(uint32_t(at.x)) | uint64_t(uint32_t(at.y)) << 32, uint64_t(level),
        u16(x0) | u16(x1) << 16 | u16(y0) << 32 | u16(y1) << 48};
  for (const CompositeStep& s : steps) {
    const auto& [type, layer, pass, blend, alpha, tile, cover] = s;
    k.push_back(uint64_t(layer) << 32 | uint64_t(type) << 16 | uint64_t(pass) << 8 | uint64_t(blend));
    k.push_back(bits(alpha));
    k.push_back(tile ? tile->id() : 0);
    k.push_back(cover ? cover->id() : 0);
  }
  return k;
}

// What an entry costs: its pixels, and roughly its key, Tile and nodes.
size_t cost(const Key& k, const TilePtr& t) { return 256 + k.size() * 8 + (t && !t->is_solid() ? kOutBytes : 0); }
size_t cap() { return tile_budget() / 4; }

// Entries most recently used first within cap(), as mip.cpp's; tiles it lets go of are freed once unlocked.
struct Cache {
  struct Item {
    Key key;
    TilePtr tile;  // null: transparent
    size_t bytes;  // cost()
  };
  std::mutex m;
  std::list<Item> items;
  std::map<Key, std::list<Item>::iterator> index;
  size_t used = 0;
  BudgetUser user{[this](size_t want) { return release(want); }};

  const Item* get(const Key& k) {  // lock held; marks it used
    const auto it = index.find(k);
    if (it == index.end()) return nullptr;
    items.splice(items.begin(), items, it->second);
    return &*it->second;
  }
  void drop(std::list<Item>::iterator it, std::vector<TilePtr>& out) {  // lock held
    used -= it->bytes;
    index.erase(it->key);
    out.push_back(std::move(it->tile));
    items.erase(it);
  }
  // t under k, unless an entry is there already (another thread's, composited meanwhile): the entry's tile.
  TilePtr keep(Key k, TilePtr t) {
    const size_t most = cap();
    std::vector<TilePtr> out;  // freed once unlocked
    const std::lock_guard<std::mutex> l(m);
    if (const Item* e = get(k)) return e->tile;
    const size_t bytes = cost(k, t);
    items.push_front({k, t, bytes});
    index.emplace(std::move(k), items.begin());
    used += bytes;
    while (used > most && items.size() > 1) drop(std::prev(items.end()), out);
    return t;
  }
  // The store over budget with no tile left to push out: lets go of tiles nothing else holds, least recently used
  // first, about `want` bytes of pixels, and gives what that freed in RAM.
  size_t release(size_t want) {
    PROFILE_ZONE("compcache_release");
    const TileStats was = tile_stats();
    std::vector<TilePtr> out;
    {
      const std::lock_guard<std::mutex> l(m);
      size_t pixels = 0;
      for (auto it = items.end(); it != items.begin() && pixels < want;) {
        const auto at = std::prev(it);
        if (at->tile && !at->tile->is_solid() && at->tile.use_count() == 1) {
          pixels += at->bytes;
          drop(at, out);
        } else {
          it = at;
        }
      }
    }
    out.clear();
    const TileStats now = tile_stats();
    const size_t a = was.raw + was.compressed, b = now.raw + now.compressed;
    return a > b ? a - b : 0;
  }
};
// Never destroyed, as mip.cpp's: jobs may still composite while static objects go at exit.
Cache& cache() {
  static Cache& c = *new Cache;
  return c;
}
std::atomic<uint64_t> g_built{0};

// set_composite_gpu()'s. Never destroyed either: a GpuComposite left at exit must not outlive its device.
struct Gpu {
  std::shared_mutex m;  // shared: composites on it; exclusive: set_composite_gpu()
  gpu::Device* dev = nullptr;
  std::mutex* lock = nullptr;
  std::unique_ptr<GpuComposite> gc;  // under *lock, made on first use
};
Gpu& the_gpu() {
  static Gpu& g = *new Gpu;
  return g;
}

using Done = std::function<void(size_t, TilePtr)>;

// Tiles todo[0, n) of `at` composited on the GPU, kGridBatch at a time, each through done(): n, all of them but where
// there is no GPU or a lot threw (a stack deeper than the atlas holds, the device lost, a tile damaged in its file),
// the rest left to the CPU, which composites them or throws what composite_tile() does.
size_t on_gpu(const RenderInput& in, int level, const std::vector<TileXY>& at, const std::vector<size_t>& todo,
              const CancelToken& cancel, const Done& done) {
  Gpu& g = the_gpu();
  const std::shared_lock<std::shared_mutex> use(g.m);
  if (!g.dev) return 0;
  std::vector<TileXY> tiles;
  std::vector<int> slots;
  std::vector<uint16_t> px;  // the output's slots up to the last one used
  for (size_t b = 0; b < todo.size(); b += kGridBatch) {
    if (cancelled(cancel)) throw Cancelled();
    const size_t e = std::min(todo.size(), b + kGridBatch);
    tiles.clear();
    for (size_t j = b; j < e; j++) tiles.push_back(at[todo[j]]);
    {
      PROFILE_ZONE("compcache_gpu");
      const std::lock_guard<std::mutex> l(*g.lock);
      try {
        if (!g.gc) g.gc = std::make_unique<GpuComposite>(*g.dev, kGpuBytes);
        slots = g.gc->composite(in, level, tiles);
        const int last = *std::max_element(slots.begin(), slots.end());
        if (last >= 0) {
          px.resize(size_t(last + 1) * kPixels * 4);
          g.dev->download(g.gc->output(), px.data(), px.size() * 2);
        }
        g.dev->flush();
      } catch (const std::exception&) {
        return b;
      }
    }
    parallel_for(int(e - b), [&](int begin, int end) {
      for (int j = begin; j < end; j++) {
        const int s = slots[size_t(j)];
        TilePtr t;  // null at -1: transparent
        if (s >= 0) {
          const std::shared_ptr<Tile> o = Tile::make({Depth::U16, 4});
          {
            const WritePin w = o->write();
            std::memcpy(w.data(), px.data() + size_t(s) * kPixels * 4, kOutBytes);
          }
          t = compact(o, {});  // as composite_tile() leaves its tiles
        }
        done(todo[b + size_t(j)], std::move(t));
      }
    });
  }
  return todo.size();
}

// Tiles `at` of the composite at `level`, out of the cache or composited into it (on the GPU when `gpu` and there is
// one); null where transparent. Plans in parallel first, then what the cache lacks. Throws Cancelled once `cancel` is
// set, between tiles.
std::vector<TilePtr> fill(const RenderInput& in, int level, const std::vector<TileXY>& at, const CancelToken& cancel,
                          bool gpu) {
  PROFILE_ZONE("compcache_fill");
  const bool keep = !in.stroke;
  const size_t n = at.size();
  std::vector<TilePtr> out(n);
  std::vector<Key> keys(n);
  std::vector<uint8_t> lack(n, 0);
  Cache& c = cache();
  parallel_for(int(n), [&](int begin, int end) {
    for (size_t i = size_t(begin); i < size_t(end); i++) {
      if (cancelled(cancel)) throw Cancelled();
      const CompositePlan plan = composite_plan(in, level, at[i].x, at[i].y);
      if (plan.steps.empty()) continue;  // transparent
      if (keep) {
        keys[i] = key(plan, level, at[i]);
        const std::lock_guard<std::mutex> l(c.m);
        if (const Cache::Item* e = c.get(keys[i])) {
          out[i] = e->tile;
          continue;
        }
      }
      lack[i] = 1;
    }
  });
  std::vector<size_t> todo;
  for (size_t i = 0; i < n; i++)
    if (lack[i]) todo.push_back(i);
  const Done done = [&](size_t i, TilePtr t) {
    g_built.fetch_add(1, std::memory_order_relaxed);
    out[i] = keep ? c.keep(std::move(keys[i]), std::move(t)) : std::move(t);
  };
  const size_t on = gpu && !todo.empty() ? on_gpu(in, level, at, todo, cancel, done) : 0;
  parallel_for(int(todo.size() - on), [&](int begin, int end) {
    for (size_t j = on + size_t(begin); j < on + size_t(end); j++) {
      if (cancelled(cancel)) throw Cancelled();
      done(todo[j], composite_tile(in, level, at[todo[j]].x, at[todo[j]].y));
    }
  });
  return out;
}

// Prefill's job: level 0 of the document into the cache, the tiles of `visible` first, then the canvas's others row by
// row, kLot at a time, no more than the cache keeps (past that it would push out what the job filled first, the
// view's). Throws Cancelled once `token` is set.
void fill_ahead(const DocPtr& doc, TileRect visible, const CancelToken& token) {
  PROFILE_ZONE("compcache_prefill");
  const TileRect all = canvas_tiles(*doc, 0);
  const TileRect v{std::max(visible.x0, all.x0), std::max(visible.y0, all.y0), std::min(visible.x1, all.x1),
                   std::min(visible.y1, all.y1)};
  std::vector<TileXY> order;
  for (int y = v.y0; y < v.y1; y++)
    for (int x = v.x0; x < v.x1; x++) order.push_back({x, y});
  for (int y = all.y0; y < all.y1; y++)
    for (int x = all.x0; x < all.x1; x++)
      if (x < v.x0 || x >= v.x1 || y < v.y0 || y >= v.y1) order.push_back({x, y});
  order.resize(std::min(order.size(), cap() / kMost));
  const RenderInput in{doc};
  for (size_t i = 0; i < order.size(); i += kLot) {
    const auto from = order.begin() + ptrdiff_t(i);
    fill(in, 0, std::vector<TileXY>(from, from + ptrdiff_t(std::min(kLot, order.size() - i))), token, false);
  }
}

}  // namespace

TilePtr composite_cached(const RenderInput& in, int level, int tx, int ty) {
  PROFILE_ZONE("composite_cached");
  return fill(in, level, {{tx, ty}}, {}, false)[0];
}

TileRect canvas_tiles(const DocState& d, int level) {
  if (level < 0 || level > 31) throw std::invalid_argument("pb: canvas_tiles: no such mip level");
  const int64_t w = (int64_t(d.width) + (int64_t(1) << level) - 1) >> level;
  const int64_t h = (int64_t(d.height) + (int64_t(1) << level) - 1) >> level;
  if (w <= 0 || h <= 0) return {};
  return {0, 0, int((w + S - 1) / S), int((h + S - 1) / S)};
}

TileRect view_tiles(const View& v, int level) {
  if (level < 0 || level > 31) throw std::invalid_argument("pb: view_tiles: no such mip level");
  if (!(v.width > 0 && v.height > 0 && v.zoom > 0 && v.dpr > 0)) return {};
  double x0 = HUGE_VAL, y0 = HUGE_VAL, x1 = -HUGE_VAL, y1 = -HUGE_VAL;
  for (const double cx : {0.0, v.width})
    for (const double cy : {0.0, v.height}) {
      const std::array<double, 2> p = v.to_doc(cx, cy);
      x0 = std::min(x0, p[0]), x1 = std::max(x1, p[0]), y0 = std::min(y0, p[1]), y1 = std::max(y1, p[1]);
    }
  const double side = std::ldexp(double(S), level);  // document pixels across a tile of the level
  const auto tile = [](double t) { return int(std::clamp(t, -1e9, 1e9)); };
  return {tile(std::floor(x0 / side)), tile(std::floor(y0 / side)), tile(std::ceil(x1 / side)),
          tile(std::ceil(y1 / side))};
}

TileGrid composite_grid(const RenderInput& in, int level, TileRect rect, const CancelToken& cancel) {
  PROFILE_ZONE("composite_grid");
  if (!in.doc) throw std::invalid_argument("pb: composite_grid: no document");
  const TileRect c = canvas_tiles(*in.doc, level);
  std::vector<TileXY> at;
  for (int y = std::max(rect.y0, c.y0); y < std::min(rect.y1, c.y1); y++)
    for (int x = std::max(rect.x0, c.x0); x < std::min(rect.x1, c.x1); x++) at.push_back({x, y});
  const std::vector<TilePtr> t = fill(in, level, at, cancel, true);
  TileGrid g({Depth::U16, 4});
  for (size_t i = 0; i < at.size(); i++)
    if (t[i]) g.set(at[i].x, at[i].y, t[i]);
  return g;
}

void set_composite_gpu(gpu::Device* dev, std::mutex* lock) {
  if (!dev || !lock) dev = nullptr, lock = nullptr;
  Gpu& g = the_gpu();
  const std::unique_lock<std::shared_mutex> l(g.m);  // once the composites on it are done
  if (g.gc && (dev != g.dev || lock != g.lock)) {
    const std::lock_guard<std::mutex> d(*g.lock);
    g.gc.reset();
  }
  g.dev = dev, g.lock = lock;
}

struct Prefill::State {
  std::mutex m;
  std::condition_variable cv;  // set(), a job done, the end
  Clock::duration still{};
  DocPtr doc;
  Clock::time_point changed{};
  TileRect visible{};
  uint64_t asked = 0, started = 0, filled = 0;  // set()s that changed something; the last the job started for, filled
  CancelToken token;                            // the last job's
  int running = 0;                              // jobs submitted, not done
  bool stop = false;
};

Prefill::Prefill(Clock::duration still) : s_(std::make_shared<State>()) {
  s_->still = still;
  timer_ = std::thread([s = s_] {
    std::unique_lock<std::mutex> l(s->m);
    while (!s->stop) {
      if (!s->doc || s->started == s->asked) {
        s->cv.wait(l);
        continue;
      }
      if (const Clock::time_point at = s->changed + s->still; Clock::now() < at) {
        s->cv.wait_until(l, at);
        continue;
      }
      s->started = s->asked;
      s->token = make_token();
      s->running++;
      submit(Priority::Background, [s, doc = s->doc, visible = s->visible, token = s->token, asked = s->asked] {
        bool all = false;
        try {
          fill_ahead(doc, visible, token);
          all = true;
        } catch (const Cancelled&) {
        } catch (const std::exception& e) {
          std::fprintf(stderr, "pb: prefill: %s\n", e.what());
        }
        const std::lock_guard<std::mutex> l(s->m);
        if (all) s->filled = std::max(s->filled, asked);
        s->running--;
        s->cv.notify_all();
      });
    }
  });
}

Prefill::~Prefill() {
  {
    const std::lock_guard<std::mutex> l(s_->m);
    s_->stop = true;
    if (s_->token) s_->token->store(true);
  }
  s_->cv.notify_all();
  timer_.join();
  std::unique_lock<std::mutex> l(s_->m);
  s_->cv.wait(l, [&] { return s_->running == 0; });
}

void Prefill::set(DocPtr doc, Clock::time_point changed, TileRect visible) {
  State& s = *s_;
  {
    const std::lock_guard<std::mutex> l(s.m);
    const TileRect& v = s.visible;
    if (doc == s.doc && changed == s.changed && visible.x0 == v.x0 && visible.y0 == v.y0 && visible.x1 == v.x1 &&
        visible.y1 == v.y1)
      return;
    if (s.token) s.token->store(true);  // the job running stops at its next tile
    std::swap(s.doc, doc);              // the one before freed once unlocked
    s.changed = changed, s.visible = visible;
    s.asked++;
  }
  s.cv.notify_all();
}

bool Prefill::wait(Clock::duration most) {
  std::unique_lock<std::mutex> l(s_->m);
  return s_->cv.wait_for(l, most, [&] { return !s_->doc || s_->filled == s_->asked; });
}

void clear_composites() {
  std::list<Cache::Item> items;  // freed once unlocked
  Cache& c = cache();
  const std::lock_guard<std::mutex> l(c.m);
  items.swap(c.items);
  c.index.clear();
  c.used = 0;
}

uint64_t composites_built() { return g_built.load(std::memory_order_relaxed); }

}  // namespace pb
