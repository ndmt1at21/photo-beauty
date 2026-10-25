#pragma once
// Exact geometry of tile grids (T30): every pixel lands whole somewhere else, none is resampled. A layer, mask,
// selection or channel moved by whole pixels (Move's drop, Align, Distribute, a nudge, Move Selection), turned by
// quarters or mirrored with the canvas (Image Rotation's 90° and 180°, Flip Canvas, Edit > Transform's exact cases),
// cropped (Crop with Delete Cropped Pixels, Canvas Size, Trim, Image > Crop); and where a grid's pixels lie (Trim,
// Align, snapping to layers, Free Transform's box, Layer Edges). Every format: 1, 2 or 4 channels, U8 or U16.
// Tiles that stay whole stay shared: a shift by whole tiles only gives each tile another key, the same tile and id.
// Otherwise each tile out gathers its pixels from the up to four tiles under it, tiles in parallel: none where those
// are all empty, one of them where they are all solid of one value (no pixel allocated either way), else a new tile,
// compacted as a grid stores it (compact()).
#include <cstdint>
#include <optional>

#include "tile.h"

namespace pb {

// Document pixels x0 <= x < x1, y0 <= y < y1, as TileRect counts tiles; empty unless both sides are positive.
struct PixelRect {
  int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  bool empty() const { return x0 >= x1 || y0 >= y1; }
};
inline bool operator==(const PixelRect& a, const PixelRect& b) {
  return a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1;
}
inline bool operator!=(const PixelRect& a, const PixelRect& b) { return !(a == b); }

// Where place() moves each pixel: (x, y) to (y, x) when `transpose` (mirrored across the diagonal), else as it is;
// then to (-1 - x, y) when `mirror_x` (mirrored about x = 0, as a pixel spans x to x + 1) and to (x, -1 - y) when
// `mirror_y`; then on by (dx, dy). The eight symmetries of the pixel grid with a shift: every exact move of whole
// pixels. shift(), rotate(), flip() and crop() below are such placements; X31 builds its own for Edit > Transform's
// quarter turns and flips about a box (90° clockwise: transpose, mirror_x).
struct Placement {
  bool transpose = false, mirror_x = false, mirror_y = false;
  int dx = 0, dy = 0;
};

// The grid with every pixel moved as p says: the same format and fill, pixel (x, y)'s value read back where p puts
// it. With `clip` (where pixels land), only those landing within it: fill() elsewhere, no tile past it, the tiles
// across its edge cut. A tile out lying over one tile whole is that tile when p only shifts (so a shift by whole tiles
// without clip keeps every tile, reading none); where all it lies over is empty, or solid alike fill(), it is empty;
// where all is solid of another value, the solid tile it lies most over (the first along each axis on a tie), so that
// four quarter turns or two flips give such tiles back, ids and all. Neither reads a pixel nor allocates one; any other
// tile out is new, gathered from the up to four tiles under it, compacted. Throws std::out_of_range when pixels would
// land past int's range, as the functions below do. Tiles out in parallel (parallel_for); any thread, while `grid`
// stays as it is (a published copy, or the caller's own, unwritten meanwhile).
TileGrid place(const TileGrid& grid, const Placement& p, const std::optional<PixelRect>& clip = std::nullopt);
// Where p moves the pixels of r: all 0 for an empty r. Throws std::out_of_range as place().
PixelRect place(const PixelRect& r, const Placement& p);

// Pixel (x, y) to (x + dx, y + dy): Move, Align, Distribute, a nudge. By whole tiles (dx, dy multiples of kTileSize)
// every tile stays as it was, only under another key.
TileGrid shift(const TileGrid& grid, int dx, int dy);

// The canvas of width x height pixels from (0, 0) turned clockwise by `quarters` quarter turns (any integer, mod 4: 1
// is Image Rotation's 90° Clockwise, 2 its 180°, 3 or -1 its 90° Counter Clockwise), every pixel with it, past the
// canvas too: (x, y) goes to (height - 1 - y, x) for one, (width - 1 - x, height - 1 - y) for two, (y, width - 1 - x)
// for three; the canvas is height x width after an odd number. Four turns give the grid back.
TileGrid rotate(const TileGrid& grid, int quarters, int width, int height);
Placement rotation(int quarters, int width, int height);  // rotate()'s

// Flip Canvas Horizontal: (x, y) to (width - 1 - x, y); Vertical: (x, height - 1 - y). Two give the grid back.
enum class Mirror : uint8_t { Horizontal, Vertical };
TileGrid flip(const TileGrid& grid, Mirror, int width, int height);
Placement mirroring(Mirror, int width, int height);  // flip()'s

// The pixels within r moved so that its top left corner is (0, 0), fill() elsewhere: r as the new canvas, in the old
// one's pixels (Crop with Delete Cropped Pixels, Canvas Size's Background, Trim, Image > Crop). The pixels within the
// canvas of a selection moved by (dx, dy) (Move Selection, S31) are crop(selection, {-dx, -dy, width - dx,
// height - dy}): a move and a cut in one pass. Throws as place().
TileGrid crop(const TileGrid& grid, const PixelRect& r);

// The smallest rectangle holding every pixel that differs from fill() (with alpha, transparent pixels are alike
// whatever their colour, as compact() has them): a layer's pixels of any opacity, a mask's or a selection's other than
// its fill. Exact; all 0 when there is none. Only tiles along its edges are looked at: from each side in, the first
// column or row of tiles holding anything, its solid tiles whole without reading them, its data tiles scanned from
// that side until a pixel counts (in parallel). Any thread, as place().
PixelRect content_bounds(const TileGrid& grid);
// The same against `value` and within `area` only: of the pixels within it, those not alike value (empty tiles read
// as fill(), so that where fill() is not alike value they count whole). Trim's Top Left Pixel Color and Bottom Right
// Pixel Color on the canvas; content_bounds(grid) is this with fill() over the whole plane.
PixelRect content_bounds(const TileGrid& grid, Pixel value, const PixelRect& area);

}  // namespace pb
