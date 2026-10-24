#pragma once
// What a tile of the composite is made of, at any mip level: the input tiles and how they blend, bottom up, and the
// part of the tile on the canvas. composite_tile() (render.h, composite.cpp) runs the plan on the CPU; the GPU
// composite (B2, B3) runs the same one, so both read the same tiles.
#include <cstdint>
#include <vector>

#include "render.h"

namespace pb {

// Layer: `tile` over what is below in `blend` at `alpha`, the stroke's coverage `cover` painted or erased on its pixels
// first (render.h's StrokeOverlay). Push ... Pop: a group's steps in between go onto transparency, then blend as one
// layer (isolated), or onto a copy of what is below, then mix with it at `alpha` (`pass`: a Pass Through group below
// full opacity; at full opacity its layers come without Push and Pop).
struct CompositeStep {
  enum Type : uint8_t { Layer, Push, Pop } type = Layer;
  LayerId layer = 0;            // the layer's id, or the group's
  bool pass = false;            // Push, Pop
  Blend blend = Blend::Normal;  // Layer, Pop
  float alpha = 1;              // Layer, Pop: opacity x fill, 0 < alpha <= 1
  TilePtr tile;                 // Layer: its pixels at the level, or the preview's; null where none and the stroke paints
  TilePtr cover;                // Layer: the stroke buffer's tile at the level when the stroke is on this layer
};

struct CompositePlan {
  std::vector<CompositeStep> steps;    // bottom up; none: the tile comes out transparent
  int x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // the tile's pixels on the canvas: x0 <= x < x1, y0 <= y < y1 within the tile
  int depth = 0;                       // Push nested at most
};

// The plan of tile (tx, ty) at mip level `level` (as composite_tile()): each layer's tile is mip(pixels, level, tx, ty)
// (mip.h; at level 0 the layer's own), a text or shape layer's as a pixel layer's, but where RenderInput::preview has
// a tile of the layer at the level: that one (none where it is transparent and solid). The stroke buffer's likewise.
// Hidden layers, layers at opacity or fill 0 or without pixels there and groups of none leave no steps. The canvas at
// level L is ceil(width / 2^L) x ceil(height / 2^L) pixels, a pixel partly on it counting. Builds the mip tiles the
// cache lacks. Throws std::invalid_argument for no document, a level outside 0..31, a stroke buffer other than
// 1-channel U16, a preview's tiles not in pixel_format(). Any thread.
// Not yet (the props and masks are kept, not composited): adjustment and fill layers leave no steps until D40; masks,
// clipping, Blend If and Channels come with B34, layer styles with L34. RenderInput::warp is left alone until X30:
// from then on a layer it lists composites the tile warp_tile() makes of it (warp.h), its mask too where linked.
CompositePlan composite_plan(const RenderInput&, int level, int tx, int ty);

}  // namespace pb
