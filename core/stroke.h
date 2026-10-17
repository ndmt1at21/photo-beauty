#pragma once
// A brush stroke: pen events in, dabs into a stroke buffer, the buffer merged into the layer at pen-up. Declared
// here; C1 places dabs (dab.h), C2 implements Stroke (stroke.cpp).
#include <array>
#include <cstddef>
#include <functional>
#include <memory>

#include "doc.h"
#include "render.h"  // StrokeOverlay
#include "tile.h"

namespace pb {

// One pen or mouse sample as the UI got it.
struct PenEvent {
  double t = 0;                  // seconds on the monotonic clock (std::chrono::steady_clock)
  float x = 0, y = 0;            // document pixels (View::to_doc()), pixel (i, j) covering [i, i + 1) x [j, j + 1)
  float pressure = 1;            // 0..1; a mouse gives 1
  float tilt_x = 0, tilt_y = 0;  // degrees, -60..60, as QTabletEvent
  float rotation = 0;            // degrees, -180..180, as QTabletEvent
};

// The Brush and Eraser tools' options.
struct BrushParams {
  float size = 30;        // diameter, document pixels, at full pressure
  float hardness = 1;     // 0..1
  float opacity = 1;      // 0..1: the most the whole stroke covers
  float flow = 1;         // 0..1: how much each dab adds
  float spacing = 0.25f;  // dab to dab, a fraction of the current size: 0.01..10
  bool pressure_size = true, pressure_opacity = false, pressure_flow = false;  // pen pressure scales them
  std::array<float, 3> color{};  // the document's encoded values, 0..1 (grey: color[0])
  bool erase = false;            // the Eraser: takes alpha away instead of laying color
};

// What a stroke published: after each batch of events it applied, and once more when finish() merged it.
struct StrokeState {
  std::shared_ptr<const TileGrid> buffer;  // coverage so far, as StrokeOverlay::buffer: changed tiles have new ids
  size_t events = 0;                       // events applied so far, in the order added
  std::shared_ptr<const TileGrid> pixels;  // once merged: the layer's pixels with the stroke (SetPixels), else null
  // What frames composite (RenderInput::stroke), as pen-up merges it: the brush's colour, opacity and mode, clamped
  // to 0..1, over `buffer` with the stroke's tail stamped in too. The tail is the dabs from the event before the
  // newest to the newest: DabPlacer (dab.h) gives those only with the next event, as the curve depends on it, so these
  // are where they would fall were the pen lifted now, and the next state moves them. A frame thus shows every event
  // applied without waiting for the next (5 ms at 200 Hz). The buffer is `buffer` itself where the tail has no dabs,
  // and once merged.
  StrokeOverlay overlay;
};

// One stroke on one pixel layer, pen down to pen up. The UI thread makes, feeds and finishes it; Interactive jobs
// apply the events in order (never Latest: no event is dropped), alone write the stroke buffer, and publish copies.
// The buffer holds coverage B of canvas pixels only (as Photoshop paints): each dab takes B toward its cap by its
// mask d times its flow, B = B + d x flow x (cap - B) rounded up to 1/65535, cap 1 but where the pen's pressure sets
// the opacity (then the dab's opacity over the stroke's): the stroke covers at most its opacity, as Photoshop's.
class Stroke {
 public:
  // `doc` as the stroke starts, `layer` one of its pixel layers (else std::invalid_argument). on_publish runs on the
  // stroke's job after each published state (a frame schedules itself there); keep it short.
  Stroke(DocPtr doc, LayerId layer, const BrushParams&, std::function<void()> on_publish = {});
  ~Stroke();                  // never waits: what is not published by then is dropped
  void add(const PenEvent&);  // UI thread: queued, never dropped or coalesced
  void finish();              // UI thread: pen up, after the last add()
  std::shared_ptr<const StrokeState> state() const;  // any thread: the latest published, never null
  // Blocks until every event added so far is applied, and merged after finish(): tests, bench, replay.
  std::shared_ptr<const StrokeState> wait() const;

 private:
  struct Impl;
  std::shared_ptr<Impl> impl_;  // shared with the stroke's jobs
};

// How many threads strokes stamp and merge on: 1, each stroke's job alone; anything else (0 by default) the pool's.
// What they make never depends on it, each tile taking its dabs in order: tests and replays (C4) compare. Any thread.
void set_stroke_threads(int n);

}  // namespace pb
