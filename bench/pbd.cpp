// pb-bench pbd [dir]: D1 (docs.h) saved into a new .pbd in dir (the temporary directory by default): a full save; then
// three times a 300 px stroke on its selected layer (Burn, mid-stack: a scribble of 400 events, as pb-bench strokes
// makes D1's) and a save into the same file: an append (pbd.h). Prints the full save's time and the file's size, each
// append's time and bytes, and their median. Then a full save's parts, measured apart on the same document: building
// the mips it keeps (levels 1 to the top, none cached, as for a document new since its last frame), compressing every
// data tile as a save does (zstd 1 with a checksum and a CRC, on the pool, nothing written), writing as many bytes as
// its file (the disk alone: the write, the sync, the close), and a full save copying the frames of a saved document
// (Save As: no compression).
#include <zlib.h>
#include <zstd.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <random>
#include <string>
#include <system_error>
#include <unordered_set>
#include <vector>

#include "bench.h"
#include "docs.h"
#include "jobs.h"
#include "mapped.h"
#include "mip.h"
#include "pbd.h"
#include "stroke.h"

using namespace pb;
namespace fs = std::filesystem;
using bench::Clock;
using bench::ms_since;
using bench::report;

namespace {

// `d` after a 300 px stroke on its selected layer: 400 events at 200 Hz wandering about 12 px each, pressure drifting.
DocPtr stroke(const DocPtr& d, uint32_t seed) {
  BrushParams b;
  b.size = 300, b.hardness = 0.8f, b.color = {0.85f, 0.3f, 0.25f};
  Stroke s(d, d->selected[0], b);
  std::mt19937 rng(seed);
  double x = d->width / 5 + rng() % unsigned(d->width * 3 / 5), y = d->height / 5 + rng() % unsigned(d->height * 3 / 5);
  double vx = 0, vy = 0, p = 0.5;
  for (int i = 0; i < 400; i++) {
    vx = vx * 0.9 + 12. * (int(rng() % 41) - 20) / 50, vy = vy * 0.9 + 12. * (int(rng() % 41) - 20) / 50;
    x += vx, y += vy, p = std::clamp(p + (int(rng() % 41) - 20) / 1000., 0.1, 1.);
    s.add({i / 200., float(x), float(y), float(p)});
  }
  s.finish();
  return run(*d, SetPixels{d->selected[0], s.wait()->pixels});
}

std::vector<const TileGrid*> grids(const Layer& g) {
  std::vector<const TileGrid*> out;
  for (const LayerPtr& l : g.children)
    if (l->kind == Kind::Group) {
      const std::vector<const TileGrid*> in = grids(*l);
      out.insert(out.end(), in.begin(), in.end());
    } else {
      out.push_back(l->pixels.get());
    }
  return out;
}

// A full save's parts, apart.
void parts(const DocState& d, const fs::path& dir, uintmax_t file_bytes) {
  int top = 3;
  while ((int64_t(kTileSize) << top) < std::max(d.width, d.height)) top++;
  clear_mips();
  auto t0 = Clock::now();
  for (const TileGrid* g : grids(*d.root)) {
    const TileRect r = g->bounds();
    prefetch(*g, top, {r.x0 >> top, r.y0 >> top, ((r.x1 - 1) >> top) + 1, ((r.y1 - 1) >> top) + 1});
  }
  report("pbd_mips_d1", ms_since(t0), "ms");

  std::vector<const Tile*> tiles;
  std::unordered_set<uint64_t> seen;
  for (const TileGrid* g : grids(*d.root))
    for (const auto& [xy, t] : g->tiles())
      if (!t->is_solid() && seen.insert(t->id()).second) tiles.push_back(t.get());
  std::atomic<size_t> packed{0};
  t0 = Clock::now();
  parallel_for(int(tiles.size()), [&](int begin, int end) {
    ZSTD_CCtx* z = ZSTD_createCCtx();
    ZSTD_CCtx_setParameter(z, ZSTD_c_compressionLevel, 1);
    ZSTD_CCtx_setParameter(z, ZSTD_c_checksumFlag, 1);
    const size_t n = size_t(kTileSize) * kTileSize * size_t(tiles[size_t(begin)]->format().bytes());
    std::vector<uint8_t> out(ZSTD_compressBound(n));
    size_t sum = 0;
    for (int i = begin; i < end; i++) {
      const Pin p = tiles[size_t(i)]->pin();
      const size_t size = ZSTD_compress2(z, out.data(), out.size(), p.data(), n);
      if (ZSTD_isError(size)) continue;
      (void)crc32_z(0, out.data(), size);  // as a save checks each frame
      sum += size;
    }
    ZSTD_freeCCtx(z);
    packed += sum;
  });
  report("pbd_compress_d1", ms_since(t0), "ms");
  report("pbd_compressed_d1", double(packed) / 1e6, "MB");

  {
    std::vector<uint8_t> buf(size_t(32) << 20);
    std::mt19937_64 rng(1);
    for (size_t i = 0; i < buf.size(); i += 8) {
      const uint64_t v = rng();
      std::memcpy(&buf[i], &v, 8);
    }
    t0 = Clock::now();
    OutputFile out(dir / "io.bin");
    for (uintmax_t left = file_bytes; left;) {
      const size_t n = size_t(std::min<uintmax_t>(left, buf.size()));
      out.write(buf.data(), n);
      left -= n;
    }
    const double write = ms_since(t0);
    auto t1 = Clock::now();
    out.sync();
    const double sync = ms_since(t1);
    t1 = Clock::now();
    out.close();
    const double close = ms_since(t1);
    report("pbd_disk_d1", write + sync + close, "ms");
    report("pbd_disk_write_d1", write, "ms");
    report("pbd_disk_sync_d1", sync, "ms");
    report("pbd_disk_close_d1", close, "ms");
    fs::remove(dir / "io.bin");
  }

  t0 = Clock::now();
  save_pbd(d, dir / "copy.pbd");
  report("pbd_save_as_d1", ms_since(t0), "ms");
}

}  // namespace

BENCH(pbd) {
  const fs::path dir = (args.empty() ? fs::temp_directory_path() : fs::path(args[0])) /
                       ("pb-bench-pbd-" + std::to_string(std::random_device{}()));
  fs::create_directories(dir);
  const fs::path file = dir / "d1.pbd";
  {
    DocPtr d = bench::make_d1();
    auto t0 = Clock::now();
    save_pbd(*d, file);
    report("pbd_save_full_d1", ms_since(t0), "ms");
    report("pbd_file_d1", double(fs::file_size(file)) / 1e6, "MB");
    std::vector<double> times;
    for (uint32_t i = 0; i < 3; i++) {
      d = stroke(d, i + 1);
      const uintmax_t before = fs::file_size(file);
      t0 = Clock::now();
      save_pbd(*d, file);
      times.push_back(ms_since(t0));
      report("pbd_append_d1", times.back(), "ms");
      report("pbd_appended_d1", double(fs::file_size(file) - before) / 1e6, "MB");
    }
    std::sort(times.begin(), times.end());
    report("pbd_save_incremental_d1", times[1], "ms");
    parts(*d, dir, fs::file_size(file));
  }
  clear_mips();  // its tiles keep the files mapped, which Windows will not delete
  std::error_code ec;
  fs::remove_all(dir, ec);
}
