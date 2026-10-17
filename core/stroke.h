#pragma once
// A brush stroke: pen events in, dabs into a stroke buffer, the buffer merged into the layer at pen-up. Declared
// here; C1 places dabs (dab.h), C2 implements Stroke (stroke.cpp).
#include <array>
#include <cstddef>
#include <functional>
#include <memory>

#include "doc.h"
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
};

// One stroke on one pixel layer, pen down to pen up. The UI thread makes, feeds and finishes it; Interactive jobs
// apply the events in order (never Latest: no event is dropped), alone write the stroke buffer, and publish copies.
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

}  // namespace pb
