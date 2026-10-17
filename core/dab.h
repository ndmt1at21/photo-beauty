#pragma once
// Dabs: where a stroke's round brush tip lands and how strongly (C1), for Stroke (C2) to stamp into its buffer.
// DabPlacer turns pen events into dabs along a curve through them; dab_mask() gives a dab's coverage, cached. The same
// events give the same dabs and masks bit for bit whatever the threads, the batches events come in, the order masks are
// asked for, the compiler or the CPU: C4 replays strokes recorded elsewhere and compares hashes.
//
//   DabPlacer placer(params);                    // pen down
//   for (const Dab& d : placer.add(event)) {     // each event, in order
//     const DabMask m = dab_mask(d, params.hardness);  // stamp m.px over m.x, m.y, m.w, m.h
//   }
//   ... placer.finish() ...                      // pen up: the dabs after the last but one event
#include <cstdint>
#include <memory>
#include <vector>

#include "stroke.h"

namespace pb {

// One stamp of the brush tip.
struct Dab {
  float x = 0, y = 0;  // centre, document pixels (as PenEvent)
  float size = 0;      // diameter, document pixels: BrushParams::size, times the pressure when pressure_size
  float opacity = 1;   // 0..1: BrushParams::opacity, times the pressure when pressure_opacity
  float flow = 1;      // 0..1: BrushParams::flow, times the pressure when pressure_flow
};

// Places one stroke's dabs as its events come: one at the first event, then along a centripetal Catmull-Rom curve
// through the events (no loops or cusps between them, even where events bunch up), one every spacing x the size of the
// dab before it of arc length (1 px at least); the pressure goes linearly with the curve's parameter between events.
// The curve from an event to the next depends on the one after, so add() returns the dabs up to the event before the
// newest and finish() the rest: what they return never changes after. A plain value: copy it and finish the copy to see
// where the stroke would end now. One thread at a time (the stroke's job); any number of placers at once.
class DabPlacer {
 public:
  explicit DabPlacer(const BrushParams&);  // its values are clamped to their ranges (size to 5,000 px)
  // The dabs this event made certain, in order. An event within 1/1000 px of the last one only sets its pressure;
  // one with x or y not finite is skipped; x, y are clamped to +-2^20 px, the pressure to 0..1.
  std::vector<Dab> add(const PenEvent&);
  std::vector<Dab> finish();  // pen up: the rest; add() and finish() return none after it

 private:
  struct Point {
    double x = 0, y = 0, pressure = 0;
  };
  static Point mirror(const Point& b, const Point& c);  // c reflected about b
  void span(const Point& a, const Point& b, const Point& c, const Point& d, std::vector<Dab>& out);
  double place(double x, double y, double pressure, std::vector<Dab>& out) const;

  BrushParams params_;
  Point pts_[4];      // the last four points, the newest last
  int n_ = 0;         // points so far; -1 once finished
  double carry_ = 0;  // arc length from the last point to the next dab
};

// A dab's coverage, 0..65535: pixel (i, j) for x <= i < x + w, y <= j < y + h at px[(j - y) * w + i - x].
// Empty (w, h 0, px null) for a dab under 1/8 px.
struct DabMask {
  int x = 0, y = 0, w = 0, h = 0;
  std::shared_ptr<const std::vector<uint16_t>> px;  // shared with the cache and every dab alike
};

// The mask of a dab with this hardness (0..1). The size and the centre are rounded to 1/4 px and the hardness to 1%,
// and the mask depends on those alone (never on which dab asked first): masks are cached by them, least recently used
// out past 64 MB. Any thread; the shape:
// - hardness h keeps the centre covered fully out to h of the radius, as Photoshop's manual has it ("the size of the
//   brush's hard center"); from there to the rim the coverage falls as (1 - t^2)^4, t going 0..1 across the fall-off.
//   Our choice, not measured from Photoshop yet: a bell like a Gaussian of sigma a third of the fall-off, flat where it
//   leaves the centre and fading to 0 with no slope at the rim, so a soft dab looks smaller than its size as
//   Photoshop's do (at 0%: half covered at 0.40 of the radius, a tenth at 0.66). B4 can stamp single dabs in Photoshop
//   (100% opacity and flow, black on white, 100 px, hardness 0, 50, 90%) to fit kFalloffPower in dab.cpp or the curve.
// - hardness 100% (or a fall-off under 1/8 px): a hard edge, each pixel covered by its share of the disc (exact across
//   8 rows of it and down 8 columns), so the dab covers pi r^2 to 1% from 1.5 px. A fall-off under 2 px wide takes
//   8 x 8 samples a pixel near it; a wider one its centre's value.
DabMask dab_mask(const Dab&, float hardness);
DabMask make_dab_mask(const Dab&, float hardness);  // the same made afresh, not cached: tests, bench

}  // namespace pb
