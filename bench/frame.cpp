// pb-bench frame <log.jsonl ...> [--budget <MB>] [cpu]: C4's strokes played at their own pace through a Painting
// (frame.h) on D1 (make_d1(), bench/docs.h), each on its selected layer (Burn) with each event added at its time from
// the first, its frames 1920 x 1080 offscreen at the log's zoom about the stroke, on the GPU there is (lavapipe in
// WSL, the Iris Xe from the mingw build) or with `cpu` on the CPU; each GPU frame copied into a texture and waited for,
// as the canvas does (where the device exports textures; else only waited for). Before each stroke its view settles:
// its mips made, the caches warm. Over every event, p50, p95 and p99 of the time from its PenEvent::t to the submit of
// the first frame with it; over the strokes' frames, p50, p95 and p99 of their time, and on the GPU of its part after
// the submit (the GPU's work, the copy). --budget sets the tile budget (MB; else 60% of RAM). C4's D1 strokes, 300 px
// with pressure on size: pb-bench make-strokes <dir>.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "bench.h"
#include "docs.h"
#include "frame.h"
#include "strokelog.h"
#include "tilecache.h"

using namespace pb;
using pb::bench::Clock;
using pb::bench::report;

namespace {

constexpr int kW = 1920, kH = 1080;

// Until t: asleep while more than 2 ms off, then yielding. Windows sleeps 15.6 ms at a time: there it yields all along.
void wait_until(Clock::time_point t) {
#ifndef _WIN32
  while (t - Clock::now() > std::chrono::milliseconds(2)) std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif
  while (Clock::now() < t) std::this_thread::yield();
}

double percentile(std::vector<double> v, double q) {
  std::sort(v.begin(), v.end());
  return v.empty() ? 0 : v[std::min(v.size() - 1, size_t(q * double(v.size())))];
}

}  // namespace

BENCH(frame) {
  std::vector<std::string> logs;
  bool cpu = false;
  for (size_t i = 0; i < args.size(); i++) {
    if (args[i] == "--budget" && i + 1 < args.size())
      set_tile_budget(size_t(std::stod(args[++i]) * (1 << 20)));
    else if (args[i] == "cpu")
      cpu = true;
    else
      logs.push_back(args[i]);
  }
  if (logs.empty()) return void(std::puts("frame: pb-bench frame <log.jsonl ...> [--budget <MB>] [cpu] plays strokes on D1"));
  const std::unique_ptr<gpu::Device> dev = cpu ? nullptr : gpu::Device::create(true);
  std::printf("frame: %s\n", dev ? (dev->name() + ", " + dev->driver()).c_str() : "on the CPU");
  Document doc(bench::make_d1(), "Open");

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
  gpu::TexturePtr tex;
  Target target;
  if (dev)
    target.buffer = (buf = dev->buffer(size_t(kW) * kH * 4)).get(), tex = dev->texture(kW, kH);
  else
    rows.resize(size_t(kW) * kH), target.pixels = rows.data();
  std::atomic<bool> pending{false};  // the last frame's
  Painting::Hooks hooks;
  hooks.target = [&](const View::Pixels&) { return target; };
  std::vector<double> waited;  // under m: each GPU frame's shown(), the GPU's work left and the copy
  hooks.shown = [&](const Presenter::Frame& f, const Target&) {
    pending = f.pending;
    if (!dev) return;
    const auto t0 = Clock::now();
    if (tex) dev->copy(*buf, *tex, 0, 0, kW, kH), dev->release(*tex), dev->flush();
    else dev->wait(f.submission);
    const std::lock_guard<std::mutex> l(m);
    waited.push_back(bench::ms_since(t0));
  };
  Painting painting(doc, p, hooks);

  std::vector<double> latency, frames, gpu;
  size_t events = 0, shown = 0;
  for (const std::string& path : logs) {
    const StrokeRecord r = read_stroke_log(path);
    if (r.events.empty()) continue;
    View v;  // the stroke's box's centre
    float x0 = r.events[0].x, x1 = x0, y0 = r.events[0].y, y1 = y0;
    for (const PenEvent& e : r.events)
      x0 = std::min(x0, e.x), x1 = std::max(x1, e.x), y0 = std::min(y0, e.y), y1 = std::max(y1, e.y);
    v.x = (x0 + x1) / 2., v.y = (y0 + y1) / 2., v.zoom = r.zoom, v.width = kW, v.height = kH;
    for (;;) {
      int n;
      {
        const std::lock_guard<std::mutex> l(m);
        n = ready;
      }
      painting.frame(v);
      painting.wait();
      if (!pending) break;
      std::unique_lock<std::mutex> l(m);
      cv.wait_for(l, std::chrono::seconds(60), [&] { return ready > n; });
    }
    {
      const std::lock_guard<std::mutex> l(m);
      waited.clear();
    }
    painting.begin(doc.state()->selected[0], r.brush);
    const auto t0 = Clock::now();
    for (PenEvent e : r.events) {
      wait_until(t0 + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(e.t - r.events[0].t)));
      e.t = std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
      painting.add(e);
    }
    painting.end();
    painting.wait();
    const Painting::Stats s = painting.stats();
    latency.insert(latency.end(), s.latency.begin(), s.latency.end());
    frames.insert(frames.end(), s.frame_ms.begin(), s.frame_ms.end());
    events += s.added, shown += s.shown;
    const std::lock_guard<std::mutex> l(m);
    gpu.insert(gpu.end(), waited.begin(), waited.end());
  }
  report("frame_events", double(events), "events");
  report("frame_shown", double(shown), "events");
  report("frame_latency_p50", percentile(latency, 0.5), "ms");
  report("frame_latency_p95", percentile(latency, 0.95), "ms");
  report("frame_latency_p99", percentile(latency, 0.99), "ms");
  report("frame_frames", double(frames.size()), "frames");
  report("frame_p50", percentile(frames, 0.5), "ms");
  report("frame_p95", percentile(frames, 0.95), "ms");
  report("frame_p99", percentile(frames, 0.99), "ms");
  if (!dev) return;
  report("frame_gpu_p50", percentile(gpu, 0.5), "ms");  // of frame_*: after the submit, until the copy was done
  report("frame_gpu_p95", percentile(gpu, 0.95), "ms");
}
