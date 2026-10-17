// pb-bench strokes [log.jsonl ...]: strokes played at their own pace, each event added at its time from the first, on
// replay_doc() of their canvas (for D1 one layer: a stroke's job reads and writes no other); over every event, p50, p95
// and p99 of the time from add() to the first published state that has it, and p99 of how late events were added.
// Without logs, made-up D1 strokes (8256 x 5504, 16 bits, 300 px with pressure on size; as make-strokes writes them).
// A log's buffer hash is checked, and its pixels' when it was painted on replay_doc() (doc "plain").
// pb-bench make-strokes <dir> [test]: those D1 strokes as logs into dir; with `test`, small ones in each mode and
// depth, painting and erasing, for tests/data/strokes/.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "bench.h"
#include "strokelog.h"

using namespace pb;
using pb::bench::Clock;

namespace {

using Logs = std::vector<std::pair<std::string, StrokeRecord>>;  // name, stroke

// A scribble of n events at 200 Hz: it wanders about `step` px an event, now and then past the canvas's edges, its
// pressure drifting between 0.1 and 1.
std::vector<PenEvent> scribble(std::mt19937& rng, int n, int w, int h, double step) {
  std::vector<PenEvent> ev;
  double x = w / 5 + rng() % unsigned(w * 3 / 5), y = h / 5 + rng() % unsigned(h * 3 / 5), vx = 0, vy = 0, p = 0.5;
  for (int i = 0; i < n; i++) {
    vx = vx * 0.9 + step * (int(rng() % 41) - 20) / 50, vy = vy * 0.9 + step * (int(rng() % 41) - 20) / 50;
    x = std::clamp(x + vx, -step, w + step), y = std::clamp(y + vy, -step, h + step);
    p = std::clamp(p + (int(rng() % 41) - 20) / 1000., 0.1, 1.);
    ev.push_back({i / 200., float(x), float(y), float(p)});
  }
  return ev;
}

// D1's: 2 s each, 300 px with pressure on size, hard and soft, at full and low flow, and erasing.
Logs d1_strokes() {
  std::mt19937 rng(1);
  Logs out;
  for (int i = 0; i < 4; i++) {
    StrokeRecord r{8256, 5504, Mode::Rgb, Depth::U16, 1, {}, 0.5, "plain"};
    BrushParams& b = r.brush;
    b.size = 300, b.hardness = i == 1 ? 0.f : 0.8f, b.flow = i == 2 ? 0.3f : 1.f, b.erase = i == 3;
    b.color = {0.85f, 0.3f, 0.25f};
    r.events = scribble(rng, 400, r.width, r.height, 12);
    out.push_back({"d1-" + std::to_string(i + 1), std::move(r)});
  }
  return out;
}

// Small ones for tests: 60 events on 700 x 500 (edge tiles the canvas cuts), each mode and depth, pressure on each of
// size, opacity and flow.
Logs test_strokes() {
  struct Kind {
    const char* name;
    Mode mode;
    Depth depth;
    bool erase;
    float size, hardness, opacity, flow, spacing;
    int pressure;  // on size, opacity, flow
    double zoom;
  };
  const Kind kinds[] = {{"rgb8-paint", Mode::Rgb, Depth::U8, false, 30, 0.5f, 0.8f, 0.6f, 0.25f, 0, 1},
                        {"gray16-paint", Mode::Gray, Depth::U16, false, 90, 0, 1, 0.3f, 0.1f, 1, 0.5},
                        {"rgb16-erase", Mode::Rgb, Depth::U16, true, 12, 1, 0.7f, 1, 0.5f, 2, 2},
                        {"gray8-erase", Mode::Gray, Depth::U8, true, 3, 1, 1, 0.8f, 0.05f, 0, 0.3}};
  std::mt19937 rng(2);
  Logs out;
  for (const Kind& k : kinds) {
    StrokeRecord r{700, 500, k.mode, k.depth, 1, {}, k.zoom, "plain"};
    BrushParams& b = r.brush;
    b.size = k.size, b.hardness = k.hardness, b.opacity = k.opacity, b.flow = k.flow, b.spacing = k.spacing;
    b.pressure_size = k.pressure == 0, b.pressure_opacity = k.pressure == 1, b.pressure_flow = k.pressure == 2;
    b.color = {0.9f, 0.2f, 0.1f}, b.erase = k.erase;
    r.events = scribble(rng, 60, r.width, r.height, k.size / 4 + 3);
    out.push_back({k.name, std::move(r)});
  }
  return out;
}

// Until t: asleep while more than 2 ms off, then yielding. Windows sleeps 15.6 ms at a time: there it yields all along.
void wait_until(Clock::time_point t) {
#ifndef _WIN32
  while (t - Clock::now() > std::chrono::milliseconds(2)) std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif
  while (Clock::now() < t) std::this_thread::yield();
}

double ms(Clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); }

// r played on d at its own pace; appends each event's time from add() to the first published state that has it, and
// how late it was added (ms). The merged state.
std::shared_ptr<const StrokeState> paced(const DocPtr& d, const StrokeRecord& r, std::vector<double>& lags,
                                         std::vector<double>& late) {
  const size_t n = r.events.size();
  std::vector<Clock::time_point> added(n);
  std::vector<double> lag(n);
  size_t seen = 0;                // events whose lag is known: on_publish's, which runs on one job at a time
  std::atomic<bool> done{false};  // on_publish is through with the merged state
  const Stroke* self = nullptr;   // set before the first add(), which starts the jobs
  Stroke s(d, d->selected[0], r.brush, [&] {
    const auto st = self->state();
    const auto now = Clock::now();
    for (; seen < st->events; seen++) lag[seen] = ms(now - added[seen]);
    if (st->pixels) done = true;
  });
  self = &s;
  const auto t0 = Clock::now();
  for (size_t i = 0; i < n; i++) {
    const std::chrono::duration<double> t(r.events[i].t - r.events[0].t);
    const auto at = t0 + std::chrono::duration_cast<Clock::duration>(t);
    wait_until(at);
    added[i] = Clock::now();
    late.push_back(ms(added[i] - at));
    s.add(r.events[i]);
  }
  s.finish();
  const auto st = s.wait();
  while (st->pixels && !done) std::this_thread::yield();
  lags.insert(lags.end(), lag.begin(), lag.end());
  return st;
}

double percentile(std::vector<double> v, double q) {
  std::sort(v.begin(), v.end());
  return v[size_t(q * double(v.size() - 1) + 0.5)];
}

}  // namespace

BENCH(strokes) {
  Logs logs;
  if (args.empty()) logs = d1_strokes();
  for (const std::string& a : args) logs.push_back({a, read_stroke_log(a)});
  std::vector<double> lags, late;
  for (const auto& [name, r] : logs) {
    const auto st = paced(replay_doc(r), r, lags, late);
    if (args.empty()) continue;  // made up here: no hash to check
    const StrokeHash h = st->pixels ? stroke_hash(*st) : StrokeHash{};
    if (h.buffer != r.hash.buffer || (r.doc == "plain" && h.pixels != r.hash.pixels))
      std::fprintf(stderr, "strokes: %s: painted otherwise than recorded\n", name.c_str());
  }
  if (lags.empty()) return;
  pb::bench::report("strokes_events", double(lags.size()), "events");
  pb::bench::report("strokes_p50", percentile(lags, 0.5), "ms");
  pb::bench::report("strokes_p95", percentile(lags, 0.95), "ms");
  pb::bench::report("strokes_p99", percentile(lags, 0.99), "ms");
  pb::bench::report("strokes_late_p99", percentile(late, 0.99), "ms");
}

BENCH(make_strokes) {
  if (args.empty()) return void(std::puts("make-strokes: pb-bench make-strokes <dir> [test] writes made-up strokes"));
  std::filesystem::create_directories(args[0]);
  for (auto& [name, r] : args.size() > 1 && args[1] == "test" ? test_strokes() : d1_strokes()) {
    const DocPtr d = replay_doc(r);
    Stroke s(d, d->selected[0], r.brush);
    for (const PenEvent& e : r.events) s.add(e);
    s.finish();
    r.hash = stroke_hash(*s.wait());
    const std::string path = args[0] + "/" + name + ".jsonl";
    const bool written = bool(std::ofstream(path, std::ios::binary) << to_jsonl(r));
    std::printf("%s%s\n", written ? "" : "cannot write ", path.c_str());
  }
}
