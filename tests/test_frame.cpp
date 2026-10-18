// Painting (frame.h): pen events through it into an offscreen target come out as C2's pen-up and the reference composite
// do (a CPU presenter's frame of the merged state), on the CPU and on the GPU there is, painting a layer in a group and
// erasing; before pen-up the frames show the stroke as its overlay composites; each stroke one step, "Brush" and
// "Eraser", even when the next begins at once; undo and redo give the tile ids and frames back; every event added is
// applied and shown, with its latency; the logs PB_RECORD_STROKES asks for; a painting dropped mid-stroke; two on one
// device lock.
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "check.h"
#include "frame.h"
#include "mip.h"
#include "strokelog.h"

using namespace pb;
namespace fs = std::filesystem;

namespace {

constexpr int S = kTileSize;
constexpr int W = 700, H = 500;  // 3 x 2 tiles, the last ones cut
using Px = std::array<uint8_t, 4>;

double now() {  // as PenEvent::t
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void set_env(const char* name, const std::string& value) {
#ifdef _WIN32
  _putenv_s(name, value.c_str());
#else
  if (value.empty())
    unsetenv(name);
  else
    setenv(name, value.c_str(), 1);
#endif
}

uint8_t u8(double v) { return uint8_t(std::lround(std::clamp(v, 0., 1.) * 255)); }

// A layer's pixels, RGBA U8: c(x, y) over the canvas's tiles where has(tx, ty).
std::shared_ptr<const TileGrid> grid(const std::function<bool(int, int)>& has, const std::function<Px(int, int)>& c) {
  auto g = std::make_shared<TileGrid>(Format{Depth::U8, 4});
  std::vector<uint8_t> px(size_t(S) * S * 4);
  for (int ty = 0; ty * S < H; ty++)
    for (int tx = 0; tx * S < W; tx++) {
      if (!has(tx, ty)) continue;
      for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) {
          const Px v = c(tx * S + x, ty * S + y);
          std::copy(v.begin(), v.end(), px.begin() + (ptrdiff_t(y) * S + x) * 4);
        }
      g->write_rect(tx * S, ty * S, S, S, px.data(), size_t(S) * 4);
    }
  return g;
}

// 8-bit sRGB, bottom up: 1 Background, opaque; 4 a group at 80 % (Normal: isolated) of 2 Paint (blobs with alpha
// over half the tiles) and 3 Multiply; 5 Screen at 50 % over the lower tiles.
DocPtr sample() {
  LayerProps p;
  DocPtr d = new_document(W, H, Mode::Rgb, Depth::U8, nullptr);
  p.name = "Background";
  d = run(*d, AddLayer{0, 0, Kind::Pixel, p, grid([](int, int) { return true; }, [](int x, int y) {
                         return Px{u8(x / 700.), u8(y / 500.), u8(0.5 + 0.4 * std::sin(x * 0.02 + y * 0.03)), 255};
                       })});
  p.name = "Paint";
  d = run(*d, AddLayer{0, 1, Kind::Pixel, p, grid([](int tx, int ty) { return (tx + ty) % 2 == 0; }, [](int x, int y) {
                         return Px{u8(0.2), u8(0.6), u8(0.9), u8(0.5 + 0.5 * std::sin(x * 0.05) * std::cos(y * 0.04))};
                       })});
  p.name = "Multiply", p.blend = Blend::Multiply;
  d = run(*d, AddLayer{0, 2, Kind::Pixel, p, grid([](int tx, int) { return tx != 1; }, [](int x, int y) {
                         return Px{u8(0.9), u8(0.8 - y / 1000.), u8(0.5 + x / 2000.), u8(0.6)};
                       })});
  p = {}, p.name = "Group", p.opacity = 0.8f;
  d = run(*d, GroupLayers{{2, 3}, p});
  p.name = "Screen", p.blend = Blend::Screen, p.opacity = 0.5f;
  d = run(*d, AddLayer{0, 2, Kind::Pixel, p, grid([](int, int ty) { return ty == 1; }, [](int x, int y) {
                         return Px{u8((x % 50) / 50.), u8(0.3), u8((y % 70) / 70.), u8(0.7)};
                       })});
  CHECK(find_layer(*d, 4)->kind == Kind::Group && find_layer(*d, 4)->children.size() == 2 && find_layer(*d, 5));
  return d;
}

// What C2 makes of a stroke alone: its overlay once every event is applied, its pen-up, the state after it.
struct Ref {
  StrokeOverlay overlay;
  std::shared_ptr<const StrokeState> merged;
  DocPtr after;
};
Ref reference(const DocPtr& d, LayerId layer, const BrushParams& b, const std::vector<PenEvent>& events) {
  Stroke s(d, layer, b);
  for (const PenEvent& e : events) s.add(e);
  Ref r;
  r.overlay = s.wait()->overlay;
  s.finish();
  r.merged = s.wait();
  r.after = run(*d, SetPixels{layer, r.merged->pixels});
  return r;
}

bool same_pixels(const DocState& a, const DocState& b, LayerId id) {
  std::vector<uint8_t> pa(size_t(W) * H * 4), pb(pa.size());
  find_layer(a, id)->pixels->read_rect(0, 0, W, H, pa.data(), size_t(W) * 4);
  find_layer(b, id)->pixels->read_rect(0, 0, W, H, pb.data(), size_t(W) * 4);
  return pa == pb;
}

std::vector<std::pair<TileXY, uint64_t>> ids(const DocState& d, LayerId id) {
  std::vector<std::pair<TileXY, uint64_t>> v;
  for (const auto& [xy, t] : find_layer(d, id)->pixels->tiles()) v.push_back({xy, t->id()});
  return v;
}

// Every event added applied and shown, each latency from its own time to before now.
void check_counts(const Painting::Stats& s, const std::vector<double>& times) {
  const size_t n = times.size();
  CHECK(s.added == n && s.applied == n && s.shown == n && s.latency.size() == n && !s.frame_ms.empty());
  const double t = now();
  for (size_t i = 0; i < n; i++) CHECK(s.latency[i] >= 0 && s.latency[i] <= (t - times[i]) * 1e3);
}

// The events added now, their times.
std::vector<double> paint(Painting& p, const std::vector<PenEvent>& events) {
  std::vector<double> times;
  for (PenEvent e : events) {
    times.push_back(e.t = now());
    p.add(e);
  }
  return times;
}

// Frames of a painting through dev's presenter (the CPU's when null) into an offscreen target, as a canvas takes them,
// against a CPU presenter's frames of the references.
void check(gpu::Device* dev, const fs::path& logs) {
  const DocPtr d0 = sample();
  std::vector<PenEvent> ea, eb;  // across group and canvas edge with pressure; erasing up the background
  for (int i = 0; i < 40; i++) {
    ea.push_back({0, float(30 + i * 18), float(250 + 170 * std::sin(i * 0.3)), float(0.3 + 0.7 * std::abs(std::sin(i * 0.17)))});
    eb.push_back({0, float(660 - i * 16), float(60 + i * 10 + 30 * std::sin(i * 0.6))});
  }
  BrushParams ba, bb;
  ba.size = 40, ba.hardness = 0.6f, ba.opacity = 0.9f, ba.flow = 0.8f, ba.color = {0.9f, 0.2f, 0.1f};
  bb.size = 30, bb.opacity = 0.7f, bb.spacing = 0.2f, bb.erase = true;
  const Ref a = reference(d0, 2, ba, ea), b = reference(a.after, 1, bb, eb);

  View v;  // the canvas and a pasteboard round it, at 100 %
  v.x = W / 2., v.y = H / 2., v.width = W + 20, v.height = H + 20;
  const View::Pixels vp = v.pixels();
  Presenter cpu;
  const auto expect = [&](const RenderInput& in) {
    std::vector<uint32_t> px(size_t(vp.width) * size_t(vp.height));
    Target t;
    t.pixels = px.data();
    CHECK(!cpu.frame(in, v, t).pending);
    return px;
  };

  Document doc(d0, "Open");
  Presenter p(dev);
  const size_t n = size_t(vp.width) * size_t(vp.height);
  std::vector<uint32_t> rows(n);  // a CPU presenter's target
  const gpu::BufferPtr buf = dev ? dev->buffer(n * 4) : nullptr;
  Target target;
  if (dev)
    target.buffer = buf.get();
  else
    target.pixels = rows.data();
  std::mutex m;
  std::vector<uint32_t> px;  // the last frame shown: a stroke's job may yet ask for one more alike after wait()
  const auto shot = [&] {
    const std::lock_guard<std::mutex> l(m);
    return px;
  };
  std::atomic<int> merged{0};
  Painting::Hooks hooks;
  hooks.target = [&](const View::Pixels& q) {
    CHECK(q.width == vp.width && q.height == vp.height);
    return target;
  };
  hooks.shown = [&](const Presenter::Frame& f, const Target&) {
    CHECK(!f.pending && f.missing == 0);
    std::vector<uint32_t> got = rows;
    if (dev) dev->download(*buf, got.data(), n * 4), dev->wait(dev->submit());  // as a canvas copies it on
    const std::lock_guard<std::mutex> l(m);
    px = std::move(got);
  };
  hooks.merged = [&] { merged++; };
  {
    Painting painting(doc, p, hooks);
    painting.frame(v);
    painting.wait();
    const std::vector<uint32_t> open = shot();
    CHECK(open == expect({d0}));

    set_env("PB_RECORD_STROKES", logs.string());
    painting.begin(2, ba);
    const std::vector<double> ta = paint(painting, ea);
    painting.wait();
    CHECK(shot() == expect({d0, a.overlay}) && shot() != open);  // shown before pen-up
    check_counts(painting.stats(), ta);
    CHECK(doc.steps() == 1 && merged == 0);
    painting.end();
    painting.begin(1, bb);  // at once: the stroke before goes in first
    set_env("PB_RECORD_STROKES", "");
    CHECK(doc.steps() == 2 && doc.step(1).name == "Brush" && same_pixels(*doc.state(), *a.after, 2));
    const std::vector<double> tb = paint(painting, eb);
    painting.wait();
    CHECK(shot() == expect({a.after, b.overlay}));
    const int seen = merged;
    painting.end();
    const auto t0 = std::chrono::steady_clock::now();
    while (merged == seen) {  // a frame of the merged stroke, which the canvas would commit() upon
      CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(60));
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(doc.steps() == 2);
    painting.wait();
    CHECK(shot() == expect({b.after}));
    CHECK(doc.steps() == 3 && doc.step(2).name == "Eraser" && same_pixels(*doc.state(), *b.after, 1));
    check_counts(painting.stats(), tb);

    // Undo and redo: the tiles the steps had, their frames.
    const auto i1 = ids(*doc.state(), 1), i2 = ids(*doc.state(), 2);
    CHECK(i1 != ids(*d0, 1) && i2 != ids(*d0, 2));
    CHECK(doc.undo());
    painting.frame(v);
    painting.wait();
    CHECK(ids(*doc.state(), 1) == ids(*d0, 1) && ids(*doc.state(), 2) == i2 && shot() == expect({a.after}));
    CHECK(doc.undo());
    painting.frame(v);
    painting.wait();
    CHECK(ids(*doc.state(), 2) == ids(*d0, 2) && shot() == open);
    CHECK(doc.redo() && doc.redo());
    painting.frame(v);
    painting.wait();
    CHECK(ids(*doc.state(), 1) == i1 && ids(*doc.state(), 2) == i2 && shot() == expect({b.after}));
  }

  // The logs, as the strokes were painted.
  std::vector<StrokeRecord> rec;
  for (const fs::directory_entry& e : fs::directory_iterator(logs)) rec.push_back(read_stroke_log(e.path().string()));
  CHECK(rec.size() == 2);
  if (rec[0].brush.erase) std::swap(rec[0], rec[1]);
  CHECK(rec[0].layer == 2 && rec[0].zoom == 1 && rec[0].hash == stroke_hash(*a.merged));
  CHECK(rec[1].layer == 1 && rec[1].brush.erase && rec[1].hash == stroke_hash(*b.merged));
  for (size_t i = 0; i < ea.size(); i++) CHECK(rec[0].events.size() == ea.size() && rec[0].events[i].x == ea[i].x);
  for (size_t i = 0; i < eb.size(); i++) CHECK(rec[1].events.size() == eb.size() && rec[1].events[i].y == eb[i].y);

  {  // dropped mid-stroke: its frames and stroke stop, no step
    Document d(d0, "Open");
    Painting gone(d, p, hooks);
    gone.frame(v);
    gone.begin(2, ba);
    paint(gone, ea);
  }
}

// Two paintings on one device lock (Hooks::device) never draw at once; a frame that throws lets go of it.
void check_lock() {
  const DocPtr d0 = sample();
  View v;
  v.x = W / 2., v.y = H / 2., v.width = 64, v.height = 48;
  std::mutex device;
  std::atomic<int> inside{0}, frames{0};
  std::atomic<bool> overlap{false};
  std::vector<uint32_t> rows[2];
  const auto hooks = [&](int i) {
    Painting::Hooks h;
    h.target = [&, i](const View::Pixels& q) {
      if (++inside > 1) overlap = true;
      std::this_thread::sleep_for(std::chrono::milliseconds(2));  // long enough for the other to come in
      rows[i].resize(size_t(q.width) * size_t(q.height));
      Target t;
      t.pixels = rows[i].data();
      return t;
    };
    h.shown = [&](const Presenter::Frame&, const Target&) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      inside--, frames++;
    };
    h.device = &device;
    return h;
  };
  Document da(d0, "Open"), db(d0, "Open"), dc(d0, "Open");
  Presenter pa, pb, pc;
  {
    Painting a(da, pa, hooks(0)), b(db, pb, hooks(1));
    for (int k = 0; k < 20; k++) {
      a.frame(v), b.frame(v);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    a.wait(), b.wait();
  }
  CHECK(!overlap && frames >= 4 && inside == 0);
  Painting::Hooks bad = hooks(0);
  bad.target = [](const View::Pixels&) { return Target{}; };  // no pixels: Presenter::frame() throws
  Painting c(dc, pc, bad);
  c.frame(v);
  c.wait();
  CHECK(device.try_lock());
  device.unlock();
}

}  // namespace

TEST(test_frame) {
  const fs::path dir =
      fs::temp_directory_path() / ("pb-frame-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  check(nullptr, dir / "cpu");
  check_lock();
  if (const auto dev = test::gpu_device()) {
    std::printf("frame: %s\n", dev->name().c_str());
    check(dev.get(), dir / "gpu");
  }
  fs::remove_all(dir);
  clear_mips();
}
