#pragma once
// photo-beauty's own file, .pbd (M2; its layout in docs/architecture.md): each data tile a zstd frame, the layer tree
// with each layer's table of tiles in JSON, a composite for the first frame and thumbnails, and at the end an index of
// every chunk with its CRC32, then a footer. Saving reads a snapshot while the document goes on changing; opening
// reads the footer, the index, the JSON, the ICC profile and the composite, and leaves the tiles in the file, mapped
// read-only, until something pins them.
#include <cstdint>
#include <filesystem>
#include <functional>
#include <vector>

#include "doc.h"
#include "jobs.h"

namespace pb {

// The composite a .pbd keeps, for the canvas to show until tiles composite, and for thumbnails: mip level `level` of
// the composite (render.h), the first 2048 pixels or fewer both ways, so that a pixel covers 2^level x 2^level
// document pixels and the canvas ceil(width / 2^level) x ceil(height / 2^level) of them; flattened over white, 8 bits
// in the document's encoding.
struct Preview {
  int level = 0, width = 0, height = 0;
  int channels = 0;             // 3, RGB; 1, grey
  std::vector<uint8_t> pixels;  // rows top down, width x channels bytes each
};

struct OpenedPbd {
  DocPtr doc;
  Preview preview;
  uint64_t bytes_read = 0;  // of the file: header, footer, index, JSON, ICC profile, preview
};

// The document in the .pbd at `path`, and its preview. Its data tiles are Tile::elsewhere() ones on the file mapped
// read-only (mapped while any of them lives), read when first pinned: pin() throws std::runtime_error for one damaged
// in the file, as its zstd frame carries a checksum. Mip tiles from level 3 on come attached (mip.h), so composites
// at those levels read no full-size tile. Each tile is remembered with its place in the file, for a later save to
// copy (and M3's to keep) its frame. Throws DecodeError (open.h), what() "<path>: <reason>": Corrupt for a file
// damaged, cut short, or not a .pbd; Codec, `codec` "pbd_version", for a format newer than this one. Any thread.
OpenedPbd open_pbd(const std::filesystem::path& path);

struct SaveOptions {
  CancelToken cancel;                    // once set, the save stops soon, throwing Cancelled
  std::function<void(double)> progress;  // the part done, 0 to 1, after each batch of tiles, on the saving thread
};

// Writes `doc` at `path` as a .pbd: into a new file beside it, flushed to the disk before and after its footer, then
// moved over `path`, so that `path` is either as it was or the whole new file. Frames of tiles still as some .pbd
// opened here holds them are copied; the other tiles are compressed on the pool, with the mip tiles from level 3 on.
// Blocks until done: call it from a Background job to save while the UI goes on (handing over a snapshot is all the
// UI thread does), or straight (bench, tests). Throws Cancelled (`path` untouched, the new file removed),
// std::invalid_argument for a state validate() refuses, std::runtime_error (std::system_error) when a file cannot be
// written or a tile cannot be read back. Saving over the .pbd a living document was opened from replaces it, Windows
// too; the document reads on from the old file, still mapped. Any thread.
void save_pbd(const DocState& doc, const std::filesystem::path& path, const SaveOptions& options = {});

}  // namespace pb
