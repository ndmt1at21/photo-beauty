// .pbd format 2 (pbd.h, M3b): predict() (tile.h) writes what tile.h says byte for byte (a sample-by-sample copy here),
// and unpredict() gives a 16-bit tile back sample for sample (noise over all 16 bits, one value, 0 and 65535 alone and
// by turns, a ramp with grain; 1, 2 and 4 channels), the ramp's frame less than half its plain one; a predicted frame
// as Tile::elsewhere() takes it pins to those pixels. Documents (16-bit RGB and grey, 8-bit RGB; data, solid and empty
// tiles, tiles over the canvas's edges and past them): a full save writes format 2 and opens exact; a format 1 file, as
// apps before M3b wrote, opens exact, takes appends in format 1, and saved elsewhere becomes format 2; a format 2 file
// takes appends, every byte before kept. A save that cannot move its file into place leaves nothing behind.
#include <zstd.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <random>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unordered_set>
#include <vector>

#include "check.h"
#include "doc.h"
#include "mip.h"
#include "pbd.h"
#include "tile.h"

using namespace pb;
namespace fs = std::filesystem;

namespace {

size_t tile_bytes(Format f) { return size_t(kTileSize) * kTileSize * size_t(f.bytes()); }

// A tile's samples: 0 noise over all 16 bits, 1 every sample `seed`, 2 0 and 65535 by turns (the largest steps both
// ways), 3 a ramp with a little grain.
std::vector<uint8_t> samples(Format f, int kind, uint32_t seed) {
  std::vector<uint16_t> s(tile_bytes(f) / 2);
  std::mt19937 rng(seed);
  for (size_t i = 0; i < s.size(); i++) {
    const size_t px = i / size_t(f.channels), x = px % kTileSize, y = px / kTileSize;
    s[i] = kind == 0   ? uint16_t(rng())
           : kind == 1 ? uint16_t(seed)
           : kind == 2 ? uint16_t((x + y + i % size_t(f.channels)) % 2 ? 65535 : 0)
                       : uint16_t(x * 150 + y * 60 + i % size_t(f.channels) * 900 + rng() % 4);
  }
  std::vector<uint8_t> out(tile_bytes(f));
  std::memcpy(out.data(), s.data(), out.size());
  return out;
}

// predict() as tile.h words it, sample by sample: the format itself, which faster code must keep.
std::vector<uint8_t> by_hand(Format f, const std::vector<uint8_t>& raw) {
  const size_t n = size_t(kTileSize) * kTileSize, channels = size_t(f.channels);
  const auto at = [&](int x, int y, size_t c) -> int {
    uint16_t v;
    std::memcpy(&v, &raw[((size_t(y) * kTileSize + size_t(x)) * channels + c) * 2], 2);
    return v;
  };
  std::vector<uint8_t> out(raw.size());
  for (size_t c = 0; c < channels; c++)
    for (int y = 0; y < kTileSize; y++)
      for (int x = 0; x < kTileSize; x++) {
        const int guess = x && y ? at(x - 1, y, c) + at(x, y - 1, c) - at(x - 1, y - 1, c)
                          : x    ? at(x - 1, y, c)
                          : y    ? at(x, y - 1, c)
                                 : 0;
        const uint16_t r = uint16_t(at(x, y, c) - guess);                 // mod 2^16
        const uint16_t z = uint16_t(r << 1 ^ (r & 0x8000 ? 0xFFFF : 0));  // the sign to the lowest bit
        const size_t i = c * n + size_t(y) * kTileSize + size_t(x);
        out[i] = uint8_t(z >> 8), out[channels * n + i] = uint8_t(z);
      }
  return out;
}

std::vector<uint8_t> zstd(const std::vector<uint8_t>& in) {
  std::vector<uint8_t> out(ZSTD_compressBound(in.size()));
  const size_t n = ZSTD_compress(out.data(), out.size(), in.data(), in.size(), 1);
  CHECK(!ZSTD_isError(n));
  out.resize(n);
  return out;
}

void check_codec() {
  for (const int channels : {1, 2, 4}) {
    const Format f{Depth::U16, channels};
    const std::pair<int, uint32_t> kinds[] = {{0, 1}, {0, 2}, {1, 0}, {1, 65535}, {1, 12345}, {2, 0}, {3, 7}};
    for (const auto& [kind, seed] : kinds) {
      const std::vector<uint8_t> raw = samples(f, kind, seed);
      std::vector<uint8_t> p(raw.size()), back(raw.size(), 0xAB);
      predict(f, raw.data(), p.data());
      CHECK(p == by_hand(f, raw));
      unpredict(f, p.data(), back.data());
      CHECK(back == raw);
      if (kind == 3) CHECK(zstd(p).size() * 2 < zstd(raw).size());

      // As a .pbd's tile: a frame of predict()'s bytes, pinned back.
      const auto frame = std::make_shared<const std::vector<uint8_t>>(zstd(p));
      const TilePtr t = Tile::elsewhere(f, frame, frame->data(), frame->size(), true);
      CHECK(std::memcmp(t->pin().data(), raw.data(), raw.size()) == 0);
    }
  }
  const std::vector<uint8_t> frame = zstd(std::vector<uint8_t>(tile_bytes({Depth::U8, 4})));
  bool threw = false;
  try {
    Tile::elsewhere({Depth::U8, 4}, nullptr, frame.data(), frame.size(), true);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  CHECK(threw);
}

// ---- Documents ----

// A directory for a check's files, removed when the check ends; the mip cache lets go of its tiles first, as attached
// ones keep their file mapped, which Windows will not delete.
struct TempDir {
  fs::path path = fs::temp_directory_path() / ("pb-test-pbd-v2-" + std::to_string(std::random_device{}()));
  TempDir() { fs::create_directories(path); }
  ~TempDir() {
    clear_mips();
    std::error_code ec;
    fs::remove_all(path, ec);
  }
  fs::path operator/(const std::string& name) const { return path / name; }
};

std::vector<uint8_t> get(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
}
uint32_t format_of(const fs::path& p) {
  const std::vector<uint8_t> f = get(p);
  CHECK(f.size() >= 12);
  return uint32_t(f[8]) | uint32_t(f[9]) << 8 | uint32_t(f[10]) << 16 | uint32_t(f[11]) << 24;
}

TilePtr data_tile(Format f, int kind, uint32_t seed) {
  const std::shared_ptr<Tile> t = Tile::make(f);
  const std::vector<uint8_t> s = f.depth == Depth::U16 ? samples(f, kind, seed) : std::vector<uint8_t>(tile_bytes(f));
  const WritePin w = t->write();
  std::memcpy(w.data(), s.data(), s.size());
  if (f.depth == Depth::U8)
    for (size_t i = 0; i < s.size(); i++) w.data()[i] = uint8_t(i / 700 + seed);
  return t;
}

// 700 x 500 (3 x 2 tiles, the last column and row cut by the canvas's edge): per layer a ramp, noise, a solid tile,
// gaps, and tiles past the edges.
DocPtr sample(Mode mode, Depth depth, uint32_t seed) {
  DocPtr d = new_document(700, 500, mode, depth, nullptr);
  const Format f = pixel_format(*d);
  for (int k = 0; k < 2; k++) {
    auto g = std::make_shared<TileGrid>(f);
    g->set(0, 0, data_tile(f, 3, seed + uint32_t(k)));
    g->set(2, 1, data_tile(f, k ? 0 : 3, seed + 10));  // over both edges
    g->set(-1, 0, data_tile(f, 2, 0));                  // past the left edge
    g->set(1, 1, Tile::solid(f, {1000, 2000, 3000, 65535}));
    d = run(*d, AddLayer{0, k, Kind::Pixel, {}, g});
  }
  return d;
}

bool same_pixels(const DocState& a, const DocState& b) {
  const std::function<bool(const Layer&, const Layer&)> same = [&](const Layer& x, const Layer& y) {
    if (x.kind != y.kind || x.children.size() != y.children.size()) return false;
    if (x.kind == Kind::Pixel) {
      if (x.pixels->tiles().size() != y.pixels->tiles().size()) return false;
      for (auto i = x.pixels->tiles().begin(), j = y.pixels->tiles().begin(); i != x.pixels->tiles().end(); ++i, ++j) {
        const Tile &s = *i->second, &t = *j->second;
        if (!(i->first == j->first) || s.is_solid() != t.is_solid()) return false;
        if (s.is_solid() ? s.value() != t.value()
                         : std::memcmp(s.pin().data(), t.pin().data(), tile_bytes(s.format())) != 0)
          return false;
      }
    }
    for (size_t i = 0; i < x.children.size(); i++)
      if (!same(*x.children[i], *y.children[i])) return false;
    return true;
  };
  return a.depth == b.depth && a.mode == b.mode && same(*a.root, *b.root);
}

// How many of the file's chunks are a zstd frame of one of d's data tiles' pixels exactly as pin() gives them, read
// through its index alone (pbd.cpp's layout): format 1 frames every data tile so, format 2 only 8-bit ones.
size_t plain_frames(const fs::path& p, const DocState& d) {
  std::unordered_set<std::string> pixels;
  const std::function<void(const Layer&)> walk = [&](const Layer& g) {
    for (const LayerPtr& l : g.children)
      if (l->kind == Kind::Group)
        walk(*l);
      else
        for (const auto& [xy, t] : l->pixels->tiles())
          if (!t->is_solid())
            pixels.insert(std::string(reinterpret_cast<const char*>(t->pin().data()), tile_bytes(t->format())));
  };
  walk(*d.root);
  const std::vector<uint8_t> f = get(p);
  const auto u64 = [&](size_t at) {
    uint64_t v;
    std::memcpy(&v, &f[at], 8);
    return v;
  };
  const size_t index = size_t(u64(f.size() - 24)), n = size_t(u64(index) & 0xFFFFFFFF);
  std::string out(tile_bytes(pixel_format(d)), '\0');
  size_t plain = 0;
  for (size_t i = 0; i < n; i++) {
    const size_t at = size_t(u64(index + 4 + 16 * i)), size = size_t(u64(index + 12 + 16 * i) & 0xFFFFFFFF);
    if (ZSTD_decompress(&out[0], out.size(), &f[at], size) == out.size() && pixels.count(out)) plain++;
  }
  return plain;
}

// d with tile (0, 0) of its first layer anew.
DocPtr changed(const DocPtr& d, uint32_t seed) {
  const LayerId id = d->root->children[0]->id;
  TileGrid g = *d->root->children[0]->pixels;
  g.set(0, 0, data_tile(g.format(), 3, seed));
  return run(*d, SetPixels{id, std::make_shared<const TileGrid>(std::move(g))});
}

// An append to p: the file grows, every byte before kept, in the same format; it opens to d.
void check_appends(const fs::path& p, const DocPtr& d) {
  const std::vector<uint8_t> before = get(p);
  save_pbd(*d, p);
  const std::vector<uint8_t> after = get(p);
  CHECK(after.size() > before.size() && std::equal(before.begin(), before.end(), after.begin()));
  CHECK(same_pixels(*d, *open_pbd(p).doc));
}

void check_documents() {
  const TempDir dir;
  int n = 0;
  for (const auto& [mode, depth] : {std::pair(Mode::Rgb, Depth::U16), std::pair(Mode::Gray, Depth::U16),
                                    std::pair(Mode::Rgb, Depth::U8)}) {
    const DocPtr d = sample(mode, depth, uint32_t(++n));
    const fs::path v2 = dir / ("v2-" + std::to_string(n) + ".pbd"), v1 = dir / ("v1-" + std::to_string(n) + ".pbd");
    save_pbd(*d, v2);
    SaveOptions old;
    old.version = 1;
    save_pbd(*d, v1, old);
    CHECK(format_of(v2) == 2 && format_of(v1) == 1);
    if (depth == Depth::U16) CHECK(fs::file_size(v2) < fs::file_size(v1));
    const DocPtr from1 = open_pbd(v1).doc, from2 = open_pbd(v2).doc;
    CHECK(same_pixels(*d, *from1) && same_pixels(*d, *from2));
    CHECK(plain_frames(v1, *d) == 6 && plain_frames(v2, *d) == (depth == Depth::U16 ? 0 : 6));  // 3 a layer

    // Appends keep each file's format; v1's document saved elsewhere is format 2.
    check_appends(v1, changed(from1, 100));
    CHECK(format_of(v1) == 1);
    check_appends(v2, changed(from2, 200));
    CHECK(format_of(v2) == 2);
    const fs::path as = dir / ("as-" + std::to_string(n) + ".pbd");
    save_pbd(*from1, as);
    CHECK(format_of(as) == 2 && same_pixels(*d, *open_pbd(as).doc));
  }
  bool threw = false;
  SaveOptions bad;
  bad.version = 3;
  try {
    save_pbd(*sample(Mode::Rgb, Depth::U8, 9), dir / "bad.pbd", bad);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  CHECK(threw && !fs::exists(dir / "bad.pbd"));
}

// A save whose file cannot take the place of `path` (a directory there) throws and leaves nothing beside it: what it
// wrote, mapped already for its tiles, lets go of the file before it is removed (Windows keeps a mapped file).
void check_failed_move() {
  const TempDir dir;
  fs::create_directories(dir / "a.pbd" / "in");
  bool threw = false;
  try {
    save_pbd(*sample(Mode::Rgb, Depth::U16, 3), dir / "a.pbd");
  } catch (const std::system_error&) {
    threw = true;
  }
  CHECK(threw && std::distance(fs::directory_iterator(dir.path), fs::directory_iterator()) == 1);
}

}  // namespace

TEST(test_pbd_v2) {
  check_codec();
  check_documents();
  check_failed_move();
}
