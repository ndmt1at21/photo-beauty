#pragma once
// What the canvas shows: the composite, tile by tile at a mip level, and where it sits on screen. Declared here; B1
// implements composite_tile() (composite.cpp), V1 the View (view.cpp); A30 adds a filter's preview and the transform
// being dragged to what a composite reads.
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "doc.h"
#include "tile.h"

namespace pb {

// The stroke being painted, composited on its layer until pen-up merges it (stroke.h): painting lays `color` at
// alpha opacity x B over the layer's pixels (Normal), erasing scales their alpha by 1 - opacity x B.
struct StrokeOverlay {
  LayerId layer = 0;
  std::shared_ptr<const TileGrid> buffer;  // coverage B: 1 channel U16, fill 0, B = value / 65535
  std::array<float, 3> color{};            // the document's encoded values, 0..1 (grey: color[0])
  float opacity = 1;                       // 0..1
  bool erase = false;
};

// A filter's preview (F30): a layer's tiles at one mip level, worked out for the dialog, in place of its own there.
struct LayerPreview {
  LayerId layer = 0;  // a pixel, text or shape layer
  int level = 0;      // of `tiles`: at other levels the layer shows its own
  // pixel_format() of the document, tile (tx, ty) of the level: each in place of the layer's tile there. Where it
  // stores none the layer's own shows, so a preview that clears a tile stores a transparent solid one.
  std::shared_ptr<const TileGrid> tiles;
};

// Photoshop's interpolation methods (Free Transform's, Image Size's; X30 and T33 share the kernels). Automatic is
// Bicubic Automatic: Bicubic Smoother enlarging, Bicubic Sharper reducing.
enum class Interp : uint8_t { Nearest, Bilinear, Bicubic, BicubicSmoother, BicubicSharper, Automatic };

// Warp's mesh (X32) over the layers' content: cols x rows patches, each a bicubic Bezier surface. At rest, patch
// (i, j) is the rectangle xs[i]..xs[i + 1] by ys[j]..ys[j + 1] (both increasing); `points`, the (3 cols + 1) x
// (3 rows + 1) control points row by row, are where the warp left them. A patch's point (s, t) at rest (0..1 across,
// down) goes to its surface's point (s, t). No patches, no mesh.
struct BezierMesh {
  int cols = 0, rows = 0;
  std::vector<double> xs, ys;  // cols + 1, rows + 1
  std::vector<std::array<double, 2>> points;
};

// Layers shown transformed before the transform is applied (Free Transform, Warp, Move, Liquify), by an inverse map
// from each document point to the point of the layers' content it shows: q = matrix x (x, y, 1), divided by its third
// coordinate; then the point that the mesh took to q, or q moved by the field there (one of them at most). Points in
// View's document coordinates: pixel (i, j) covers [i, i + 1) x [j, j + 1) and shows its centre's. X30 composites it.
struct LayerWarp {
  // Transformed together (selected, linked): a group stands for what it holds; their masks go along where linked.
  std::vector<LayerId> layers;
  std::array<double, 9> matrix{1, 0, 0, 0, 1, 0, 0, 0, 1};  // row by row, document to content: Free Transform's inverse
  BezierMesh mesh;
  // Liquify's backward displacement, x and y in pixels: 1-channel U16 grids of IEEE half floats, fill 0, at half
  // size: their pixel (i, j) holds it at the point (2i + 1, 2j + 1), bilinear between; null: none.
  std::array<std::shared_ptr<const TileGrid>, 2> field;
  Interp interp = Interp::Automatic;
  // The selection that floats (Free Transform, Move on a pixel layer with one): mask_format(), where the transform
  // began. The layers' pixels times it move, the rest stays; where they were a Background (A40) shows
  // background_color, other layers nothing. Null: all of them move.
  std::shared_ptr<const TileGrid> floating;
  std::array<float, 3> background_color{};  // the document's encoded values, 0..1 (grey: [0])
};

// What a composite reads: a document state; the stroke being painted, a filter's preview, layers being transformed,
// if any. Snapshots, read on any thread.
struct RenderInput {
  DocPtr doc;
  std::optional<StrokeOverlay> stroke;
  std::optional<LayerPreview> preview;
  std::optional<LayerWarp> warp;
};

// Tile (tx, ty) of the composite at mip level `level` (0 full size; a pixel of level L covers 2^L x 2^L document
// pixels, its tile (tx, ty) document pixels from tx << (kTileLog2 + L) across): RGBA U16, alpha not premultiplied,
// the visible layers bottom up over transparency (what of P1's it composites yet: composite.h), pixels outside the
// canvas transparent. Null where all of it comes out transparent, a solid tile where uniform; the caller owns the
// result. The CPU reference (exports, flattening, no GPU, tests); the GPU composite (B2, gpu_composite.h) means the
// same. Any thread, several at once.
TilePtr composite_tile(const RenderInput&, int level, int tx, int ty);

// Where the canvas shows the document. Canvas coordinates: logical pixels (Qt's item coordinates) from the canvas's
// top left, `dpr` device pixels each; document coordinates: pixels from the canvas's top left, pixel (i, j)
// covering [i, i + 1) x [j, j + 1). A value: the UI thread changes it, frames get copies.
struct View {
  double x = 0, y = 0;           // the document point at the canvas's centre (pan)
  double zoom = 1;               // device pixels per document pixel, 0.01..128: 1 is Photoshop's 100%
  double angle = 0;              // the view's rotation, degrees clockwise
  double width = 0, height = 0;  // the canvas, logical pixels
  double dpr = 1;                // device pixels per logical pixel (QWindow::devicePixelRatio)

  std::array<double, 2> to_canvas(double doc_x, double doc_y) const;
  std::array<double, 2> to_doc(double canvas_x, double canvas_y) const;
  int level() const;  // the mip level frames show: floor(log2(1 / zoom)), not below 0

  // A frame of the canvas in device pixels, width x dpr by height x dpr rounded, stretched over it: pixel (i, j)
  // shows the document point at its centre, origin + i * across + j * down. What kernels step by.
  struct Pixels {
    int width = 0, height = 0;  // 0 x 0 for an empty canvas
    std::array<double, 2> origin{}, across{}, down{};
  };
  Pixels pixels() const;
};

// A test card the canvas shows in place of the document (V1: DocCanvas with PB_CANVAS=pattern), of the document pixel
// (x, y) under each device pixel's centre: red (x >> 4) & 255, green (y >> 4) & 255, blue a checkerboard of squares 8
// grid steps wide; grid lines along the document's pixel edges every 2^grid pixels, the fewest that keep them 4 device
// pixels apart (every edge from 400 % on), 1 device pixel wide, in the inverse colours. Pixel (i, j) of a frame of
// View::pixels(): packed RGBA8, R in the low byte, opaque. shaders/pattern.slang draws the same to the bit from
// pattern_consts(), its push constants.
struct PatternConsts {
  float origin[2], across[2], down[2];  // View::pixels(), in single precision
  int32_t grid;                         // grid lines every 2^grid document pixels
  int32_t width, height;                // the frame, device pixels
};
PatternConsts pattern_consts(const View&);
uint32_t pattern_pixel(const PatternConsts&, int i, int j);

}  // namespace pb
