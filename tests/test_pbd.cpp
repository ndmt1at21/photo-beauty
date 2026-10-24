// The .pbd (pbd.h): random documents (trees of every kind of layer, masks, P1's props; U8 and U16, RGB and grey, odd
// sizes; data, solid and empty tiles, tiles past the canvas, fills, layers sharing tiles and masks) saved and opened
// again exact, the same file once saved again, then changed and saved into it (appended) and opened exact again; mips
// from level 3 attached, masks' too; P1's kinds and masks laid out as docs/architecture.md says, a 16-bit mask's frames
// of predict()'s bytes from format 2 on; the preview; opening reads the header, footer, index, JSON, ICC profile and
// preview only, tiles staying in the file; frames of an opened .pbd copied by the next save; damage: every cut, a byte
// flipped in each part, the JSON rewritten at random with CRCs made right, a kind of no known name, a grey mask, a
// mask's tile on a layer's frame: an error that lets go of the file, never a crash (ASan's run); a tile damaged in the
// file throws on pin; saving in the background while the UI thread edits; cancelling; saving over the file a document
// came from.
// test_pbd_big (PB_TEST_HEAVY=1, under tools/heavy.sh): documents shaped as D1 and D2-mini, timed.
#include <zlib.h>
#include <zstd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <random>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include "check.h"
#include "doc.h"
#include "jobs.h"
#include "mip.h"
#include "open.h"
#include "pbd.h"
#include "render.h"
#include "tile.h"
#include "tilecache.h"

using namespace pb;
namespace fs = std::filesystem;
using json = nlohmann::json;
using Clock = std::chrono::steady_clock;

namespace {

double ms_since(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

// A directory for a check's files, removed when the check ends or, should a CHECK fail (std::exit), at exit. The mip
// cache lets go of its tiles first: attached ones keep their file mapped, which Windows will not delete.
struct TempDir {
  fs::path path = fs::temp_directory_path() / ("pb-test-pbd-" + std::to_string(std::random_device{}()));
  TempDir() {
    fs::create_directories(path);
    left().push_back(path);
  }
  ~TempDir() {
    remove(path);
    left().pop_back();
  }
  static void remove(const fs::path& p) {
    clear_mips();
    std::error_code ec;
    fs::remove_all(p, ec);
  }
  static std::vector<fs::path>& left() {
    static struct Left {
      std::vector<fs::path> dirs;
      ~Left() {
        for (const fs::path& p : dirs) remove(p);
      }
    } left;
    return left.dirs;
  }
  fs::path operator/(const char* name) const { return path / name; }
  size_t files() const { return size_t(std::distance(fs::directory_iterator(path), fs::directory_iterator())); }
};

void put(const fs::path& p, const std::vector<uint8_t>& bytes) {
  std::ofstream f(p, std::ios::binary);
  CHECK(f.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size())) && (f.close(), !f.fail()));
}
std::vector<uint8_t> get(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
}

uint64_t mix(uint64_t x) {  // splitmix64
  x += 0x9e3779b97f4a7c15ull;
  x = (x ^ x >> 30) * 0xbf58476d1ce4e5b9ull;
  x = (x ^ x >> 27) * 0x94d049bb133111ebull;
  return x ^ x >> 31;
}
size_t tile_bytes(Format f) { return size_t(kTileSize) * kTileSize * size_t(f.bytes()); }
// The levels a save keeps: 3 up to where a tile covers the canvas.
int top_mip(const DocState& d) {
  int top = 3;
  while ((int64_t(kTileSize) << top) < std::max(d.width, d.height)) top++;
  return top;
}

// ---- The file as docs/architecture.md lays it out, read here on its own ----

uint32_t u32(const uint8_t* p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
uint64_t u64(const uint8_t* p) { return u32(p) | uint64_t(u32(p + 4)) << 32; }
void p32(std::vector<uint8_t>& b, uint32_t v) {
  for (int i = 0; i < 4; i++) b.push_back(uint8_t(v >> 8 * i));
}
uint32_t crc(const void* p, size_t n) { return uint32_t(crc32_z(0, static_cast<const Bytef*>(p), n)); }

struct Chunk {
  uint64_t offset;
  uint32_t size, crc;
};
struct Layout {
  uint64_t index_at = 0;
  uint32_t index_size = 0;
  std::vector<Chunk> chunks;
  json doc;  // chunk 0
};
Layout layout(const std::vector<uint8_t>& f) {
  Layout l;
  const uint8_t* foot = f.data() + f.size() - 24;
  CHECK(std::memcmp(foot + 16, "PBDINDEX", 8) == 0 && std::memcmp(f.data(), "\x89PBD\r\n\x1a\n", 8) == 0);
  l.index_at = u64(foot), l.index_size = u32(foot + 8);
  const uint8_t* index = f.data() + l.index_at;
  CHECK(crc(index, l.index_size) == u32(foot + 12));
  for (uint32_t i = 0, n = u32(index); i < n; i++) {
    const uint8_t* e = index + 4 + 16 * i;
    l.chunks.push_back({u64(e), u32(e + 8), u32(e + 12)});
    CHECK(crc(f.data() + l.chunks.back().offset, l.chunks.back().size) == l.chunks.back().crc);
  }
  l.doc = json::parse(f.begin() + std::ptrdiff_t(l.chunks[0].offset),
                      f.begin() + std::ptrdiff_t(l.chunks[0].offset + l.chunks[0].size));
  return l;
}
// The chunks the JSON names as tiles, full size and mip, of layers' pixels and masks.
std::vector<uint32_t> tile_chunks(const Layout& l) {
  std::vector<uint32_t> out;
  const auto grid = [&](const json& g) {
    for (const json& e : g["tiles"])
      if (e[2].is_number()) out.push_back(e[2].get<uint32_t>());
    for (const json& e : g["mips"])
      if (e[3].is_number()) out.push_back(e[3].get<uint32_t>());
  };
  const std::function<void(const json&)> walk = [&](const json& layers) {
    for (const json& j : layers) {
      if (j.contains("layers")) walk(j["layers"]);
      if (j.contains("tiles")) grid(j);
      if (j.contains("mask")) grid(j["mask"]);
    }
  };
  walk(l.doc["layers"]);
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}
// f with its JSON replaced by `text`: appended after the chunks, an index and a footer to match, CRCs right.
std::vector<uint8_t> with_json(const std::vector<uint8_t>& f, Layout l, const std::string& text) {
  std::vector<uint8_t> out(f.begin(), f.begin() + std::ptrdiff_t(l.index_at));
  l.chunks[0] = {out.size(), uint32_t(text.size()), crc(text.data(), text.size())};
  out.insert(out.end(), text.begin(), text.end());
  std::vector<uint8_t> index;
  p32(index, uint32_t(l.chunks.size()));
  for (const Chunk& c : l.chunks)
    p32(index, uint32_t(c.offset)), p32(index, uint32_t(c.offset >> 32)), p32(index, c.size), p32(index, c.crc);
  const uint64_t at = out.size();
  out.insert(out.end(), index.begin(), index.end());
  p32(out, uint32_t(at)), p32(out, uint32_t(at >> 32)), p32(out, uint32_t(index.size())),
      p32(out, crc(index.data(), index.size()));
  out.insert(out.end(), {'P', 'B', 'D', 'I', 'N', 'D', 'E', 'X'});
  return out;
}

// ---- Comparing documents ----

bool same_tile(const Tile* a, const Tile* b) {
  if (!a || !b) return a == b;
  if (a->format() != b->format() || a->is_solid() != b->is_solid()) return false;
  if (a->is_solid()) return a->value() == b->value();
  const Pin p = a->pin(), q = b->pin();
  return std::memcmp(p.data(), q.data(), tile_bytes(a->format())) == 0;
}
bool same_grid(const TileGrid& a, const TileGrid& b) {
  if (a.format() != b.format() || a.fill() != b.fill() || a.tiles().size() != b.tiles().size()) return false;
  for (auto i = a.tiles().begin(), j = b.tiles().begin(); i != a.tiles().end(); ++i, ++j)
    if (!(i->first == j->first) || !same_tile(i->second.get(), j->second.get())) return false;
  return true;
}
bool same_layer(const Layer& a, const Layer& b) {
  if (a.id != b.id || a.kind != b.kind || to_json(a.props) != to_json(b.props) ||
      a.children.size() != b.children.size() || !a.pixels != !b.pixels || !a.mask != !b.mask)
    return false;
  if ((a.pixels && !same_grid(*a.pixels, *b.pixels)) || (a.mask && !same_grid(*a.mask, *b.mask))) return false;
  for (size_t i = 0; i < a.children.size(); i++)
    if (!same_layer(*a.children[i], *b.children[i])) return false;
  return true;
}
bool same_doc(const DocState& a, const DocState& b) {
  return a.width == b.width && a.height == b.height && a.mode == b.mode && a.depth == b.depth &&
         a.next_id == b.next_id && a.selected == b.selected && (a.icc ? b.icc && *a.icc == *b.icc : !b.icc) &&
         same_layer(*a.root, *b.root);
}
void pixel_layers(const Layer& g, std::vector<const Layer*>& out) {
  for (const LayerPtr& l : g.children)
    if (l->kind == Kind::Pixel)
      out.push_back(l.get());
    else
      pixel_layers(*l, out);
}
std::vector<const Layer*> pixel_layers(const DocState& d) {
  std::vector<const Layer*> out;
  pixel_layers(*d.root, out);
  return out;
}
// The grids of the layers in the tree's order, each layer's pixels then its mask; one shared, once a layer.
void grids(const Layer& g, std::vector<const TileGrid*>& out) {
  for (const LayerPtr& l : g.children) {
    if (l->pixels) out.push_back(l->pixels.get());
    if (l->mask) out.push_back(l->mask.get());
    grids(*l, out);
  }
}
std::vector<const TileGrid*> grids(const DocState& d) {
  std::vector<const TileGrid*> out;
  grids(*d.root, out);
  return out;
}

// The opened grids' mips from level 3 up, pixels' and masks', are those saved, attached: the same as the original's,
// none built anew.
void check_mips(const DocState& saved, const DocState& opened) {
  const std::vector<const TileGrid*> a = grids(saved), b = grids(opened);
  CHECK(a.size() == b.size());
  const uint64_t built = mips_built();
  for (size_t i = 0; i < a.size(); i++)
    for (int level = 3; level <= top_mip(saved); level++)
      for (const auto& [xy, t] : b[i]->tiles()) {
        const int x = xy.x >> level, y = xy.y >> level;
        CHECK(same_tile(mip(*a[i], level, x, y).get(), mip(*b[i], level, x, y).get()));
      }
  CHECK(mips_built() == built);
}

// ---- Documents ----

std::shared_ptr<Tile> data_tile(Format f, std::mt19937& rng) {
  const std::shared_ptr<Tile> t = Tile::make(f);
  const WritePin w = t->write();
  const uint64_t seed = rng();
  const bool ramp = rng() % 2;  // else noise
  for (size_t i = 0; i < tile_bytes(f); i++) w.data()[i] = uint8_t(ramp ? i / 509 + seed : mix(seed + i));
  return t;
}
Pixel random_px(Format f, std::mt19937& rng) {
  Pixel v{};
  for (int c = 0; c < f.channels; c++) v[size_t(c)] = uint16_t(rng() % unsigned(max_value(f.depth) + 1));
  return v;
}
// Up to 6 tiles of g anywhere over the canvas and a tile past it: data, solid, or fill with a byte changed.
void random_tiles(TileGrid& g, const DocState& d, std::mt19937& rng) {
  const Format f = g.format();
  const unsigned across = unsigned(d.width + kTileSize - 1) / kTileSize + 2,
                 down = unsigned(d.height + kTileSize - 1) / kTileSize + 2;
  for (int n = int(rng() % 7); n > 0; n--) {
    const int tx = int(rng() % across) - 1, ty = int(rng() % down) - 1;
    if (rng() % 3 == 0)
      g.set(tx, ty, Tile::solid(f, random_px(f, rng)));
    else if (rng() % 2)
      g.set(tx, ty, data_tile(f, rng));
    else
      g.write(tx, ty).data()[rng() % tile_bytes(f)] ^= 0x5a;
  }
}
// A layer's pixels, their fill's colour not black now and then (alpha 0, as a layer's).
std::shared_ptr<const TileGrid> random_grid(const DocState& d, std::mt19937& rng) {
  const Format f = pixel_format(d);
  Pixel fill = rng() % 3 ? Pixel{} : random_px(f, rng);
  fill[size_t(f.channels - 1)] = 0;
  auto g = std::make_shared<TileGrid>(f, fill);
  random_tiles(*g, d, rng);
  return g;
}
// A layer mask: white (Reveal All) or black where empty.
std::shared_ptr<const TileGrid> random_mask(const DocState& d, std::mt19937& rng) {
  const Format f = mask_format(d);
  auto g = std::make_shared<TileGrid>(f, Pixel{uint16_t(rng() % 2 ? max_value(f.depth) : 0)});
  random_tiles(*g, d, rng);
  return g;
}
// P0's props, and P1's (A30) now and then: clipping, the mask's, Blend If, Channels, an adjustment's name and the JSON
// objects as props_from_json() writes them (members sorted, no spaces).
LayerProps random_props(std::mt19937& rng, bool group) {
  static const char* const kBits[] = {"a", "Z", " ", "Lớp", "ảnh", "\"", "\\", "\n", "\x01", "€", "😀", "{"};
  static const char* const kExtras[] = {"", R"({"a":1})", R"({"z":null,"é":"ü"})",
                                        R"({"blendX":{"gray":[0,255,0,255]}})"};  // blendIf has its own shape since A30
  static const char* const kObjects[] = {
      "{}", R"({"a":1})", R"({"l":10,"rng":"Yellows","s":-30})",
      R"({"dropShadow":{"angle":120,"on":true},"stroke":{"color":"#f7c873","size":6}})",
      R"({"lines":["Xin chào","ảnh \"1\""],"size":72.5})"};
  static const char* const kAdjs[] = {"Curves", "Hue/Saturation", "Black & White"};
  const auto slider = [&] {
    std::array<uint8_t, 4> v{};
    for (uint8_t& x : v) x = uint8_t(rng() % 4 ? rng() : (rng() % 2) * 255);
    std::sort(v.begin(), v.end());
    return v;
  };
  LayerProps p;
  for (int n = int(rng() % 4); n > 0; n--) p.name += kBits[rng() % std::size(kBits)];
  p.visible = rng() % 4;
  p.opacity = rng() % 2 ? 1 : float(rng() % 1000) / 999;
  p.fill = rng() % 3 ? 1 : float(rng() % 1000) / 999;
  p.blend = Blend(rng() % std::size(kBlendNames));
  if (p.blend == Blend::PassThrough && !group) p.blend = Blend::Multiply;
  p.lock_pixels = rng() % 5 == 0, p.lock_all = rng() % 7 == 0;
  if (rng() % 2) {
    p.clip = rng() % 2, p.mask_off = rng() % 3 == 0, p.mask_linked = rng() % 3;
    p.mask_density = rng() % 2 ? 1 : float(rng() % 1000) / 999;
    p.mask_feather = rng() % 2 ? 0 : float(rng() % 100001) / 100;
    for (BlendRange& r : p.blend_if)
      if (rng() % 4 == 0) r.layer = slider(), r.under = slider();
    for (bool* c : {&p.channels.r, &p.channels.g, &p.channels.b, &p.channels.gray}) *c = rng() % 4;
    if (rng() % 3 == 0) p.adj = kAdjs[rng() % std::size(kAdjs)];
    for (JsonPtr* j : {&p.vals, &p.fx, &p.content, &p.source})
      if (rng() % 3 == 0) *j = std::make_shared<const std::string>(kObjects[rng() % std::size(kObjects)]);
  }
  p.extra = kExtras[rng() % std::size(kExtras)];
  return p;
}
void ids(const Layer& g, std::vector<LayerId>& all, std::vector<LayerId>& groups) {
  for (const LayerPtr& l : g.children) {
    all.push_back(l->id);
    if (l->kind == Kind::Group) groups.push_back(l->id), ids(*l, all, groups);
  }
}
// n random commands on d: layers of every kind (pixel, text and shape ones with pixels, adjustment and fill ones
// without), groups (nested), with a mask now and then; duplicates sharing tiles and masks; masks set, replaced and
// deleted; layers replaced (a text layer rasterized, drawn again); moves, deletions (gaps in the ids), selections.
// Those d refuses (a move into itself, say) are skipped.
DocPtr commands(DocPtr d, std::mt19937& rng, int n) {
  static const Kind kKinds[] = {Kind::Adj, Kind::Fill, Kind::Text, Kind::Shape, Kind::Pixel};  // P1's first
  for (; n > 0; n--) {
    std::vector<LayerId> all, groups{0};
    ids(*d->root, all, groups);
    const LayerId g = groups[rng() % groups.size()];
    const int at = int(rng() % (find_layer(*d, g)->children.size() + 1));
    const auto some = [&] { return all.empty() ? LayerId(1) : all[rng() % all.size()]; };
    const auto mask = [&] { return rng() % 3 ? nullptr : random_mask(*d, rng); };
    const auto pixels = [&](Kind k) { return has_pixels(k) ? random_grid(*d, rng) : nullptr; };
    const Kind added = kKinds[rng() % 4], replaced = kKinds[rng() % std::size(kKinds)];
    Command c = AddLayer{g, at, Kind::Pixel, random_props(rng, false), random_grid(*d, rng), mask()};
    switch (rng() % 11) {
      case 0:
        c = AddLayer{g, at, Kind::Group, random_props(rng, true), nullptr, mask()};
        break;
      case 1:
        c = DuplicateLayers{{some()}};
        break;
      case 2:
        c = MoveLayer{some(), g, 0};
        break;
      case 3:
        c = DeleteLayers{{some()}};
        break;
      case 4:
        c = SelectLayers{{some(), some()}};
        break;
      case 5:
      case 6:
        c = AddLayer{g, at, added, random_props(rng, false), pixels(added), mask()};
        break;
      case 7:
        c = SetMask{some(), rng() % 4 ? random_mask(*d, rng) : nullptr};
        break;
      case 8:
        c = ReplaceLayer{some(), replaced, random_props(rng, false), pixels(replaced), mask()};
        break;
      default:
        break;
    }
    try {
      d = run(*d, c);
    } catch (const std::invalid_argument&) {
    }
  }
  return d;
}
// A size from 1 x 1 to 900 x 700, up to 14 random commands, an ICC profile of random bytes or none.
DocPtr random_doc(std::mt19937& rng, Mode mode, Depth depth) {
  std::shared_ptr<std::vector<uint8_t>> icc;
  if (rng() % 4) {
    icc = std::make_shared<std::vector<uint8_t>>(rng() % 3000);
    for (uint8_t& b : *icc) b = uint8_t(rng());
  }
  const DocPtr d = new_document(1 + int(rng() % 900), 1 + int(rng() % 700), mode, depth, icc);
  return commands(d, rng, int(rng() % 15));
}

// Whether `p` is mapped in this process (Linux; false elsewhere): a failed open must let go of its file, which mip
// tiles attached before the failure, or a grid of it leaked, would keep mapped (on Windows, locked against writes).
bool mapped(const fs::path& p) {
#ifdef __linux__
  std::ifstream maps("/proc/self/maps");
  const std::string name = ' ' + fs::weakly_canonical(p).string();
  for (std::string line; std::getline(maps, line);)
    if (line.size() >= name.size() && line.compare(line.size() - name.size(), name.size(), name) == 0) return true;
#else
  (void)p;
#endif
  return false;
}

// Whether opening p throws a DecodeError of this kind naming the file, and lets go of it.
bool fails(const fs::path& p, DecodeError::Kind kind = DecodeError::Kind::Corrupt) {
  try {
    open_pbd(p);
  } catch (const DecodeError& e) {
    if (e.kind == kind && std::strstr(e.what(), p.filename().string().c_str()) && !mapped(p)) return true;
    std::printf("unexpected error: %s (kind %d)\n", e.what(), int(e.kind));
  }
  return false;
}

// ---- Checks ----

// Random documents of each mode and depth come back as they were: tree, kinds, properties' JSON, ids, selection, ICC
// profile, fills, every tile and sample of pixels and masks; layers sharing tiles or a mask share the tiles again;
// mips attached; the preview's size; saved again, the same file. Then changed (masks and kinds too) and saved into
// their file, which takes an append when it holds some of their tiles, every byte before kept: back as they were.
void check_round_trip() {
  TempDir dir;
  std::mt19937 rng(1);
  int masks = 0, tiles = 0, appended = 0;
  std::array<int, std::size(kKindNames)> kinds{};
  for (int i = 0; i < 48; i++) {
    const Mode mode = i % 2 ? Mode::Gray : Mode::Rgb;
    const Depth depth = i / 2 % 2 ? Depth::U16 : Depth::U8;
    const DocPtr d = random_doc(rng, mode, depth);
    save_pbd(*d, dir / "r.pbd");
    const std::vector<uint8_t> saved = get(dir / "r.pbd");
    const OpenedPbd o = open_pbd(dir / "r.pbd");
    CHECK(same_doc(*d, *o.doc));
    validate(*o.doc);
    check_mips(*d, *o.doc);
    const std::vector<const TileGrid*> a = grids(*d), b = grids(*o.doc);
    std::map<const TileGrid*, size_t> first;  // grids shared in d: their tiles shared in o
    for (size_t k = 0; k < a.size(); k++) {
      tiles += int(a[k]->tiles().size());
      if (const auto [it, fresh] = first.emplace(a[k], k); !fresh)
        for (auto x = b[k]->tiles().begin(), y = b[it->second]->tiles().begin(); x != b[k]->tiles().end(); ++x, ++y)
          CHECK(x->second->is_solid() || x->second == y->second);
    }
    const std::function<void(const Layer&)> count = [&](const Layer& g) {
      for (const LayerPtr& l : g.children) kinds[size_t(l->kind)]++, masks += bool(l->mask), count(*l);
    };
    count(*d->root);
    int level = 0;
    while (((int64_t(d->width) - 1) >> level) + 1 > 2048 || ((int64_t(d->height) - 1) >> level) + 1 > 2048) level++;
    const Preview& p = o.preview;
    CHECK(p.level == level && p.width == ((d->width - 1) >> level) + 1 && p.height == ((d->height - 1) >> level) + 1);
    CHECK(p.channels == (mode == Mode::Gray ? 1 : 3) &&
          p.pixels.size() == size_t(p.width) * size_t(p.height) * size_t(p.channels));

    // Changed, saved into its file; the opened document then saved elsewhere, its frames copied: the same bytes.
    const DocPtr e = commands(o.doc, rng, 1 + int(rng() % 4));
    save_pbd(*e, dir / "r.pbd");
    const std::vector<uint8_t> now = get(dir / "r.pbd");
    appended += now.size() > saved.size() && std::equal(saved.begin(), saved.end(), now.begin());
    const OpenedPbd q = open_pbd(dir / "r.pbd");
    CHECK(same_doc(*e, *q.doc));
    validate(*q.doc);
    check_mips(*e, *q.doc);
    save_pbd(*o.doc, dir / "c.pbd");
    CHECK(get(dir / "c.pbd") == saved);
  }
  std::string names;
  for (size_t k = 0; k < kinds.size(); k++) names += (k ? ", " : "") + std::to_string(kinds[k]) + ' ' + kKindNames[k];
  std::printf("pbd: 48 random documents (%s layers, %d masks, %d tiles) back as they were, %d of them appended to\n",
              names.c_str(), masks, tiles, appended);
  CHECK(*std::min_element(kinds.begin(), kinds.end()) >= 10 && masks >= 30 && appended >= 16);
}

// The layers of a .pbd's JSON by id, those in groups too.
std::map<LayerId, json> layers_by_id(const json& doc) {
  std::map<LayerId, json> out;
  const std::function<void(const json&)> walk = [&](const json& layers) {
    for (const json& j : layers) {
      out[j["id"].get<LayerId>()] = j;
      if (j.contains("layers")) walk(j["layers"]);
    }
  };
  walk(doc["layers"]);
  return out;
}
// Chunk c of file f unframed: a tile's zstd frame, n bytes.
std::vector<uint8_t> unframed(const std::vector<uint8_t>& f, const Chunk& c, size_t n) {
  std::vector<uint8_t> out(n);
  CHECK(ZSTD_decompress(out.data(), n, f.data() + c.offset, c.size) == n);
  return out;
}
// What a frame of tile t holds in format `version`: its pixels, a 16-bit tile's predict()'s bytes from 2 on.
std::vector<uint8_t> framed(const Tile& t, uint32_t version) {
  const Pin p = t.pin();
  std::vector<uint8_t> out(p.data(), p.data() + tile_bytes(t.format()));
  if (version >= 2 && t.format().depth == Depth::U16) {
    const std::vector<uint8_t> raw = out;
    predict(t.format(), raw.data(), out.data());
  }
  return out;
}

// P1's kinds and masks as docs/architecture.md lays them out, in formats 2 and 1: each kind by its name; pixel, text
// and shape layers with fill, tiles and mips, adjustment and fill layers without; masks of any kind under "mask", one
// sample a pixel, with their mips; a mask copies share framed once; a 16-bit mask's frames of predict()'s bytes in 2,
// of its pixels in 1. Saved into again (a mask set, a text layer rasterized), the file keeps its format and every byte,
// the frames of the mask it held where they lay.
void check_kinds() {
  TempDir dir;
  for (const uint32_t version : {2u, 1u}) {
    std::mt19937 rng(version);
    DocPtr d = new_document(1100, 300, Mode::Rgb, Depth::U16, nullptr);  // tiles 0 to 4 across: one mip at level 3
    const Format f = pixel_format(*d), m = mask_format(*d);
    const auto pixels = [&] {
      auto g = std::make_shared<TileGrid>(f);
      g->set(2, 0, data_tile(f, rng));
      return g;
    };
    auto mask = std::make_shared<TileGrid>(m, Pixel{65535});
    mask->set(0, 0, data_tile(m, rng));
    mask->set(1, 0, Tile::solid(m, {32768}));
    LayerProps curves, color, title;
    curves.adj = "Curves", curves.vals = std::make_shared<const std::string>(R"({"l":10})");
    color.content = std::make_shared<const std::string>(R"({"color":"#808080"})");
    title.source = std::make_shared<const std::string>(R"({"lines":["Xin chào"]})");
    d = run(*d, AddLayer{0, 0, Kind::Pixel, {}, pixels()});                                     // 1
    d = run(*d, AddLayer{0, 1, Kind::Adj, curves, nullptr, mask});                              // 2
    d = run(*d, AddLayer{0, 2, Kind::Fill, color, nullptr});                                    // 3
    d = run(*d, AddLayer{0, 3, Kind::Text, title, pixels(), mask});                             // 4
    d = run(*d, AddLayer{0, 4, Kind::Shape, {}, pixels()});                                     // 5
    d = run(*d, AddLayer{0, 5, Kind::Group, {}, nullptr, std::make_shared<const TileGrid>(m)});  // 6: Hide All
    d = run(*d, AddLayer{6, 0, Kind::Fill, color, nullptr});                                    // 7
    d = run(*d, DuplicateLayers{{2}});                                                          // 8
    SaveOptions o;
    o.version = version;
    const fs::path path = dir.path / ("k" + std::to_string(version) + ".pbd");
    save_pbd(*d, path, o);
    const std::vector<uint8_t> file = get(path);
    const Layout l = layout(file);
    std::map<LayerId, json> by_id = layers_by_id(l.doc);
    CHECK(by_id.size() == 8);
    for (const auto& [id, j] : by_id) {
      const Layer& x = *find_layer(*d, id);
      CHECK(j["kind"] == kKindNames[size_t(x.kind)] && j.contains("layers") == (x.kind == Kind::Group));
      for (const char* key : {"fill", "tiles", "mips"}) CHECK(j.contains(key) == has_pixels(x.kind));
      CHECK(j.contains("mask") == bool(x.mask) && (!x.mask || j["mask"]["fill"] == json::array({x.mask->fill()[0]})));
    }
    const json& shared = by_id[2]["mask"];
    CHECK(by_id[4]["mask"] == shared && by_id[8]["mask"] == shared);
    CHECK(shared["tiles"].size() == 2 && shared["tiles"][1] == json::parse("[1,0,[32768]]"));
    CHECK(shared["mips"].size() == 1 && shared["mips"][0][0] == 3 && shared["mips"][0][3].is_number());
    CHECK(by_id[6]["mask"] == json::parse(R"({"fill":[0],"tiles":[],"mips":[]})"));
    CHECK(tile_chunks(l).size() == 8);  // the data tiles of 1, 4, 5 and the mask, and their mips: each once
    const Chunk& tile = l.chunks[shared["tiles"][0][2].get<uint32_t>()];
    CHECK(unframed(file, tile, tile_bytes(m)) == framed(*mask->get(0, 0), version));
    CHECK(unframed(file, l.chunks[shared["mips"][0][3].get<uint32_t>()], tile_bytes(m)) ==
          framed(*mip(*mask, 3, 0, 0), version));

    const OpenedPbd op = open_pbd(path);
    CHECK(same_doc(*d, *op.doc));
    check_mips(*d, *op.doc);
    CHECK(find_layer(*op.doc, 2)->mask->get(0, 0) == find_layer(*op.doc, 8)->mask->get(0, 0));
    auto drawn = std::make_shared<TileGrid>(m);
    drawn->set(3, 1, data_tile(m, rng));
    DocPtr e = run(*op.doc, SetMask{1, drawn});
    LayerProps flat = find_layer(*e, 4)->props;
    flat.source = nullptr;
    e = run(*e, ReplaceLayer{4, Kind::Pixel, flat, find_layer(*e, 4)->pixels, nullptr});
    save_pbd(*e, path);
    const std::vector<uint8_t> after = get(path);
    CHECK(after.size() > file.size() && std::equal(file.begin(), file.end(), after.begin()) && u32(&after[8]) == version);
    const Layout la = layout(after);
    std::map<LayerId, json> now = layers_by_id(la.doc);
    CHECK(now[4]["kind"] == "pixel" && !now[4].contains("mask") && now[2]["mask"]["tiles"].size() == 2);
    const Chunk& kept = la.chunks[now[2]["mask"]["tiles"][0][2].get<uint32_t>()];
    const Chunk& added = la.chunks[now[1]["mask"]["tiles"][0][2].get<uint32_t>()];
    CHECK(kept.offset == tile.offset && added.offset >= file.size());
    CHECK(unframed(after, added, tile_bytes(m)) == framed(*drawn->get(3, 1), version));
    const OpenedPbd q = open_pbd(path);
    CHECK(same_doc(*e, *q.doc));
    check_mips(*e, *q.doc);
  }
}

// The preview is the composite at the first level 2048 pixels or fewer across both ways, flattened over white, within
// JPEG's error: smooth layers, one half transparent over an opaque one, in RGB and grey; its size at that level.
void check_preview() {
  TempDir dir;
  for (const Mode mode : {Mode::Rgb, Mode::Gray}) {
    const int w = 700, h = 500;
    DocPtr d = new_document(w, h, mode, Depth::U16, nullptr);
    const Format f = pixel_format(*d);
    for (int k = 0; k < 2; k++) {
      std::vector<uint16_t> px(size_t(w) * h * size_t(f.channels));
      for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
          uint16_t* p = &px[(size_t(y) * w + x) * size_t(f.channels)];
          for (int c = 0; c < f.channels - 1; c++)
            p[c] = uint16_t(k ? 60000 - x * 70 - c * 4000 : 5000 + y * 100 + c * 3000);
          p[f.channels - 1] = uint16_t(k ? 20000 + y * 60 : 65535);
        }
      auto g = std::make_shared<TileGrid>(f);
      g->write_rect(0, 0, w, h, reinterpret_cast<const uint8_t*>(px.data()), size_t(w) * size_t(f.bytes()));
      d = run(*d, AddLayer{0, k, Kind::Pixel, {}, g});
    }
    save_pbd(*d, dir / "p.pbd");
    const Preview p = open_pbd(dir / "p.pbd").preview;
    CHECK(p.level == 0 && p.width == w && p.height == h && p.channels == (mode == Mode::Gray ? 1 : 3));
    double sum = 0;
    int worst = 0;
    for (int ty = 0; ty < 2; ty++)
      for (int tx = 0; tx < 3; tx++) {
        const TilePtr t = composite_tile({d, std::nullopt}, 0, tx, ty);
        const Pin pin = t->pin();
        const auto* c = reinterpret_cast<const uint16_t*>(pin.data());
        for (int y = ty * kTileSize; y < std::min(h, (ty + 1) * kTileSize); y++)
          for (int x = tx * kTileSize; x < std::min(w, (tx + 1) * kTileSize); x++) {
            const uint16_t* v = c + (size_t(y - ty * kTileSize) * kTileSize + size_t(x - tx * kTileSize)) * 4;
            for (int ch = 0; ch < p.channels; ch++) {
              const double a = v[3] / 65535., want = (v[ch] / 65535. * a + 1 - a) * 255;
              const double off =
                  std::abs(want - p.pixels[(size_t(y) * w + size_t(x)) * size_t(p.channels) + size_t(ch)]);
              sum += off, worst = std::max(worst, int(std::lround(off)));
            }
          }
      }
    const double mean = sum / (double(w) * h * p.channels);
    std::printf("pbd: preview %s: %.2f levels off on average, %d at most\n", mode == Mode::Gray ? "grey" : "RGB", mean,
                worst);
    CHECK(mean < 1.5 && worst <= 16);
  }
  // Sizes: the first level within 2048; no layers, all white.
  const int sizes[][5] = {
      {5000, 300, 2, 1250, 75}, {2048, 2048, 0, 2048, 2048}, {2049, 10, 1, 1025, 5}, {1, 1, 0, 1, 1}};
  for (const auto& s : sizes) {
    save_pbd(*new_document(s[0], s[1], Mode::Rgb, Depth::U8, nullptr), dir / "s.pbd");
    const Preview p = open_pbd(dir / "s.pbd").preview;
    CHECK(p.level == s[2] && p.width == s[3] && p.height == s[4]);
    CHECK(std::all_of(p.pixels.begin(), p.pixels.end(), [](uint8_t v) { return v == 255; }));
  }
}

// A document over several tiles, with groups, solid tiles, masks (the group's white where empty, a layer's black)
// and an ICC profile.
DocPtr sample_doc(Mode mode, Depth depth, int w, int h, uint32_t seed) {
  std::mt19937 rng(seed);
  DocPtr d = new_document(w, h, mode, depth, std::make_shared<const std::vector<uint8_t>>(200, uint8_t(seed)));
  // A grid of format f over the canvas: solid, data and empty tiles by turns, from k on.
  const auto tiles = [&](Format f, Pixel fill, int k) {
    auto g = std::make_shared<TileGrid>(f, fill);
    for (int ty = 0; ty < (h + kTileSize - 1) / kTileSize; ty++)
      for (int tx = 0; tx < (w + kTileSize - 1) / kTileSize; tx++)
        if ((tx + ty + k) % 3 == 0)
          g->set(tx, ty, Tile::solid(f, random_px(f, rng)));
        else if ((tx + ty + k) % 3 == 1)
          g->set(tx, ty, data_tile(f, rng));
    return g;
  };
  const Format f = pixel_format(*d), m = mask_format(*d);
  d = run(*d, AddLayer{0, 0, Kind::Group, random_props(rng, true), nullptr, tiles(m, {uint16_t(max_value(depth))}, 2)});
  for (int k = 0; k < 4; k++)
    d = run(*d, AddLayer{k % 2 ? 1u : 0u, 0, Kind::Pixel, random_props(rng, false), tiles(f, {}, k),
                         k == 1 ? tiles(m, {}, 0) : nullptr});
  return d;
}
// d with every layer hidden: its preview pins no tile.
DocPtr hidden(DocPtr d) {
  std::vector<LayerId> all, groups;
  ids(*d->root, all, groups);
  for (const LayerId id : all) {
    LayerProps p = find_layer(*d, id)->props;
    p.visible = false;
    d = run(*d, SetProps{id, p});
  }
  return d;
}

// Opening reads what it needs and no tile: bytes read are the header, footer, index, JSON, ICC profile and preview,
// and no tile has its pixels in RAM after. With every tile's frame damaged in the file, it opens all the same; each
// tile, full size or mip, throws on every pin, harmlessly; saving that document fails, leaving nothing behind, even
// with its layers hidden, so that only the frames' CRCs, checked as they are copied, tell.
void check_lazy() {
  TempDir dir;
  const DocPtr d = sample_doc(Mode::Rgb, Depth::U16, 1100, 900, 2);
  save_pbd(*d, dir / "a.pbd");
  const std::vector<uint8_t> f = get(dir / "a.pbd");
  const Layout l = layout(f);
  const uint64_t want = 32 + 24 + l.index_size + l.chunks[0].size + l.chunks[l.doc["icc"].get<uint32_t>()].size +
                        l.chunks[l.doc["preview"]["chunk"].get<uint32_t>()].size;
  trim_tiles();
  const TileStats s0 = tile_stats();
  {
    const OpenedPbd o = open_pbd(dir / "a.pbd");
    std::printf("pbd: opening a %zu-byte file read %llu bytes\n", f.size(), (unsigned long long)o.bytes_read);
    CHECK(o.bytes_read == want && tile_stats().raw <= s0.raw && same_doc(*d, *o.doc));
  }
  std::vector<uint8_t> bad = f;
  const std::vector<uint32_t> tiles = tile_chunks(l);
  CHECK(tiles.size() > 20);
  for (const uint32_t c : tiles) bad[l.chunks[c].offset + l.chunks[c].size / 2] ^= 0x55;
  put(dir / "bad.pbd", bad);
  const OpenedPbd o = open_pbd(dir / "bad.pbd");
  CHECK(o.doc->root->children.size() == d->root->children.size() && o.preview.width == 1100);
  int threw = 0, data = 0;
  for (const TileGrid* g : grids(*o.doc))
    for (const auto& [xy, t] : g->tiles()) {
      std::vector<TilePtr> all{t};
      for (int level = 3; level <= top_mip(*d); level++) all.push_back(mip(*g, level, xy.x >> level, xy.y >> level));
      for (const TilePtr& u : all)
        for (int k = 0; u && !u->is_solid() && k < 2; k++, data++) try {
            u->pin();
          } catch (const std::runtime_error&) {
            threw++;
          }
    }
  CHECK(data > 40 && threw == data && tile_stats().pinned == s0.pinned);
  for (const DocPtr& doc : {o.doc, hidden(o.doc)}) {
    bool failed = false;
    try {
      save_pbd(*doc, dir / "c.pbd");
    } catch (const std::runtime_error&) {
      failed = true;
    }
    CHECK(failed && !fs::exists(dir / "c.pbd") && dir.files() == 2);
  }
}

// Saving a document opened from a .pbd copies its frames: with its layers hidden (so the preview pins nothing), no
// tile is read back at all; the copy opens to the same pixels.
void check_copy() {
  TempDir dir;
  const DocPtr d = hidden(sample_doc(Mode::Gray, Depth::U8, 1300, 700, 3));
  save_pbd(*d, dir / "a.pbd");
  {
    const OpenedPbd o = open_pbd(dir / "a.pbd");
    trim_tiles();
    const size_t raw = tile_stats().raw;
    save_pbd(*o.doc, dir / "b.pbd");
    CHECK(tile_stats().raw <= raw);
  }
  CHECK(same_doc(*d, *open_pbd(dir / "b.pbd").doc));
  CHECK(get(dir / "a.pbd") == get(dir / "b.pbd"));  // the same document makes the same file
}

// Every cut is refused; a byte flipped anywhere opening reads (magic and version, footer, index, JSON, ICC profile,
// preview) is refused; a newer format says so; the JSON rewritten at random with CRCs made right opens to an error or
// to a document validate() takes, whose tiles then read or throw, never crash; kinds read by name, one of no known
// name refused, as are a grey mask and a mask's tile or mip on a layer's frame, a layer's on a mask's; a failed open
// leaves the file unmapped.
void check_damage() {
  TempDir dir;
  // Small, so that the files written here stay small: ramps compress well. Group 2 (masked) at the bottom of the root
  // holding layer 3, layer 1 over it, adjustment layer 4 on top with the group's mask.
  DocPtr d =
      new_document(300, 200, Mode::Rgb, Depth::U8, std::make_shared<const std::vector<uint8_t>>(200, uint8_t(9)));
  const Format f8 = pixel_format(*d), m8 = mask_format(*d);
  const auto ramp = [](Format f, int k) {
    const std::shared_ptr<Tile> t = Tile::make(f);
    const WritePin w = t->write();
    for (size_t i = 0; i < tile_bytes(f); i++) w.data()[i] = uint8_t(i / 777 + size_t(k) * 50);
    return t;
  };
  auto mask = std::make_shared<TileGrid>(m8, Pixel{255});
  mask->set(0, 0, ramp(m8, 3));
  mask->set(1, 0, Tile::solid(m8, {128}));
  for (int k = 0; k < 2; k++) {
    auto g = std::make_shared<TileGrid>(f8);
    g->set(k - 1, 0, ramp(f8, k));
    g->set(1, 0, Tile::solid(f8, {1, 2, 3, uint16_t(100 + k)}));
    d = run(*d, AddLayer{0, 0, k ? Kind::Group : Kind::Pixel, {}, k ? nullptr : g, k ? mask : nullptr});
    if (k) d = run(*d, AddLayer{d->next_id - 1, 0, Kind::Pixel, {}, g});
  }
  LayerProps curves;
  curves.adj = "Curves", curves.vals = std::make_shared<const std::string>(R"({"l":10})");
  d = run(*d, AddLayer{0, 2, Kind::Adj, curves, nullptr, mask});
  save_pbd(*d, dir / "a.pbd");
  const std::vector<uint8_t> f = get(dir / "a.pbd");
  const Layout l = layout(f);
  fs::path p;
  int files = 0;
  const auto fresh = [&] {  // a new name each time: Windows will not write over a file the mip cache still maps
    std::error_code ec;
    if (!p.empty()) fs::remove(p, ec);
    return dir.path / ("x" + std::to_string(files++) + ".pbd");
  };
  int cut = 0;
  for (size_t k = 0; k < f.size(); k += k < 64 || k + 24 > f.size() ? 1 : 1 + f.size() / 300, cut++) {
    put(p = fresh(), std::vector<uint8_t>(f.begin(), f.begin() + std::ptrdiff_t(k)));
    CHECK(fails(p));
  }
  std::mt19937 rng(5);
  std::vector<size_t> at;  // where to flip
  for (size_t i = 0; i < 12; i++) at.push_back(i);
  for (size_t i = l.index_at; i < f.size(); i++) at.push_back(i);
  for (const json& c : {json(0), l.doc["icc"], l.doc["preview"]["chunk"]}) {
    const Chunk& k = l.chunks[c.get<uint32_t>()];
    for (size_t i = k.offset; i < k.offset + k.size; i += 1 + rng() % 9) at.push_back(i);
  }
  for (const size_t i : at) {
    std::vector<uint8_t> b = f;
    b[i] ^= uint8_t(1 + rng() % 255);
    put(p = fresh(), b);
    if (i >= 8 && i < 12 && u32(&b[8]) == 1)  // format 1, which frames 8-bit tiles as 2 does
      CHECK(same_doc(*d, *open_pbd(p).doc));
    else
      CHECK(fails(p, i >= 8 && i < 12 && u32(&b[8]) > 2 ? DecodeError::Kind::Codec : DecodeError::Kind::Corrupt));
  }
  std::vector<uint8_t> newer = f;
  newer[8] = 3;
  put(p = fresh(), newer);
  CHECK(fails(p, DecodeError::Kind::Codec));

  const std::string text(f.begin() + std::ptrdiff_t(l.chunks[0].offset),
                         f.begin() + std::ptrdiff_t(l.chunks[0].offset + l.chunks[0].size));
  put(p = fresh(), with_json(f, l, text));
  CHECK(same_doc(*d, *open_pbd(p).doc));
  static const char* const kValues[] = {"-1",
                                        "0",
                                        "1",
                                        "2",
                                        "3",
                                        "4",
                                        "7",
                                        "31",
                                        "32",
                                        "255",
                                        "65535",
                                        "65536",
                                        "2147483647",
                                        "4294967296",
                                        "9223372036854775807",
                                        "18446744073709551615",
                                        "-9223372036854775808",
                                        "1e9",
                                        "0.5",
                                        "null",
                                        "true",
                                        "\"x\"",
                                        "[]",
                                        "{}",
                                        "[0]"};
  int opened = 0, fuzzed = 0;
  for (; fuzzed < 1500; fuzzed++) {
    std::string s = text;
    for (int n = 1 + int(rng() % 3); n > 0; n--) {
      const size_t i = rng() % s.size();
      if (rng() % 4) {  // a number or what follows a colon or comma, replaced by another value
        size_t a = i;
        while (a < s.size() && !std::strchr(":,[", s[a])) a++;
        size_t b = ++a;
        while (b < s.size() && !std::strchr(",]}", s[b])) b++;
        if (b < s.size()) s.replace(a, b - a, kValues[rng() % std::size(kValues)]);
      } else if (rng() % 2) {
        s[i] = char(rng());
      } else {
        const size_t n = std::min<size_t>(rng() % 40, s.size() - i);
        if (rng() % 2)
          s.erase(i, n);
        else
          s.insert(i, s.substr(i, n));
      }
    }
    put(p = fresh(), with_json(f, l, s));
    try {
      const OpenedPbd o = open_pbd(p);
      opened++;
      for (const TileGrid* g : grids(*o.doc))
        for (const auto& [xy, t] : g->tiles()) try {
            t->pin();
            mip(*g, 3, xy.x >> 3, xy.y >> 3);
          } catch (const std::runtime_error&) {
          }
    } catch (const DecodeError& e) {
      CHECK(e.kind == DecodeError::Kind::Corrupt && !mapped(p));
    }
  }
  std::printf("pbd: %d cut files and %zu flipped bytes refused; %d JSONs rewritten, %d opened\n", cut, at.size(),
              fuzzed, opened);

  // Kinds by name: each of kKindNames reads as itself on layer 1 (an adjustment or fill layer passing over its tiles,
  // a group missing its "layers"); "so" (smart objects wait for A31), another case, none, a number: refused.
  const json doc = json::parse(text);
  const auto edited = [&](const std::function<void(json&)>& edit) {
    json j = doc;
    edit(j);
    put(p = fresh(), with_json(f, l, j.dump()));
    return p;
  };
  CHECK(doc["layers"][1]["id"] == 1 && doc["layers"][1]["kind"] == "pixel");
  for (size_t k = 0; k < std::size(kKindNames); k++) {
    const fs::path q = edited([&](json& j) { j["layers"][1]["kind"] = kKindNames[k]; });
    if (Kind(k) == Kind::Group) {
      CHECK(fails(q));
      continue;
    }
    const OpenedPbd o = open_pbd(q);
    const Layer& x = *find_layer(*o.doc, 1);
    CHECK(x.kind == Kind(k) && bool(x.pixels) == has_pixels(Kind(k)));
    CHECK(!x.pixels || same_grid(*x.pixels, *find_layer(*d, 1)->pixels));
  }
  for (const json& kind : {json("so"), json("Pixel"), json(""), json(0), json(nullptr)})
    CHECK(fails(edited([&](json& j) { j["layers"][1]["kind"] = kind; })));
  // Masks: grey where empty; a mask's tile or mip tile on the frame of a layer's tile read before it (layer 3, in
  // group 2) or after (layer 1), a layer's tile on a mask's frame: refused.
  CHECK(fails(edited([](json& j) { j["layers"][0]["mask"]["fill"] = json::array({128}); })));
  const json before = doc["layers"][0]["layers"][0]["tiles"][0][2], after = doc["layers"][1]["tiles"][0][2];
  const json masked = doc["layers"][0]["mask"]["tiles"][0][2];
  CHECK(before.is_number() && after.is_number() && masked.is_number() && doc["layers"][0]["mask"]["mips"][0][3].is_number());
  for (const json& c : {before, after}) {
    CHECK(fails(edited([&](json& j) { j["layers"][0]["mask"]["tiles"][0][2] = c; })));
    CHECK(fails(edited([&](json& j) { j["layers"][0]["mask"]["mips"][0][3] = c; })));
  }
  CHECK(fails(edited([&](json& j) { j["layers"][1]["tiles"][0][2] = masked; })));
  CHECK(same_doc(*d, *open_pbd(edited([](json&) {})).doc));

  // Nesting: groups 200 deep open; arrays 100,000 deep in a layer's properties are refused before parsing (dumping
  // them would overflow the stack), as are groups 300 deep.
  for (const int deep : {200, 300}) {
    DocPtr g = new_document(10, 10, Mode::Gray, Depth::U8, nullptr);
    for (int k = 0; k < deep; k++) g = run(*g, AddLayer{LayerId(k), 0, Kind::Group, {}, nullptr});
    save_pbd(*g, p = fresh());
    if (deep == 200)
      CHECK(same_doc(*g, *open_pbd(p).doc));
    else
      CHECK(fails(p));
  }
  std::string nested = text;
  nested.insert(nested.find("\"props\":{") + 9, "\"x\":" + std::string(100000, '[') + std::string(100000, ']') + ',');
  put(p = fresh(), with_json(f, l, nested));
  CHECK(fails(p));
}

// A save stopped from its progress, or before it starts, throws Cancelled and leaves the file there as it was, with
// nothing beside it; progress runs up to 1.
void check_cancel() {
  TempDir dir;
  const DocPtr d = sample_doc(Mode::Rgb, Depth::U16, 2400, 1700, 6);  // 3 batches of full-size tiles
  const std::vector<uint8_t> old = {'o', 'l', 'd'};
  put(dir / "a.pbd", old);
  for (int when = 0; when < 2; when++) {
    SaveOptions o;
    o.cancel = make_token();
    o.cancel->store(when == 0);
    o.progress = [&](double) { o.cancel->store(true); };
    bool threw = false;
    try {
      save_pbd(*d, dir / "a.pbd", o);
    } catch (const Cancelled&) {
      threw = true;
    }
    CHECK(threw && get(dir / "a.pbd") == old && dir.files() == 1);
  }
  std::vector<double> parts;
  save_pbd(*d, dir / "a.pbd", {nullptr, [&](double x) { parts.push_back(x); }});
  CHECK(parts.size() > 3 && parts.back() == 1 && std::is_sorted(parts.begin(), parts.end()) && parts[0] > 0);
  CHECK(same_doc(*d, *open_pbd(dir / "a.pbd").doc));
}

// Saved in a Background job while the UI thread goes on changing the document: the UI thread only hands over the
// snapshot, its edits meanwhile never wait, and the file holds the snapshot.
void check_background() {
  TempDir dir;
  DocPtr doc = sample_doc(Mode::Rgb, Depth::U16, 2400, 1700, 7);
  const DocPtr snapshot = doc;
  std::mutex m;
  std::condition_variable cv;
  bool done = false;
  std::exception_ptr error;
  const auto t0 = Clock::now();
  submit(Priority::Background, [&, snapshot] {
    try {
      save_pbd(*snapshot, dir / "bg.pbd");
    } catch (...) {
      error = std::current_exception();
    }
    const std::lock_guard l(m);
    done = true;
    cv.notify_all();
  });
  const double ui = ms_since(t0);
  int edits = 0;
  double slowest = 0;
  for (bool saving = true; saving; edits++) {
    const auto e0 = Clock::now();
    const std::vector<const Layer*> layers = pixel_layers(*doc);
    const Layer& l = *layers[size_t(edits) % layers.size()];
    TileGrid g = *l.pixels;
    const std::vector<uint8_t> px(64 * 64 * 8, uint8_t(edits));
    g.write_rect(edits * 37 % 2000, edits * 53 % 1500, 64, 64, px.data(), 64 * 8);
    LayerProps props = l.props;
    props.opacity = float(edits % 100) / 100;
    const LayerId id = l.id;
    doc = run(*doc, SetPixels{id, std::make_shared<const TileGrid>(std::move(g))});
    doc = run(*doc, SetProps{id, props});
    slowest = std::max(slowest, ms_since(e0));
    const std::lock_guard lock(m);
    saving = !done;
  }
  {
    std::unique_lock l(m);
    cv.wait(l, [&] { return done; });
  }
  if (error) std::rethrow_exception(error);
  std::printf("pbd: saving in the background: %.3f ms on the UI thread, %d edits meanwhile, the slowest %.2f ms\n", ui,
              edits, slowest);
  CHECK(ui <= 5);
  const OpenedPbd o = open_pbd(dir / "bg.pbd");
  CHECK(same_doc(*snapshot, *o.doc) && !same_doc(*doc, *o.doc));
}

// Saving over the file a living document was opened from replaces it (Windows too: the mapping lets it be renamed
// over), the document reading on from the old file.
void check_same_path() {
  TempDir dir;
  const DocPtr d = sample_doc(Mode::Gray, Depth::U16, 700, 600, 8);
  save_pbd(*d, dir / "a.pbd");
  const OpenedPbd o = open_pbd(dir / "a.pbd");
  const LayerId id = pixel_layers(*o.doc)[0]->id;
  LayerProps p = pixel_layers(*o.doc)[0]->props;
  p.name = "changed";
  const DocPtr changed = run(*o.doc, SetProps{id, p});
  save_pbd(*changed, dir / "a.pbd");
  CHECK(same_doc(*d, *o.doc) && same_doc(*changed, *open_pbd(dir / "a.pbd").doc) && dir.files() == 1);
}

// ---- Documents shaped as D1 and D2-mini (plan.md), data made to compress about as photographs and paint do ----

// A layer made tile by tile on the pool: where any(tx, ty), fill(tx, ty, pixels) writes a tile's pixels; each kept
// compacted.
std::shared_ptr<const TileGrid> made(const DocState& d, const std::function<bool(int, int)>& any,
                                     const std::function<void(int, int, uint8_t*)>& fill) {
  const Format f = pixel_format(d);
  const int across = (d.width + kTileSize - 1) / kTileSize, down = (d.height + kTileSize - 1) / kTileSize;
  std::vector<TilePtr> tiles(size_t(across) * size_t(down));
  parallel_for(across * down, [&](int a, int b) {
    for (int i = a; i < b; i++) {
      if (!any(i % across, i / across)) continue;
      const std::shared_ptr<Tile> t = Tile::make(f);
      fill(i % across, i / across, t->write().data());
      tiles[size_t(i)] = compact(t, {});
    }
  });
  auto g = std::make_shared<TileGrid>(f);
  for (int i = 0; i < across * down; i++) g->set(i % across, i / across, tiles[size_t(i)]);
  return g;
}

// Smooth colour: slow waves, per channel.
double wave(int x, int y, int c) {
  return 0.5 + 0.28 * std::sin(x * 0.0021 + c * 1.7) * std::cos(y * 0.0017 - c * 0.9) +
         0.15 * std::sin((x + y) * 0.0006 + c);
}
// Noise in -1..1, the same for the same pixel and seed.
double grain(uint64_t seed, int x, int y, int c) {
  return double(mix(seed ^ (uint64_t(uint32_t(y)) << 34 | uint64_t(uint32_t(x)) << 2 | uint64_t(c))) >> 11) /
             double(1ull << 52) -
         1;
}
// Soft round blobs over the canvas, covering about `cover` of it: alpha 1 inside, falling to 0 over their edge.
struct Blobs {
  struct Blob {
    double x, y, r;
  };
  std::vector<Blob> all;
  Blobs(const DocState& d, double cover, uint32_t seed) {
    std::mt19937 rng(seed);
    for (double area = 0; area < cover * d.width * d.height;) {
      const double r = 150 + rng() % 600;
      all.push_back({double(rng() % unsigned(d.width)), double(rng() % unsigned(d.height)), r});
      area += 3.14159 * r * r * 0.8;  // overlaps and the canvas's edge take some
    }
  }
  bool near(int tx, int ty) const {
    for (const Blob& b : all) {
      const double cx = std::clamp(b.x, double(tx * kTileSize), double((tx + 1) * kTileSize));
      const double cy = std::clamp(b.y, double(ty * kTileSize), double((ty + 1) * kTileSize));
      if ((cx - b.x) * (cx - b.x) + (cy - b.y) * (cy - b.y) < b.r * b.r) return true;
    }
    return false;
  }
  double alpha(int x, int y) const {
    double a = 0;
    for (const Blob& b : all) a = std::max(a, std::clamp((b.r - std::hypot(x - b.x, y - b.y)) / 60, 0., 1.));
    return a;
  }
};

// Writes a tile of samples v(x, y, c) (0..1; c 0 to 2 for colour, 3 for alpha) at the document's depth.
void write_tile(const DocState& d, int tx, int ty, uint8_t* out, const std::function<double(int, int, int)>& v) {
  const int channels = pixel_format(d).channels;
  for (int y = 0; y < kTileSize; y++)
    for (int x = 0; x < kTileSize; x++) {
      const int px = tx * kTileSize + x, py = ty * kTileSize + y;
      const size_t at = (size_t(y) * kTileSize + size_t(x)) * size_t(channels);
      for (int c = 0; c < channels; c++) {
        const double s = px < d.width && py < d.height ? std::clamp(v(px, py, c == channels - 1 ? 3 : c), 0., 1.) : 0;
        if (d.depth == Depth::U16)
          reinterpret_cast<uint16_t*>(out)[at + size_t(c)] = uint16_t(std::lround(s * 65535));
        else
          out[at + size_t(c)] = uint8_t(std::lround(s * 255));
      }
    }
}

// D1: 8256 x 5504, 16 bits, 15 layers. A photograph (waves, grain of about 300 levels as a camera's 16 bits have),
// its frequency separation (Low: the waves; High at Linear Light: the grain around mid grey), 5 paint layers of soft
// blobs over about 20%, 5 layers standing in for adjustment layers (Soft Light, Overlay, Multiply) over about 25% in a
// group, and a small layer of sharp stripes for the text.
DocPtr make_d1() {
  const DocPtr blank = new_document(8256, 5504, Mode::Rgb, Depth::U16, nullptr);
  const DocState& s = *blank;
  DocPtr d = blank;
  const auto add = [&](LayerId parent, const char* name, Blend blend, const std::function<bool(int, int)>& any,
                       const std::function<double(int, int, int)>& v) {
    LayerProps p;
    p.name = name, p.blend = blend;
    const auto g = made(s, any, [&](int tx, int ty, uint8_t* o) { write_tile(s, tx, ty, o, v); });
    d = run(*d, AddLayer{parent, int(find_layer(*d, parent)->children.size()), Kind::Pixel, p, g});
  };
  const auto everywhere = [](int, int) { return true; };
  add(0, "Background", Blend::Normal, everywhere,
      [](int x, int y, int c) { return c == 3 ? 1 : wave(x, y, c) + grain(1, x, y, c) * 300 / 65535; });
  add(0, "Low", Blend::Normal, everywhere,
      [](int x, int y, int c) { return c == 3 ? 1 : wave(x, y, c) + grain(2, x, y, c) * 8 / 65535; });
  add(0, "High", Blend::LinearLight, everywhere,
      [](int x, int y, int c) { return c == 3 ? 1 : 0.5 + grain(1, x, y, c) * 150 / 65535; });
  for (int k = 0; k < 5; k++) {
    const Blobs b(s, 0.2, 10 + uint32_t(k));
    add(
        0, "Paint", Blend::Normal, [&](int tx, int ty) { return b.near(tx, ty); },
        [&](int x, int y, int c) {
          return c == 3 ? b.alpha(x, y) : wave(x + 900 * k, y, c) + grain(3, x, y, c) * 40 / 65535;
        });
  }
  d = run(*d, AddLayer{0, int(d->root->children.size()), Kind::Group, {}, nullptr});
  const LayerId group = d->next_id - 1;
  const Blend looks[] = {Blend::SoftLight, Blend::Overlay, Blend::Multiply, Blend::SoftLight, Blend::Overlay};
  for (int k = 0; k < 5; k++) {
    const Blobs b(s, 0.25, 20 + uint32_t(k));
    add(
        group, "Look", looks[k], [&](int tx, int ty) { return b.near(tx, ty); },
        [&](int x, int y, int c) {
          return c == 3 ? b.alpha(x, y) * 0.6 : 0.5 + 0.2 * std::sin(x * 0.001 + y * 0.0013 + c + k);
        });
  }
  add(
      0, "Text", Blend::Normal, [](int tx, int ty) { return tx >= 4 && tx < 9 && ty == 18; },
      [](int x, int y, int c) { return c == 3 ? double((x / 9 + y / 13) % 3 == 0) : 0.9; });
  return d;
}

// D2-mini: 5000 x 3750, 8 bits, 40 layers, each about 30% of the canvas in soft blobs of waves with a little grain.
DocPtr make_d2_mini() {
  const DocPtr blank = new_document(5000, 3750, Mode::Rgb, Depth::U8, nullptr);
  const DocState& s = *blank;
  DocPtr d = blank;
  for (int k = 0; k < 40; k++) {
    const Blobs b(s, 0.3, 100 + uint32_t(k));
    LayerProps p;
    p.name = "Layer " + std::to_string(k + 1);
    const auto g = made(
        s, [&](int tx, int ty) { return b.near(tx, ty); },
        [&](int tx, int ty, uint8_t* o) {
          write_tile(s, tx, ty, o, [&](int x, int y, int c) {
            return c == 3 ? b.alpha(x, y) : wave(x + 300 * k, y - 200 * k, c) + grain(uint64_t(k), x, y, c) * 2 / 255;
          });
        });
    d = run(*d, AddLayer{0, k, Kind::Pixel, p, g});
  }
  return d;
}

// Saves `d` blocking and in the background while the UI thread edits, opens it, checks every 5th tile; prints times.
void time_doc(const char* name, const DocPtr& d) {
  TempDir dir;
  size_t raw = 0, tiles = 0, canvas = 0;
  for (const Layer* l : pixel_layers(*d))
    for (const auto& [xy, t] : l->pixels->tiles()) raw += t->is_solid() ? 0 : tile_bytes(t->format()), tiles++;
  canvas = size_t((d->width + kTileSize - 1) / kTileSize) * size_t((d->height + kTileSize - 1) / kTileSize);
  std::printf("%s: %zu pixel layers, %zu tiles (%.0f%% of %zu a layer), %.0f MB of pixels\n", name,
              pixel_layers(*d).size(), tiles, 100. * double(tiles) / double(canvas * pixel_layers(*d).size()), canvas,
              double(raw) / 1e6);
  auto t0 = Clock::now();
  save_pbd(*d, dir / "a.pbd");
  const double save = ms_since(t0);
  const double size = double(fs::file_size(dir / "a.pbd"));
  std::printf("%s: saved in %.0f ms, %.0f MB (%.2fx), %.0f MB/s of pixels\n", name, save, size / 1e6,
              double(raw) / size, double(raw) / 1e3 / save);

  DocPtr doc = d;
  std::atomic<bool> done{false}, saved{false};
  t0 = Clock::now();
  submit(Priority::Background, [&, d] {
    try {
      save_pbd(*d, dir / "b.pbd");
      saved = true;
    } catch (const std::exception& e) {
      std::printf("%s: %s\n", name, e.what());
    }
    done = true;
  });
  const double ui = ms_since(t0);
  int edits = 0;
  double slowest = 0;
  for (; !done; edits++) {
    const auto e0 = Clock::now();
    const LayerPtr l = d->root->children[size_t(edits) % 3];
    LayerProps p = l->props;
    p.opacity = float(edits % 100) / 100;
    doc = run(*doc, SetProps{l->id, p});
    slowest = std::max(slowest, ms_since(e0));
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  std::printf(
      "%s: saved in the background in %.0f ms: %.3f ms on the UI thread, %d edits meanwhile, the slowest %.2f ms\n",
      name, ms_since(t0), ui, edits, slowest);
  CHECK(saved && ui <= 5);

  t0 = Clock::now();
  const OpenedPbd o = open_pbd(dir / "a.pbd");
  const double open = ms_since(t0);
  std::printf("%s: opened in %.1f ms, %llu bytes read, preview %d x %d at level %d\n", name, open,
              (unsigned long long)o.bytes_read, o.preview.width, o.preview.height, o.preview.level);
  t0 = Clock::now();
  const TilePtr first = o.doc->root->children[0]->pixels->tiles().begin()->second;
  first->pin();
  std::printf("%s: the first tile read in %.2f ms\n", name, ms_since(t0));
  const std::vector<const Layer*> a = pixel_layers(*d), b = pixel_layers(*o.doc);
  CHECK(a.size() == b.size());
  std::atomic<int> wrong{0};
  for (size_t i = 0; i < a.size(); i++) {
    CHECK(a[i]->pixels->tiles().size() == b[i]->pixels->tiles().size() && to_json(a[i]->props) == to_json(b[i]->props));
    std::vector<std::pair<const Tile*, const Tile*>> pairs;
    size_t k = 0;
    for (auto x = a[i]->pixels->tiles().begin(), y = b[i]->pixels->tiles().begin(); x != a[i]->pixels->tiles().end();
         ++x, ++y, ++k)
      if (k % 5 == 0) pairs.push_back({x->second.get(), y->second.get()});
    parallel_for(int(pairs.size()), [&](int s, int e) {
      for (int j = s; j < e; j++) wrong += !same_tile(pairs[size_t(j)].first, pairs[size_t(j)].second);
    });
  }
  CHECK(!wrong);
}

}  // namespace

TEST(test_pbd) {
  check_round_trip();
  check_kinds();
  check_preview();
  check_lazy();
  check_copy();
  check_damage();
  check_cancel();
  check_background();
  check_same_path();
}

// D1 and D2-mini: a few GB of RAM and disk, so only when asked (PB_TEST_HEAVY=1 tools/heavy.sh build/default/pb-tests
// test_pbd_big; d1 or d2mini for one of them).
TEST(test_pbd_big) {
  const char* which = std::getenv("PB_TEST_HEAVY");  // 1: both; d1 or d2mini: that one
  if (!which) return void(std::printf("skipping test_pbd_big: set PB_TEST_HEAVY=1\n"));
  for (int k = 0; k < 2; k++) {
    if (std::strcmp(which, k ? "d1" : "d2mini") == 0) continue;
    auto t0 = Clock::now();
    const DocPtr d = k ? make_d2_mini() : make_d1();
    std::printf("%s: made in %.0f ms\n", k ? "D2-mini" : "D1", ms_since(t0));
    time_doc(k ? "D2-mini" : "D1", d);
  }
}
