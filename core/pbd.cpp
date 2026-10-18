// .pbd (pbd.h): saving and opening. The layout (docs/architecture.md), integers little-endian:
//   header  32 bytes: magic, format version, 0, the app's version (version.h) padded with zeros;
//   chunks  back to back from there: tiles' zstd frames (with checksums), the ICC profile, the preview (JPEG), the
//   JSON; index   the number of chunks (u32), then each one's offset (u64), size (u32), CRC32 (u32); chunk 0 is the
//   JSON; footer  24 bytes, the file's last: the index's offset (u64), size (u32), CRC32 (u32), then magic.
// The JSON is the document: size, mode, depth, next id, selection, the ICC profile's chunk, the preview's, and the
// layer tree, each pixel layer with its fill, its tiles ([tx, ty, chunk], or [tx, ty, [samples]] for a solid one)
// and its mip tiles from level 3 on ([level, tx, ty, chunk | [samples] | null for one all fill]).
#include "pbd.h"

#include <turbojpeg.h>
#include <zlib.h>
#include <zstd.h>

#include <algorithm>
#include <array>
#include <bit>
#include <climits>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <nlohmann/json.hpp>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "mapped.h"
#include "mip.h"
#include "open.h"
#include "profile.h"
#include "render.h"
#include "version.h"

namespace pb {

// Tiles keep U16 samples as the machine holds them (tile.h), and a .pbd holds tiles' pixels as they are.
static_assert(std::endian::native == std::endian::little, "a .pbd is little-endian; so must the machine be");

namespace {

using json = nlohmann::json;

constexpr char kMagic[8] = {'\x89', 'P', 'B', 'D', '\r', '\n', '\x1a', '\n'};  // as PNG's: mangled line ends show
constexpr char kFooterMagic[8] = {'P', 'B', 'D', 'I', 'N', 'D', 'E', 'X'};
constexpr uint32_t kFormat = 1;
constexpr size_t kHeader = 32, kFooter = 24, kEntry = 16;
constexpr int kZstdLevel = 1;  // as the tile store's (K1 weighed 1 against 3)
constexpr int kFirstMip = 3;   // levels saved from here on, so that a whole-image view reads no full-size tile
constexpr int kPreviewSide = 2048;
constexpr int kQuality = 90;       // the preview's JPEG
constexpr size_t kBatch = 64;      // tiles compressed on the pool at a time, then written in order
constexpr int kMaxNesting = 256;   // groups in groups (Photoshop: 10)
constexpr int kMaxTile = 1 << 23;  // a tile's |tx|, |ty|, so that its pixels' coordinates fit an int

void put32(std::vector<uint8_t>& b, uint32_t v) {
  for (int i = 0; i < 4; i++) b.push_back(uint8_t(v >> 8 * i));
}
void put64(std::vector<uint8_t>& b, uint64_t v) { put32(b, uint32_t(v)), put32(b, uint32_t(v >> 32)); }
uint32_t get32(const uint8_t* p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
uint64_t get64(const uint8_t* p) { return get32(p) | uint64_t(get32(p + 4)) << 32; }
uint32_t crc_of(const void* p, size_t n) { return uint32_t(crc32_z(0, static_cast<const Bytef*>(p), n)); }

struct Chunk {
  uint64_t offset = 0;
  uint32_t size = 0, crc = 0;
};

// ---- Where tiles opened from a .pbd lie ----

// A .pbd as this process last left it, opened or saved: how far it reaches, the footer it ends with, and the ICC
// profile its last JSON names. Saves only ever append to a .pbd, so a file at any path that still ends so (the same
// size, the same last 24 bytes: the index's place and CRC) holds these bytes, whatever its name or identity, and a
// save appends to it. Shared by the mappings of its parts (PbdFile); under saving().
struct Line {
  uint64_t end = 0;
  std::array<uint8_t, kFooter> footer{};
  std::shared_ptr<const std::vector<uint8_t>> icc;
  Chunk icc_chunk;
};

// A mapped part of a .pbd, from byte `base` to the end the file had: it holds the frames of the tiles opened from it
// or saved into it (each tile holds the file, as Tile::elsewhere()'s owner), and the ids of those tiles, remembered
// while it lives.
struct PbdFile {
  MappedFile map;
  uint64_t base;
  std::shared_ptr<Line> line;  // set before the tiles are remembered
  std::vector<uint64_t> ids;   // under places().m
  PbdFile(const std::filesystem::path& p, uint64_t from) : map(p, from), base(from) {}
  ~PbdFile();
  const uint8_t* at(uint64_t offset) const { return map.data() + (offset - base); }  // byte `offset` of the file
};

// A tile's frame in a .pbd: its pixels exactly, for as long as its id stays (a new id comes with new pixels).
struct Place {
  std::weak_ptr<const PbdFile> file;
  const PbdFile* raw = nullptr;  // the same file, to tell its own entries when it goes
  uint64_t offset = 0;
  uint32_t size = 0, crc = 0;
};
// Every tile opened from a .pbd or saved into one, by id, in the last file it went to: what a save copies, and what a
// save into that file keeps. Never destroyed: tiles may outlive main().
struct Places {
  std::mutex m;
  std::unordered_map<uint64_t, Place> by_id;
};
Places& places() {
  static Places* p = new Places;
  return *p;
}

PbdFile::~PbdFile() {
  Places& p = places();
  const std::lock_guard l(p.m);
  for (const uint64_t id : ids)
    if (const auto it = p.by_id.find(id); it != p.by_id.end() && it->second.raw == this) p.by_id.erase(it);
}

// One save at a time in the process: an append checks, writes and notes the end of its file as one step.
// ponytail: one lock for every file; one per Line if saves of different documents must overlap.
std::mutex& saving() {
  static std::mutex* m = new std::mutex;
  return *m;
}

// ---- Saving ----

// A tile's zstd frame on its way into the file: compressed here (`bytes`), or lying in a .pbd opened here (`keep`).
struct Frame {
  std::unique_ptr<uint8_t[]> bytes;
  std::shared_ptr<const PbdFile> keep;
  const uint8_t* data = nullptr;
  size_t size = 0;
  uint32_t crc = 0;
};

// t's frame as a .pbd opened here holds it, its CRC checked, if one does.
std::optional<Frame> filed(const Tile& t) {
  Frame f;
  Place at;
  {
    Places& p = places();
    const std::lock_guard l(p.m);
    const auto it = p.by_id.find(t.id());
    if (it == p.by_id.end()) return std::nullopt;
    at = it->second;
    f.keep = at.file.lock();
  }
  if (!f.keep) return std::nullopt;
  f.data = f.keep->at(at.offset), f.size = at.size, f.crc = at.crc;
  if (crc_of(f.data, f.size) != f.crc)
    return std::nullopt;  // damaged there: compressing it again pins it, which says so
  return f;
}

// A data tile's frame: copied from a .pbd, else compressed now with a checksum, so that pin() catches damage later.
Frame frame(const Tile& t) {
  PROFILE_ZONE("pbd_frame");
  if (std::optional<Frame> f = filed(t)) return std::move(*f);
  struct Context {
    ZSTD_CCtx* c = ZSTD_createCCtx();
    Context() {
      ZSTD_CCtx_setParameter(c, ZSTD_c_compressionLevel, kZstdLevel);
      ZSTD_CCtx_setParameter(c, ZSTD_c_checksumFlag, 1);
    }
    ~Context() { ZSTD_freeCCtx(c); }
  };
  thread_local Context z;
  if (!z.c) throw std::bad_alloc();
  const Pin p = t.pin();
  const size_t n = size_t(kTileSize) * kTileSize * size_t(t.format().bytes());
  Frame f;
  const size_t bound = ZSTD_compressBound(n);
  f.bytes.reset(new uint8_t[bound]);  // not zeroed
  const size_t size = ZSTD_compress2(z.c, f.bytes.get(), bound, p.data(), n);
  if (ZSTD_isError(size)) throw std::runtime_error(std::string("pb: zstd: ") + ZSTD_getErrorName(size));
  f.data = f.bytes.get(), f.size = size, f.crc = crc_of(f.data, size);
  return f;
}

// Where t's frame lies in the file `line` reaches through, when it lies there (a save into that file keeps it).
std::optional<Chunk> kept(const Tile& t, const Line* line) {
  if (!line) return std::nullopt;
  std::shared_ptr<const PbdFile> f;  // let go after the lock: it may be the last
  Chunk c;
  {
    Places& p = places();
    const std::lock_guard l(p.m);
    const auto it = p.by_id.find(t.id());
    if (it == p.by_id.end()) return std::nullopt;
    f = it->second.file.lock();
    c = {it->second.offset, it->second.size, it->second.crc};
  }
  return f && f->line.get() == line ? std::optional<Chunk>(c) : std::nullopt;
}

// The file a save to `path` appends to: the Line of one of `tiles` that the file at `path` still ends as, else null
// (a full save). Under saving().
std::shared_ptr<Line> appendable(const std::filesystem::path& path, const std::vector<const Tile*>& tiles) {
  std::vector<std::shared_ptr<Line>> lines;  // those the tiles lie in: one, mostly
  {
    std::vector<std::shared_ptr<const PbdFile>> files;  // let go after the lock
    Places& p = places();
    const std::lock_guard l(p.m);
    for (const Tile* t : tiles)
      if (const auto it = p.by_id.find(t->id()); it != p.by_id.end())
        if (std::shared_ptr<const PbdFile> f = it->second.file.lock();
            f && std::find(lines.begin(), lines.end(), f->line) == lines.end()) {
          lines.push_back(f->line);
          files.push_back(std::move(f));
        }
  }
  std::error_code e;
  const uint64_t size = std::filesystem::file_size(path, e);
  if (e || size < kHeader + kFooter) return nullptr;
  for (const std::shared_ptr<Line>& line : lines)
    if (line->end == size) {
      const MappedFile tail(path, size - kFooter);  // the file may have changed since: its own last bytes
      if (tail.size() == kFooter && std::memcmp(tail.data(), line->footer.data(), kFooter) == 0) return line;
    }
  return nullptr;
}

// Chunks appended to the file, and the index of them.
struct Writer {
  OutputFile& out;
  std::vector<Chunk> chunks = std::vector<Chunk>(1);  // chunk 0, the JSON, comes last
  Chunk write(const void* data, size_t n, uint32_t crc) {
    if (n > UINT32_MAX) throw std::length_error("pb: a .pbd chunk past 4 GB");
    const Chunk c{out.size(), uint32_t(n), crc};
    out.write(data, n);
    return c;
  }
  uint32_t add(const void* data, size_t n, uint32_t crc) { return keep(write(data, n, crc)); }
  uint32_t add(const Frame& f) { return add(f.data, f.size, f.crc); }
  uint32_t keep(const Chunk& c) {  // one in the file already
    chunks.push_back(c);
    return uint32_t(chunks.size() - 1);
  }
};

// After a save into `path`, finished as `next` says (its end, footer, ICC profile): the file mapped from `from` (where
// the save began to write), each tile it wrote a frame for reading from there (Tile::also_elsewhere(), so that its
// copies in RAM go for free) and remembered there, `line` set to `next`. Should the file at `path` not end so any more
// (moved over meanwhile) or not map, nothing changes: the next save to it is a full one.
void adopt(const std::filesystem::path& path, uint64_t from, const std::shared_ptr<Line>& line, const Line& next,
           const std::vector<std::pair<const Tile*, Chunk>>& written) {
  PROFILE_ZONE("pbd_adopt");
  std::shared_ptr<PbdFile> f;
  try {
    f = std::make_shared<PbdFile>(path, from);
  } catch (const std::system_error&) {
    return;
  }
  if (f->map.size() != next.end - from || std::memcmp(f->at(next.end - kFooter), next.footer.data(), kFooter) != 0)
    return;
  f->line = line;
  *line = next;
  for (const auto& [t, c] : written) t->also_elsewhere(f, f->at(c.offset), c.size);
  Places& p = places();
  const std::lock_guard l(p.m);
  for (const auto& [t, c] : written) {
    p.by_id[t->id()] = {f, f.get(), c.offset, c.size, c.crc};
    f->ids.push_back(t->id());
  }
}

void samples(std::string& s, const Pixel& v, int channels) {
  for (int c = 0; c < channels; c++) s += (c ? "," : "[") + std::to_string(v[size_t(c)]);
  s += ']';
}

int side(int64_t n, int level) { return int((n + (int64_t(1) << level) - 1) >> level); }

// The composite at the first mip level 2048 pixels or fewer both ways, flattened over white into 8 bits.
Preview preview(const DocState& doc) {
  PROFILE_ZONE("pbd_preview");
  Preview p;
  while (side(doc.width, p.level) > kPreviewSide || side(doc.height, p.level) > kPreviewSide) p.level++;
  p.width = side(doc.width, p.level), p.height = side(doc.height, p.level);
  p.channels = doc.mode == Mode::Gray ? 1 : 3;
  p.pixels.resize(size_t(p.width) * size_t(p.height) * size_t(p.channels));
  const RenderInput in{DocPtr(DocPtr(), &doc), std::nullopt};  // not owned: the caller's for the call
  const int across = (p.width + kTileSize - 1) / kTileSize, down = (p.height + kTileSize - 1) / kTileSize;
  parallel_for(across * down, [&](int begin, int end) {
    for (int i = begin; i < end; i++) {
      const int x0 = i % across * kTileSize, y0 = i / across * kTileSize;
      const TilePtr t = composite_tile(in, p.level, i % across, i / across);
      const Pin pin = t ? t->pin() : Pin();
      const Pixel solid = t ? t->value() : Pixel{};  // transparent where null
      const auto* px = reinterpret_cast<const uint16_t*>(pin.data());
      for (int y = y0; y < std::min(y0 + kTileSize, p.height); y++)
        for (int x = x0; x < std::min(x0 + kTileSize, p.width); x++) {
          const uint16_t* v = px ? px + (size_t(y - y0) * kTileSize + size_t(x - x0)) * 4 : solid.data();
          const float a = to_unit(v[3], Depth::U16);
          uint8_t* o = &p.pixels[(size_t(y) * size_t(p.width) + size_t(x)) * size_t(p.channels)];
          for (int c = 0; c < p.channels; c++)
            o[c] = uint8_t(from_unit(to_unit(v[c], Depth::U16) * a + (1 - a), Depth::U8));
        }
    }
  });
  return p;
}

std::vector<uint8_t> jpeg(const Preview& p, const std::vector<uint8_t>* icc) {
  PROFILE_ZONE("pbd_preview_jpeg");
  const std::unique_ptr<void, void (*)(tjhandle)> h(tj3Init(TJINIT_COMPRESS), tj3Destroy);
  tjhandle t = h.get();
  unsigned char* buf = nullptr;
  size_t size = 0;
  if (!t || tj3Set(t, TJPARAM_QUALITY, kQuality) ||
      tj3Set(t, TJPARAM_SUBSAMP, p.channels == 1 ? TJSAMP_GRAY : TJSAMP_420) ||
      (icc && !icc->empty() && tj3SetICCProfile(t, const_cast<uint8_t*>(icc->data()), icc->size())) ||
      tj3Compress8(t, p.pixels.data(), p.width, 0, p.height, p.channels == 1 ? TJPF_GRAY : TJPF_RGB, &buf, &size)) {
    tj3Free(buf);
    throw std::runtime_error(std::string("pb: TurboJPEG: ") + (t ? tj3GetErrorStr(t) : "no handle"));
  }
  std::vector<uint8_t> out(buf, buf + size);
  tj3Free(buf);
  return out;
}

// ---- Opening ----

[[noreturn]] void corrupt(const std::string& what) {
  throw DecodeError(DecodeError::Kind::Corrupt, {}, "pbd: " + what);
}

const json& member(const json& j, const char* key) {
  const auto it = j.is_object() ? j.find(key) : j.end();
  if (it == j.end()) corrupt(std::string("the JSON has no \"") + key + '"');
  return *it;
}
const json& array(const json& j, const char* key) {
  const json& a = member(j, key);
  if (!a.is_array()) corrupt(std::string("the JSON's \"") + key + "\" is no array");
  return a;
}
// How deep arrays and objects nest in JSON text, strings aside: past a bound a file is refused before parsing, as
// dumping a layer's properties (for props_from_json()) recurses.
size_t nesting(const char* p, const char* end) {
  size_t depth = 0, most = 0;
  for (bool quoted = false; p < end; p++) {
    if (quoted && *p == '\\')
      p += p + 1 < end;  // what it escapes
    else if (*p == '"')
      quoted = !quoted;
    else if (!quoted && (*p == '[' || *p == '{'))
      most = std::max(most, ++depth);
    else if (!quoted && (*p == ']' || *p == '}') && depth)
      depth--;
  }
  return most;
}

int64_t integer(const json& j, int64_t lo, int64_t hi, const char* what) {
  if (j.is_number_unsigned() && j.get<uint64_t>() <= uint64_t(INT64_MAX)) {
    const int64_t v = int64_t(j.get<uint64_t>());
    if (v >= lo && v <= hi) return v;
  } else if (j.is_number_integer()) {
    const int64_t v = j.get<int64_t>();
    if (v >= lo && v <= hi) return v;
  }
  corrupt(std::string(what) + " is not an integer from " + std::to_string(lo) + " to " + std::to_string(hi));
}

// A .pbd being opened: its mapping, its chunks, the tiles made of them, the bytes read so far.
struct Reader {
  std::shared_ptr<PbdFile> file;
  const uint8_t* data = nullptr;
  std::vector<Chunk> chunks;
  std::vector<TilePtr> tiles;  // by chunk: layers sharing a tile share it again
  // Mip tiles to attach once the whole file has opened: the cache would keep a file that fails mapped (and, on
  // Windows, locked against being written).
  struct Mip {
    std::shared_ptr<const TileGrid> grid;
    int level, tx, ty;
    TilePtr tile;
  };
  std::vector<Mip> mips;
  Format format;
  uint64_t read = 0;

  const uint8_t* bytes(uint64_t at, uint64_t n) {
    read += n;
    return data + at;
  }
  uint32_t number(const json& j) { return uint32_t(integer(j, 0, int64_t(chunks.size()) - 1, "a chunk number")); }
  const uint8_t* chunk(uint32_t i, const char* what) {
    const uint8_t* p = bytes(chunks[i].offset, chunks[i].size);
    if (crc_of(p, chunks[i].size) != chunks[i].crc) corrupt(std::string(what) + " is damaged (CRC)");
    return p;
  }

  Pixel values(const json& a) {
    if (!a.is_array() || a.size() != size_t(format.channels)) corrupt("a solid tile without its samples");
    Pixel v{};
    for (int c = 0; c < format.channels; c++)
      v[size_t(c)] = uint16_t(integer(a[size_t(c)], 0, max_value(format.depth), "a sample"));
    return v;
  }
  int coordinate(const json& j) { return int(integer(j, -kMaxTile, kMaxTile - 1, "a tile coordinate")); }
  // A tile entry's last item: the frame's chunk (an elsewhere() tile, made once) or a solid tile's samples.
  TilePtr tile(const json& j) {
    if (j.is_array()) return Tile::solid(format, values(j));
    const uint32_t i = number(j);
    if (!tiles[i]) {
      if (!chunks[i].size) corrupt("a tile of 0 bytes");
      tiles[i] = Tile::elsewhere(format, file, data + chunks[i].offset, chunks[i].size);
    }
    return tiles[i];
  }

  LayerPtr layer(const json& j, int depth) {
    if (depth > kMaxNesting) corrupt("groups nested too deep");
    auto l = std::make_shared<Layer>();
    l->id = LayerId(integer(member(j, "id"), 1, UINT32_MAX, "a layer's id"));
    const std::optional<LayerProps> props = props_from_json(member(j, "props").dump());
    if (!props) corrupt("a layer's properties are malformed");
    l->props = *props;
    const json& kind = member(j, "kind");
    if (kind == "group") {
      l->kind = Kind::Group;
      for (const json& c : array(j, "layers")) l->children.push_back(layer(c, depth + 1));
      return l;
    }
    if (kind != "pixel") corrupt("a layer of no known kind");
    TileGrid grid(format, values(member(j, "fill")));
    for (const json& e : array(j, "tiles")) {
      if (!e.is_array() || e.size() != 3) corrupt("a tile entry is malformed");
      grid.set(coordinate(e[0]), coordinate(e[1]), tile(e[2]));
    }
    auto pixels = std::make_shared<const TileGrid>(std::move(grid));
    for (const json& e : array(j, "mips")) {  // keyed by the tiles under them, so by the whole grid
      if (!e.is_array() || e.size() != 4) corrupt("a mip entry is malformed");
      // All that may throw before the braces: GCC 9 leaks the members a braced init made before a throw (PR 66139),
      // here `pixels`, which would keep the file mapped.
      const int level = int(integer(e[0], kFirstMip, 31, "a mip level")), tx = coordinate(e[1]), ty = coordinate(e[2]);
      TilePtr t = e[3].is_null() ? nullptr : tile(e[3]);
      mips.push_back({pixels, level, tx, ty, std::move(t)});
    }
    l->pixels = std::move(pixels);
    return l;
  }

  Preview preview(uint32_t i, int level, const DocState& d) {
    const uint8_t* jpeg = chunk(i, "the preview");
    Preview p;
    p.level = level, p.width = side(d.width, level), p.height = side(d.height, level);
    p.channels = d.mode == Mode::Gray ? 1 : 3;
    if (p.width > kPreviewSide || p.height > kPreviewSide) corrupt("the preview is too big");
    const std::unique_ptr<void, void (*)(tjhandle)> h(tj3Init(TJINIT_DECOMPRESS), tj3Destroy);
    if (!h) throw std::bad_alloc();
    if (tj3DecompressHeader(h.get(), jpeg, chunks[i].size) || tj3Get(h.get(), TJPARAM_JPEGWIDTH) != p.width ||
        tj3Get(h.get(), TJPARAM_JPEGHEIGHT) != p.height)
      corrupt("the preview is not the composite's size");
    p.pixels.resize(size_t(p.width) * size_t(p.height) * size_t(p.channels));
    if (tj3Decompress8(h.get(), jpeg, chunks[i].size, p.pixels.data(), 0, p.channels == 1 ? TJPF_GRAY : TJPF_RGB))
      corrupt(std::string("the preview: ") + tj3GetErrorStr(h.get()));
    return p;
  }
};

}  // namespace

void save_pbd(const DocState& doc, const std::filesystem::path& path, const SaveOptions& o) {
  PROFILE_ZONE("save_pbd");
  validate(doc);
  const int channels = pixel_format(doc).channels;

  // The pixel layers' grids, each once (duplicates share theirs), and their data tiles, each once by id.
  std::vector<const TileGrid*> grids;
  std::unordered_map<const TileGrid*, size_t> grid_at;
  std::vector<const Tile*> tiles;  // the snapshot holds them
  std::unordered_set<uint64_t> seen;
  const std::function<void(const Layer&)> gather = [&](const Layer& g) {
    for (const LayerPtr& l : g.children) {
      if (l->kind == Kind::Group) {
        gather(*l);
      } else if (grid_at.emplace(l->pixels.get(), grids.size()).second) {
        grids.push_back(l->pixels.get());
        for (const auto& [xy, t] : l->pixels->tiles())
          if (!t->is_solid() && seen.insert(t->id()).second) tiles.push_back(t.get());
      }
    }
  };
  gather(*doc.root);
  // Mip tiles from level 3 up to where one tile covers the canvas, those over stored tiles: each level's built from the
  // one below (mip.h caches them), so a level's come after the level below in the list.
  int top = kFirstMip;
  while ((int64_t(kTileSize) << top) < std::max(doc.width, doc.height)) top++;
  struct Job {
    size_t grid;
    int level;
    TileXY at;
  };
  std::vector<Job> mips;
  for (size_t g = 0; g < grids.size(); g++)
    for (int level = kFirstMip; level <= top; level++) {
      std::vector<TileXY> at;
      for (const auto& [xy, t] : grids[g]->tiles()) at.push_back({xy.x >> level, xy.y >> level});
      std::sort(at.begin(), at.end());
      at.erase(std::unique(at.begin(), at.end()), at.end());
      for (const TileXY xy : at) mips.push_back({g, level, xy});
    }

  // Into the file the document's tiles lie in, when `path` is that file as this process left it: appended to, its
  // footer before staying good. Else a new file beside `path`, moved over it at the end.
  const std::lock_guard one(saving());
  std::shared_ptr<Line> line = appendable(path, tiles);
  std::filesystem::path tmp;  // a full save's file, until moved over `path`
  try {
    std::optional<OutputFile> file;
    if (line) {
      file.emplace(path, true);
      if (file->size() != line->end) file.reset(), line = nullptr;  // changed since
    }
    if (!line) {
      tmp = path;
      tmp += ".tmp-" + std::to_string(std::random_device{}());
      file.emplace(tmp);
      std::vector<uint8_t> head(kMagic, kMagic + 8);
      put32(head, kFormat), put32(head, 0);
      head.insert(head.end(), version(), version() + std::min<size_t>(std::strlen(version()), kHeader - 16));
      head.resize(kHeader);
      file->write(head.data(), head.size());
    }
    OutputFile& out = *file;
    const uint64_t from = out.size();  // where this save's chunks begin
    Writer w{out};
    std::unordered_map<uint64_t, uint32_t> chunk_of;  // tile id -> chunk
    std::vector<std::pair<const Tile*, Chunk>> written;  // tiles whose frames this save writes
    std::vector<const Tile*> fresh;                      // of `tiles`, those not in the file yet
    for (const Tile* t : tiles)
      if (const std::optional<Chunk> c = kept(*t, line.get()))
        chunk_of[t->id()] = w.keep(*c);
      else
        fresh.push_back(t);
    size_t done = 0;
    const auto batch = [&](size_t n) {
      done += n;
      if (o.progress) o.progress(double(done) / double(fresh.size() + mips.size() + 1));
    };
    const auto check = [&] {
      if (cancelled(o.cancel)) throw Cancelled();
    };

    // Full-size tiles a batch at a time, compressed on the pool while the batch before is written, in order (by item 0
    // of the batch's parallel_for, on whichever thread takes it).
    std::vector<Frame> compressed;  // the batch before, fresh[b - its size, b)
    const auto write = [&](size_t end) {
      for (size_t i = 0; i < compressed.size(); i++) {
        const Tile* t = fresh[end - compressed.size() + i];
        const uint32_t c = chunk_of[t->id()] = w.add(compressed[i]);  // what throws, before the braces (GCC 9)
        written.push_back({t, w.chunks[c]});
      }
    };
    for (size_t b = 0; b < fresh.size(); b += kBatch) {
      check();
      const size_t n = std::min(kBatch, fresh.size() - b);
      std::vector<Frame> frames(n);
      parallel_for(int(n) + 1, [&](int i0, int i1) {
        for (int i = i0; i < i1; i++)
          if (i == 0)
            write(b);
          else
            frames[size_t(i - 1)] = frame(*fresh[b + size_t(i - 1)]);
      });
      compressed = std::move(frames);
      batch(n);
    }
    write(fresh.size());
    // Then the mip tiles, built as they go, each new one (by id) written once.
    std::vector<std::string> mip_entries(grids.size());
    std::vector<TilePtr> held;  // mip tiles written, alive until adopted
    for (size_t b = 0; b < mips.size(); b += kBatch) {
      check();
      const size_t n = std::min(kBatch, mips.size() - b);
      std::vector<TilePtr> made(n);
      std::vector<std::optional<Chunk>> in_file(n);
      std::vector<Frame> frames(n);
      parallel_for(int(n), [&](int i0, int i1) {
        for (int i = i0; i < i1; i++) {
          const Job& m = mips[b + size_t(i)];
          const TilePtr& t = made[size_t(i)] = mip(*grids[m.grid], m.level, m.at.x, m.at.y);
          if (t && !t->is_solid() && !chunk_of.count(t->id()) && !(in_file[size_t(i)] = kept(*t, line.get())))
            frames[size_t(i)] = frame(*t);
        }
      });
      for (size_t i = 0; i < n; i++) {
        const Job& m = mips[b + i];
        std::string& s = mip_entries[m.grid];
        s += (s.empty() ? "[[" : ",[") + std::to_string(m.level) + ',' + std::to_string(m.at.x) + ',' +
             std::to_string(m.at.y) + ',';
        const Tile* t = made[i].get();
        if (!t) {
          s += "null";
        } else if (t->is_solid()) {
          samples(s, t->value(), channels);
        } else {
          const auto [it, first] = chunk_of.emplace(t->id(), 0);
          if (first && in_file[i]) {
            it->second = w.keep(*in_file[i]);
          } else if (first) {
            it->second = w.add(frames[i]);
            written.push_back({t, w.chunks[it->second]});
            held.push_back(made[i]);
          }
          s += std::to_string(it->second);
        }
        s += ']';
      }
      batch(n);
    }

    check();
    const Preview pv = preview(doc);
    const std::vector<uint8_t> pv_jpeg = jpeg(pv, doc.icc.get());
    const uint32_t pv_chunk = w.add(pv_jpeg.data(), pv_jpeg.size(), crc_of(pv_jpeg.data(), pv_jpeg.size()));
    Line next;  // the file once saved
    next.icc = doc.icc;
    if (doc.icc)
      next.icc_chunk = line && line->icc && *line->icc == *doc.icc
                           ? line->icc_chunk
                           : w.write(doc.icc->data(), doc.icc->size(), crc_of(doc.icc->data(), doc.icc->size()));
    std::string s = "{\"app\":" + json(version()).dump() + ",\"width\":" + std::to_string(doc.width) +
                    ",\"height\":" + std::to_string(doc.height) + ",\"mode\":\"" +
                    (doc.mode == Mode::Gray ? "gray" : "rgb") +
                    "\",\"depth\":" + (doc.depth == Depth::U16 ? "16" : "8") +
                    ",\"nextId\":" + std::to_string(doc.next_id) + ",\"selected\":[";
    for (size_t i = 0; i < doc.selected.size(); i++) s += (i ? "," : "") + std::to_string(doc.selected[i]);
    s += ']';
    if (doc.icc) s += ",\"icc\":" + std::to_string(w.keep(next.icc_chunk));
    // Bytes no chunk of this save's index covers (indexes, JSONs, previews and tiles of saves before), for compacting.
    uint64_t used = kHeader;
    for (size_t i = 1; i < w.chunks.size(); i++) used += w.chunks[i].size;
    s += ",\"waste\":" + std::to_string(out.size() - used) + ",\"preview\":{\"chunk\":" + std::to_string(pv_chunk) +
         ",\"level\":" + std::to_string(pv.level) + "},\"layers\":";
    const std::function<void(const Layer&)> layers = [&](const Layer& g) {
      s += '[';
      for (size_t i = 0; i < g.children.size(); i++) {
        const Layer& l = *g.children[i];
        s += (i ? ",{\"id\":" : "{\"id\":") + std::to_string(l.id) + ",\"kind\":\"" +
             (l.kind == Kind::Group ? "group" : "pixel") + "\",\"props\":" + to_json(l.props);
        if (l.kind == Kind::Group) {
          s += ",\"layers\":";
          layers(l);
        } else {
          s += ",\"fill\":";
          samples(s, l.pixels->fill(), channels);
          s += ",\"tiles\":[";
          bool first = true;
          for (const auto& [xy, t] : l.pixels->tiles()) {
            s += (first ? "[" : ",[") + std::to_string(xy.x) + ',' + std::to_string(xy.y) + ',';
            if (t->is_solid())
              samples(s, t->value(), channels);
            else
              s += std::to_string(chunk_of.at(t->id()));
            s += ']';
            first = false;
          }
          const std::string& m = mip_entries[grid_at.at(l.pixels.get())];
          s += "],\"mips\":" + (m.empty() ? std::string("[]") : m + ']');
        }
        s += '}';
      }
      s += ']';
    };
    layers(*doc.root);
    s += '}';
    w.chunks[0] = w.write(s.data(), s.size(), crc_of(s.data(), s.size()));

    check();
    std::vector<uint8_t> index;
    put32(index, uint32_t(w.chunks.size()));
    for (const Chunk& c : w.chunks) put64(index, c.offset), put32(index, c.size), put32(index, c.crc);
    if (index.size() > UINT32_MAX) throw std::length_error("pb: a .pbd index past 4 GB");
    const uint64_t at = out.size();
    out.write(index.data(), index.size());
    out.sync();  // everything the footer points to lies on the disk before it does
    std::vector<uint8_t> foot;
    put64(foot, at), put32(foot, uint32_t(index.size())), put32(foot, crc_of(index.data(), index.size()));
    foot.insert(foot.end(), kFooterMagic, kFooterMagic + 8);
    out.write(foot.data(), foot.size());
    out.sync();
    next.end = out.size();
    std::copy(foot.begin(), foot.end(), next.footer.begin());
    out.close();
    if (!line) {
      replace_file(tmp, path);
      tmp.clear();
      line = std::make_shared<Line>();
    }
    adopt(path, from, line, next, written);
  } catch (...) {
    std::error_code e;
    if (!tmp.empty()) std::filesystem::remove(tmp, e);
    throw;
  }
  if (o.progress) o.progress(1);
}

OpenedPbd open_pbd(const std::filesystem::path& path) {
  PROFILE_ZONE("open_pbd");
  try {
    Reader r;
    try {
      r.file = std::make_shared<PbdFile>(path, 0);
    } catch (const std::system_error& e) {
      corrupt(std::string("cannot open: ") + e.code().message());
    }
    r.data = r.file->map.data();
    const size_t n = r.file->map.size();
    if (n < kHeader + kFooter) corrupt("cut short, or not a .pbd");
    const uint8_t* head = r.bytes(0, kHeader);
    if (std::memcmp(head, kMagic, 8) != 0) corrupt("not a .pbd");
    if (const uint32_t v = get32(head + 8); v != kFormat)
      throw DecodeError(v > kFormat ? DecodeError::Kind::Codec : DecodeError::Kind::Corrupt,
                        v > kFormat ? "pbd_version" : "",
                        "pbd: format " + std::to_string(v) + (v > kFormat ? ", newer than this app's" : ""));
    // The last footer whose index is whole: past it lies what a save cut short left, if anything, searched back once.
    // ponytail: byte by byte through a torn tail (a GB a second); a marker at each save's start if big appends make
    // opening after a crash slow.
    uint64_t end = n, at = 0;
    uint32_t size = 0, count = 0;
    for (;; end--) {
      if (end < kHeader + kFooter) corrupt("no footer: cut short, or damaged");
      if (r.data[end - 1] != kFooterMagic[7] || std::memcmp(r.data + end - 8, kFooterMagic, 8) != 0) continue;
      const uint8_t* foot = r.data + end - kFooter;
      at = get64(foot), size = get32(foot + 8);
      if (at < kHeader || at > end - kFooter || size > end - kFooter - at || size < 4) continue;
      const uint8_t* index = r.data + at;
      count = get32(index);
      if (crc_of(index, size) == get32(foot + 12) && count >= 1 && size - 4 == uint64_t(count) * kEntry) break;
    }
    r.read += n - end + kFooter + size;
    const uint8_t* index = r.data + at;
    r.chunks.resize(count);
    for (uint32_t i = 0; i < count; i++) {
      const uint8_t* e = index + 4 + kEntry * i;
      Chunk& c = r.chunks[i];
      c = {get64(e), get32(e + 8), get32(e + 12)};
      if (c.offset < kHeader || c.offset > at || c.size > at - c.offset) corrupt("a chunk lies outside the file");
    }

    const char* text = reinterpret_cast<const char*>(r.chunk(0, "the JSON"));
    if (nesting(text, text + r.chunks[0].size) > 4 * kMaxNesting) corrupt("the JSON nests too deep");
    const json j = json::parse(text, text + r.chunks[0].size, nullptr, false);  // discarded when malformed
    if (!j.is_object()) corrupt("the JSON is malformed");
    OpenedPbd out;
    try {
      auto d = std::make_shared<DocState>();
      d->width = int(integer(member(j, "width"), 1, INT_MAX, "the width"));
      d->height = int(integer(member(j, "height"), 1, INT_MAX, "the height"));
      const json& mode = member(j, "mode");
      if (mode != "rgb" && mode != "gray") corrupt("no such mode");
      d->mode = mode == "gray" ? Mode::Gray : Mode::Rgb;
      const int64_t depth = integer(member(j, "depth"), 8, 16, "the depth");
      if (depth != 8 && depth != 16) corrupt("no such depth");
      d->depth = depth == 16 ? Depth::U16 : Depth::U8;
      d->next_id = LayerId(integer(member(j, "nextId"), 1, UINT32_MAX, "the next layer id"));
      for (const json& id : array(j, "selected"))
        d->selected.push_back(LayerId(integer(id, 1, UINT32_MAX, "a selected layer")));
      auto line = std::make_shared<Line>();  // what a save into this file appends to
      line->end = end;
      std::memcpy(line->footer.data(), r.data + end - kFooter, kFooter);
      if (j.contains("icc")) {
        const uint32_t i = r.number(member(j, "icc"));
        const uint8_t* p = r.chunk(i, "the ICC profile");
        d->icc = line->icc = std::make_shared<const std::vector<uint8_t>>(p, p + r.chunks[i].size);
        line->icc_chunk = r.chunks[i];
      }
      r.file->line = std::move(line);
      const json& pv = member(j, "preview");
      out.preview =
          r.preview(r.number(member(pv, "chunk")), int(integer(member(pv, "level"), 0, 31, "the preview's level")), *d);
      r.format = pixel_format(*d);
      r.tiles.resize(count);
      auto root = std::make_shared<Layer>();
      root->kind = Kind::Group;
      for (const json& l : array(j, "layers")) root->children.push_back(r.layer(l, 1));
      d->root = std::move(root);
      validate(*d);
      out.doc = std::move(d);
    } catch (const std::invalid_argument& e) {  // validate()'s
      corrupt(std::string("the document breaks a rule: ") + e.what());
    } catch (const json::exception& e) {
      corrupt(std::string("the JSON: ") + e.what());
    }
    for (const Reader::Mip& m : r.mips) attach_mip(*m.grid, m.level, m.tx, m.ty, m.tile);
    {
      Places& p = places();
      const std::lock_guard l(p.m);
      for (uint32_t i = 0; i < count; i++)
        if (const TilePtr& t = r.tiles[i]) {
          p.by_id[t->id()] = {r.file, r.file.get(), r.chunks[i].offset, r.chunks[i].size, r.chunks[i].crc};
          r.file->ids.push_back(t->id());
        }
    }
    out.bytes_read = r.read;
    return out;
  } catch (const DecodeError& e) {
    const auto name = path.u8string();
    throw DecodeError(e.kind, e.codec, std::string(name.begin(), name.end()) + ": " + e.what());
  }
}

}  // namespace pb
