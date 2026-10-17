// pb-bench dab: dabs a second for brushes of 300 and 5 px (spacing 25%, pressure on size) along a long scribble:
// placing them (DabPlacer), making their masks afresh (every dab its own size, as under pressure; hard and soft), and
// masks from the cache (a mouse: one size). Medians of 5 runs.
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "bench.h"
#include "dab.h"

using namespace pb;
using pb::bench::Clock;

namespace {

template <class F>
double median_s(F run) {
  std::vector<double> s;
  for (int i = 0; i < 5; i++) {
    const auto t0 = Clock::now();
    run();
    s.push_back(pb::bench::ms_since(t0) / 1000);
  }
  std::nth_element(s.begin(), s.begin() + 2, s.end());
  return s[2];
}

}  // namespace

BENCH(dab) {
  std::vector<PenEvent> ev;  // 4,000 events at 200 Hz, a loose Lissajous about 6 px apart, pressure 0.3 to 1
  for (int i = 0; i < 4000; i++) {
    const double t = i / 200.;
    ev.push_back({t, float(2000 + 1500 * std::sin(0.31 * t)), float(1500 + 1000 * std::sin(0.47 * t + 1)),
                  float(0.65 + 0.35 * std::sin(1.3 * t))});
  }
  for (const float size : {300.f, 5.f}) {
    BrushParams p;
    p.size = size;
    std::vector<Dab> dabs;
    const double place = median_s([&] {
      for (int run = 0; run < 10; run++) {
        DabPlacer placer(p);
        dabs.clear();
        for (const PenEvent& e : ev) {
          const std::vector<Dab> d = placer.add(e);
          dabs.insert(dabs.end(), d.begin(), d.end());
        }
        const std::vector<Dab> d = placer.finish();
        dabs.insert(dabs.end(), d.begin(), d.end());
      }
    });
    const std::string px = std::to_string(int(size)) + "px";
    pb::bench::report(("dab_place_" + px).c_str(), 10 * double(dabs.size()) / place, "dabs/s");
    // Masks made afresh: at most 2,000 of the dabs, spread over the stroke.
    std::vector<Dab> some;
    for (size_t i = 0; i < dabs.size(); i += std::max<size_t>(1, dabs.size() / 2000)) some.push_back(dabs[i]);
    for (const float hard : {1.f, 0.f}) {
      const double fresh = median_s([&] {
        for (const Dab& d : some) make_dab_mask(d, hard);  // in another translation unit: not elided
      });
      const std::string name = "dab_mask_" + px + (hard == 1 ? "_hard" : "_soft");
      pb::bench::report(name.c_str(), double(some.size()) / fresh, "dabs/s");
    }
    for (Dab& d : some) d.size = size;  // a mouse: one size, 16 masks in all
    const double cached = median_s([&] {
      for (const Dab& d : some) dab_mask(d, p.hardness);
    });
    pb::bench::report(("dab_mask_" + px + "_cached").c_str(), double(some.size()) / cached, "dabs/s");
  }
}
