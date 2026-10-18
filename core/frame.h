#pragma once
// Painting on the canvas (C3a of docs/p0-tasks.md), without Qt: the UI thread's pen events into a Stroke (stroke.h);
// frames of the document's state with the stroke laid over it (StrokeState::overlay, which GpuComposite starts from its
// cache below the layer) through a Presenter (present.h), on a Latest job at Interactive that each state the stroke
// publishes asks for; pen-up's pixels a step of the Document (history.h), "Brush" or "Eraser"; the stroke's log while
// PB_RECORD_STROKES is set (strokelog.h); each event's time from PenEvent::t to the Device::submit() of the first frame
// that shows it (Tracy plot "stroke latency"). C3b's Canvas feeds it; pb-bench frame plays C4's logs through it.
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include "history.h"
#include "present.h"
#include "stroke.h"

namespace pb {

class Painting {
 public:
  // Called on the frame job, one frame at a time: the presenter's device's thread meanwhile. None may wait for the UI
  // thread.
  struct Hooks {
    // Each frame's target, for its View::pixels(): a buffer of the presenter's device, or rows for a CPU presenter.
    std::function<Target(const View::Pixels&)> target;
    // After Presenter::frame() returned (on the GPU: submitted): the canvas copies the target into its texture and
    // hands it to its window; tests and benches read it.
    std::function<void(const Presenter::Frame&, const Target&)> shown;
    // After a frame of a stroke that pen-up merged: the canvas has the UI thread commit() it.
    std::function<void()> merged;
    // Held from target() to the end of shown(), let go of when the frame throws: the lock of a device that other
    // paintings and users share (one gpu::Device for the app's canvases); null for none. ~Painting() and wait() wait
    // for the frame being drawn: never with it held.
    std::mutex* device = nullptr;
  };

  // Frames of `doc`'s states through `presenter`, which only this calls from now on; both outlive it. UI thread.
  Painting(Document& doc, Presenter& presenter, Hooks hooks);
  ~Painting();  // waits for the frame being drawn; drops a stroke not committed
  Painting(const Painting&) = delete;
  Painting& operator=(const Painting&) = delete;

  // UI thread: a frame of the document's state now, the stroke being painted over it, as `view` shows it: whenever the
  // view, the look or the document (a command, undo) changed, or the presenter's on_ready() came. The stroke's frames
  // keep them.
  void frame(const View& view, const Look& look = {});
  // UI thread: pen down on pixel layer `layer` of the document's state (else std::invalid_argument, as Stroke), once
  // the stroke before is ended and committed; recorded while PB_RECORD_STROKES is set, at the last frame()'s zoom.
  void begin(LayerId layer, const BrushParams& brush);
  // UI thread: the next event, in document pixels (View::to_doc()), its t on std::chrono::steady_clock; none dropped
  // or coalesced. Nothing without a stroke begun.
  void add(const PenEvent& e);
  void end();  // UI thread: pen up; the stroke merges on its job
  // UI thread: an ended stroke's step, "Brush" or "Eraser", once it merged (waiting for that); frames then show the
  // document without its overlay. Run no other command on the document from begin() until then.
  void commit();
  // UI thread, for tests and benches: until every event added is applied (and committed after end()) and a frame drawn
  // since shows it, and the logs are written.
  void wait();

  // Since the last begin(): its stroke's events, and the frames. ponytail: whole, some KB a minute of painting or
  // panning; a ring once a canvas pans for hours between strokes.
  struct Stats {
    size_t added = 0;              // events
    size_t applied = 0;            // of them, those the stroke applied
    size_t shown = 0;              // of them, those a frame showed
    std::vector<double> latency;   // ms, each shown event's, in order: PenEvent::t to the submit of its first frame
    std::vector<double> frame_ms;  // each frame's: Presenter::frame() and Hooks::shown()
  };
  Stats stats() const;  // any thread

 private:
  struct Impl;
  std::shared_ptr<Impl> impl_;  // shared with the jobs
};

}  // namespace pb
