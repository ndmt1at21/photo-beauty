// pb-bench make d1|d2|d2mini|d4 [out.pbd] [--samples <dir>] [--budget <MB>] [--scratch <dir>]: builds a benchmark
// document (docs.h) and prints what it holds against its definition: its size, layers and groups, each pixel layer's
// coverage (its pixels in the canvas with alpha above 0, every tile read back) and their mean, its tiles and their
// pixels' bytes, the time the build took, and a hash of it all (tree, properties, every tile's place and bytes) that
// another build, or the document read back from a file, must give again; for D1 its frequency separation checked: Low
// against a Gaussian summed afresh, Low and High composited back to the photo. Meanwhile, once a second, the RSS and
// the tile store's tiers; at the end the RSS's peak against the tile budget, which D2 is built under. --budget sets the
// budget (MB, else 60% of RAM), --scratch where the scratch file goes (made when missing), --samples where the photos
// are (docs.h).
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "bench.h"
#include "docs.h"
#include "jobs.h"
#include "render.h"
#include "tilecache.h"

using namespace pb;

namespace {

constexpr double kMB = 1048576;
constexpr int S = kTileSize;

uint64_t fnv(uint64_t h, uint64_t word) { return (h ^ word) * 0x100000001b3ull; }  // FNV-1a, a word at a time
constexpr uint64_t kBasis = 0xcbf29ce484222325ull;

// What reading a pixel layer back finds: its pixels in the w x h canvas with alpha above 0, its tiles and those of
// data, and a hash of every tile's place and kind and of its pixels, summed: the same whatever order tiles are read in.
struct Scan {
  size_t covered = 0, tiles = 0, data = 0;
  uint64_t hash = 0;
};
Scan scan(const TileGrid& g, int w, int h) {
  std::vector<std::pair<TileXY, const Tile*>> at;
  for (const auto& [xy, t] : g.tiles()) at.push_back({xy, t.get()});
  const Format f = g.format();
  const size_t alpha = size_t(f.channels - 1), bytes = size_t(S) * S * size_t(f.bytes());
  std::atomic<size_t> covered{0}, data{0};
  std::atomic<uint64_t> hash{0};
  parallel_for(int(at.size()), [&](int b, int e) {
    size_t n = 0, m = 0;
    uint64_t sum = 0;
    for (int i = b; i < e; i++) {
      const TileXY xy = at[size_t(i)].first;
      const Tile& t = *at[size_t(i)].second;
      const int x0 = std::max(xy.x * S, 0), x1 = std::min((xy.x + 1) * S, w);
      const int y0 = std::max(xy.y * S, 0), y1 = std::min((xy.y + 1) * S, h);
      uint64_t k = fnv(fnv(fnv(kBasis, uint32_t(xy.x)), uint32_t(xy.y)), t.is_solid());
      if (t.is_solid()) {
        for (const uint16_t v : t.value()) k = fnv(k, v);
        if (t.value()[alpha] && x0 < x1 && y0 < y1) n += size_t(x1 - x0) * size_t(y1 - y0);
      } else {
        m++;
        const Pin p = t.pin();
        for (size_t j = 0; j < bytes; j += 8) {
          uint64_t word;
          std::memcpy(&word, p.data() + j, 8);
          k = fnv(k, word);
        }
        const auto* p16 = reinterpret_cast<const uint16_t*>(p.data());
        for (int y = y0; y < y1; y++)
          for (int x = x0; x < x1; x++) {
            const size_t a = (size_t(y - xy.y * S) * S + size_t(x - xy.x * S)) * size_t(f.channels) + alpha;
            n += (f.depth == Depth::U16 ? p16[a] : p.data()[a]) != 0;
          }
      }
      sum += k;
    }
    covered += n, data += m, hash += sum;
  });
  return {covered, at.size(), data, hash};
}

// Prints the RSS and the tile store's tiers once a second while it lives.
class Watch {
 public:
  Watch() : t_([this] { run(); }) {}
  ~Watch() {
    {
      const std::lock_guard<std::mutex> l(m_);
      stop_ = true;
    }
    cv_.notify_all();
    t_.join();
  }

 private:
  void run() {
    const auto t0 = bench::Clock::now();
    std::unique_lock<std::mutex> l(m_);
    for (int s = 1; !cv_.wait_until(l, t0 + std::chrono::seconds(s), [this] { return stop_; }); s++) {
      const TileStats t = tile_stats();
      std::printf("make: %d s: rss %.0f MB; tiles raw %.0f, compressed %.0f, scratch %.0f MB\n", s,
                  bench::resident_bytes() / kMB, t.raw / kMB, t.compressed / kMB, t.scratch / kMB);
      std::fflush(stdout);
    }
  }
  std::mutex m_;
  std::condition_variable cv_;
  bool stop_ = false;
  std::thread t_;  // last: the rest is there before it starts
};

// D1's Low against a Gaussian of the Background summed afresh in doubles, edges extended, at its corners, the middles
// of its edges and 16 points inside: the largest difference, levels of 16 bits.
double low_error(const DocState& d) {
  const TileGrid &photo = *find_layer(d, 1)->pixels, &low = *find_layer(d, 3)->pixels;
  constexpr int R = bench::kLowRadius;
  double k[2 * R + 1], sum = 0;
  for (int i = -R; i <= R; i++) sum += k[i + R] = std::exp(-i * i / (2 * bench::kLowSigma * bench::kLowSigma));
  const int w = d.width, h = d.height;
  std::vector<std::pair<int, int>> at = {{0, 0},     {w - 1, 0},     {0, h - 1},     {w - 1, h - 1},
                                         {w / 2, 0}, {0, h / 2}, {w - 1, h / 2}, {w / 2, h - 1}};
  std::mt19937 rng(7);
  for (int i = 0; i < 16; i++) {
    const int x = int(rng() % uint32_t(w)), y = int(rng() % uint32_t(h));
    at.push_back({x, y});
  }
  double worst = 0;
  for (const auto& [x, y] : at) {
    const int x0 = std::max(x - R, 0), y0 = std::max(y - R, 0), rw = std::min(x + R + 1, w) - x0, rh = std::min(y + R + 1, h) - y0;
    std::vector<uint16_t> in(size_t(rw) * size_t(rh) * 4);
    photo.read_rect(x0, y0, rw, rh, reinterpret_cast<uint8_t*>(in.data()), size_t(rw) * 8);
    uint16_t got[4];
    low.read_rect(x, y, 1, 1, reinterpret_cast<uint8_t*>(got), sizeof got);
    for (int c = 0; c < 3; c++) {
      double v = 0;
      for (int j = -R; j <= R; j++)
        for (int i = -R; i <= R; i++)
          v += k[i + R] * k[j + R] *
               in[(size_t(std::clamp(y + j, 0, h - 1) - y0) * size_t(rw) + size_t(std::clamp(x + i, 0, w - 1) - x0)) * 4 + size_t(c)];
      worst = std::max(worst, std::abs(v / (sum * sum) - got[c]));
    }
  }
  return worst;
}

// D1's frequency separation (its group, Low and High in Linear Light) composited alone against the Background, at
// tiles of its corners and inside: the largest difference, levels of 16 bits.
double fs_error(const DocState& d) {
  auto alone = std::make_shared<DocState>(d);
  auto root = std::make_shared<Layer>(*d.root);
  root->children.erase(std::remove_if(root->children.begin(), root->children.end(), [](const LayerPtr& l) { return l->id != 2; }),
                       root->children.end());
  alone->root = root;
  const TileGrid& photo = *find_layer(d, 1)->pixels;
  const int nx = (d.width + kTileSize - 1) / kTileSize, ny = (d.height + kTileSize - 1) / kTileSize;
  int worst = 0;
  for (const TileXY xy : {TileXY{0, 0}, {nx - 1, 0}, {0, ny - 1}, {nx - 1, ny - 1}, {nx / 2, ny / 2}, {3, 2}}) {
    TileGrid out({Depth::U16, 4});
    out.set(0, 0, composite_tile({alone, {}}, 0, xy.x, xy.y));
    std::vector<uint16_t> a(size_t(kTileSize) * kTileSize * 4), b(a.size());
    out.read_rect(0, 0, kTileSize, kTileSize, reinterpret_cast<uint8_t*>(a.data()), size_t(kTileSize) * 8);
    photo.read_rect(xy.x * kTileSize, xy.y * kTileSize, kTileSize, kTileSize, reinterpret_cast<uint8_t*>(b.data()),
                    size_t(kTileSize) * 8);
    for (int y = 0; y < std::min(kTileSize, d.height - xy.y * kTileSize); y++)
      for (int x = 0; x < std::min(kTileSize, d.width - xy.x * kTileSize); x++)
        for (size_t c = 0; c < 3; c++) {
          const size_t i = (size_t(y) * kTileSize + size_t(x)) * 4 + c;
          worst = std::max(worst, std::abs(a[i] - b[i]));
        }
  }
  return worst;
}

}  // namespace

BENCH(make) {
  using bench::report;
  std::string name, out;
  std::filesystem::path samples = bench::kSamples;
  for (size_t i = 0; i < args.size(); i++) {
    const bool more = i + 1 < args.size();
    if (args[i] == "--samples" && more) {
      samples = args[++i];
    } else if (args[i] == "--budget" && more) {
      set_tile_budget(size_t(std::stod(args[++i]) * kMB));
    } else if (args[i] == "--scratch" && more) {
      std::filesystem::create_directories(args[++i]);
      set_scratch_dir(args[i]);
    } else if (name.empty()) {
      name = args[i];
    } else {
      out = args[i];
    }
  }
  if (name != "d1" && name != "d2" && name != "d2mini" && name != "d4")
    return void(std::printf("make: pb-bench make d1|d2|d2mini|d4 [out.pbd] [--samples <dir>] [--budget <MB>] "
                            "[--scratch <dir>]\n"));
  const std::string n = "make_" + name;
  DocPtr d;
  double build = 0, read = 0;
  struct Row {
    const Layer* layer;
    int depth;
    Scan scan;
  };
  std::vector<Row> rows;  // every layer, in the tree's order
  {
    const Watch watch;
    std::printf("make: %s, tile budget %.0f MB\n", name.c_str(), tile_budget() / kMB);
    auto t0 = bench::Clock::now();
    d = name == "d1" ? bench::make_d1(samples) : name == "d4" ? bench::make_d4(samples) : bench::make_d2(name == "d2mini", samples);
    build = bench::ms_since(t0) / 1000;
    const std::function<void(const Layer&, int)> walk = [&](const Layer& g, int depth) {
      for (const LayerPtr& l : g.children) {
        rows.push_back({l.get(), depth});
        walk(*l, depth + 1);
      }
    };
    walk(*d->root, 1);
    t0 = bench::Clock::now();
    for (Row& r : rows)
      if (r.layer->pixels) r.scan = ::scan(*r.layer->pixels, d->width, d->height);
    read = bench::ms_since(t0) / 1000;
  }

  const double canvas = double(d->width) * d->height;
  size_t pixel = 0, tiles = 0, data = 0;
  int depth = 0;  // of the deepest group
  double cover = 0;
  std::map<std::string, int> kinds;  // D4's pixel layers by their names but the number
  uint64_t hash = fnv(fnv(fnv(fnv(kBasis, uint32_t(d->width)), uint32_t(d->height)), uint64_t(d->mode)), uint64_t(d->depth));
  for (const Row& r : rows) {
    hash = fnv(fnv(hash, r.layer->id), uint64_t(r.depth));
    for (const char c : to_json(r.layer->props)) hash = fnv(hash, uint8_t(c));
    if (!r.layer->pixels) {
      depth = std::max(depth, r.depth);
      continue;
    }
    hash = fnv(hash, r.scan.hash);
    pixel++, tiles += r.scan.tiles, data += r.scan.data, cover += r.scan.covered / canvas;
    kinds[r.layer->props.name.substr(0, r.layer->props.name.rfind(' '))]++;
  }
  std::printf("make: %s: %d x %d, %s, %d bits, %zu layers: %zu pixel layers, %zu groups nested %d deep\n", name.c_str(),
              d->width, d->height, d->mode == Mode::Rgb ? "RGB" : "grey", d->depth == Depth::U16 ? 16 : 8, rows.size(),
              pixel, rows.size() - pixel, depth);
  std::printf("make: %s: hash %016llx\n", name.c_str(), static_cast<unsigned long long>(hash));
  if (rows.size() <= 60) {
    for (const Row& r : rows) {
      std::printf("make: %*s%u %s (%s%s %.0f%%)", 2 * r.depth, "", r.layer->id, r.layer->props.name.c_str(),
                  r.layer->pixels ? "" : "group, ", kBlendNames[size_t(r.layer->props.blend)], 100 * r.layer->props.opacity);
      if (r.layer->pixels)
        std::printf(": covers %.2f%%, %zu tiles, %zu of data", 100 * r.scan.covered / canvas, r.scan.tiles, r.scan.data);
      std::printf("\n");
    }
  } else {
    for (const auto& [kind, count] : kinds) std::printf("make: %d pixel layers named %s n\n", count, kind.c_str());
  }
  report((n + "_build").c_str(), build, "s");
  report((n + "_layers").c_str(), double(rows.size()), "layers");
  report((n + "_coverage").c_str(), 100 * cover / double(pixel), "%");
  report((n + "_tiles").c_str(), double(tiles), "tiles");
  report((n + "_pixels").c_str(), double(data) * kTileSize * kTileSize * pixel_format(*d).bytes() / kMB, "MB");
  report((n + "_read").c_str(), read, "s");
  if (name == "d1") {
    report((n + "_low_error").c_str(), low_error(*d), "levels");
    report((n + "_frequency_separation_error").c_str(), fs_error(*d), "levels");
  }
  report((n + "_rss_peak").c_str(), bench::peak_resident_bytes() / kMB, "MB");
  report((n + "_budget").c_str(), tile_budget() / kMB, "MB");
  if (!out.empty()) std::printf("make: %s not written: .pbd files wait for M2\n", out.c_str());
}
