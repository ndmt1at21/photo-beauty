#pragma once
// Gaussian blur of tile grids (S30): Feather Selection here; the blur of Gaussian Blur (F32), mask Feather (B36), Drop
// Shadow (L32), frequency separation (H36) and skin smoothing (H38). Separable, across then down. Up to kDirectSigma
// the kernel is the sampled Gaussian out to 3 sigma, renormalised. Above it, kBoxes box filters in a row with that
// kernel's variance (each box's two end taps weighted for a width between integers), each a running sum, so that a
// pass costs the same per pixel whatever sigma: on hard edges, thin bars and corners they stay within 0.75% of the
// full range of the direct kernel (three boxes, the textbook count, reach 1.65% on corners and bars). With alpha (2
// and 4 channels) colours count by it: premultiplied, blurred with alpha, divided back, so that transparent pixels do
// not darken edges (as mip.h). Every pixel out is a function of the pixels about it alone, in a fixed order of
// operations (direct taps in one order, box sums as exact integers, rounded to 16 bits between the two passes), so the
// result does not depend on how the plane is cut into tiles nor on the threads; a uniform neighbourhood comes out as
// it is without being filtered. A region at a time (gauss_pixels(), gauss_tile()) a tile reads (256 + 2 margin)^2
// pixels, so that its cost grows with sigma squared once the margin passes the tile; gauss() with boxes goes by strips
// of tiles instead (see there), each pixel through each pass about once whatever sigma.
#include <cstddef>
#include <cstdint>

#include "tile.h"

namespace pb {

// Photoshop's Radius (Feather, Gaussian Blur, mask Feather, Drop Shadow's size, ...) as the Gaussian's sigma, px:
// sigma = kSigmaPerRadius x radius, one function for all of them. A hypothesis: Adobe publishes no formula. Users
// measured Select > Modify > Feather of r px as 50% on the selection's edge, tapering over 2.7 r on either side
// (Adobe Community, "How do I create a feathered square similar to Illustrator?", posts of 25 April 2009): a
// Gaussian of sigma r is within one 8-bit level of its ends at 2.7 sigma (Phi(2.7) = 99.65%). S39 (Feather) and F31
// (Gaussian Blur) compare it with Photoshop's own images; B38 settles it.
inline constexpr double kSigmaPerRadius = 1;
inline double sigma_of_radius(double radius) { return kSigmaPerRadius * radius; }

inline constexpr double kDirectSigma = 8;  // up to this sigma the kernel is the sampled Gaussian itself
inline constexpr int kBoxes = 6;           // box filters along each axis above it
inline constexpr double kMaxSigma = 4000;  // what the integer box sums hold exactly

// How far past each side of the pixels it writes a Gaussian of sigma reads, px: ceil(3 sigma) up to kDirectSigma,
// the boxes' reach above (about 4.2 sigma: 210 at 50, 4188 at 1,000); 0 for sigma 0 (no blur). What a filter adds
// round a tile (F30) and what a cache keyed by the tiles under one (B36) looks at. Throws std::invalid_argument for
// sigma below 0, above kMaxSigma or NaN, as every function here. Any thread.
int gauss_margin(double sigma);

// What a blur reads past the canvas, and which tiles it writes. No canvas (width or height 0): the grid's own pixels
// everywhere, its tiles and fill() between them; tiles written wherever the blur reaches. A canvas of width x height
// pixels from (0, 0): pixels past it read as the nearest pixel on it (`extend`: edges replicated, as Feather without
// Apply effect at canvas bounds) or as `outside` (Feather with it: 0, not selected), whatever the grid holds there;
// only the tiles over the canvas are written (a selection has none past it, A40), their pixels past its edge blurred
// alike.
struct GaussEdges {
  int width = 0, height = 0;
  bool extend = true;
  Pixel outside{};
};

// The kernel on pixels in memory, as F30's filters get them: `in` holds (w + 2 m) x (h + 2 m) pixels of the format,
// m = gauss_margin(sigma), rows in_stride bytes apart; out gets the w x h pixels at their centre blurred, rows
// out_stride bytes apart (not overlapping in). Throws std::invalid_argument for a format without 1, 2 or 4 channels.
// Any thread.
void gauss_pixels(Format, const uint8_t* in, size_t in_stride, int w, int h, double sigma, uint8_t* out,
                  size_t out_stride);

// Tile (tx, ty) of the blurred grid, compacted as a grid stores it (null where all fill(), solid where uniform): the
// tile `grid` holds there itself where everything within the margin about it is alike and so is it, a solid tile
// where all of that is one value, without filtering or allocating pixels either way. For whoever keeps blurred tiles
// (B36's mask Feather, a level L blurred by sigma / 2^L in a grid of mip tiles). A tile reads its whole region, so
// that this suits sigmas whose margin is about a tile or less; working memory 2 (256 + 2 margin) x 256 x channels
// doubles (2.8 MB at sigma 50, one channel). Any thread, while `grid` stays as it is (a published copy).
TilePtr gauss_tile(const TileGrid& grid, double sigma, int tx, int ty, const GaussEdges& = {});

// The grid blurred: a new grid of its format and fill, sharing the tiles that stay as they were, the same pixels as
// gauss_tile() would give each tile, to the bit. Up to kDirectSigma tiles in parallel (parallel_for), each as
// gauss_tile(). Above, tiles whose neighbourhood is uniform come out as gauss_tile() makes them; the others go by
// strips: the pass across along runs of tiles of a tile row, the margin read once at either end of a run, into a grid
// of 16-bit tiles of the store (premultiplied) for the tile rows the pass down reads; then the pass down from it along
// runs of a tile column; runs in parallel. Its memory: that grid, 2 bytes a channel for each pixel of those rows, and
// two lines of 16 doubles a pixel along a run with its margins for each thread (4.3 MB at sigma 1,000 across D1). Sigma
// 0 copies the grid (the tiles over the canvas, given one). Any thread, as gauss_tile().
TileGrid gauss(const TileGrid& grid, double sigma, const GaussEdges& = {});

// Select > Modify > Feather (Shift F6): the selection (a 1-channel grid, fill 0, A40) blurred by a Gaussian of sigma
// sigma_of_radius(radius), Feather Radius 0.1 to 1,000 px in Photoshop's dialog (0 gives the selection back as it is).
// Apply effect at canvas bounds off: what is selected up to the canvas's edge stays so there, its pixels extended
// past it; on: past the canvas is not selected, so the selection fades towards the edge (50% on it) as anywhere else.
// The command and its History step ("Feather") are S31's and N44's. Throws std::invalid_argument for a grid with
// alpha or several channels, a radius below 0, above kFeatherMax or NaN, a canvas without pixels. Any thread, as
// gauss().
inline constexpr double kFeatherMax = 1000;
TileGrid feather_selection(const TileGrid& selection, double radius, int width, int height, bool at_canvas_bounds);

}  // namespace pb
