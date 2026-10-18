// Painting (frame.h): the UI thread holds the Document, the Stroke and its log; what frames read sits under a mutex:
// the state they show, the stroke laid over it until its step is in that state, the view, the events' times. One
// Latest job draws the newest of it, asked for by frame(), commit() and each state the stroke publishes.
#include "frame.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <future>
#include <mutex>
#include <string>
#include <utility>

#include "jobs.h"
#include "profile.h"
#include "strokelog.h"
#ifdef PB_TRACY
#include <tracy/Tracy.hpp>
#endif

namespace pb {
namespace {

double now() {  // as PenEvent::t
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

}  // namespace

struct Painting::Impl : std::enable_shared_from_this<Impl> {
  Document& doc;
  Presenter& presenter;
  const Hooks hooks;
  Latest job{Priority::Interactive};

  // The UI thread's.
  std::shared_ptr<Stroke> stroke;  // begun, until committed
  LayerId layer = 0;
  bool erase = false, ended = false;
  std::unique_ptr<StrokeLog> log;
  std::vector<std::future<std::string>> logs;  // being written

  // Under m.
  mutable std::mutex m;
  std::condition_variable cv;
  DocPtr shows;                  // the state frames show
  std::shared_ptr<Stroke> over;  // laid over it
  std::shared_ptr<Stroke> last;  // the stroke of `stats`
  View view;
  Look look;
  std::vector<double> times;  // the last stroke's events' PenEvent::t
  Stats stats;
  uint64_t strokes = 0;           // begun: a frame adds to the stats of the one it started with
  uint64_t asked = 0, drawn = 0;  // frames asked for; the last ask a finished frame started after
  bool dead = false, drawing = false;

  Impl(Document& d, Presenter& p, Hooks h) : doc(d), presenter(p), hooks(std::move(h)), shows(d.state()) {}

  // UI thread: frames show the document's state now, and the stroke being painted over it.
  void sync() {
    const std::lock_guard<std::mutex> l(m);
    shows = doc.state(), over = stroke;
  }

  // Any thread: a frame, of what is newest when it starts. Its ask, 0 once the painting is gone.
  uint64_t request() {
    uint64_t n;
    {
      const std::lock_guard<std::mutex> l(m);
      if (dead) return 0;
      n = ++asked;
    }
    job.submit([w = weak_from_this()](const CancelToken&) {
      if (const auto i = w.lock()) i->draw();
    });
    return n;
  }

  void draw() {
    PROFILE_ZONE("frame");
    RenderInput in;
    std::shared_ptr<Stroke> s;
    View v;
    Look lk;
    uint64_t ask, ours;
    size_t visible;  // events of the last stroke the frame shows
    {
      const std::lock_guard<std::mutex> l(m);
      if (dead) return;
      drawing = true;
      ask = asked, ours = strokes, in.doc = shows, s = over, v = view, lk = look, visible = times.size();
    }
    bool merged = false;
    if (s) {
      const std::shared_ptr<const StrokeState> st = s->state();
      in.stroke = st->overlay, visible = st->events, merged = bool(st->pixels);
    }
    double at = 0, ms = -1;  // when it was submitted; how long it took, -1 for no frame
    const View::Pixels px = v.pixels();
    try {
      if (in.doc && px.width > 0 && px.height > 0) {
        std::unique_lock<std::mutex> device;
        if (hooks.device) device = std::unique_lock<std::mutex>(*hooks.device);
        const double t0 = now();
        const Target t = hooks.target(px);
        const Presenter::Frame f = presenter.frame(in, v, t, lk);
        at = now();
        if (hooks.shown) hooks.shown(f, t);
        ms = (now() - t0) * 1e3;
      }
      if (merged && hooks.merged) hooks.merged();
    } catch (const std::exception& e) {  // no hang in wait(), ~Painting(): the next frame tries again
      std::fprintf(stderr, "pb: frame failed: %s\n", e.what());
    } catch (...) {
      std::fprintf(stderr, "pb: frame failed\n");
    }
    const std::lock_guard<std::mutex> l(m);
    drawing = false, drawn = ask;
    if (ms >= 0) {
      stats.frame_ms.push_back(ms);
      for (; ours == strokes && stats.shown < visible; stats.shown++) {
        const double lag = (at - times[stats.shown]) * 1e3;
        stats.latency.push_back(lag);
#ifdef PB_TRACY
        TracyPlot("stroke latency", lag);
#endif
      }
    }
    cv.notify_all();
  }
};

Painting::Painting(Document& doc, Presenter& presenter, Hooks hooks)
    : impl_(std::make_shared<Impl>(doc, presenter, std::move(hooks))) {}

Painting::~Painting() {
  Impl& i = *impl_;
  std::unique_lock<std::mutex> l(i.m);
  i.dead = true;  // a frame queued draws nothing; the stroke's jobs ask for none
  i.cv.wait(l, [&] { return !i.drawing; });
}

void Painting::frame(const View& view, const Look& look) {
  Impl& i = *impl_;
  {
    const std::lock_guard<std::mutex> l(i.m);
    i.view = view, i.look = look;
  }
  i.sync();
  i.request();
}

void Painting::begin(LayerId layer, const BrushParams& brush) {
  Impl& i = *impl_;
  end();
  commit();
  const DocPtr d = i.doc.state();
  const std::weak_ptr<Impl> w = impl_;
  i.stroke = std::make_shared<Stroke>(d, layer, brush, [w] {
    if (const auto p = w.lock()) p->request();
  });
  i.layer = layer, i.erase = brush.erase, i.ended = false;
  {
    const std::lock_guard<std::mutex> l(i.m);  // at once, so that a frame's stroke is the stats' or none
    i.shows = d, i.over = i.last = i.stroke, i.times.clear(), i.stats = {}, i.strokes++;
  }
  i.log = StrokeLog::record(*d, layer, brush, i.view.zoom);  // the UI thread alone writes the view
}

void Painting::add(const PenEvent& e) {
  Impl& i = *impl_;
  if (!i.stroke || i.ended) return;
  {
    const std::lock_guard<std::mutex> l(i.m);
    i.times.push_back(e.t);  // before the stroke can apply it
    i.stats.added++;
  }
  i.stroke->add(e);
  if (i.log) i.log->add(e);
}

void Painting::end() {
  Impl& i = *impl_;
  if (!i.stroke || i.ended) return;
  i.ended = true;
  i.stroke->finish();
}

void Painting::commit() {
  Impl& i = *impl_;
  if (!i.stroke || !i.ended) return;
  PROFILE_ZONE("frame_commit");
  const std::shared_ptr<const StrokeState> st = i.stroke->wait();
  const std::unique_ptr<StrokeLog> log = std::move(i.log);
  i.stroke.reset();
  if (st->pixels) {  // else the stroke failed (stroke.cpp said why): no step
    i.doc.run(SetPixels{i.layer, st->pixels}, i.erase ? "Eraser" : "Brush");
    if (log) i.logs.push_back(log->finish(st));
  }
  i.sync();
  i.request();
}

void Painting::wait() {
  Impl& i = *impl_;
  if (i.stroke) i.stroke->wait();
  commit();
  for (const std::future<std::string>& f : i.logs) f.wait();
  i.logs.clear();
  const uint64_t n = i.request();
  std::unique_lock<std::mutex> l(i.m);
  i.cv.wait(l, [&] { return i.drawn >= n; });
}

Painting::Stats Painting::stats() const {
  Stats s;
  std::shared_ptr<Stroke> last;
  {
    const std::lock_guard<std::mutex> l(impl_->m);
    s = impl_->stats;
    last = impl_->last;
  }
  if (last) s.applied = last->state()->events;
  return s;
}

}  // namespace pb
