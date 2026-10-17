#pragma once
// Opening a JPEG, PNG or TIFF as a document (M1). The file's own samples are decoded here, as stored: no linear
// light, no alpha over white.
#include <filesystem>
#include <stdexcept>
#include <string>

#include "doc.h"

namespace pb {

// Why a file does not open; what() is "<path>: <reason>".
struct DecodeError : std::runtime_error {
  enum class Kind {
    Codec,    // stored in a way no decoder here reads: `codec` says which
    Corrupt,  // damaged, cut short, or not a file of a kind this opens
    TooBig,   // past the size or memory limits
  } kind;
  std::string codec;
  DecodeError(Kind k, std::string codec, const std::string& what) : std::runtime_error(what), kind(k), codec(std::move(codec)) {}
};

// A new document of the image at `path`, its kind told by its signature whatever its name says. Grey or RGB by the
// file's channels, with alpha or without (then opaque); 8 or 16 bits by its samples, PNG's 1, 2 and 4 bits scaled to
// 8 and its palettes looked up into RGB at 8. Samples as stored: alpha kept (TIFF's premultiplied made straight),
// TIFF's MinIsWhite turned over, no gamma or colour conversion; the file's ICC profile byte for byte, sRGB's
// (icc_profile() of color.h) where it has none. One pixel layer, "Background", selected; its empty and solid tiles
// compacted. EXIF orientation waits for P1.
// Throws DecodeError, what() "<path>: <reason>": TooBig past 65,000 px a side or 512 MP (read from the
// header, before allocating), Corrupt for a damaged or cut short file or one of no such kind, Codec for a variant it
// does not open, `codec` saying which ("jpeg_cmyk", "jpeg_ycck", "jpeg_12bit", "tiff_cmyk", "tiff_lab", "tiff_float",
// "tiff_32bit", "tiff_photometric_3" for a palette...). Any thread: decoding runs on it, tiles on the pool.
DocPtr open_image(const std::filesystem::path& path);

}  // namespace pb
