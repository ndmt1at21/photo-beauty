// pb-bench present [cpu] [--budget <MB>]: 1920 x 1080 frames of D1 (make_d1(), bench/docs.h) offscreen through the
// Presenter (present.h), on the GPU there is (lavapipe in WSL, the Iris Xe from the mingw build) or with `cpu` on the
// CPU; --budget sets the tile budget (tilecache.h), as `pb-bench make` does, where RAM is short. Fit to
// the screen (mip level 2) and at 100 % (the centre, level 0), each with a cold cache and a new presenter: the first
// frame, the time until all of it is sharp, then the median frame once its tiles are there. Then paths at 60 Hz: a pan
// at 100 %, a pan at 50 % (level 1) and a zoom from 100 % out to fit, each over mips made afresh: the p95 frame time
// and the longest any part of the frame waited to be sharp. Times in ms, frame() and the GPU's work until it is done.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "bench.h"
#include "docs.h"
#include "mip.h"
#include "present.h"
#include "tilecache.h"

using namespace pb;
using pb::bench::Clock;
using pb::bench::ms_since;
using pb::bench::report;

namespace {

constexpr int kW = 1920, kH = 1080;

// on_ready()'s calls, to wait for.
struct Ready {
  std::mutex m;
  std::condition_variable cv;
  int n = 0;
  std::function<void()> fn() {
    return [this] {
      const std::lock_guard l(m);
      n++;
      cv.notify_all();
    };
  }
  int count() {
    const std::lock_guard l(m);
    return n;
  }
  void wait_past(int k) {
    std::unique_lock l(m);
    cv.wait_for(l, std::chrono::seconds(60), [&] { return n > k; });
  }
};

// Frames of one presenter into one target: a buffer on the GPU (the canvas copies it into its texture), else rows.
struct Frames {
  gpu::Device* dev;
  const RenderInput& in;
  Ready ready;
  std::unique_ptr<Presenter> p;
  std::vector<uint32_t> px;
  gpu::BufferPtr buf;
  Target target;

  Frames(gpu::Device* d, const RenderInput& input) : dev(d), in(input) {
    if (dev)
      target.buffer = (buf = dev->buffer(size_t(kW) * kH * 4)).get();
    else
      px.resize(size_t(kW) * kH), target.pixels = px.data();
    renew();
  }
  void renew() { p = std::make_unique<Presenter>(dev, ready.fn()); }
  double frame(const View& v, Presenter::Frame& f) {  // ms
    const auto t0 = Clock::now();
    f = p->frame(in, v, target);
    if (dev) dev->wait(f.submission);
    return ms_since(t0);
  }
  // The time until a frame of v is sharp, ms from t0; the frames it took into ms.
  double sharp(const View& v, Clock::time_point t0, std::vector<double>* ms = nullptr) {
    for (;;) {
      const int n = ready.count();
      Presenter::Frame f;
      const double t = frame(v, f);
      if (ms) ms->push_back(t);
      if (!f.pending) return ms_since(t0);
      ready.wait_past(n);
    }
  }
};

View at(double x, double y, double zoom) {
  View v;
  v.x = x, v.y = y, v.zoom = zoom, v.width = kW, v.height = kH;
  return v;
}

double percentile(std::vector<double> v, double q) {
  std::sort(v.begin(), v.end());
  return v.empty() ? 0 : v[std::min(v.size() - 1, size_t(q * double(v.size())))];
}

// A view, cold: a new presenter, the cache's mips gone. The first frame, the time until sharp, the median of 9 then
// and the mips those rebuilt.
void still(Frames& fr, const View& v, const std::string& name) {
  clear_mips();
  fr.renew();
  Presenter::Frame f;
  const auto t0 = Clock::now();
  report((name + "_first").c_str(), fr.frame(v, f), "ms");
  report((name + "_first_coarse").c_str(), f.coarser + f.missing, "tiles");
  report((name + "_sharp").c_str(), fr.sharp(v, t0), "ms");
  std::vector<double> ms;
  const uint64_t built = mips_built();
  for (int i = 0; i < 9; i++) ms.push_back(fr.frame(v, f));
  report(name.c_str(), percentile(ms, 0.5), "ms");
  report((name + "_rebuilt").c_str(), double(mips_built() - built), "mips");  // 0 unless the mip cache is too small
}

// A path of views at 60 Hz, paced in real time (a frame late starts at once), over mips made afresh: the p95 frame
// time, and the longest a frame went on showing some tile from a coarser level or none, until one showed all sharp.
void path(Frames& fr, const std::function<View(double)>& view, double seconds, const std::string& name) {
  clear_mips();
  std::vector<double> ms;
  double lag = 0;
  bool behind = false;
  Clock::time_point since;
  const auto t0 = Clock::now();
  for (int i = 0; i <= int(seconds * 60); i++) {
    std::this_thread::sleep_until(t0 + std::chrono::microseconds(int64_t(i * 1e6 / 60)));
    Presenter::Frame f;
    const auto start = Clock::now();
    ms.push_back(fr.frame(view(i / 60.), f));
    if (f.coarser + f.missing > 0 && !behind) behind = true, since = start;
    if (f.coarser + f.missing == 0 && behind) behind = false, lag = std::max(lag, ms_since(since));
  }
  if (behind) lag = std::max(lag, fr.sharp(view(seconds), since));
  report((name + "_p95").c_str(), percentile(ms, 0.95), "ms");
  report((name + "_lag").c_str(), lag, "ms");
}

}  // namespace

BENCH(present) {
  bool cpu = false;
  for (size_t i = 0; i < args.size(); i++)
    if (args[i] == "cpu")
      cpu = true;
    else if (args[i] == "--budget" && i + 1 < args.size())
      set_tile_budget(size_t(std::stod(args[++i]) * (1 << 20)));
  const std::unique_ptr<gpu::Device> dev = cpu ? nullptr : gpu::Device::create(true);
  if (dev)
    std::printf("present: %s, %s\n", dev->name().c_str(), dev->driver().c_str());
  else
    std::printf("present: on the CPU\n");
  std::printf("present: tile budget %zu MB\n", tile_budget() >> 20);
  const RenderInput in{bench::make_d1()};
  const DocState& d = *in.doc;
  Frames fr(dev.get(), in);
  const double fit = std::min(double(kW) / d.width, double(kH) / d.height), cx = d.width / 2., cy = d.height / 2.;
  still(fr, at(cx, cy, fit), "present_d1_fit");
  still(fr, at(cx, cy, 1), "present_d1_100");
  // 1,200 document pixels a second across the middle; at 50 % 2,400; the zoom out by a constant factor a frame.
  path(fr, [&](double t) { return at(cx - 1800 + 1200 * t, cy, 1); }, 3, "present_d1_pan_100");
  path(fr, [&](double t) { return at(cx - 3600 + 2400 * t, cy, 0.5); }, 3, "present_d1_pan_50");
  path(fr, [&](double t) { return at(cx, cy, std::pow(fit, std::min(t, 1.))); }, 1.5, "present_d1_zoom_out");
  report("present_peak_rss", double(bench::peak_resident_bytes() >> 20), "MB");
}
