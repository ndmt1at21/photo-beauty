// Saving into the file a document lies in (pbd.h, M3). In process: a save appends what the file lacks (frames of new
// tiles and mips, the preview, the JSON, an index, a footer), keeping every byte before, so that the file cut back to
// its footer before, or cut anywhere in what was appended, opens to the save before, and bytes after its last footer
// change nothing; "waste" counts the bytes the newest index leaves out; tiles just saved read from the file, their
// copies in RAM gone; a cancelled append loses nothing; saving elsewhere, a document from elsewhere, or after a torn
// tail is a full save. test_pbd_crash then kills a process saving on and on 200 times at random moments: the file
// opens each time to the last save finished, or the one under way, exact.
#include <zlib.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <nlohmann/json.hpp>
#include <random>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include "check.h"
#include "doc.h"
#include "jobs.h"
#include "mapped.h"
#include "mip.h"
#include "pbd.h"
#include "tile.h"
#include "tilecache.h"

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

using namespace pb;
namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

// A directory for a check's files, removed when the check ends or, should a CHECK fail (std::exit), at exit; the mip
// cache lets go of its tiles first, as attached ones keep their file mapped, which Windows will not delete.
struct TempDir {
  fs::path path = fs::temp_directory_path() / ("pb-test-pbd-crash-" + std::to_string(std::random_device{}()));
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
  fs::path operator/(const std::string& name) const { return path / name; }
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

// ---- The file as docs/architecture.md lays it out, read here on its own: the footer at its very end ----

uint32_t u32(const uint8_t* p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
uint64_t u64(const uint8_t* p) { return u32(p) | uint64_t(u32(p + 4)) << 32; }
uint32_t crc(const void* p, size_t n) { return uint32_t(crc32_z(0, static_cast<const Bytef*>(p), n)); }

struct Chunk {
  uint64_t offset;
  uint32_t size;
};
struct Layout {
  uint64_t index_at = 0;
  uint32_t index_size = 0;
  std::vector<Chunk> chunks;
  json doc;  // chunk 0
  uint64_t waste() const { return doc["waste"].get<uint64_t>(); }
  // What waste should be: bytes before the index that no chunk of it covers.
  uint64_t uncovered() const {
    uint64_t used = 32;
    for (const Chunk& c : chunks) used += c.size;
    return index_at - used;
  }
  size_t past(uint64_t end) const {  // chunks from `end` on
    return size_t(std::count_if(chunks.begin(), chunks.end(), [&](const Chunk& c) { return c.offset >= end; }));
  }
};
Layout layout(const std::vector<uint8_t>& f) {
  Layout l;
  CHECK(f.size() > 56 && std::memcmp(f.data() + f.size() - 8, "PBDINDEX", 8) == 0);
  const uint8_t* foot = f.data() + f.size() - 24;
  l.index_at = u64(foot), l.index_size = u32(foot + 8);
  const uint8_t* index = f.data() + l.index_at;
  CHECK(crc(index, l.index_size) == u32(foot + 12));
  for (uint32_t i = 0, n = u32(index); i < n; i++) {
    const uint8_t* e = index + 4 + 16 * i;
    l.chunks.push_back({u64(e), u32(e + 8)});
    CHECK(crc(f.data() + l.chunks.back().offset, l.chunks.back().size) == u32(e + 12));
  }
  l.doc = json::parse(f.begin() + std::ptrdiff_t(l.chunks[0].offset),
                      f.begin() + std::ptrdiff_t(l.chunks[0].offset + l.chunks[0].size));
  return l;
}

// ---- Documents ----

bool same_tile(const Tile* a, const Tile* b) {
  if (!a || !b) return a == b;
  if (a->format() != b->format() || a->is_solid() != b->is_solid()) return false;
  if (a->is_solid()) return a->value() == b->value();
  const Pin p = a->pin(), q = b->pin();
  return std::memcmp(p.data(), q.data(), tile_bytes(a->format())) == 0;
}
bool same_layer(const Layer& a, const Layer& b) {
  if (a.id != b.id || a.kind != b.kind || to_json(a.props) != to_json(b.props) ||
      a.children.size() != b.children.size())
    return false;
  if (a.kind == Kind::Pixel) {
    const TileGrid &x = *a.pixels, &y = *b.pixels;
    if (x.fill() != y.fill() || x.tiles().size() != y.tiles().size()) return false;
    for (auto i = x.tiles().begin(), j = y.tiles().begin(); i != x.tiles().end(); ++i, ++j)
      if (!(i->first == j->first) || !same_tile(i->second.get(), j->second.get())) return false;
  }
  for (size_t i = 0; i < a.children.size(); i++)
    if (!same_layer(*a.children[i], *b.children[i])) return false;
  return true;
}
bool same_doc(const DocState& a, const DocState& b) {
  return a.width == b.width && a.height == b.height && a.mode == b.mode && a.depth == b.depth &&
         a.next_id == b.next_id && a.selected == b.selected && (a.icc ? b.icc && *a.icc == *b.icc : !b.icc) &&
         same_layer(*a.root, *b.root);
}
std::vector<const Layer*> pixel_layers(const DocState& d) {
  std::vector<const Layer*> out;
  const std::function<void(const Layer&)> walk = [&](const Layer& g) {
    for (const LayerPtr& l : g.children)
      if (l->kind == Kind::Pixel)
        out.push_back(l.get());
      else
        walk(*l);
  };
  walk(*d.root);
  return out;
}
// Every pixel of every layer, read back.
uint64_t pixels_hash(const DocState& d) {
  uint64_t h = 0;
  for (const Layer* l : pixel_layers(d))
    for (const auto& [xy, t] : l->pixels->tiles()) {
      h = mix(h ^ uint64_t(uint32_t(xy.x)) << 32 ^ uint32_t(xy.y));
      if (t->is_solid()) {
        for (const uint16_t v : t->value()) h = mix(h ^ v);
        continue;
      }
      const Pin p = t->pin();
      for (size_t i = 0; i < tile_bytes(t->format()); i += 8) {
        uint64_t w;
        std::memcpy(&w, p.data() + i, 8);
        h = mix(h ^ w);
      }
    }
  return h;
}

// A data tile: a ramp (zstd takes it to a few percent) or noise (incompressible).
TilePtr data_tile(Format f, uint64_t seed, bool noise) {
  const std::shared_ptr<Tile> t = Tile::make(f);
  const WritePin w = t->write();
  for (size_t i = 0; i < tile_bytes(f); i++) w.data()[i] = uint8_t(noise ? mix(seed + i) : i / 509 + seed);
  return t;
}
Pixel random_px(Format f, std::mt19937& rng) {
  Pixel v{};
  for (int c = 0; c < f.channels; c++) v[size_t(c)] = uint16_t(rng() % unsigned(max_value(f.depth) + 1));
  return v;
}
// Tiles over the canvas and one past its edge now and then: data, solid or none.
std::shared_ptr<const TileGrid> random_grid(const DocState& d, std::mt19937& rng, bool noise) {
  const Format f = pixel_format(d);
  auto g = std::make_shared<TileGrid>(f);
  const int across = (d.width + kTileSize - 1) / kTileSize, down = (d.height + kTileSize - 1) / kTileSize;
  for (int ty = -1; ty <= down; ty++)
    for (int tx = -1; tx <= across; tx++) {
      const bool inside = tx >= 0 && ty >= 0 && tx < across && ty < down;
      const unsigned r = rng() % (inside ? 4 : 16);
      if (r == 0) g->set(tx, ty, Tile::solid(f, random_px(f, rng)));
      if (r == 1 || r == 2) {
        const uint64_t seed = rng();
        g->set(tx, ty, data_tile(f, seed, noise && rng() % 2));
      }
    }
  return g;
}
// d with tile (tx, ty) of layer `id` new, a ramp or noise from `seed`.
DocPtr with_tile(const DocPtr& d, LayerId id, int tx, int ty, uint64_t seed, bool noise) {
  TileGrid g = *find_layer(*d, id)->pixels;
  g.set(tx, ty, data_tile(g.format(), seed, noise));
  return run(*d, SetPixels{id, std::make_shared<const TileGrid>(std::move(g))});
}
DocPtr renamed(const DocPtr& d, LayerId id, const std::string& name) {
  LayerProps p = find_layer(*d, id)->props;
  p.name = name;
  return run(*d, SetProps{id, p});
}
// w x h, 8-bit RGB with a profile of random bytes, layers of random tiles from `seed`.
DocPtr sample(uint32_t seed, int w, int h, int layers, bool noise) {
  std::mt19937 rng(seed);
  auto icc = std::make_shared<std::vector<uint8_t>>(300);
  for (uint8_t& b : *icc) b = uint8_t(rng());
  DocPtr d = new_document(w, h, Mode::Rgb, Depth::U8, icc);
  for (int k = 0; k < layers; k++) d = run(*d, AddLayer{0, k, Kind::Pixel, {}, random_grid(*d, rng, noise)});
  return d;
}

// ---- In process ----

// Tiles saved read from the file: once saved, their copies in RAM (zstd, raw) go for free, after a full save and after
// an append, and they read back right, also from the part appended (mapped on its own).
void check_adopted() {
  TempDir dir;
  trim_tiles();
  clear_mips();
  const TileStats s0 = tile_stats();
  DocPtr z = sample(7, 1300, 700, 3, false);  // ramps: zstd keeps them in RAM when pushed out
  const LayerId id = pixel_layers(*z)[1]->id;
  for (int k = 0; k < 2; k++) {
    if (k) z = with_tile(z, id, 2, 1, 99, false);
    const uint64_t hash = pixels_hash(*z);
    trim_tiles();
    CHECK(tile_stats().compressed > s0.compressed);
    save_pbd(*z, dir / "z.pbd");
    clear_mips();  // levels 1 and 2, made for the save's, are the cache's alone
    trim_tiles();
    const TileStats s = tile_stats();
    CHECK(s.compressed <= s0.compressed && s.raw <= s0.raw && s.scratch <= s0.scratch);
    CHECK(pixels_hash(*z) == hash);
  }
  const Layout l = layout(get(dir / "z.pbd"));
  CHECK(l.waste() > 0 && l.waste() == l.uncovered());  // the second save appended
}

// A full save; appends: a tile anew, a layer renamed, a layer of another .pbd pasted with another profile; the file cut
// back or torn; a cancelled append; full saves: after a torn tail, over a file replaced behind this process's back,
// elsewhere, of a document from elsewhere.
void check_append() {
  TempDir dir;
  const fs::path f = dir / "a.pbd";
  const DocPtr a = sample(1, 700, 500, 4, false);
  save_pbd(*a, f);
  const std::vector<uint8_t> fa = get(f);
  const Layout la = layout(fa);
  CHECK(la.waste() == 0 && la.uncovered() == 0);

  // B: one tile of one layer anew. Appended: that tile, the mip over it, the preview and the JSON; the ICC profile,
  // every other tile and mip kept where they lie.
  const LayerId changed = pixel_layers(*a)[2]->id;
  const DocPtr b = with_tile(a, changed, 1, 1, 5, false);
  save_pbd(*b, f);
  const std::vector<uint8_t> fb = get(f);
  CHECK(fb.size() > fa.size() && std::equal(fa.begin(), fa.end(), fb.begin()));
  const Layout lb = layout(fb);
  CHECK(lb.past(fa.size()) == 4 && lb.chunks[lb.doc["icc"].get<size_t>()].offset < fa.size());
  CHECK(lb.waste() > 0 && lb.waste() == lb.uncovered());
  CHECK(same_doc(*b, *open_pbd(f).doc));

  // C: a layer renamed, no pixel changed: only the preview and the JSON appended; waste grows by exactly B's index,
  // footer, JSON and preview.
  const DocPtr c = renamed(b, changed, "renamed");
  save_pbd(*c, f);
  const std::vector<uint8_t> fc = get(f);
  const Layout lc = layout(fc);
  const uint32_t pv = lb.doc["preview"]["chunk"].get<uint32_t>();
  CHECK(lc.past(fb.size()) == 2 && lc.waste() == lc.uncovered());
  CHECK(lc.waste() == lb.waste() + lb.index_size + 24 + lb.chunks[0].size + lb.chunks[pv].size);
  CHECK(same_doc(*c, *open_pbd(f).doc));

  // C2: a layer of another .pbd pasted in, and another profile: their frames appended, copied; C's kept.
  save_pbd(*sample(4, 600, 400, 1, false), dir / "y.pbd");
  const DocPtr y = open_pbd(dir / "y.pbd").doc;
  DocPtr c2 = run(*c, AddLayer{0, 1, Kind::Pixel, {}, pixel_layers(*y)[0]->pixels});
  {
    auto s = std::make_shared<DocState>(*c2);
    s->icc = std::make_shared<const std::vector<uint8_t>>(500, uint8_t(77));
    c2 = s;
  }
  save_pbd(*c2, f);
  std::vector<uint8_t> fc2 = get(f);
  const Layout lc2 = layout(fc2);
  CHECK(lc2.waste() == lc2.uncovered() && lc2.chunks[lc2.doc["icc"].get<size_t>()].offset >= fc.size());
  CHECK(same_doc(*c2, *open_pbd(f).doc));

  // The footer before still holds A: so does the file cut anywhere in what B appended; bytes after B's footer are
  // passed over; C2's index damaged, the file opens to C. In a directory of their own, removed at the end: the mip
  // cache keeps what it opened mapped, which Windows will not delete.
  const TempDir torn;
  int cuts = 0;
  for (size_t k = fa.size(); k < fb.size(); k += k + 64 > fb.size() ? 1 : 1 + (fb.size() - fa.size()) / 50) {
    const fs::path p = torn / ("cut" + std::to_string(cuts++) + ".pbd");
    put(p, std::vector<uint8_t>(fb.begin(), fb.begin() + std::ptrdiff_t(k)));
    CHECK(same_doc(*a, *open_pbd(p).doc));
  }
  std::vector<uint8_t> more = fb;
  for (int i = 0; i < 3000; i++) more.push_back(uint8_t(i % 7 ? mix(uint64_t(i)) : 'X'));
  put(torn / "more.pbd", more);
  CHECK(same_doc(*b, *open_pbd(torn / "more.pbd").doc));
  fc2[lc2.index_at + 5] ^= 1;
  put(torn / "flip.pbd", fc2);
  CHECK(same_doc(*c, *open_pbd(torn / "flip.pbd").doc));

  // D: a tile anew, its save cancelled once its tiles are written: the file still opens to C2, nothing beside it.
  // Opened so, the file's first save, past those bytes, is a full one.
  const DocPtr d = with_tile(c2, changed, 0, 0, 6, true);
  SaveOptions o;
  o.cancel = make_token();
  o.progress = [&](double) { o.cancel->store(true); };
  bool threw = false;
  try {
    save_pbd(*d, f, o);
  } catch (const Cancelled&) {
    threw = true;
  }
  CHECK(threw && fs::file_size(f) > fc2.size() && dir.files() == 2);  // a.pbd, y.pbd
  const DocPtr reopened = open_pbd(f).doc;
  CHECK(same_doc(*c2, *reopened));
  const DocPtr d2 = with_tile(reopened, changed, 2, 0, 7, false);
  save_pbd(*d2, f);
  CHECK(layout(get(f)).waste() == 0 && same_doc(*d2, *open_pbd(f).doc) && dir.files() == 2);

  // E: the file replaced behind this process's back by another .pbd, padded to the same size: a full save again.
  save_pbd(*sample(5, 300, 200, 1, false), dir / "other.pbd");
  clear_mips();  // its mips keep other.pbd mapped, which Windows would not write over
  std::vector<uint8_t> other = get(dir / "other.pbd");
  CHECK(other.size() < fs::file_size(f));
  other.resize(fs::file_size(f));
  put(dir / "other.pbd", other);
  fs::rename(dir / "other.pbd", f);
  const DocPtr e = with_tile(d2, changed, 1, 0, 8, false);
  save_pbd(*e, f);
  CHECK(layout(get(f)).waste() == 0 && same_doc(*e, *open_pbd(f).doc));

  // Saved elsewhere: a full save there, this file as it was. A document from elsewhere saved over it: a full save,
  // and A still reads from the file it saved first, replaced (Windows too: renamed over).
  const std::vector<uint8_t> fe = get(f);
  save_pbd(*e, dir / "elsewhere.pbd");
  CHECK(layout(get(dir / "elsewhere.pbd")).waste() == 0 && get(f) == fe);
  const uint64_t hash = pixels_hash(*a);
  const DocPtr x = sample(2, 600, 300, 2, false);
  save_pbd(*x, f);
  trim_tiles();
  CHECK(layout(get(f)).waste() == 0 && same_doc(*x, *open_pbd(f).doc) && pixels_hash(*a) == hash);
  CHECK(dir.files() == 3);  // a.pbd, y.pbd, elsewhere.pbd: nothing left beside them
  std::printf("pbd append: %zu + %zu bytes, the file then cut %d ways; %llu bytes of waste after a rename\n",
              fa.size(), fb.size() - fa.size(), cuts, (unsigned long long)lc.waste());
}

// ---- Killed while saving ----

// What the child saves, version by version: v0 a document of a version group (its name "v<n>") and noisy layers;
// each version renames it and changes something at random: a tile or three, a layer added, removed, duplicated,
// its properties, or every layer anew (a full save then). Pure: the same version from the same document.
constexpr LayerId kVersion = 1;
DocPtr start() {
  DocPtr d = sample(3, 700, 500, 0, true);
  d = run(*d, AddLayer{0, 0, Kind::Group, {}, nullptr});
  d = renamed(d, kVersion, "v0");
  std::mt19937 rng(4);
  for (int k = 0; k < 3; k++) d = run(*d, AddLayer{0, k + 1, Kind::Pixel, {}, random_grid(*d, rng, true)});
  return d;
}
DocPtr change(DocPtr d, int v) {
  std::mt19937 rng(uint32_t(v) * 2654435761u);
  d = renamed(d, kVersion, "v" + std::to_string(v));
  std::vector<LayerId> px;
  for (const Layer* l : pixel_layers(*d)) px.push_back(l->id);
  const LayerId some = px[rng() % px.size()];
  switch (rng() % 12) {
    case 0:
      for (const LayerId id : px) d = run(*d, SetPixels{id, random_grid(*d, rng, true)});
      break;
    case 1:
      if (px.size() < 6) {
        const int at = int(rng() % px.size()) + 1;
        d = run(*d, AddLayer{0, at, Kind::Pixel, {}, random_grid(*d, rng, true)});
      }
      break;
    case 2:
      if (px.size() > 2) d = run(*d, DeleteLayers{{some}});
      break;
    case 3:
      if (px.size() < 6) d = run(*d, DuplicateLayers{{some}});
      break;
    case 4: {
      LayerProps p = find_layer(*d, some)->props;
      p.opacity = float(rng() % 101) / 100, p.blend = Blend(1 + rng() % 10), p.visible = rng() % 4;  // no Pass Through
      d = run(*d, SetProps{some, p});
      break;
    }
    default:
      for (int n = 1 + int(rng() % 3); n > 0; n--) {
        const int tx = int(rng() % 4) - 1, ty = int(rng() % 3) - 1;
        const uint64_t seed = rng();
        if (seed % 5) {
          d = with_tile(d, some, tx, ty, seed, rng() % 2);
        } else {
          TileGrid g = *find_layer(*d, some)->pixels;
          g.set(tx, ty, nullptr);
          d = run(*d, SetPixels{some, std::make_shared<const TileGrid>(std::move(g))});
        }
      }
  }
  return d;
}
int version_of(const DocState& d) { return std::stoi(find_layer(d, kVersion)->props.name.substr(1)); }

// The child: saves on until killed, from the file's version on (or v0, when there is no file yet), saying
// "saved <v>" after each save returns.
void say(const std::string& s) {
  if (std::fputs(s.c_str(), stdout) == EOF || std::fflush(stdout) == EOF) std::exit(1);  // the test is gone
}
void save_on(const fs::path& file) {
  DocPtr d;
  int v = 0;
  if (fs::exists(file)) {
    d = open_pbd(file).doc;
    v = version_of(*d);
  } else {
    d = start();
    save_pbd(*d, file);
    say("saved 0\n");
  }
  say("ready " + std::to_string(v) + "\n");
  for (;;) {
    d = change(d, ++v);
    save_pbd(*d, file);
    say("saved " + std::to_string(v) + "\n");
  }
}

// A child process of these tests, its standard output a pipe: started saving into `file`, killed.
struct Child {
  std::string out;  // what it said
#ifdef _WIN32
  PROCESS_INFORMATION pi{};
  HANDLE r = nullptr;
#else
  pid_t pid = -1;
  int r = -1;
#endif

  explicit Child(const fs::path& file) {
#ifdef _WIN32
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE w = nullptr;
    CHECK(CreatePipe(&r, &w, &sa, 0) && SetHandleInformation(r, HANDLE_FLAG_INHERIT, 0));
    wchar_t exe[MAX_PATH];
    CHECK(GetModuleFileNameW(nullptr, exe, MAX_PATH) > 0);
    std::wstring cmd = L"\"" + std::wstring(exe) + L"\" test_pbd_crash_child";
    SetEnvironmentVariableW(L"PB_PBD_CHILD", file.wstring().c_str());
    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE), si.hStdOutput = w, si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    const bool spawned = CreateProcessW(exe, cmd.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi);
    SetEnvironmentVariableW(L"PB_PBD_CHILD", nullptr);
    CloseHandle(w);
    CHECK(spawned);
#else
    int fds[2];
    CHECK(pipe(fds) == 0);
    std::vector<std::string> env;
    for (char** e = environ; *e; e++) env.emplace_back(*e);
    env.push_back("PB_PBD_CHILD=" + file.string());
    std::vector<char*> envp;
    for (std::string& e : env) envp.push_back(e.data());
    envp.push_back(nullptr);
    char arg0[] = "pb-tests", arg1[] = "test_pbd_crash_child";
    char* argv[] = {arg0, arg1, nullptr};
    pid = fork();
    if (pid == 0) {
      dup2(fds[1], 1);
      close(fds[0]), close(fds[1]);
      execve("/proc/self/exe", argv, envp.data());
      _exit(127);
    }
    close(fds[1]);
    r = fds[0];
    CHECK(pid > 0);
#endif
  }
  // Reads what it says until `until` (a whole line) or the pipe's end.
  void read(const char* until) {
    char buf[4096];
    while (!until || out.find(until) == std::string::npos || out.back() != '\n') {
#ifdef _WIN32
      DWORD n = 0;
      if (!ReadFile(r, buf, sizeof buf, &n, nullptr) || !n) return;
#else
      const ssize_t n = ::read(r, buf, sizeof buf);
      if (n < 0 && errno == EINTR) continue;
      if (n <= 0) return;
#endif
      out.append(buf, size_t(n));
    }
  }
  void kill() {
#ifdef _WIN32
    TerminateProcess(pi.hProcess, 1);
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess), CloseHandle(pi.hThread);
#else
    ::kill(pid, SIGKILL);
    int status = 0;
    waitpid(pid, &status, 0);
#endif
    read(nullptr);  // what it said before it died
#ifdef _WIN32
    CloseHandle(r);
#else
    close(r);
#endif
  }
  // The last number after `word` it said, -1 for none.
  int last(const char* word) const {
    int v = -1;
    for (size_t at = out.find(word); at != std::string::npos; at = out.find(word, at + 1))
      if (out.find('\n', at) != std::string::npos) v = std::atoi(out.c_str() + at + std::strlen(word));
    return v;
  }
};

// 200 children, each killed at random within about its first three saves (the window three times a save's mean time
// so far, sanitizers and Windows alike): every time the file opens to the last save it reported finished, or to the
// one after (its footer down, its report not yet), every pixel as that version has it. Each child goes on from what
// the one before left, a torn tail and all.
void check_kills() {
  TempDir dir;
  const fs::path file = dir / "k.pbd";
  std::mt19937 rng(11);
  DocPtr want = start();  // the version the file holds, made here
  int want_v = 0, torn = 0, unreported = 0, leftovers = 0;
  size_t biggest = 0;
  double window = 0.06, alive = 0;  // seconds
  const auto t0 = std::chrono::steady_clock::now();
  constexpr int kKills = 200;
  for (int round = 0, saves = 0; round < kKills; round++) {
    Child c(file);
    c.read("ready ");
    // Killed before any CHECK: a failing one would leave it running.
    const auto ready_at = std::chrono::steady_clock::now();
    const auto at = ready_at + std::chrono::microseconds(rng() % unsigned(window * 1e6));
    while (std::chrono::steady_clock::now() < at) std::this_thread::yield();
    c.kill();
    const int ready = c.last("ready "), saved = std::max(c.last("saved "), ready);
    alive += std::chrono::duration<double>(at - ready_at).count(), saves += saved - ready;
    window = std::clamp(3 * alive / std::max(saves, 1), 0.02, 2.);
    if (ready < 0) std::printf("the child said: %s\n", c.out.c_str());
    CHECK(ready >= 0);
    const std::vector<uint8_t> bytes = get(file);
    biggest = std::max(biggest, bytes.size());
    torn += bytes.size() < 8 || std::memcmp(bytes.data() + bytes.size() - 8, "PBDINDEX", 8) != 0;
    DocPtr got;
    try {
      got = open_pbd(file).doc;
    } catch (const std::exception& e) {
      std::printf("round %d: %s\n", round, e.what());
    }
    CHECK(got);
    const int v = version_of(*got);
    if (v != saved && v != saved + 1) std::printf("round %d: version %d, the child saved %d\n", round, v, saved);
    CHECK(v == saved || v == saved + 1);
    unreported += v == saved + 1;
    CHECK(v >= want_v);
    while (want_v < v) want = change(want, ++want_v);
    CHECK(same_doc(*want, *got));
    got = nullptr;
    clear_mips();  // lets go of the file's mappings
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir.path))  // what killed full saves left
      if (e.path() != file) leftovers++, fs::remove(e.path(), ec);
  }
  std::printf(
      "pbd crash: %d kills in %.1f s (within %.0f ms of starting): up to version %d; %d torn tails, %d full saves cut "
      "short, %d saves done but not reported; the file at most %zu bytes\n",
      kKills, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(), window * 1e3, want_v, torn,
      leftovers, unreported, biggest);
  CHECK(torn > 0 && want_v > kKills);
}

}  // namespace

TEST(test_pbd_crash) {
  check_adopted();
  check_append();
  check_kills();
}

// What check_kills() runs in a process of its own (PB_PBD_CHILD: the file); else nothing.
TEST(test_pbd_crash_child) {
  if (const char* file = std::getenv("PB_PBD_CHILD")) save_on(file);
}
