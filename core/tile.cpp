#include "tile.h"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <new>
#include <stdexcept>

#include "profile.h"

namespace pb {
namespace {

constexpr size_t kPixels = size_t(kTileSize) * kTileSize;
std::atomic<uint64_t> g_last_id{0};

uint64_t new_id() { return g_last_id.fetch_add(1, std::memory_order_relaxed) + 1; }

void validate(Format f) {
  if ((f.channels != 1 && f.channels != 2 && f.channels != 4) || (f.depth != Depth::U8 && f.depth != Depth::U16))
    throw std::invalid_argument("pb: a tile format has 1, 2 or 4 channels, U8 or U16");
}

// Entries past the channels 0 and samples within the depth, so that pixels compare with ==.
Pixel normal(Format f, Pixel v) {
  for (int c = 0; c < 4; c++) v[c] = c < f.channels ? std::min(v[c], uint16_t(max_value(f.depth))) : 0;
  return v;
}
bool transparent(Format f, const Pixel& v) { return f.alpha() && v[f.channels - 1] == 0; }
bool alike(Format f, const Pixel& a, const Pixel& b) { return a == b || (transparent(f, a) && transparent(f, b)); }

void store(Format f, const Pixel& v, uint8_t* dst) {
  if (f.depth == Depth::U16) return void(std::memcpy(dst, v.data(), size_t(f.channels) * 2));
  for (int c = 0; c < f.channels; c++) dst[c] = uint8_t(v[c]);
}
Pixel load(Format f, const uint8_t* src) {
  Pixel v{};
  if (f.depth == Depth::U16)
    std::memcpy(v.data(), src, size_t(f.channels) * 2);
  else
    for (int c = 0; c < f.channels; c++) v[c] = src[c];
  return v;
}
// n >= 1 pixels of v from dst on: one stored, then doubled.
void repeat(Format f, const Pixel& v, uint8_t* dst, size_t n) {
  store(f, v, dst);
  const size_t total = n * size_t(f.bytes());
  for (size_t done = size_t(f.bytes()); done < total; done *= 2) std::memcpy(dst + done, dst, std::min(done, total - done));
}

}  // namespace

// The store's side of a tile (pin(), writable(), the destructor...) is in tilecache.cpp.
Tile::Tile(Format f, Pixel v, bool data) : format_(f), solid_(!data), value_(normal(f, v)), id_(new_id()) {}

std::shared_ptr<Tile> Tile::make(Format f, Pixel fill) {
  validate(f);
  return std::shared_ptr<Tile>(new Tile(f, fill, true));
}

TilePtr Tile::solid(Format f, Pixel value) {
  validate(f);
  return TilePtr(new Tile(f, value, false));
}

TilePtr Tile::elsewhere(Format f, std::shared_ptr<const void> owner, const void* zstd, size_t size) {
  validate(f);
  if (!zstd || !size || size > UINT32_MAX) throw std::invalid_argument("pb: Tile::elsewhere: no frame");
  std::shared_ptr<Tile> t(new Tile(f, {}, true));
  t->packed_ = Packed::Elsewhere;
  t->zstd_ = true;
  t->bytes_ = static_cast<const uint8_t*>(zstd);
  t->size_ = uint32_t(size);
  t->owner_ = std::move(owner);
  return t;
}

void Tile::fill(uint8_t* raw) const { repeat(format_, value_, raw, kPixels); }

WritePin Tile::write() {
  WritePin w = writable();
  id_ = new_id();
  return w;
}

TileGrid::TileGrid(Format f, Pixel fill) : format_(f), fill_(normal(f, fill)) { validate(f); }

// Tiles shared with a copy are never written in place again. The writer's own copies mark them on its thread; a
// copy of a published grid (already marked) only loads, so readers do not contend on the flags.
TileGrid::TileGrid(const TileGrid& o) : format_(o.format_), fill_(o.fill_), tiles_(o.tiles_) {
  PROFILE_ZONE("grid_copy");
  for (const auto& [xy, t] : tiles_)
    if (!t->shared_.load(std::memory_order_relaxed)) t->shared_.store(true, std::memory_order_relaxed);
}

TileGrid& TileGrid::operator=(const TileGrid& o) {
  if (this != &o) *this = TileGrid(o);
  return *this;
}

const Tile* TileGrid::get(int tx, int ty) const {
  const auto it = tiles_.find({tx, ty});
  return it == tiles_.end() ? nullptr : it->second.get();
}

TileRect TileGrid::bounds() const {
  if (tiles_.empty()) return {};
  TileRect r{INT_MAX, tiles_.begin()->first.y, INT_MIN, std::prev(tiles_.end())->first.y + 1};
  for (const auto& [xy, t] : tiles_) r.x0 = std::min(r.x0, xy.x), r.x1 = std::max(r.x1, xy.x + 1);
  return r;
}

void TileGrid::set(int tx, int ty, TilePtr t) {
  if (!t) return void(tiles_.erase({tx, ty}));
  if (t->format() != format_) throw std::invalid_argument("pb: TileGrid::set: a tile of another format");
  t->shared_.store(true, std::memory_order_relaxed);
  tiles_[{tx, ty}] = std::move(t);
}

WritePin TileGrid::write(int tx, int ty) {
  const auto it = tiles_.find({tx, ty});
  // use_count() only sees this thread's holders exactly: other threads hold tiles through copies, which marked them.
  if (it != tiles_.end() && !it->second->solid_ && !it->second->shared_.load(std::memory_order_relaxed) &&
      it->second.use_count() == 1)
    return const_cast<Tile&>(*it->second).write();
  std::shared_ptr<Tile> t = Tile::make(format_, it == tiles_.end() ? fill_ : it->second->value_);
  WritePin w = it == tiles_.end() || it->second->solid_ ? t->write() : [&] {
    PROFILE_ZONE("tile_copy");
    const Pin from = it->second->pin();
    return t->write_copy(from.data());
  }();
  if (it == tiles_.end())
    tiles_.emplace(TileXY{tx, ty}, std::move(t));
  else
    it->second = std::move(t);
  return w;
}

void TileGrid::read_rect(int x, int y, int w, int h, uint8_t* dst, size_t stride) const {
  PROFILE_ZONE("read_rect");
  if (w <= 0 || h <= 0) return;
  const size_t bpp = size_t(format_.bytes()), row = kTileSize * bpp;
  const int64_t x1 = int64_t(x) + w, y1 = int64_t(y) + h;
  for (int64_t ty = y >> kTileLog2; ty <= (y1 - 1) >> kTileLog2; ty++) {
    const int64_t top = std::max<int64_t>(y, ty << kTileLog2), bottom = std::min(y1, (ty + 1) << kTileLog2);
    for (int64_t tx = x >> kTileLog2; tx <= (x1 - 1) >> kTileLog2; tx++) {
      const int64_t left = std::max<int64_t>(x, tx << kTileLog2), right = std::min(x1, (tx + 1) << kTileLog2);
      const size_t n = size_t(right - left);
      uint8_t* out = dst + size_t(top - y) * stride + size_t(left - x) * bpp;
      const Tile* t = get(int(tx), int(ty));
      const Pin p = t ? t->pin() : Pin();
      if (!p.data()) {
        const Pixel v = t ? t->value_ : fill_;
        for (int64_t r = top; r < bottom; r++, out += stride) repeat(format_, v, out, n);
        continue;
      }
      const uint8_t* in = p.data() + size_t(top - (ty << kTileLog2)) * row + size_t(left - (tx << kTileLog2)) * bpp;
      for (int64_t r = top; r < bottom; r++, out += stride, in += row) std::memcpy(out, in, n * bpp);
    }
  }
}

void TileGrid::write_rect(int x, int y, int w, int h, const uint8_t* src, size_t stride) {
  PROFILE_ZONE("write_rect");
  if (w <= 0 || h <= 0) return;
  const size_t bpp = size_t(format_.bytes()), row = kTileSize * bpp;
  const int64_t x1 = int64_t(x) + w, y1 = int64_t(y) + h;
  for (int64_t ty = y >> kTileLog2; ty <= (y1 - 1) >> kTileLog2; ty++) {
    const int64_t top = std::max<int64_t>(y, ty << kTileLog2), bottom = std::min(y1, (ty + 1) << kTileLog2);
    for (int64_t tx = x >> kTileLog2; tx <= (x1 - 1) >> kTileLog2; tx++) {
      const int64_t left = std::max<int64_t>(x, tx << kTileLog2), right = std::min(x1, (tx + 1) << kTileLog2);
      const size_t n = size_t(right - left);
      {
        const WritePin p = write(int(tx), int(ty));
        uint8_t* out = p.data() + size_t(top - (ty << kTileLog2)) * row + size_t(left - (tx << kTileLog2)) * bpp;
        const uint8_t* in = src + size_t(top - y) * stride + size_t(left - x) * bpp;
        for (int64_t r = top; r < bottom; r++, out += row, in += stride) std::memcpy(out, in, n * bpp);
      }
      const auto it = tiles_.find({int(tx), int(ty)});
      if (TilePtr c = compact(it->second, fill_); !c)
        tiles_.erase(it);
      else if (c != it->second)
        it->second = std::move(c);  // a new solid tile: solid ones are never written in place
    }
  }
}

TilePtr compact(const TilePtr& t, Pixel fill) {
  if (!t) return nullptr;
  const Format f = t->format();
  Pixel v = t->value();
  if (!t->is_solid()) {
    PROFILE_ZONE("compact");
    const Pin p = t->pin();
    const uint8_t* d = p.data();
    const size_t bpp = size_t(f.bytes()), row = kTileSize * bpp;
    const size_t a0 = bpp - (f.depth == Depth::U16 ? 2 : 1), a1 = bpp - 1;  // alpha's bytes in a pixel
    bool clear = f.alpha();
    for (const uint8_t* q = d; clear && q < d + kPixels * bpp; q += bpp) clear = (q[a0] | q[a1]) == 0;
    if (clear) {
      v = {};
    } else {
      for (size_t i = 1; i < size_t(kTileSize); i++)
        if (std::memcmp(d + i * bpp, d, bpp) != 0) return t;
      for (size_t r = 1; r < size_t(kTileSize); r++)
        if (std::memcmp(d + r * row, d, row) != 0) return t;
      v = load(f, d);
    }
  }
  if (alike(f, v, normal(f, fill))) return nullptr;
  return t->is_solid() ? t : Tile::solid(f, v);
}

}  // namespace pb
