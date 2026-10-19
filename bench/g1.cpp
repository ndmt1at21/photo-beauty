// The G1 gate's benches (plan.md, Cổng G1; E3 of docs/p0-tasks.md), beside those they reuse: pb-bench frame (C3a) for
// the stroke latency, which `latency` names too, and pb-bench pan (present.cpp) for pan and zoom.
// pb-bench latency <log.jsonl ...> [--budget <MB>] [cpu]: pb-bench frame; its frame_latency_p95 is G1's stroke latency.
// pb-bench cpu-gpu [--budget <MB>]: D1 (make_d1(), docs.h) composited on the CPU (composite_tile()) and on the GPU
//   (GpuComposite), every tile of it fit to a 1920 x 1080 screen (mip level 2, its mips made first) and those of the
//   100 % view at its centre (level 0): the largest difference in levels of 16 bits, and of 8 once both are rounded
//   to 8 bits, and how many samples differ.
// pb-bench open-paint <doc.pbd> [--budget <MB>] [cpu] [nozstd] [log.jsonl ...]: a .pbd (D2) opened as the app opens
//   it (open_pbd()), 20 strokes painted through a Painting (frame.h) at their own pace on the middle pixel layer of its
//   stack, each in its cell of a 5 x 4 grid over the canvas with the view about it until sharp, then a pan over all of
//   it at 100 %, a view at a time; frames 1920 x 1080 offscreen. Meanwhile, every 100 ms, the process's anonymous
//   resident memory (Linux RssAnon; Windows the private bytes, PrivateUsage) against the tile budget, and apart the
//   file's pages mapped in (Linux RssFile), which the kernel takes back at will (E1 saw 6 GB of them reading D2 back):
//   its peak, the budget, the part over it. The strokes: the logs given, else C4's four of D1 (made in g1-strokes/ of
//   the working directory) shrunk into the cells, five times over. nozstd: cold tiles straight to scratch, the zstd
//   tier off (set_tile_compression(false)), which E4 weighs.
// pb-bench undo-thin [--budget <MB>]: the undo memory of fine strokes, which the tile's size sets (E4 weighs 128 px
//   tiles against 256): C4's four D1 strokes five times over, 2 to 5 px wide without pressure, 64 px further down and
//   right each time round, each a step of a Document of D1 (make_d1()) on Burn (selected, mid-stack, a fifth of it
//   painted), then afresh on the Background (pixels all over): history_bytes() after the 20 steps, and the steps kept.
// pb-bench g1 [d2.pbd] [--budget <MB>]: G1's four rows one after the other, each after a line with what to read and
//   its target: RAM (open-paint on the file given, else build/docs' d2.pbd, else its d2mini.pbd), then on D1 the
//   stroke latency (frame, on C4's strokes made in g1-strokes/), pan and zoom (pan) and the CPU against the GPU
//   (cpu-gpu). --budget sets the tile budget of them all (MB; else 60% of RAM).
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "bench.h"
#include "docs.h"
#include "frame.h"
#include "gpu_composite.h"
#include "mip.h"
#include "pbd.h"
#include "strokelog.h"
#include "tilecache.h"

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
// after windows.h; its 2nd version is kernel32's, nothing more to link
#define PSAPI_VERSION 2
#include <psapi.h>
#elif defined(__GLIBC__)
#include <malloc.h>
#endif

using namespace pb;
using pb::bench::Args;
using pb::bench::Clock;
using pb::bench::ms_since;
using pb::bench::report;

namespace {

constexpr int kW = 1920, kH = 1080;
constexpr double kMB = 1048576;

// Another of pb-bench's benches, by its name.
void run(const char* name, const Args& args) {
  for (const auto& b : bench::benches())
    if (std::strcmp(b.name, name) == 0) return b.run(args);
}

// C4's four D1 strokes (pb-bench make-strokes, strokes.cpp) as logs in g1-strokes/ of the working directory.
Args d1_logs() {
  run("make_strokes", {"g1-strokes"});
  Args logs;
  for (int i = 1; i <= 4; i++) logs.push_back("g1-strokes/d1-" + std::to_string(i) + ".jsonl");
  return logs;
}

double percentile(std::vector<double> v, double q) {
  std::sort(v.begin(), v.end());
  return v.empty() ? 0 : v[std::min(v.size() - 1, size_t(q * double(v.size())))];
}

// The process's resident memory, bytes: anonymous (the heap, what tiles take), and the pages of files mapped in.
// ponytail: 0 on macOS; task_vm_info's phys_footprint once E4 has a Mac.
struct Memory {
  size_t anon = 0, file = 0;
};
Memory memory() {
  Memory out;
#ifdef _WIN32
  PROCESS_MEMORY_COUNTERS_EX m{};
  if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&m), sizeof m)) {
    // ponytail: the file's pages as the working set past the private bytes, right while those stay in RAM;
    // PROCESS_MEMORY_COUNTERS_EX2's private working set once mingw has it.
    out.anon = m.PrivateUsage;
    out.file = m.WorkingSetSize > m.PrivateUsage ? m.WorkingSetSize - m.PrivateUsage : 0;
  }
#else
  if (FILE* f = std::fopen("/proc/self/status", "r")) {
    char line[256];
    size_t kb;
    while (std::fgets(line, sizeof line, f)) {
      if (std::sscanf(line, "RssAnon: %zu kB", &kb) == 1) out.anon = kb << 10;
      if (std::sscanf(line, "RssFile: %zu kB", &kb) == 1) out.file = kb << 10;
    }
    std::fclose(f);
  }
#endif
  return out;
}

// Prints memory() against the tile budget, and what the tile store holds of it, every 100 ms while it lives; keeps
// the peaks.
class Watch {
 public:
  explicit Watch(const gpu::Device* dev = nullptr) : dev_(dev), t_([this] { run(); }) {}
  ~Watch() {
    {
      const std::lock_guard<std::mutex> l(m_);
      stop_ = true;
    }
    cv_.notify_all();
    t_.join();
  }
  Memory peak() {
    const std::lock_guard<std::mutex> l(m_);
    return peak_;
  }

 private:
  void run() {
    const auto t0 = Clock::now();
    std::unique_lock<std::mutex> l(m_);
    for (int i = 0; !cv_.wait_until(l, t0 + std::chrono::milliseconds(100 * i), [this] { return stop_; }); i++) {
      const Memory now = memory();
      const TileStats t = tile_stats();
      peak_.anon = std::max(peak_.anon, now.anon), peak_.file = std::max(peak_.file, now.file);
      const gpu::Device::Held g = dev_ ? dev_->held() : gpu::Device::Held{};
      std::printf("open-paint: %.1f s: anon %.0f MB, file %.0f MB, budget %.0f MB; tiles raw %.0f, compressed %.0f, "
                  "charged %.0f, scratch %.0f MB; device %.0f, staging %.0f, kept %.0f MB\n",
                  i / 10., now.anon / kMB, now.file / kMB, tile_budget() / kMB, t.raw / kMB, t.compressed / kMB,
                  t.charged / kMB, t.scratch / kMB, g.device / kMB, g.staging / kMB, g.kept / kMB);
      std::fflush(stdout);
    }
  }
  std::mutex m_;
  std::condition_variable cv_;
  bool stop_ = false;
  Memory peak_;
  const gpu::Device* dev_;
  std::thread t_;  // last: the rest is there before it starts
};

// The pixel layer in the middle of d's stack, bottom up through groups.
LayerId middle_layer(const DocState& d) {
  std::vector<LayerId> ids;
  const std::function<void(const Layer&)> walk = [&](const Layer& g) {
    for (const LayerPtr& l : g.children) {
      if (l->pixels) ids.push_back(l->id);
      walk(*l);
    }
  };
  walk(*d.root);
  return ids.empty() ? 0 : ids[ids.size() / 2];
}

// The CPU's composite and the GPU's of `tiles` at `level`: the largest difference in 16-bit levels and in 8-bit ones
// (both rounded to 8 bits), the samples that differ, and the tiles with pixels on either side (none: nothing compared).
struct Diff {
  int d16 = 0, d8 = 0, tiles = 0;
  size_t differ = 0;
};
Diff compare(gpu::Device& dev, GpuComposite& gc, const RenderInput& in, int level, const std::vector<TileXY>& tiles) {
  const std::vector<int> slots = gc.composite(in, level, tiles);
  const size_t n = size_t(kTileSize) * kTileSize * 4;  // samples in a tile
  std::vector<uint16_t> got(size_t(gc.output_tiles()) * n), want(n);
  dev.download(gc.output(), got.data(), got.size() * 2);
  dev.flush();
  const auto to8 = [](int v) { return (v * 255 + 32767) / 65535; };
  Diff out;
  for (size_t i = 0; i < tiles.size(); i++) {
    std::fill(want.begin(), want.end(), uint16_t(0));
    const TilePtr c = composite_tile(in, level, tiles[i].x, tiles[i].y);
    out.tiles += c || slots[i] >= 0;
    if (c) {
      TileGrid g({Depth::U16, 4});
      g.set(0, 0, c);
      g.read_rect(0, 0, kTileSize, kTileSize, reinterpret_cast<uint8_t*>(want.data()), size_t(kTileSize) * 8);
    }
    for (size_t j = 0; j < n; j++) {
      const int a = slots[i] < 0 ? 0 : got[size_t(slots[i]) * n + j], b = want[j];
      out.d16 = std::max(out.d16, std::abs(a - b)), out.d8 = std::max(out.d8, std::abs(to8(a) - to8(b)));
      out.differ += a != b;
    }
  }
  return out;
}

std::vector<TileXY> rect(TileRect r) {
  std::vector<TileXY> out;
  for (int ty = r.y0; ty < r.y1; ty++)
    for (int tx = r.x0; tx < r.x1; tx++) out.push_back({tx, ty});
  return out;
}

// What the bench before let go of, out of the process: its mips (the cache keeps them past their document) and the
// heap's free pages.
void let_go() {
  clear_mips();
#ifdef __GLIBC__
  malloc_trim(0);
#endif
}

}  // namespace

BENCH(latency) { run("frame", args); }

BENCH(cpu_gpu) {
  for (size_t i = 0; i + 1 < args.size(); i++)
    if (args[i] == "--budget") set_tile_budget(size_t(std::stod(args[i + 1]) * kMB));
  const std::unique_ptr<gpu::Device> dev = gpu::Device::create(true);
  if (!dev) return void(std::puts("cpu-gpu: no Vulkan device"));
  std::printf("cpu-gpu: %s, %s, tile budget %.0f MB\n", dev->name().c_str(), dev->driver().c_str(), tile_budget() / kMB);
  const RenderInput in{bench::make_d1()};
  const DocState& d = *in.doc;
  // Fit to the screen: level 2, where D1 is 2064 x 1376, 9 x 6 tiles, its mips made first as the canvas has them;
  // 100 %: the 1920 x 1080 at the centre, columns 3168 to 5088, rows 2212 to 3292 (as pb-bench gpu-composite's).
  const int s = kTileSize << 2;
  const TileRect fit{0, 0, (d.width + s - 1) / s, (d.height + s - 1) / s};
  const std::function<void(const Layer&)> mips = [&](const Layer& l) {
    if (l.pixels) prefetch(*l.pixels, 2, fit);
    for (const LayerPtr& c : l.children) mips(*c);
  };
  mips(*d.root);
  GpuComposite gc(*dev);
  const auto row = [&](const char* name, int level, TileRect r) {
    const Diff diff = compare(*dev, gc, in, level, rect(r));
    const std::string n = std::string("cpu_gpu_d1_") + name;
    report((n + "_diff16").c_str(), diff.d16, "levels");
    report((n + "_diff8").c_str(), diff.d8, "levels");
    report((n + "_differ").c_str(), double(diff.differ), "samples");
    report((n + "_tiles").c_str(), diff.tiles, "tiles");
  };
  row("fit", 2, fit);
  row("100", 0, {(d.width / 2 - kW / 2) / kTileSize, (d.height / 2 - kH / 2) / kTileSize,
                 (d.width / 2 + kW / 2 + kTileSize - 1) / kTileSize, (d.height / 2 + kH / 2 + kTileSize - 1) / kTileSize});
}

BENCH(open_paint) {
  std::string file;
  Args logs;
  bool cpu = false, zstd = true;
  for (size_t i = 0; i < args.size(); i++)
    if (args[i] == "--budget" && i + 1 < args.size())
      set_tile_budget(size_t(std::stod(args[++i]) * kMB));
    else if (args[i] == "cpu")
      cpu = true;
    else if (args[i] == "nozstd")
      set_tile_compression(zstd = false);
    else if (file.empty())
      file = args[i];
    else
      logs.push_back(args[i]);
  if (file.empty())
    return void(std::puts("open-paint: pb-bench open-paint <doc.pbd> [--budget <MB>] [cpu] [nozstd] [log.jsonl ...]"));
  if (logs.empty()) logs = d1_logs();
  std::vector<StrokeRecord> strokes;
  for (const std::string& l : logs) strokes.push_back(read_stroke_log(l));
  const std::unique_ptr<gpu::Device> dev = cpu ? nullptr : gpu::Device::create(true);
  std::printf("open-paint: %s: %s, tile budget %.0f MB%s, device memory %.0f MB\n", file.c_str(),
              dev ? (dev->name() + ", " + dev->driver()).c_str() : "on the CPU", tile_budget() / kMB,
              zstd ? "" : " without the zstd tier", dev ? dev->memory() / kMB : 0.);
  const std::string n = "open_paint_" + std::filesystem::path(file).stem().string();
  report((n + "_rss_anon_base").c_str(), memory().anon / kMB, "MB");  // before the document: code, the driver
  Memory peak;
  {
    Watch watch(dev.get());
    auto t0 = Clock::now();
    Document doc(open_pbd(file).doc, "Open");
    report((n + "_open").c_str(), ms_since(t0), "ms");
    const DocState& d = *doc.state();
    const LayerId layer = middle_layer(d);

    std::mutex m;  // on_ready()'s calls, to wait for
    std::condition_variable cv;
    int ready = 0;
    Presenter p(dev.get(), [&] {
      const std::lock_guard<std::mutex> l(m);
      ready++;
      cv.notify_all();
    });
    std::vector<uint32_t> rows;
    gpu::BufferPtr buf;
    Target target;
    if (dev)
      target.buffer = (buf = dev->buffer(size_t(kW) * kH * 4)).get();
    else
      rows.resize(size_t(kW) * kH), target.pixels = rows.data();
    std::atomic<bool> pending{false};  // the last frame's
    Painting::Hooks hooks;
    hooks.target = [&](const View::Pixels&) { return target; };
    hooks.shown = [&](const Presenter::Frame& f, const Target&) {
      pending = f.pending;
      if (dev) dev->wait(f.submission);  // done with it, as the canvas's copy is
    };
    Painting painting(doc, p, hooks);
    const auto show = [&](double x, double y, double zoom) {  // until sharp
      View v;
      v.x = x, v.y = y, v.zoom = zoom, v.width = kW, v.height = kH;
      for (;;) {
        int k;
        {
          const std::lock_guard<std::mutex> l(m);
          k = ready;
        }
        painting.frame(v);
        painting.wait();
        if (!pending) return;
        std::unique_lock<std::mutex> l(m);
        cv.wait_for(l, std::chrono::seconds(60), [&] { return ready > k; });
      }
    };

    t0 = Clock::now();
    std::vector<double> latency;
    const double cw = d.width / 5., ch = d.height / 4.;  // the grid's cells
    for (int i = 0; i < 20; i++) {
      StrokeRecord r = strokes[size_t(i) % strokes.size()];
      const double x0 = (i % 5) * cw, y0 = (i / 5) * ch;
      for (PenEvent& e : r.events) e.x = float(x0 + e.x * cw / r.width), e.y = float(y0 + e.y * ch / r.height);
      show(x0 + cw / 2, y0 + ch / 2, r.zoom);
      painting.begin(layer, r.brush);
      const auto s0 = Clock::now();
      for (PenEvent e : r.events) {
        std::this_thread::sleep_until(
            s0 + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(e.t - r.events[0].t)));
        e.t = std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
        painting.add(e);
      }
      painting.end();
      painting.wait();
      const Painting::Stats st = painting.stats();
      latency.insert(latency.end(), st.latency.begin(), st.latency.end());
    }
    report((n + "_strokes").c_str(), ms_since(t0) / 1000, "s");
    report((n + "_latency_p95").c_str(), percentile(latency, 0.95), "ms");  // not G1's: that is D1's

    t0 = Clock::now();  // a pan over all of it: rows of views at 100 %, there and back
    int views = 0;
    const int across = (d.width + kW - 1) / kW;
    for (int row = 0; row * kH < d.height; row++)
      for (int c = 0; c < across; c++, views++) show(((row % 2 ? across - 1 - c : c) + 0.5) * kW, (row + 0.5) * kH, 1);
    report((n + "_pan").c_str(), ms_since(t0) / 1000, "s");
    report((n + "_pan_views").c_str(), views, "frames");
    report((n + "_steps").c_str(), double(doc.steps()), "steps");
    peak = watch.peak();
  }
  const double over = (double(peak.anon) - double(tile_budget())) / kMB;
  report((n + "_rss_anon_peak").c_str(), peak.anon / kMB, "MB");
  report((n + "_rss_file_peak").c_str(), peak.file / kMB, "MB");
  report((n + "_budget").c_str(), tile_budget() / kMB, "MB");
  report((n + "_over").c_str(), std::max(over, 0.), "MB");
}

BENCH(undo_thin) {
  for (size_t i = 0; i + 1 < args.size(); i++)
    if (args[i] == "--budget") set_tile_budget(size_t(std::stod(args[i + 1]) * kMB));
  const Args logs = d1_logs();
  const DocPtr d1 = bench::make_d1();
  for (const LayerId layer : {d1->selected[0], LayerId(1)}) {  // Burn, the Background (docs.h)
    Document doc(d1, "Open");
    for (int i = 0; i < 20; i++) {
      StrokeRecord r = read_stroke_log(logs[size_t(i) % logs.size()]);
      r.brush.size = float(2 + i % 4), r.brush.pressure_size = false;
      Stroke s(doc.state(), layer, r.brush);
      for (PenEvent e : r.events) {
        e.x += 64.f * float(i / 4), e.y += 64.f * float(i / 4);
        s.add(e);
      }
      s.finish();
      doc.run(SetPixels{layer, s.wait()->pixels}, "Brush");
    }
    const std::string n = layer == 1 ? "undo_thin_d1_background" : "undo_thin_d1_burn";
    report(n.c_str(), doc.history_bytes() / kMB, "MB");
    report((n + "_steps").c_str(), double(doc.steps()), "steps");
  }
}

BENCH(g1) {
  std::string d2, budget;
  for (size_t i = 0; i < args.size(); i++)
    if (args[i] == "--budget" && i + 1 < args.size())
      budget = args[++i];
    else
      d2 = args[i];
  for (const char* f : {"d2.pbd", "d2mini.pbd"})
    if (d2.empty() && std::filesystem::exists(std::filesystem::path(bench::kDocs) / f))
      d2 = (std::filesystem::path(bench::kDocs) / f).string();
  const auto with = [&](Args a) {
    if (!budget.empty()) a.push_back("--budget"), a.push_back(budget);
    return a;
  };
  std::puts("g1: 1/4, RAM: D2 opened and painted within the tile budget: open_paint_<file>_over, 0 MB");
  if (d2.empty())
    std::printf("g1: no D2: pb-bench make d2 (or d2mini) makes it in %s\n", bench::kDocs);
  else
    run("open_paint", with({d2}));
  let_go();
  std::puts("g1: 2/4, stroke latency on D1: frame_latency_p95, at most 8 ms");
  run("frame", with(d1_logs()));
  let_go();
  std::puts("g1: 3/4, pan and zoom on D1: pan_d1_fps, at least 60 fps");
  run("pan", with({}));
  let_go();
  std::puts("g1: 4/4, the composite on the CPU against the GPU's on D1: cpu_gpu_d1_*_diff8, at most 1 level");
  run("cpu_gpu", with({}));
}

