#pragma once
// The documents the benchmarks measure on (plan.md, Chỉ tiêu hiệu năng), built in RAM from fixed seeds: the same
// document on every run, but for the photo they start from. `pb-bench make` (make.cpp) builds one, prints what it
// holds and saves it as a .pbd (kDocs); E3, M3, B3 and C4 build them here, or open those files.
#include <cstddef>
#include <filesystem>

#include "doc.h"

namespace pb::bench {

// Where the sample photos are (not in git; tools/configure.sh links each worktree's samples/ to these): photo16.tif, a
// 16-bit RGB TIFF exported from the raw, and the D850's raw, whose embedded full-size JPEG stands in for it. Without
// them (CI) a synthetic photo, smooth colours and grain, takes their place. make_*() print which they took.
inline const char* const kSamples = "/home/ndmt1at21/photo-beauty/samples";
// Where `pb-bench make` saves them by default (not in git): d1.pbd, d2.pbd, d2mini.pbd, d4.pbd.
inline const char* const kDocs = "/home/ndmt1at21/photo-beauty/build/docs";

// D1, a standard portrait: 8256 x 5504 (the photo's size), 16 bits, sRGB (photo16.tif's own profile), 15 layers. Bottom
// first, with their ids:
//   1 Background: photo16.tif; else the raw's JPEG lifted to 16 bits as v x 257 plus noise of up to half an 8-bit level
//     hashed from the place, so that the low bytes are as random as a 16-bit photo's.
//   2 Frequency Separation, a group (Normal: isolated) of
//     3 Low: the photo blurred, a Gaussian of sigma kLowSigma px over kLowRadius px, its edges extended;
//     4 High (Linear Light): (photo - Low) / 2 + 1/2, so that the group gives the photo back to a level of 16 bits.
//   5 Heal, 6 Clone: hard-ish strokes of the photo's own pixels from elsewhere, as a clone source lays them, on the
//     face (an ellipse about the centre), 20% of the canvas each;
//   7 Dodge, 8 Burn (Soft Light): soft strokes of white, black at a low flow there, 20% each;
//   9 Skin (Normal, 50%): soft strokes of colours taken from the photo where they start, 20%.
//   10 Curves (Soft Light), 11 Hue/Saturation (Overlay), 12 Color Balance (Soft Light), 13 Levels (Multiply),
//     14 Vignette (Multiply, round the edges): pixel layers standing for adjustment layers and their masks (P1): one
//     colour under a few large soft strokes, 25 to 45% each;
//   15 Text: a signature's hard strokes in a corner, standing for type (P1).
// Strokes 5 to 15 are synthetic pen events with pressure through C2's Stroke, each merged at pen-up. Burn, mid-stack, is
// selected. 5,440 tiles, 2.7 GB of pixels; built in some 15 s.
DocPtr make_d1(const std::filesystem::path& samples = kSamples);
constexpr double kLowSigma = 8;
constexpr int kLowRadius = 24;

// D2, larger than RAM: 20,000 x 15,000, 8 bits, sRGB, 40 pixel layers covering 30% of it on average (14 GB of pixels,
// 75,000 tiles of 19 GB): 1 Background, a mosaic of pieces of the photo over all of it, then layers of pieces with edges
// faded over 0 to 40 px, apart from each other, over 10 to 46% each, some in Multiply, Screen, Overlay or Soft Light.
// Pieces from the raw's JPEG decoded at 1/2 and 1/4 of its size (TurboJPEG's scaling). Built under the tile budget:
// tiles go out to zstd and scratch as they come (set_tile_budget(), set_scratch_dir() first). mini: D2-mini, for CI,
// 5000 x 3750 (1/16 of the area), the same layers and pieces at a quarter of the size, cropped from the same pictures:
// 8,000 tiles of 2 GB, more of them at pieces' edges.
DocPtr make_d2(bool mini, const std::filesystem::path& samples = kSamples);

// D4 as P0 has it: 6000 x 4000, 8 bits, sRGB, 1000 small pixel layers standing for what P1 brings: 300 type layers
// ("Text n": lines of glyph-like marks), 300 shapes ("Shape n": ellipses and rounded rectangles, anti-aliased, a few
// bands across), 100 smart objects ("Smart Object n": pieces of the photo at 1/4) and 300 painted layers ("Layer n":
// soft blobs), mostly Normal, a few hidden; in groups ("Group n", Pass Through but a fifth) nested 6 deep. Layer
// styles wait for P1. 9,250 tiles, 2 GB of pixels.
DocPtr make_d4(const std::filesystem::path& samples = kSamples);

// The process's resident memory now and at its peak, bytes; 0 where unknown. What the tile budget bounds.
size_t resident_bytes();
size_t peak_resident_bytes();

}  // namespace pb::bench
