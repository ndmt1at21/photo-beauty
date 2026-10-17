#pragma once
// The tile store's memory (tile.h): one budget for every open document, counting the raw pixels of data tiles and
// their zstd copies in RAM. Once 90% of it is used, a Background job pushes out tiles nothing pins, least recently
// used first, down to 80%: raw pixels with a copy elsewhere already (scratch, a .pbd) are dropped; others are
// compressed with zstd in RAM where that saves an eighth, else written to the scratch file; zstd copies past half
// the budget move to scratch too. Past 100%, the thread whose last pin of a tile would overrun the budget pushes
// tiles out itself first, so tiles never run out of memory: only pinned ones may take more. A pin brings the pixels
// back. ponytail: one mutex for the store; per-tile atomics if Tracy shows pin() contended.
#include <cstddef>
#include <filesystem>
#include <functional>

namespace pb {

size_t physical_memory();  // the machine's RAM, bytes; 0 when unknown

// What the budget holds, bytes: 60% of physical_memory() until set (at least 1 MB). Lowering it pushes tiles out in
// the background. Any thread.
size_t tile_budget();
void set_tile_budget(size_t bytes);

// Whether cold tiles are compressed in RAM (the default) or go straight to scratch; E4 keeps the tier only if it
// pays. Any thread.
void set_tile_compression(bool on);

// Where the scratch file goes: the system's temporary directory until set. The file never outlives the process: it
// has no name on Linux and macOS (unlinked once open) and is deleted on close on Windows. It is closed whenever
// nothing is left in it, its dead space with it; the next spill opens it in the directory set by then. Any thread.
// ponytail: dead space waits for the file to empty; a free list if long sessions grow it.
void set_scratch_dir(const std::filesystem::path&);

// Pushes out now, on this thread, the raw pixels of every tile nothing pins, and zstd copies past their cap, as the
// budget would (after waiting for what is on its way out): for tests, and for low memory signals (P1). Any thread.
void trim_tiles();

// Memory a cache outside the store holds, counted in the tile budget: K2's mips, B2's atlas in VRAM (integrated GPUs
// take it from RAM). charge() what is not tiles, as it comes and goes: the tiles a cache keeps count already, wherever
// they are; it never blocks. When pushing tiles out cannot bring the store under budget (they are all pinned or out of
// RAM), its Background job calls release(want) on each user, without the store's lock: drop about `want` bytes of
// what you keep (entries someone else holds free nothing) and return how many. release() may drop tiles, pin them and
// charge(), never make or destroy a BudgetUser; destroy one without holding what its release() takes, since that
// waits for a release() running. The charge is given back on destruction. Any thread.
class BudgetUser {
 public:
  explicit BudgetUser(std::function<size_t(size_t want)> release = {});
  ~BudgetUser();
  BudgetUser(const BudgetUser&) = delete;
  BudgetUser& operator=(const BudgetUser&) = delete;
  void charge(ptrdiff_t bytes);  // negative to give back

 private:
  friend struct TileStore;  // tilecache.cpp
  std::function<size_t(size_t)> release_;
  ptrdiff_t charged_ = 0;  // under the store's lock
};

}  // namespace pb
