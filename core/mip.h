#pragma once
// Mip levels of tile grids, tile by tile: what the canvas shows below 100% (V2), the composite at level L > 0
// (composite_tile), thumbnails (U2), what .pbd keeps from level 3 on (M2). Built on demand and cached by content: a
// mip tile's key hashes the ids of the level-0 tiles under it, read straight off the grid, so a change to any of them
// is a new key (nothing to invalidate, never stale pixels) and a grid read from a file gives the keys of the mip tiles
// saved with it.
#include <cstddef>
#include <cstdint>

#include "tile.h"

namespace pb {

// Tile (tx, ty) of `grid` at mip level `level`, 0..31 (std::invalid_argument otherwise). Level 0 is the grid's own
// tile; level L > 0 covers level-0 tiles tx << L to (tx + 1) << L across, likewise down, each pixel the 2 x 2 average
// of level L - 1 weighted by alpha (premultiplied, averaged, divided back) so that transparent edges do not darken. In
// integers, halves rounding up, with A = a0 + a1 + a2 + a3:
//   alpha (A + 2) / 4, colour (2 (c0 a0 + c1 a1 + c2 a2 + c3 a3) + A) / 2A, 0 where alpha comes out 0;
//   without alpha (1 channel) (v0 + v1 + v2 + v3 + 2) / 4.
// Null where all of it is fill(), a solid tile where uniform, as compact() leaves a grid's tiles: empty and solid tiles
// below make empty and solid ones without allocating pixels. Built with the levels below it as needed and kept in a
// cache shared by all grids (the same tile, id and all, while it stays); the caller may hold it as long as it likes.
// Any thread, several at once, while `grid` stays as it is (a published copy).
TilePtr mip(const TileGrid& grid, int level, int tx, int ty);

// Builds what the cache lacks of levels 1 to `level` under `rect` (tiles of `level`), a level at a time with
// parallel_for, and returns when done: from a Visible job (tiles the view is about to show) or a Background one
// (parents of tiles a stroke changed), whose priority parallel_for's helpers take. Any thread, as mip().
void prefetch(const TileGrid& grid, int level, TileRect rect);

// Puts t (null: empty) in the cache as tile (tx, ty) of `grid` at `level`, 1..31, for M2 to hand over the mip tiles a
// .pbd saved: mip() then returns t without reading the tiles under it. Throws std::invalid_argument for a tile of
// another format. Any thread.
void attach_mip(const TileGrid& grid, int level, int tx, int ty, TilePtr t);

// The cache keeps its tiles alive within two byte caps, raw pixels plus a little per entry, least recently used out
// first: an eighth of tile_budget() (tilecache.h) for levels 1 and 2, a sixteenth for 3 and up, so that work at full
// size never evicts the small levels a whole-image view needs; each new entry reads the budget again. Its tiles are
// the store's as any, pushed out to zstd and scratch once cold; when the store is over budget with no tile left to
// push out it asks the cache (a BudgetUser) to let go of those nothing else holds, levels 1 and 2 first.
// ponytail: cold tiles of levels 3 and up leave RAM as any; a priority in the store if whole-image views of D2 wait
// on scratch.
// clear_mips() empties it: for tests that count builds and bytes from a clean start. Any thread.
void clear_mips();

uint64_t mips_built();  // tiles averaged from the level below so far (tests count rebuilds); any thread

}  // namespace pb
