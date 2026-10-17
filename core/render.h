#pragma once
// What the canvas shows: the composite, tile by tile at a mip level, and where it sits on screen. Declared here; B1
// implements composite_tile() (composite.cpp), V1 the View (view.cpp).
#include <array>
#include <memory>
#include <optional>

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

// What a composite reads: a document state and the stroke being painted, if any. Snapshots, read on any thread.
struct RenderInput {
  DocPtr doc;
  std::optional<StrokeOverlay> stroke;
};

// Tile (tx, ty) of the composite at mip level `level` (0 full size; a pixel of level L covers 2^L x 2^L document
// pixels, its tile (tx, ty) document pixels from tx << (kTileLog2 + L) across): RGBA U16, alpha not premultiplied,
// the visible layers bottom up over transparency, pixels outside the canvas transparent. Null where all of it comes
// out transparent, a solid tile where uniform; the caller owns the result. The CPU reference (exports, flattening,
// no GPU, tests); the GPU composite (B2, gpu_composite.h) means the same. Any thread, several at once.
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
};

}  // namespace pb
