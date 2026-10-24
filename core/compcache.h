#pragma once
// The composite as tools read it (K30): Magic Wand, Color Range and Clone Stamp with Sample All Layers, the Eyedropper,
// Copy Merged, Merge and Stamp Visible, Select Subject, face detection, the Histogram and the Navigator. Tiles of
// composite_tile() (render.h) kept in a cache in the tile store: raw, zstd or scratch as any tile (tilecache.h),
// counted in the budget and in tile_stats(). A tile's key is its plan (composite_plan(), composite.h: the ids of the
// tiles it reads, their layers' modes and alphas) with its place, as GpuComposite's cache keys its tiles, so an edit
// composites again only the tiles it changed and undo finds what it had. A Background job fills it ahead once the
// document has stood still for a second (Prefill).
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>

#include "gpu.h"
#include "jobs.h"
#include "render.h"

namespace pb {

// Tile (tx, ty) of the composite at mip level `level`: composite_tile()'s, to the bit (null where it comes out
// transparent, solid where uniform), out of the cache, else composited on this thread and kept. Shared with the cache:
// hold it as long as you like. With a stroke being painted (RenderInput::stroke) the moment's composite, not kept:
// tools read the document. Throws as composite_tile(). Any thread, several at once.
TilePtr composite_cached(const RenderInput&, int level, int tx, int ty);

// The tiles of mip level `level` over the canvas, ceil(width / 2^level) x ceil(height / 2^level) pixels: from (0, 0),
// none for an empty canvas. Throws std::invalid_argument for a level outside 0..31.
TileRect canvas_tiles(const DocState&, int level);
// The tiles of `level` under a view: the box around its corners in document pixels (View::to_doc()), past the canvas
// too; none for an empty view. For Prefill::set() (level 0). Throws as canvas_tiles().
TileRect view_tiles(const View&, int level);

// Tiles `rect` of the composite at `level` (tiles of the level; those past the canvas left out) as a grid, RGBA U16,
// transparent where empty: composite_cached()'s, what the cache lacks composited on the GPU set_composite_gpu() gave
// (a GpuComposite of its own: kGridBatch tiles at a time, each lot under the device's lock, downloaded into tiles; the
// CPU's from a lot it throws on: a stack deeper than its atlas holds, the device lost), else with composite_tile() on
// the pool's threads; a stroke's not kept, as composite_cached(). The whole image: canvas_tiles(). Throws Cancelled
// once `cancel` is set (it looks between tiles), else as composite_tile(). Any thread, several at once, but not one
// holding the device's lock.
TileGrid composite_grid(const RenderInput&, int level, TileRect rect, const CancelToken& cancel = {});

// Tiles composite_grid() sends to the GPU at a time: the frames wait for the device while a lot composites and comes
// back. Its GpuComposite takes 4 x kGridBatch tiles of device memory at most (32 MB), in K1's tile budget where the
// device's memory is RAM, as the frames' does.
inline constexpr int kGridBatch = 16;

// The GPU composite_grid() composites on: `dev`, which whoever else uses it (the canvas's frames) records on only
// holding `lock`, as composite_grid() does; null for the CPU. Waits for composites on the GPU set before, then frees
// what it held there (under its lock): the device outlives it, so set null before destroying one. Not while holding
// either lock. Any thread.
void set_composite_gpu(gpu::Device* dev, std::mutex* lock);

// Fills the cache ahead of the tools: once the document has stood still for `still`, a Background job composites level
// 0 of it, the tiles a view shows first, then the rest of the canvas row by row, as many as the cache keeps (a quarter
// of tile_budget()); on the CPU, the device left to the frames. A change cancels it at its next tile and waits again.
// The core knows no UI: whoever shows the document tells it what it is, where the view is and when it last changed.
// ponytail: level 0 only, the tools' (the levels Select Subject, faces, Histogram and Navigator read are a fraction of
// it, made when asked).
class Prefill {
 public:
  using Clock = std::chrono::steady_clock;
  explicit Prefill(Clock::duration still = std::chrono::seconds(1));
  ~Prefill();  // cancels the job and waits for it to stop
  Prefill(const Prefill&) = delete;
  Prefill& operator=(const Prefill&) = delete;

  // The document as it is now (null: none, nothing to fill), when it last changed (a command, undo, a stroke begun: the
  // fill would take the threads from it), and the tiles of level 0 a view shows (view_tiles(view, 0); past the canvas
  // is left out). Cancels the job of the call before, at its next tile, unless all three are the same; the next job
  // starts once `changed` is `still` past (at once if it is already). UI thread (any, one at a time).
  void set(DocPtr doc, Clock::time_point changed, TileRect visible);

  // For tests: until the job of the last set() has filled all it fills (true), or `most` passes (false).
  bool wait(Clock::duration most);

 private:
  struct State;
  std::shared_ptr<State> s_;
  std::thread timer_;  // waits for the document to stand still, then submits the job
};

// Empties the cache: for tests and benches that count composites and bytes from a clean start. Any thread.
void clear_composites();

// Tiles the cache composited so far, on the CPU or the GPU, a stroke's included (tests count recomposites); any thread.
uint64_t composites_built();

}  // namespace pb
