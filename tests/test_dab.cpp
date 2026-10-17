// Dabs (dab.h): spacing along the curve, pressure, the curve through the events, masks' coverage and symmetry, the
// same dabs and masks whatever the batches, threads and order (and the platform: a fixed hash), bad input.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <thread>
#include <vector>

#include "check.h"
#include "dab.h"

using namespace pb;

namespace {

constexpr double kPi = 3.14159265358979323846;

std::vector<Dab> dabs(const BrushParams& p, const std::vector<PenEvent>& events) {
  DabPlacer placer(p);
  std::vector<Dab> out;
  for (const PenEvent& e : events) {
    const std::vector<Dab> d = placer.add(e);
    out.insert(out.end(), d.begin(), d.end());
  }
  const std::vector<Dab> d = placer.finish();
  out.insert(out.end(), d.begin(), d.end());
  return out;
}

double dist(const Dab& a, const Dab& b) { return std::hypot(double(b.x) - a.x, double(b.y) - a.y); }

bool same(const Dab& a, const Dab& b) {
  return a.x == b.x && a.y == b.y && a.size == b.size && a.opacity == b.opacity && a.flow == b.flow;
}
bool same(const std::vector<Dab>& a, const std::vector<Dab>& b) {
  return a.size() == b.size() &&
         std::equal(a.begin(), a.end(), b.begin(), [](const Dab& p, const Dab& q) { return same(p, q); });
}
bool same(const DabMask& a, const DabMask& b) {
  return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h && (a.px == nullptr) == (b.px == nullptr) &&
         (!a.px || *a.px == *b.px);
}

uint16_t at(const DabMask& m, int i, int j) {  // 0 outside
  return i < m.x || j < m.y || i >= m.x + m.w || j >= m.y + m.h ? 0 : (*m.px)[size_t(j - m.y) * m.w + size_t(i - m.x)];
}
double total(const DabMask& m) {
  double s = 0;
  if (m.px)
    for (uint16_t v : *m.px) s += v;
  return s / 65535;
}

// A wandering stroke with pressure, from integers alone so that it is the same on every platform: 1/64 px, 1/1024.
// Now and then the pen presses harder in place.
std::vector<PenEvent> scribble(unsigned seed, int n) {
  std::mt19937 rng(seed);  // the standard fixes its numbers (not a distribution's)
  std::vector<PenEvent> ev;
  int x = 32000, y = 25000, vx = 0, vy = 0, p = 512;
  for (int i = 0; i < n; i++) {
    vx = vx * 7 / 8 + int(rng() % 1025) - 512, vy = vy * 7 / 8 + int(rng() % 1025) - 512;
    x += vx, y += vy, p = std::clamp(p + int(rng() % 129) - 64, 16, 1024);
    ev.push_back({i / 200., x / 64.f, y / 64.f, p / 1024.f});
    if (rng() % 8 == 0) ev.push_back({i / 200. + 1e-3, x / 64.f, y / 64.f, std::min(p + 100, 1024) / 1024.f});
  }
  return ev;
}

// On a straight line, events at uneven gaps (from 0.3 to 40 dabs apart): the first dab at the first event, then dabs
// spacing x size apart to 1% (the size the pressure gives), on the line, up to the end.
void check_spacing() {
  std::mt19937 rng(7);
  for (float size : {0.5f, 1.f, 5.f, 30.f, 300.f})
    for (float spacing : {0.01f, 0.25f, 1.f, 2.5f}) {
      BrushParams p;
      p.size = size, p.spacing = spacing;
      const float pressure = rng() % 2 ? 1 : 0.5f, dab = size * pressure;  // a mouse, or a pen held at half
      const double step = double(spacing) * std::max(dab, 1.f);            // a dab under 1 px steps as 1 px
      std::vector<PenEvent> ev;
      double s = 0;
      for (int i = 0; i < 40; i++) {
        ev.push_back({i * 0.005, float(10.3 + 0.6 * s), float(-20.7 + 0.8 * s), pressure});
        s += step * (0.3 + (rng() % 3971) / 100.);
      }
      const double length = std::hypot(double(ev.back().x) - ev[0].x, double(ev.back().y) - ev[0].y);
      const std::vector<Dab> d = dabs(p, ev);
      CHECK(d.size() >= 2);
      CHECK(d[0].x == ev[0].x && d[0].y == ev[0].y && d[0].size == dab && d[0].opacity == 1 && d[0].flow == 1);
      const double tol = 1e-3 + 1e-6 * length;  // floats far out
      for (size_t i = 0; i < d.size(); i++) {
        const double along = 0.6 * (double(d[i].x) - ev[0].x) + 0.8 * (double(d[i].y) - ev[0].y);
        const double off = -0.8 * (double(d[i].x) - ev[0].x) + 0.6 * (double(d[i].y) - ev[0].y);
        CHECK(std::abs(off) < tol && along > -tol && along < length + tol);
        CHECK(d[i].size == dab);
        if (i > 0) CHECK(std::abs(dist(d[i - 1], d[i]) - step) <= 0.01 * step);
      }
      const double last = 0.6 * (double(d.back().x) - ev[0].x) + 0.8 * (double(d.back().y) - ev[0].y);
      CHECK(length - last < 1.01 * step);  // no dab missing at the end
    }
}

// Pressure scales size, opacity and flow as BrushParams says, interpolated along the curve: rising pressure, rising
// dabs; one long span between two events ramps smoothly rather than stepping.
void check_pressure() {
  std::vector<PenEvent> ev;  // a quarter circle of radius 400, pressure 0.05 to 1, now and then pressed in place
  for (int i = 0; i <= 60; i++) {
    const double a = kPi / 2 * i / 60;
    const float pr = float(0.05 + 0.95 * i / 60);
    ev.push_back({i * 0.005, float(400 * std::cos(a)), float(400 * std::sin(a)), pr});
    if (i % 7 == 3) ev.push_back({i * 0.005 + 1e-3, ev.back().x, ev.back().y, std::min(pr + 0.004f, 1.f)});
  }
  BrushParams p;
  p.size = 300, p.opacity = 0.8f, p.flow = 0.5f, p.spacing = 0.02f;
  p.pressure_size = p.pressure_opacity = p.pressure_flow = true;
  std::vector<Dab> d = dabs(p, ev);
  CHECK(d.size() > 100);
  CHECK(d[0].size == 300 * 0.05f && d[0].opacity == 0.8f * 0.05f && d[0].flow == 0.5f * 0.05f);
  for (size_t i = 1; i < d.size(); i++) {
    CHECK(d[i].size >= d[i - 1].size && d[i].opacity >= d[i - 1].opacity && d[i].flow >= d[i - 1].flow);
    CHECK(d[i].size <= 300 && d[i].opacity <= 0.8f && d[i].flow <= 0.5f);
    CHECK(std::abs(d[i].opacity / 0.8f - d[i].size / 300) < 1e-6f);  // the same pressure for all three
  }
  CHECK(d.back().size > 290);

  std::reverse(ev.begin(), ev.end());  // falling pressure, falling dabs
  for (PenEvent& e : ev) e.pressure = std::max(e.pressure - 0.004f, 0.f);
  d = dabs(p, ev);
  for (size_t i = 1; i < d.size(); i++) CHECK(d[i].size <= d[i - 1].size);

  p.pressure_size = false, p.pressure_flow = false;  // pressure only on opacity
  d = dabs(p, ev);
  for (const Dab& x : d) CHECK(x.size == 300 && x.flow == 0.5f);
  CHECK(d[0].opacity == 0.8f * ev[0].pressure && d.back().opacity < 0.1f);

  // Two events 1,000 px apart, pressure 0.2 to 1: sizes climb dab by dab from 60, through the span.
  p.pressure_size = true;
  p.spacing = 0.05f;
  d = dabs(p, {{0, 0, 0, 0.2f}, {0.1, 1000, 0, 1}});
  CHECK(d.size() > 100 && d[0].size == 300 * 0.2f);
  for (size_t i = 1; i < d.size(); i++) CHECK(d[i].size > d[i - 1].size && d[i].size <= 300);
}

// Through the events on a smooth curve, not chords: events every 30 degrees round a circle of radius 100 (chords cut
// 3.4 px in); the dabs between the end spans (whose tangent is the chord) keep within 0.5 px of it, 1 px apart.
void check_curve() {
  std::vector<PenEvent> ev;
  for (int a = 0; a <= 720; a += 30)
    ev.push_back({a / 3600., float(100 * std::cos(a * kPi / 180)), float(100 * std::sin(a * kPi / 180)), 1});
  BrushParams p;
  p.size = 10, p.spacing = 0.1f;
  const std::vector<Dab> d = dabs(p, ev);
  CHECK(std::abs(double(d.size()) - 4 * kPi * 100) < 5);  // two turns of 1 px steps
  int inner = 0;
  for (size_t i = 0; i < d.size(); i++) {
    if (i > 0) CHECK(std::abs(dist(d[i - 1], d[i]) - 1) < 0.01);
    if (i < 60 || i + 60 > d.size()) continue;  // within the first and last 30 degrees (52 px)
    CHECK(std::abs(std::hypot(double(d[i].x), double(d[i].y)) - 100) < 0.5);
    inner++;
  }
  CHECK(inner > 1000);
}

// A hard dab covers pi r^2 of pixels to 1% at any quarter-pixel centre, from 2 px; softer dabs cover less, the centre
// fully, falling outward; nothing past the rim; the mask's centroid is the dab's centre (to 0.02 px from 4 px: pixel
// sampling's own shift below that); a dab under 1/8 px is empty.
void check_coverage() {
  for (float size : {2.f, 2.25f, 3.f, 4.f, 5.f, 5.25f, 8.25f, 13.75f, 30.f, 100.f, 300.f})
    for (int o = 0; o < 16; o++) {
      const Dab d{7 + (o & 3) / 4.f, -3 + (o >> 2) / 4.f, size};
      const double r = size / 2.;
      CHECK(std::abs(total(make_dab_mask(d, 1)) / (kPi * r * r) - 1) <= 0.01);
    }
  std::mt19937 rng(3);
  for (int n = 0; n < 300; n++) {
    const float size = (rng() % 800 + 1) / 8.f, hard = (rng() % 101) / 100.f;
    const Dab d{(int(rng() % 20001) - 10000) / 7.f, (int(rng() % 20001) - 10000) / 9.f, size};
    const DabMask m = make_dab_mask(d, hard);
    const double qx = std::round(d.x * 4.) / 4, qy = std::round(d.y * 4.) / 4, q = std::round(size * 4.) / 8;
    double s = 0, sx = 0, sy = 0;
    for (int j = m.y; j < m.y + m.h; j++)
      for (int i = m.x; i < m.x + m.w; i++) {
        const double v = at(m, i, j), dx = i + 0.5 - qx, dy = j + 0.5 - qy;
        if (dx * dx + dy * dy >= (q + 0.75) * (q + 0.75)) CHECK(v == 0);
        s += v, sx += v * (i + 0.5), sy += v * (j + 0.5);
      }
    CHECK(s > 0);
    if (size >= 4) CHECK(std::abs(sx / s - qx) < 0.02 && std::abs(sy / s - qy) < 0.02);  // smaller: pixels' own
  }
  for (float size : {6.f, 30.f, 301.f}) {
    double last = 0;
    for (float hard : {0.f, 0.25f, 0.5f, 0.9f, 1.f}) {
      const DabMask m = make_dab_mask({0.5f, 0.5f, size}, hard);  // centred on pixel (0, 0)
      const double t = total(m);
      CHECK(t > last);
      last = t;
      CHECK(at(m, 0, 0) == 65535);
      for (int i = 1; i <= m.x + m.w; i++) CHECK(at(m, i, 0) <= at(m, i - 1, 0));
    }
  }
  const DabMask none = make_dab_mask({3, 3, 0.12f}, 1);
  CHECK(none.w == 0 && none.h == 0 && !none.px);
  CHECK(make_dab_mask({3, 3, 0.13f}, 1).px);
}

// A dab's mirror images across x = 0, y = 0 and the diagonal have the mirrored mask, value for value.
void check_symmetry() {
  std::mt19937 rng(5);
  for (int n = 0; n < 200; n++) {
    const float x = (int(rng() % 8001) - 4000) / 32.f, y = (int(rng() % 8001) - 4000) / 32.f;
    const float size = (rng() % 600 + 1) / 8.f, hard = n % 4 == 0 ? 1 : n % 4 == 1 ? 0 : (rng() % 100) / 100.f;
    const DabMask a = make_dab_mask({x, y, size}, hard), mx = make_dab_mask({-x, y, size}, hard),
                  my = make_dab_mask({x, -y, size}, hard), t = make_dab_mask({y, x, size}, hard);
    CHECK(mx.x == -a.x - a.w && mx.w == a.w && my.y == -a.y - a.h && my.h == a.h);
    CHECK(t.x == a.y && t.y == a.x && t.w == a.h && t.h == a.w);
    for (int j = a.y; j < a.y + a.h; j++)
      for (int i = a.x; i < a.x + a.w; i++) {
        const uint16_t v = at(a, i, j);
        CHECK(at(mx, -1 - i, j) == v && at(my, i, -1 - j) == v && at(t, j, i) == v);
      }
  }
}

// The same dabs and masks whatever the batches and threads: a placer copied midway goes on alike, the cache gives
// what make_dab_mask() makes from any thread in any order, never the first asker's own; and a fixed hash, so the
// same on every platform. kGolden changes only when the dabs change on purpose (say, the fall-off fitted to Photoshop).
void check_same() {
  const std::vector<PenEvent> ev = scribble(11, 400);
  BrushParams p;
  p.size = 40, p.spacing = 0.1f, p.opacity = 0.9f, p.flow = 0.7f, p.hardness = 0.6f;
  p.pressure_size = p.pressure_opacity = p.pressure_flow = true;
  const std::vector<Dab> d = dabs(p, ev);
  CHECK(d.size() > 2000);
  CHECK(same(d, dabs(p, ev)));

  for (size_t k : {size_t(0), size_t(1), size_t(2), size_t(3), size_t(57), ev.size() - 1}) {
    DabPlacer a(p);
    std::vector<Dab> out;
    for (size_t i = 0; i < k; i++) {
      const std::vector<Dab> x = a.add(ev[i]);
      out.insert(out.end(), x.begin(), x.end());
    }
    DabPlacer b = a, preview = a;
    preview.finish();  // where the stroke would end now: the others go on as if it had not been
    std::vector<Dab> rest_a, rest_b;
    for (size_t i = k; i < ev.size(); i++) {
      std::vector<Dab> x = a.add(ev[i]), y = b.add(ev[i]);
      rest_a.insert(rest_a.end(), x.begin(), x.end()), rest_b.insert(rest_b.end(), y.begin(), y.end());
    }
    std::vector<Dab> x = a.finish(), y = b.finish();
    rest_a.insert(rest_a.end(), x.begin(), x.end()), rest_b.insert(rest_b.end(), y.begin(), y.end());
    CHECK(same(rest_a, rest_b));
    out.insert(out.end(), rest_a.begin(), rest_a.end());
    CHECK(same(out, d));
    CHECK(preview.add(ev.back()).empty() && preview.finish().empty());
  }

  // Masks from 8 threads, each through the dabs from its own start, against masks made afresh.
  std::vector<DabMask> want;
  for (size_t i = 0; i < d.size(); i += 3) want.push_back(make_dab_mask(d[i], p.hardness));
  std::vector<std::thread> threads;
  std::vector<int> bad(8, 0);
  for (int t = 0; t < 8; t++)
    threads.emplace_back([&, t] {
      for (size_t n = 0; n < want.size(); n++) {
        const size_t i = (n + want.size() * t / 8) % want.size();
        bad[t] += !same(dab_mask(d[i * 3], p.hardness), want[i]);
      }
    });
  for (std::thread& t : threads) t.join();
  for (int b : bad) CHECK(b == 0);
  // Two dabs of one key: the second gets the first's mask, which is what it would make itself.
  const Dab a{100.3f, 50.1f, 30.06f}, b{100.37f, 50.04f, 29.94f};
  const DabMask ma = dab_mask(a, 0.333f), mb = dab_mask(b, 0.334f);
  CHECK(ma.px == mb.px && same(mb, make_dab_mask(b, 0.334f)) && same(ma, make_dab_mask(a, 0.333f)));

  uint64_t h = 14695981039346656037ull;  // FNV-1a
  const auto hash = [&](const void* data, size_t n) {
    for (size_t i = 0; i < n; i++) h = (h ^ static_cast<const uint8_t*>(data)[i]) * 1099511628211ull;
  };
  for (const Dab& x : d) {
    const float f[] = {x.x, x.y, x.size, x.opacity, x.flow};
    hash(f, sizeof f);
  }
  for (size_t i = 0; i < d.size(); i += 97)
    for (float hard : {0.f, 0.6f, 1.f}) {
      const DabMask m = dab_mask(d[i], hard);
      hash(&m.x, sizeof m.x), hash(&m.y, sizeof m.y), hash(&m.w, sizeof m.w), hash(&m.h, sizeof m.h);
      if (m.px) hash(m.px->data(), m.px->size() * sizeof(uint16_t));
    }
  constexpr uint64_t kGolden = 0x8356346e6f3f0f38;
  if (h != kGolden) std::fprintf(stderr, "test_dab: hash %016llx\n", (unsigned long long)h);
  CHECK(h == kGolden);
}

// Bad input never hangs nor gives anything not finite.
void check_input() {
  BrushParams p;
  DabPlacer one(p);  // a tap: one dab
  CHECK(one.add({0, 5, 6, 0.5f}).size() == 1 && one.finish().empty() && one.add({0, 9, 9, 1}).empty());
  DabPlacer still(p);  // pressing in place: still one
  size_t n = 0;
  for (int i = 0; i < 100; i++) n += still.add({i * 0.01, 5, 6, i / 100.f}).size();
  CHECK(n == 1 && still.add({1, 25, 6, 1}).empty());
  const std::vector<Dab> moved = still.finish();   // then away: dabs from the pressure it reached
  CHECK(moved.size() >= 3 && moved[0].size > 29);  // 0.99, not the 0 it touched down with
  for (const Dab& x : moved) CHECK(std::isfinite(x.x) && x.y == 6);
  CHECK(dabs(p, {}).empty());
  DabPlacer light(p);  // a pressure not finite: 0
  CHECK(light.add({0, 1, 1, std::nanf("")})[0].size == 0);

  const float nan = std::nanf(""), inf = INFINITY;
  std::vector<PenEvent> ev = {{0, 0, 0, 1}, {0, nan, 3, 1}, {0, 10, inf, 1}, {0, 10, 0, nan}, {0, 20, 0, 1}};
  ev.push_back({0, 1e30f, -1e30f, 2});
  ev.push_back({0, 1e30f, -1e30f + 1, -1});
  std::vector<Dab> d = dabs(p, ev);  // the 1e30 ones clamped to 2^20: a long way of 30 px dabs
  CHECK(d.size() > 1000);
  for (const Dab& x : d)
    CHECK(std::isfinite(x.x) && std::isfinite(x.y) && std::abs(x.x) <= 1 << 20 && std::abs(x.y) <= 1 << 20 &&
          x.size >= 0 && x.size <= 30);

  for (const float v : {0.f, -1.f, nan, inf}) {  // size and spacing out of range or not finite
    BrushParams q;
    q.size = v, q.spacing = v, q.opacity = v, q.flow = v;
    d = dabs(q, {{0, 0, 0, 1}, {0, 20, 0, 1}, {0, 40, 5, 1}});  // 40.3 px: 1% of 1 px at most
    CHECK(!d.empty() && d.size() <= 4100);
    for (const Dab& x : d)
      CHECK(x.size >= 0 && x.size <= 5000 && x.opacity >= 0 && x.opacity <= 1 && x.flow >= 0 && x.flow <= 1);
    const DabMask m = make_dab_mask({0, 0, 10}, v);  // a hardness out of range or not finite
    CHECK(m.px && m.w >= 10 && total(m) > 0);
  }

  // A 1 px brush at 1% across 2 million px: a bounded number of dabs (2^20 a span), not 200 million.
  BrushParams thin;
  thin.size = 1, thin.spacing = 0.01f;
  d = dabs(thin, {{0, -1e6f, 0, 1}, {0, 1e6f, 0, 1}});
  CHECK(d.size() > 1000000 && d.size() <= (1 << 20) + 2);
}

}  // namespace

TEST(test_dab) {
  check_spacing();
  check_pressure();
  check_curve();
  check_coverage();
  check_symmetry();
  check_same();
  check_input();
}
