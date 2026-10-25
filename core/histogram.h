#pragma once
// Histograms (D31): how many pixels of a grid lie at each of 256 levels of R, G, B and their luminosity, as
// Photoshop's Histogram panel (V32, N51) and the Levels, Curves and Threshold dialogs (D33, D38, N37) show them, and as
// Auto Tone and Equalize read them (D41). Counted at a mip level, as Photoshop counts its "cached data": where the
// layer or the composite has the level built (a view at that zoom), a histogram costs little.
#include <array>

#include "tile.h"

namespace pb {

// The luminosity's weights of R, G, B, in percent: Rec. 601's luma rounded (0.299, 0.587, 0.114), the weights cited
// for Photoshop's Luminosity and its Grayscale (Photoshop Gurus' forum, "Simple extraction of luminosity from image";
// Scott Valentine, "How Photoshop translates RGB color to gray", KelbyOne Insider). Adobe documents none: a hypothesis
// until Photoshop's numbers for a known image (D32, V32) say.
inline constexpr unsigned kLumPercent[3] = {30, 59, 11};

struct Histogram {
  enum Channel { Red, Green, Blue, Luminosity };
  // By channel, then level 0..255: the pixels there, each counted at its weight (its alpha times the selection's value
  // there: 1 for an opaque pixel wholly selected).
  std::array<std::array<double, 256>, 4> counts{};
  double total = 0;  // the weights summed: what every channel's levels add up to
};

// The pixels of `grid` at mip level `level` (mip.h: a pixel of level L covers 2^L x 2^L document pixels) within the
// document pixels x, y, w, h (the canvas, for most), a pixel partly within counting, as the composite counts the
// canvas's (render.h). A composite's tiles made at level L (composite_tile()) are a grid of their own: count them at
// level 0 with the region in that level's pixels, and a selection made of its mips likewise.
//   Levels: an 8-bit sample as it is, a 16-bit one at the nearest 8-bit level (v x 255 / 65535, rounded);
//   luminosity: the kLumPercent mix of the samples, rounded, then levelled. A grey grid (2 channels: grey and alpha;
//   1: a mask's value) counts its grey in all four.
//   Weights: each pixel its alpha (1 without), times the selection's value at the level (mip() of `selection`, 1
//   channel, 0 to its maximum; null: everything): a soft selection counts its pixels in part, a transparent pixel not
//   at all. Summed exactly, so alike in any order: tile by tile in parallel (parallel_for).
// Throws std::invalid_argument for a level outside 0..31 or a selection of more than a channel. Any thread, as mip().
Histogram histogram(const TileGrid& grid, int level, int x, int y, int w, int h, const TileGrid* selection = nullptr);

}  // namespace pb
