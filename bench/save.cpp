// pb-bench save [file.pbd] [dir] [runs]: a full save (pbd.h) of the document in file.pbd (default kDocs/d1.pbd) as if
// every tile were new since it was last saved, as a document opened from a PSD or painted anew saves first: each data
// tile copied into a new one (no frame to copy, every one compressed) and no mip built yet. `runs` saves (3) into dir
// (the temporary directory by default), each from a fresh copy: their times, the median, the file's size. Then, for
// file.pbd and the file saved, by turns, 3 rounds each: opening it, and reading its data tiles back, ms a tile, the
// first 500 on one thread, the others on the pool (the format a save writes frames 16-bit tiles otherwise: pbd.cpp);
// the medians.
// pb-bench save-disk <dir> [MB]: the disk's ceiling, MB (1100) written to dir in 8 MB blocks then flushed: through
// the file a save writes (mapped.h), and (Windows) unbuffered with 4 writes in flight, the most the drive takes.
#include <zstd.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <random>
#include <string>
#include <system_error>
#include <vector>

#include "bench.h"
#include "docs.h"
#include "jobs.h"
#include "mapped.h"
#include "mip.h"
#include "pbd.h"
#include "tile.h"

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

using namespace pb;
namespace fs = std::filesystem;
using bench::Clock;
using bench::ms_since;
using bench::report;

namespace {

constexpr size_t kBlock = size_t(8) << 20;

// `bytes` of noise written to `file` as a save writes, then synced and closed: ms.
double through_output(const fs::path& file, uint64_t bytes, const std::vector<uint8_t>& noise) {
  const auto t0 = Clock::now();
  OutputFile out(file);
  for (uint64_t done = 0; done < bytes; done += kBlock)
    out.write(noise.data(), size_t(std::min<uint64_t>(kBlock, bytes - done)));
  out.sync();
  out.close();
  const double ms = ms_since(t0);
  fs::remove(file);
  return ms;
}

#ifdef _WIN32
// The same unbuffered and written through, 4 blocks in flight: ms, or -1 when Windows refuses.
double unbuffered(const fs::path& file, uint64_t bytes) {
  constexpr int kDepth = 4;
  auto* buf = static_cast<uint8_t*>(VirtualAlloc(nullptr, kBlock * kDepth, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
  if (!buf) return -1;
  std::mt19937_64 rng(2);
  for (size_t i = 0; i < kBlock * kDepth; i += 8) {
    const uint64_t v = rng();
    std::memcpy(buf + i, &v, 8);
  }
  const auto t0 = Clock::now();
  const HANDLE h = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                               FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH | FILE_FLAG_OVERLAPPED, nullptr);
  double ms = -1;
  if (h != INVALID_HANDLE_VALUE) {
    OVERLAPPED o[kDepth] = {};
    bool busy[kDepth] = {}, ok = true;
    for (OVERLAPPED& x : o) x.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    uint64_t at = 0;
    for (int i = 0; ok; i = (i + 1) % kDepth) {
      DWORD n = 0;
      if (busy[i]) ok = GetOverlappedResult(h, &o[i], &n, TRUE), busy[i] = false;
      if (at >= bytes) {
        if (std::none_of(busy, busy + kDepth, [](bool b) { return b; })) break;
        continue;
      }
      o[i].Offset = DWORD(at), o[i].OffsetHigh = DWORD(at >> 32);
      ok = ok && (WriteFile(h, buf + size_t(i) * kBlock, DWORD(kBlock), nullptr, &o[i]) ||
                  GetLastError() == ERROR_IO_PENDING);
      busy[i] = ok, at += kBlock;
    }
    ok = ok && FlushFileBuffers(h);
    CloseHandle(h);
    for (OVERLAPPED& x : o) CloseHandle(x.hEvent);
    if (ok) ms = ms_since(t0);
    std::error_code e;
    fs::remove(file, e);
  }
  VirtualFree(buf, 0, MEM_RELEASE);
  return ms;
}
#endif

// d with every data tile copied into a new one (a new id, the same pixels): nothing of it lies in a .pbd any more.
DocPtr fresh(const DocPtr& d) {
  std::vector<LayerId> ids;
  const std::function<void(const Layer&)> walk = [&](const Layer& g) {
    for (const LayerPtr& l : g.children)
      if (l->kind == Kind::Group)
        walk(*l);
      else
        ids.push_back(l->id);
  };
  walk(*d->root);
  DocPtr out = d;
  for (const LayerId id : ids) {
    const std::function<const Layer*(const Layer&)> find = [&](const Layer& g) -> const Layer* {
      for (const LayerPtr& l : g.children)
        if (l->id == id) return l.get();
        else if (const Layer* in = l->kind == Kind::Group ? find(*l) : nullptr) return in;
      return nullptr;
    };
    TileGrid g(*find(*out->root)->pixels);
    std::vector<TileXY> at;
    for (const auto& [xy, t] : g.tiles())
      if (!t->is_solid()) at.push_back(xy);
    for (const TileXY xy : at) g.write(xy.x, xy.y);  // shared, so copied
    out = run(*out, SetPixels{id, std::make_shared<const TileGrid>(std::move(g))});
  }
  return out;
}

}  // namespace

BENCH(save_disk) {
  if (args.empty()) {
    std::printf("pb-bench save-disk <dir> [MB]: writes a GB there\n");
    return;
  }
  const fs::path dir = args[0];
  const uint64_t bytes = uint64_t(args.size() > 1 ? std::stoi(args[1]) : 1100) << 20;
  std::vector<uint8_t> noise(kBlock);
  std::mt19937_64 rng(1);
  for (size_t i = 0; i < noise.size(); i += 8) {
    const uint64_t v = rng();
    std::memcpy(&noise[i], &v, 8);
  }
  double ms = through_output(dir / "disk.bin", bytes, noise);
  report("save_disk_output", ms, "ms");
  report("save_disk_output_rate", double(bytes) / 1e3 / ms, "MB/s");
#ifdef _WIN32
  ms = unbuffered(dir / "disk.bin", bytes);
  report("save_disk_unbuffered", ms, "ms");
  report("save_disk_unbuffered_rate", double(bytes) / 1e3 / ms, "MB/s");
#endif
}

// pb-bench save-codec [file.pbd] [tiles]: what reading a 16-bit tile back costs on one thread, by format: format 1's
// frame of its pixels unpacked, format 2's of predict()'s bytes unpacked and unpredict()ed, unpredict() alone, and
// predict() alone (a save's part); ms a tile, the best of 3 passes over the first `tiles` (400) 16-bit data tiles of
// file.pbd, and their frames' size.
BENCH(save_codec) {
  const fs::path file = args.size() > 0 ? fs::path(args[0]) : fs::path(bench::kDocs) / "d1.pbd";
  const size_t want = args.size() > 1 ? size_t(std::max(1, std::stoi(args[1]))) : 400;
  if (!fs::exists(file)) {
    std::printf("pb-bench save-codec: no %s\n", file.string().c_str());
    return;
  }
  const DocPtr d = open_pbd(file).doc;
  std::vector<std::vector<uint8_t>> raw;
  Format f;
  const std::function<void(const Layer&)> walk = [&](const Layer& g) {
    for (const LayerPtr& l : g.children)
      if (l->kind == Kind::Group)
        walk(*l);
      else
        for (const auto& [xy, t] : l->pixels->tiles())
          if (raw.size() < want && !t->is_solid() && t->format().depth == Depth::U16) {
            f = t->format();
            const Pin p = t->pin();
            raw.emplace_back(p.data(), p.data() + size_t(kTileSize) * kTileSize * size_t(f.bytes()));
          }
  };
  walk(*d->root);
  if (raw.empty()) return;
  const size_t n = raw[0].size();
  std::vector<std::vector<uint8_t>> v1(raw.size()), v2(raw.size());
  std::vector<uint8_t> a(n), b(n);
  size_t s1 = 0, s2 = 0;
  for (size_t i = 0; i < raw.size(); i++) {
    v1[i].resize(ZSTD_compressBound(n)), v2[i].resize(ZSTD_compressBound(n));
    v1[i].resize(ZSTD_compress(v1[i].data(), v1[i].size(), raw[i].data(), n, 1));
    predict(f, raw[i].data(), a.data());
    v2[i].resize(ZSTD_compress(v2[i].data(), v2[i].size(), a.data(), n, 1));
    s1 += v1[i].size(), s2 += v2[i].size();
  }
  ZSTD_DCtx* z = ZSTD_createDCtx();
  bool same = true;
  double t1 = 1e9, t2 = 1e9, tu = 1e9, tp = 1e9;  // the best of 3 passes
  for (int pass = 0; pass < 3; pass++) {
    auto t0 = Clock::now();
    for (const auto& r : raw) predict(f, r.data(), b.data());
    tp = std::min(tp, ms_since(t0));
    t0 = Clock::now();
    for (const auto& v : v1) ZSTD_decompressDCtx(z, a.data(), n, v.data(), v.size());
    t1 = std::min(t1, ms_since(t0));
    t0 = Clock::now();
    for (size_t i = 0; i < v2.size(); i++) {
      ZSTD_decompressDCtx(z, b.data(), n, v2[i].data(), v2[i].size());
      unpredict(f, b.data(), a.data());
      if (pass == 0) same = same && a == raw[i];
    }
    t2 = std::min(t2, ms_since(t0));
    t0 = Clock::now();
    for (size_t i = 0; i < v2.size(); i++) unpredict(f, b.data(), a.data());
    tu = std::min(tu, ms_since(t0));
  }
  ZSTD_freeDCtx(z);
  const double k = double(raw.size());
  std::printf("save-codec: %zu tiles of %d channels%s\n", raw.size(), f.channels, same ? "" : ", NOT BACK EXACTLY");
  report("save_codec_v1", t1 / k, "ms/tile");
  report("save_codec_v2", t2 / k, "ms/tile");
  report("save_codec_unpredict", tu / k, "ms/tile");
  report("save_codec_predict", tp / k, "ms/tile");
  report("save_codec_v1_size", double(s1) / k / 1e3, "KB/tile");
  report("save_codec_v2_size", double(s2) / k / 1e3, "KB/tile");
}

BENCH(save) {
  const fs::path file = args.size() > 0 ? fs::path(args[0]) : fs::path(bench::kDocs) / "d1.pbd";
  if (!fs::exists(file)) {
    std::printf("pb-bench save: no %s (pb-bench make d1 makes it)\n", file.string().c_str());
    return;
  }
  const fs::path dir = (args.size() > 1 ? fs::path(args[1]) : fs::temp_directory_path()) /
                       ("pb-bench-save-" + std::to_string(std::random_device{}()));
  const int runs = args.size() > 2 ? std::max(1, std::stoi(args[2])) : 3;
  fs::create_directories(dir);
  std::vector<double> times;
  const fs::path out = dir / "save.pbd";
  for (int i = 0; i < runs; i++) {
    if (i) fs::remove(out);
    {
      DocPtr d = fresh(open_pbd(file).doc);
      clear_mips();  // the attached ones: built anew, as for a document never shown at those levels
      const auto t0 = Clock::now();
      save_pbd(*d, out);
      times.push_back(ms_since(t0));
      report("save_full", times.back(), "ms");
      report("save_file", double(fs::file_size(out)) / 1e6, "MB");
    }
    clear_mips();  // its tiles keep the file mapped, which Windows will not delete
  }
  std::sort(times.begin(), times.end());
  report("save_full_median", times[times.size() / 2], "ms");

  // Reading back, the two files by turns, 3 rounds, each opened anew (new tiles, unpacked anew): the medians.
  const std::pair<const char*, fs::path> files[] = {{"source", file}, {"saved", out}};
  std::vector<double> opened[2], one[2], pool[2];
  for (int round = 0; round < 3; round++)
    for (int k = 0; k < 2; k++) {
      auto t0 = Clock::now();
      const DocPtr d = open_pbd(files[k].second).doc;
      opened[k].push_back(ms_since(t0));
      std::vector<const Tile*> tiles;
      const std::function<void(const Layer&)> walk = [&](const Layer& g) {
        for (const LayerPtr& l : g.children)
          if (l->kind == Kind::Group)
            walk(*l);
          else
            for (const auto& [xy, t] : l->pixels->tiles())
              if (!t->is_solid()) tiles.push_back(t.get());
      };
      walk(*d->root);
      const size_t n = std::min<size_t>(500, tiles.size());
      t0 = Clock::now();
      for (size_t i = 0; i < n; i++) tiles[i]->pin();
      one[k].push_back(ms_since(t0) / double(n));
      t0 = Clock::now();
      parallel_for(int(tiles.size() - n), [&](int b, int e) {
        for (int i = b; i < e; i++) tiles[n + size_t(i)]->pin();
      });
      pool[k].push_back(ms_since(t0) / double(tiles.size() - n));
    }
  const auto median = [](std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
  };
  for (int k = 0; k < 2; k++) {
    const std::string name = files[k].first;
    report(("save_open_" + name).c_str(), median(opened[k]), "ms");
    report(("save_read_one_" + name).c_str(), median(one[k]), "ms/tile");
    report(("save_read_pool_" + name).c_str(), median(pool[k]), "ms/tile");
  }
  clear_mips();
  std::error_code e;
  fs::remove_all(dir, e);
}
