#pragma once
// Tiles hold every pixel: layers, masks, selections, the stroke being painted and composite caches are all grids of
// copy-on-write tiles, so undo, duplicating a layer, snapshots and saving in the background copy pointers only.
// Pixels are read and written through a Pin: A1 keeps them raw in RAM; K1 adds the memory budget, compression and
// scratch behind the same calls.
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <utility>

namespace pb {

// A tile is kTileSize x kTileSize pixels; nothing else sets the size (E4 weighs 7, 128 px, against 8).
inline constexpr int kTileLog2 = 8;
inline constexpr int kTileSize = 1 << kTileLog2;

// Samples are integers in the document's own encoding (not linear light), as in Photoshop's 8- and 16-bit modes;
// blending, mips and brushes compute on them as floats 0..1 of that encoding: value / max_value().
enum class Depth : uint8_t { U8, U16 };
inline int max_value(Depth d) { return d == Depth::U16 ? 65535 : 255; }
inline float to_unit(int v, Depth d) { return float(v) / float(max_value(d)); }
// Back to an integer as every CPU path stores a float: the nearest level, clamped (NaN gives 0).
inline uint16_t from_unit(float f, Depth d) {
  const float m = float(max_value(d));
  return uint16_t(f > 0 ? (f < 1 ? f * m + 0.5f : m) : 0.f);
}

// How a grid's pixels are laid out: channels interleaved, alpha last and not premultiplied (as PSD). 4: R, G, B,
// alpha; 2: grey, alpha; 1: a value without alpha (a mask, a selection, the stroke buffer). U16 samples are
// native-endian uint16_t.
struct Format {
  Depth depth = Depth::U8;
  int channels = 4;
  int bytes() const { return channels * (depth == Depth::U16 ? 2 : 1); }  // per pixel
  bool alpha() const { return channels > 1; }
};
inline bool operator==(Format a, Format b) { return a.depth == b.depth && a.channels == b.channels; }
inline bool operator!=(Format a, Format b) { return !(a == b); }

// One pixel's samples in its format's order and depth (0..255 or 0..65535); entries past its channels are 0.
using Pixel = std::array<uint16_t, 4>;

class Tile;
using TilePtr = std::shared_ptr<const Tile>;  // tiles are shared read-only; one lives while anything holds it

// Keeps a data tile's pixels raw in RAM while it lives (K1 never evicts a pinned tile): kTileSize rows of kTileSize
// pixels, rows kTileSize * Format::bytes() apart, 64-byte aligned. It does not own the tile: whoever holds the pin
// keeps the tile alive meanwhile. Move-only; any thread.
class Pin {
 public:
  Pin() = default;
  Pin(Pin&& o) noexcept : data_(std::exchange(o.data_, nullptr)) {}
  Pin& operator=(Pin&& o) noexcept {
    std::swap(data_, o.data_);
    return *this;
  }
  const uint8_t* data() const { return data_; }  // null for a solid tile

 protected:
  friend class Tile;
  explicit Pin(uint8_t* data) : data_(data) {}
  uint8_t* data_ = nullptr;
};

// The same with the pixels writable: from TileGrid::write(), or Tile::write() on a tile nothing else holds yet.
// Release it before copying the grid or handing the tile out: copies would see the writes.
class WritePin : public Pin {
 public:
  uint8_t* data() const { return data_; }

 private:
  friend class Tile;
  explicit WritePin(uint8_t* data) : Pin(data) {}
};

// A tile: solid (one value, no pixels allocated) or data (kTileSize² pixels). An empty tile is no Tile at all: its
// grid stores nothing there and reads fill(). Read-only once shared: grids, caches and threads hold it through
// TilePtr, and the last holder frees it.
class Tile {
 public:
  // A new data tile, every pixel `fill`, for its creator to write() before sharing it. Any thread.
  static std::shared_ptr<Tile> make(Format, Pixel fill = {});
  static TilePtr solid(Format, Pixel value);  // any thread
  ~Tile();
  Tile(const Tile&) = delete;
  Tile& operator=(const Tile&) = delete;

  Format format() const { return format_; }
  // Unique in the process and new whenever the content changes, never 0 (what caches key an empty tile by): a cache
  // keyed by ids never serves old pixels.
  uint64_t id() const { return id_; }
  bool is_solid() const { return solid_; }
  Pixel value() const { return value_; }  // a solid tile's
  Pin pin() const;                        // a data tile's pixels; any thread
  // A data tile's pixels to change, and a new id: only for its creator before sharing it (a grid's tiles change
  // through TileGrid::write(), which copies shared ones).
  WritePin write();

 private:
  friend class TileGrid;
  Tile(Format, Pixel, bool data);
  Format format_;
  bool solid_;
  Pixel value_{};
  uint64_t id_;
  uint8_t* data_ = nullptr;
  mutable std::atomic<bool> shared_{false};  // handed out by a grid copy or set(): never written in place again
};

// Tile (x, y) covers pixels x << kTileLog2 to (x + 1) << kTileLog2 across, likewise down; any sign.
struct TileXY {
  int x = 0, y = 0;
};
inline bool operator==(TileXY a, TileXY b) { return a.x == b.x && a.y == b.y; }
inline bool operator<(TileXY a, TileXY b) { return a.y != b.y ? a.y < b.y : a.x < b.x; }  // row by row
struct TileRect {  // tiles x0 <= x < x1, y0 <= y < y1
  int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};

// A sparse plane of tiles over document pixels of any sign (a layer spills past the canvas). Where no tile is
// stored ("empty") every pixel is fill(): transparent for a layer, white or black for a mask. Copying a grid copies
// pointers. One thread writes a grid (whoever made it or copied it); other threads read only the copies it published
// (shared_ptr<const TileGrid>), never the grid it goes on writing.
class TileGrid {
 public:
  explicit TileGrid(Format, Pixel fill = {});  // throws std::invalid_argument unless 1, 2 or 4 channels
  TileGrid(const TileGrid&);  // shares every tile: a later write to either grid copies the tile first
  TileGrid& operator=(const TileGrid&);
  TileGrid(TileGrid&&) = default;
  TileGrid& operator=(TileGrid&&) = default;

  Format format() const { return format_; }
  Pixel fill() const { return fill_; }
  const std::map<TileXY, TilePtr>& tiles() const { return tiles_; }  // the stored ones, row by row, none null
  const Tile* get(int tx, int ty) const;  // null where empty; valid while the grid holds the tile
  TileRect bounds() const;                // of the stored tiles, all 0 when none; O(tiles)
  // Stores t at (tx, ty), null for empty; t counts as shared from then on. Throws std::invalid_argument for a tile
  // of another format.
  void set(int tx, int ty, TilePtr t);
  // Tile (tx, ty) to change, with a new id. A data tile only this grid ever held (no copy of the grid, no set(), no
  // TilePtr outside it) is written in place; any other is copied first, a solid one into a data tile of its value,
  // an empty one of fill(). Leaves the tile as data (see compact()).
  WritePin write(int tx, int ty);
  // Pixels x, y, w, h (document pixels, any sign, across tiles) into dst in format(), rows `stride` bytes apart.
  void read_rect(int x, int y, int w, int h, uint8_t* dst, size_t stride) const;
  // The same from src, through write(), then compacts each tile it touched. For regions: every call scans the tiles
  // it touched once, so writing pixel by pixel is slow.
  void write_rect(int x, int y, int w, int h, const uint8_t* src, size_t stride);

 private:
  Format format_;
  Pixel fill_;
  std::map<TileXY, TilePtr> tiles_;
};

// t as a grid of this fill stores it best: null (empty) when every pixel is fill, a solid tile when all are alike,
// else t. With alpha, transparent pixels are alike whatever their colour, so a layer erased to alpha 0 empties.
// Scans a data tile once; any thread.
TilePtr compact(const TilePtr& t, Pixel fill);

// What the tile store holds, in bytes, for the status bar (U4) and tests: raw pixels of data tiles (K1 adds
// compressed, scratch, pinned). Any thread.
struct TileStats {
  size_t raw = 0;
};
TileStats tile_stats();

}  // namespace pb
