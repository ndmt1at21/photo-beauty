// Stroke (stroke.h): the buffer against the same dabs stamped afresh one by one, bit for bit; pen-up's pixels against
// layer + stroke in doubles (U8, U16, RGB, grey, painting, erasing), only canvas pixels changed, untouched tiles kept;
// the overlay's composite against the merged layer's; the opacity cap; erasing to empty tiles; one hash with 1 and 12
// threads and any batches; published copies later batches leave alone, the tail; undo's old tile ids; bad layers; the
// time a 300 px stroke with pressure on size takes.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <random>
#include <stdexcept>
#include <thread>
#include <vector>

#include "check.h"
#include "dab.h"
#include "render.h"
#include "stroke.h"

using namespace pb;

namespace {

constexpr int S = kTileSize;
using Clock = std::chrono::steady_clock;

double ms_since(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }
float clamp01(float v) { return v > 0 ? (v < 1 ? v : 1) : 0; }

// Pixels x, y, w, h of g as samples (uint16_t whatever the depth), row by row.
std::vector<uint16_t> read(const TileGrid& g, int x, int y, int w, int h) {
  const Format f = g.format();
  std::vector<uint8_t> raw(size_t(w) * h * size_t(f.bytes()));
  g.read_rect(x, y, w, h, raw.data(), size_t(w) * size_t(f.bytes()));
  std::vector<uint16_t> v(size_t(w) * h * size_t(f.channels));
  if (f.depth == Depth::U16)
    std::memcpy(v.data(), raw.data(), raw.size());
  else
    std::copy(raw.begin(), raw.end(), v.begin());
  return v;
}

uint64_t fnv(uint64_t h, const std::vector<uint16_t>& v) {
  for (const uint16_t x : v) h = ((h ^ (x & 255)) * 1099511628211ull ^ (x >> 8)) * 1099511628211ull;
  return h;
}

// A layer of format f: random data tiles (alpha 0 and full often), solid tiles and empty ones over the canvas's
// tiles, and a row above it and a column right of it.
std::shared_ptr<const TileGrid> random_layer(std::mt19937& rng, Format f, int w, int h) {
  auto g = std::make_shared<TileGrid>(f);
  const unsigned m = unsigned(max_value(f.depth));
  const auto sample = [&] {
    const unsigned r = rng() % 8;
    return uint16_t(r == 0 ? 0 : r == 1 ? m : rng() % (m + 1));
  };
  for (int ty = -1; ty <= (h - 1) / S; ty++)
    for (int tx = 0; tx <= (w - 1) / S + 1; tx++) {
      const unsigned kind = rng() % 4;
      if (kind == 0) continue;  // empty
      if (kind == 1) {
        Pixel v{};
        for (int c = 0; c < f.channels; c++) v[size_t(c)] = sample();
        g->set(tx, ty, Tile::solid(f, v));
        continue;
      }
      const WritePin p = g->write(tx, ty);
      for (size_t i = 0; i < size_t(S) * S * size_t(f.channels); i++) {
        if (f.depth == Depth::U16)
          reinterpret_cast<uint16_t*>(p.data())[i] = sample();
        else
          p.data()[i] = uint8_t(sample());
      }
    }
  return g;
}

// A document of a layer below and the layer to paint (its id second).
std::pair<DocPtr, LayerId> make_doc(std::mt19937& rng, Mode mode, Depth depth, int w, int h, bool empty = false) {
  DocPtr d = new_document(w, h, mode, depth, nullptr);
  const Format f = pixel_format(*d);
  d = run(*d, AddLayer{0, 0, Kind::Pixel, {}, random_layer(rng, f, w, h)});
  d = run(*d, AddLayer{0, 1, Kind::Pixel, {}, empty ? nullptr : random_layer(rng, f, w, h)});
  return {d, d->selected[0]};
}

// A wandering stroke with pressure, past the canvas's edges at times.
std::vector<PenEvent> scribble(std::mt19937& rng, int n, int w, int h, double step) {
  std::vector<PenEvent> ev;
  double x = rng() % unsigned(w), y = rng() % unsigned(h), vx = 0, vy = 0, p = 0.6;
  for (int i = 0; i < n; i++) {
    vx = vx * 0.8 + step * (int(rng() % 41) - 20) / 20, vy = vy * 0.8 + step * (int(rng() % 41) - 20) / 20;
    x = std::clamp(x + vx, -60., w + 60.), y = std::clamp(y + vy, -60., h + 60.);
    p = std::clamp(p + (int(rng() % 21) - 10) / 100., 0.05, 1.);
    ev.push_back({i / 200., float(x), float(y), float(p)});
  }
  return ev;
}

BrushParams random_brush(std::mt19937& rng, bool erase) {
  BrushParams b;
  const float sizes[] = {2, 7, 25, 90, 300}, spacings[] = {0.05f, 0.25f, 1};
  b.size = sizes[rng() % 5];
  b.hardness = float(rng() % 3) / 2;
  b.opacity = rng() % 3 ? 0.2f + 0.8f * float(rng() % 101) / 100 : 1;
  b.flow = rng() % 3 ? 0.1f + 0.9f * float(rng() % 101) / 100 : 1;
  b.spacing = spacings[rng() % 3];
  b.pressure_size = rng() % 2, b.pressure_opacity = rng() % 2, b.pressure_flow = rng() % 2;
  for (float& c : b.color) c = float(rng() % 1001) / 1000;
  b.erase = erase;
  return b;
}

std::vector<Dab> all_dabs(const BrushParams& b, const std::vector<PenEvent>& ev) {
  DabPlacer placer(b);
  std::vector<Dab> out;
  for (const PenEvent& e : ev) {
    const std::vector<Dab> d = placer.add(e);
    out.insert(out.end(), d.begin(), d.end());
  }
  const std::vector<Dab> d = placer.finish();
  out.insert(out.end(), d.begin(), d.end());
  return out;
}

// The stroke's coverage of the w x h canvas, its dabs stamped one by one with masks made afresh: B = B + d x flow x
// (cap - B), cap the dab's opacity over the stroke's; flow, cap and d x flow rounded to 1/65535, the step rounded up.
std::vector<uint16_t> ref_cover(const BrushParams& b, const std::vector<PenEvent>& ev, int w, int h) {
  std::vector<uint16_t> cov(size_t(w) * h);
  const float o = clamp01(b.opacity);
  for (const Dab& d : all_dabs(b, ev)) {
    const DabMask m = make_dab_mask(d, b.hardness);
    if (!m.px) continue;
    const double flow = std::lround(double(clamp01(d.flow)) * 65535);
    const double cap = o > 0 ? std::lround(double(clamp01(d.opacity / o)) * 65535) : 65535;
    for (int y = std::max(m.y, 0); y < std::min(m.y + m.h, h); y++)
      for (int x = std::max(m.x, 0); x < std::min(m.x + m.w, w); x++) {
        uint16_t& v = cov[size_t(y) * w + size_t(x)];
        if (v >= cap) continue;
        const double a = std::floor((*m.px)[size_t(y - m.y) * m.w + size_t(x - m.x)] * flow / 65535 + 0.5);
        v = uint16_t(v + std::ceil(a * (cap - v) / 65535));
      }
  }
  return cov;
}

// Runs a stroke: all events at once (batch 0), one at a time waiting for each (1), or in random batches (2).
std::shared_ptr<const StrokeState> paint(const DocPtr& d, LayerId l, const BrushParams& b,
                                         const std::vector<PenEvent>& ev, int batches, unsigned seed = 1) {
  Stroke s(d, l, b);
  std::mt19937 rng(seed);
  for (size_t i = 0; i < ev.size(); i++) {
    s.add(ev[i]);
    if (batches == 1 || (batches == 2 && rng() % 7 == 0)) CHECK(s.wait()->events == i + 1);
  }
  s.finish();
  const auto st = s.wait();
  CHECK(st->events == ev.size() && st->pixels && st->buffer && st->overlay.buffer == st->buffer);
  return st;
}

// Premultiplied samples of two composites agree to tol (in 16 bits); null tiles are transparent.
bool composites_agree(const TilePtr& a, const TilePtr& b, int tol) {
  TileGrid ga({Depth::U16, 4}), gb({Depth::U16, 4});
  ga.set(0, 0, a), gb.set(0, 0, b);
  const std::vector<uint16_t> pa = read(ga, 0, 0, S, S), pb = read(gb, 0, 0, S, S);
  for (size_t i = 0; i < pa.size(); i += 4) {
    if (std::abs(pa[i + 3] - pb[i + 3]) > tol) return false;
    for (int c = 0; c < 3; c++)
      if (std::abs(double(pa[i + c]) * pa[i + 3] - double(pb[i + c]) * pb[i + 3]) / 65535 > tol + 1) return false;
  }
  return true;
}

// Random strokes, painting and erasing, on random layers in each mode and depth, on a canvas of partial edge tiles:
// the buffer is the reference's to the bit, and nothing past the canvas; pen-up gives layer + stroke as render.h's
// StrokeOverlay has it to 1 level, the pixels past the canvas and the tiles the stroke left alone as they were, every
// tile compacted; the overlay's composite matches the merged layer's; the old state keeps its tiles (undo).
void check_reference() {
  std::mt19937 rng(41);
  const int w = 700, h = 500;
  for (int n = 0; n < 16; n++) {
    const Mode mode = n % 2 ? Mode::Gray : Mode::Rgb;
    const Depth depth = n % 4 < 2 ? Depth::U8 : Depth::U16;
    const BrushParams b = random_brush(rng, n % 8 >= 4);
    const auto [d, id] = make_doc(rng, mode, depth, w, h);
    const std::vector<PenEvent> ev = scribble(rng, 20 + int(rng() % 100), w, h, b.size / 4 + 2);
    const TileGrid& before = *find_layer(*d, id)->pixels;
    std::vector<std::pair<TileXY, uint64_t>> ids;  // the layer's tiles before the stroke
    for (const auto& [xy, t] : before.tiles()) ids.push_back({xy, t->id()});
    const int fx = -S, fy = -S, fw = w + 3 * S, fh = h + 2 * S;  // every tile the layer has, and more
    const std::vector<uint16_t> old_px = read(before, fx, fy, fw, fh);

    const auto st = paint(d, id, b, ev, int(rng() % 3), rng());
    const std::vector<uint16_t> cov = ref_cover(b, ev, w, h);
    CHECK(read(*st->buffer, 0, 0, w, h) == cov);
    for (const auto& [xy, t] : st->buffer->tiles()) CHECK(xy.x >= 0 && xy.y >= 0 && xy.x * S < w && xy.y * S < h);

    const TileGrid& after = *st->pixels;
    CHECK(after.format() == before.format() && after.fill() == before.fill());
    const std::vector<uint16_t> px = read(after, fx, fy, fw, fh);
    const Format f = before.format();
    const int ch = f.channels;
    const double m = max_value(depth), o = clamp01(b.opacity);
    for (int y = 0; y < fh; y++)
      for (int x = 0; x < fw; x++) {
        const size_t at = (size_t(y) * fw + size_t(x)) * size_t(ch);
        const int cx = x + fx, cy = y + fy;
        const bool in = cx >= 0 && cy >= 0 && cx < w && cy < h;
        const double k = in ? o * cov[size_t(cy) * w + size_t(cx)] / 65535 : 0;
        if (k == 0) {  // as it was, but for the colour of a transparent pixel in a tile that went empty
          if (!std::equal(px.begin() + at, px.begin() + at + ch, old_px.begin() + at))
            CHECK(px[at + ch - 1] == 0 && old_px[at + ch - 1] == 0);
          continue;
        }
        const double a = old_px[at + ch - 1] / m;
        const double ao = b.erase ? a * (1 - k) : k + a * (1 - k);
        CHECK(std::abs(px[at + ch - 1] - ao * m) <= 1);
        if (px[at + ch - 1] == 0) continue;  // colours of transparent pixels are any
        for (int c = 0; c < ch - 1; c++) {
          const double was = old_px[at + c] / m, want = b.erase ? was : (k * clamp01(b.color[size_t(c)]) + a * (1 - k) * was) / ao;
          CHECK(std::abs(px[at + c] - want * m) <= 1);
        }
      }
    // Tiles the stroke covers nowhere are the layer's own, whatever else lies in their reach; the new ones compact.
    for (const auto& [xy, t] : before.tiles()) {
      bool touched = false;
      for (int y = std::max(xy.y * S, 0); y < std::min((xy.y + 1) * S, h) && !touched; y++)
        for (int x = std::max(xy.x * S, 0); x < std::min((xy.x + 1) * S, w) && !touched; x++)
          touched = cov[size_t(y) * w + size_t(x)] != 0;
      if (!touched || !(o > 0)) CHECK(after.get(xy.x, xy.y) == t.get());
    }
    for (const auto& [xy, t] : after.tiles())
      if (t.get() != before.get(xy.x, xy.y)) CHECK(compact(t, after.fill()) == t);

    // The merged state, the overlay on the old: the composites agree to a level of 8 bits (premultiplied).
    const DocPtr merged = run(*d, SetPixels{id, st->pixels});
    for (int ty = 0; ty * S < h; ty++)
      for (int tx = 0; tx * S < w; tx++)
        CHECK(composites_agree(composite_tile({d, st->overlay}, 0, tx, ty), composite_tile({merged, {}}, 0, tx, ty),
                               depth == Depth::U8 ? 257 : 2));
    // Undo: the old state, its tiles and their pixels as they were.
    const TileGrid& kept = *find_layer(*d, id)->pixels;
    CHECK(kept.tiles().size() == ids.size());
    for (const auto& [xy, i] : ids) CHECK(kept.get(xy.x, xy.y)->id() == i);
    CHECK(read(kept, fx, fy, fw, fh) == old_px);
  }
}

// The formula's meaning: a hard dab at full flow is its mask; at flow f each dab adds f of what is left, rounded; a
// stroke never covers past its opacity, nor past the pressure's where the pen sets it, and reaches it.
void check_cover() {
  std::mt19937 rng(5);
  const auto [d, id] = make_doc(rng, Mode::Rgb, Depth::U8, 300, 300, true);
  BrushParams b;
  b.size = 41, b.hardness = 1;
  const DabMask m = make_dab_mask({100, 100, 41}, 1);
  CHECK(read(*paint(d, id, b, {{0, 100, 100, 1}}, 0)->buffer, m.x, m.y, m.w, m.h) == *m.px);
  // n hard dabs 3 px apart, each covering the middle fully, at flow 1/2: 1 - 1/2^n there.
  b.size = 300, b.flow = 0.5f, b.spacing = 0.01f;
  for (int n = 1; n <= 3; n++) {
    std::vector<PenEvent> ev;
    for (int i = 0; i < n; i++) ev.push_back({0, 100 + 3.5f * float(i), 100, 1});
    CHECK(all_dabs(b, ev).size() == size_t(n));
    const uint16_t mid = read(*paint(d, id, b, ev, 0)->buffer, 103, 100, 1, 1)[0];
    CHECK(std::abs(mid - (1 - std::pow(0.5, n)) * 65535) <= n);
  }
  // Back and forth over one patch, some 50 dabs deep: the cap is reached, never passed.
  std::vector<PenEvent> ev;
  for (int i = 0; i < 400; i++) ev.push_back({i / 200., float(100 + (i % 20) * 3), float(100 + (i / 20) * 2), 0.5f});
  b.size = 40, b.hardness = 0.5f, b.opacity = 0.6f, b.flow = 0.25f, b.spacing = 0.1f, b.color = {1, 0, 0};
  for (const bool pressure : {false, true}) {
    b.pressure_opacity = pressure, b.pressure_size = false;
    const auto st = paint(d, id, b, ev, 0);
    const std::vector<uint16_t> c = read(*st->buffer, 0, 0, 300, 300), px = read(*st->pixels, 0, 0, 300, 300);
    const long cap = std::lround((pressure ? 0.5 : 1) * 65535);  // the pressure's share of the stroke's opacity
    CHECK(*std::max_element(c.begin(), c.end()) == cap);
    uint16_t top = 0;
    for (size_t i = 3; i < px.size(); i += 4) top = std::max(top, px[i]);
    CHECK(top == std::lround(0.6 * (pressure ? 0.5 : 1) * 255));
  }
}

// An eraser over all of a canvas of whole tiles at full opacity empties them; past the canvas the layer keeps its
// tile. At half opacity alpha halves.
void check_erase() {
  std::mt19937 rng(9);
  const Format f{Depth::U16, 4};
  DocPtr d = new_document(2 * S, 2 * S, Mode::Rgb, Depth::U16, nullptr);
  auto g = std::make_shared<TileGrid>(f);
  for (int ty = 0; ty < 2; ty++)
    for (int tx = 0; tx < 3; tx++) {
      const WritePin p = g->write(tx, ty);
      auto* v = reinterpret_cast<uint16_t*>(p.data());
      for (size_t i = 0; i < size_t(S) * S * 4; i++) v[i] = i % 4 == 3 ? 65535 : uint16_t(rng());
    }
  d = run(*d, AddLayer{0, 0, Kind::Pixel, {}, g});
  const LayerId id = d->selected[0];
  BrushParams b;
  b.size = 1500, b.erase = true;
  const auto st = paint(d, id, b, {{0, 256, 256, 1}}, 0);
  for (int ty = 0; ty < 2; ty++)
    for (int tx = 0; tx < 2; tx++) CHECK(!st->pixels->get(tx, ty));
  CHECK(st->pixels->get(2, 0) == g->get(2, 0) && st->pixels->get(2, 1) == g->get(2, 1));
  b.opacity = 0.5f;
  const auto half = paint(d, id, b, {{0, 256, 256, 1}}, 0);
  const std::vector<uint16_t> px = read(*half->pixels, 0, 0, 2 * S, 2 * S), was = read(*g, 0, 0, 2 * S, 2 * S);
  for (size_t i = 0; i < px.size(); i++) CHECK(px[i] == (i % 4 == 3 ? 32768 : was[i]));
  const auto none = paint(d, id, b, {}, 0);  // pen down and up: the layer's own pixels
  CHECK(none->buffer->tiles().empty() && none->pixels == find_layer(*d, id)->pixels);
  // A dab whose square reaches tile (0, 0) but not its disc (113 px from the corner, 100 px across): the buffer has the
  // tile, all 0, and the layer keeps its own there.
  BrushParams p;
  p.size = 200;
  const auto corner = paint(d, id, p, {{0, 336, 336, 1}}, 0);
  CHECK(corner->buffer->get(0, 0) && corner->pixels->get(0, 0) == g->get(0, 0));
  CHECK(corner->pixels->get(1, 0) != g->get(1, 0) && corner->pixels->get(1, 1) != g->get(1, 1));
}

// The same stroke with the pool's threads and with one, all at once, an event at a time and in random batches: the same
// buffer and pixels, whose hash is fixed (every platform alike).
void check_same() {
  std::mt19937 rng(77);
  const auto [d, id] = make_doc(rng, Mode::Rgb, Depth::U16, 700, 500);
  BrushParams b;
  b.size = 25, b.hardness = 0.3f, b.opacity = 0.8f, b.flow = 0.4f, b.pressure_flow = true, b.color = {0.2f, 0.5f, 0.9f};
  const std::vector<PenEvent> ev = scribble(rng, 300, 700, 500, 6);
  uint64_t first = 0;
  for (int run = 0; run < 3; run++) {
    set_stroke_threads(run == 1 ? 1 : 0);
    const auto st = paint(d, id, b, ev, run, 3);
    const uint64_t h = fnv(fnv(14695981039346656037ull, read(*st->buffer, 0, 0, 700, 500)),
                           read(*st->pixels, -S, -S, 700 + 3 * S, 500 + 2 * S));
    if (run == 0) first = h;
    CHECK(h == first);
  }
  set_stroke_threads(0);
  constexpr uint64_t kGolden = 0x7ed9d43f98d61d27;
  if (first != kGolden) std::fprintf(stderr, "test_stroke: hash %016llx\n", (unsigned long long)first);
  CHECK(first == kGolden);
}

// Published states: one per batch and on_publish after each; a copy later batches leave alone, tiles they did not
// change keeping their ids; the tail shows the stroke up to the newest event, which the buffer reaches one event
// later; at pen-up the buffer is where the last tail ended.
void check_publish() {
  std::mt19937 rng(3);
  const auto [d, id] = make_doc(rng, Mode::Rgb, Depth::U8, 1000, 300, true);
  BrushParams b;
  b.size = 12, b.spacing = 0.25f;
  std::atomic<int> published{0};
  Stroke s(d, id, b, [&] { published++; });
  const auto at = [](const std::shared_ptr<const TileGrid>& g, int x, int y) { return read(*g, x, y, 1, 1)[0]; };
  const auto s0 = s.state();
  CHECK(s0->events == 0 && s0->buffer->tiles().empty() && s0->overlay.buffer == s0->buffer && !s0->pixels);
  CHECK(s0->overlay.layer == id && s0->overlay.opacity == 1 && !s0->overlay.erase);
  const float xs[] = {100, 160, 220, 600, 660, 720};
  const size_t n = std::size(xs);
  std::vector<std::shared_ptr<const StrokeState>> states;
  std::vector<uint64_t> hashes;
  for (size_t i = 0; i < n; i++) {
    s.add({double(i) / 200, xs[i], 100, 1});
    states.push_back(s.wait());
    const auto& st = states.back();
    hashes.push_back(fnv(0, read(*st->buffer, 0, 0, 1000, 300)));
    CHECK(st->events == i + 1 && !st->pixels);
    CHECK(at(st->overlay.buffer, int(xs[i]), 100) > 0);  // the tail reaches the newest event, the buffer the one before
    if (i > 0) CHECK(at(st->buffer, int(xs[i]), 100) == 0 && at(st->buffer, int(xs[i - 1]), 100) > 0);
    if (i == 0) CHECK(st->overlay.buffer == st->buffer);  // one event: no tail yet
    if (i == 1) CHECK(st->buffer == states[0]->buffer);   // nor dabs for sure
  }
  // The dabs from 600 to 660 change tile (2, 0) alone: the others keep their ids.
  CHECK(states[5]->buffer->get(0, 0)->id() == states[4]->buffer->get(0, 0)->id());
  CHECK(states[5]->buffer->get(1, 0)->id() == states[4]->buffer->get(1, 0)->id());
  CHECK(states[5]->buffer->get(2, 0)->id() != states[4]->buffer->get(2, 0)->id());
  s.finish();
  const auto end = s.wait();
  CHECK(end->events == n && end->pixels);
  CHECK(read(*end->buffer, 0, 0, 1000, 300) == read(*states[n - 1]->overlay.buffer, 0, 0, 1000, 300));
  for (size_t i = 0; i < n; i++) CHECK(fnv(0, read(*states[i]->buffer, 0, 0, 1000, 300)) == hashes[i]);
  for (int i = 0; i < 5000 && published < int(n) + 1; i++) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  CHECK(published == int(n) + 1);
  s.add({1, 900, 200, 1});  // after finish(): nothing
  CHECK(s.wait() == end);
}

// A layer not there or not of pixels is refused; a stroke dropped while busy returns at once and the pool goes on.
void check_lifetime() {
  std::mt19937 rng(8);
  const auto [d, id] = make_doc(rng, Mode::Gray, Depth::U16, 2000, 2000);
  BrushParams b;
  int refused = 0;
  for (const LayerId bad : {LayerId(0), LayerId(77)}) try {
      Stroke s(d, bad, b);
    } catch (const std::invalid_argument&) {
      refused++;
    }
  try {
    Stroke s(nullptr, id, b);
  } catch (const std::invalid_argument&) {
    refused++;
  }
  CHECK(refused == 3);
  b.size = 300;
  const auto t0 = Clock::now();
  {
    Stroke s(d, id, b, [] {});
    for (const PenEvent& e : scribble(rng, 2000, 2000, 2000, 20)) s.add(e);
  }
  std::printf("test_stroke: a stroke of 2000 events dropped in %.2f ms\n", ms_since(t0));
  CHECK(paint(d, id, b, {{0, 10, 10, 1}}, 0)->pixels);
  // The jobs end soon after and let go of the document: none outlives the test (static objects they use, as dab
  // masks' cache, go before the pool's threads stop at exit).
  for (int i = 0; i < 10000 && d.use_count() > 1; i++) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  CHECK(d.use_count() == 1);
}

// A 300 px stroke with pressure on size at 200 Hz, events about 10 px apart (nearly every dab a new mask): the time
// from add() to its state, each event waited for, and pen-up's merge; then another such stroke at once, per dab.
void check_time() {
  std::mt19937 rng(12);
  DocPtr d = new_document(4000, 3000, Mode::Rgb, Depth::U16, nullptr);
  auto g = std::make_shared<TileGrid>(Format{Depth::U16, 4});
  for (int ty = 0; ty * S < 3000; ty++)
    for (int tx = 0; tx * S < 4000; tx++) g->set(tx, ty, Tile::solid(g->format(), {40000, 30000, 20000, 65535}));
  d = run(*d, AddLayer{0, 0, Kind::Pixel, {}, g});
  const LayerId id = d->selected[0];
  BrushParams b;
  b.size = 300, b.hardness = 0.8f, b.color = {0.9f, 0.1f, 0.1f};
  const auto events = [](double phase) {  // other phases: other sizes and places, masks not cached
    std::vector<PenEvent> ev;
    for (int i = 0; i < 400; i++) {
      const double t = i / 200.;
      ev.push_back({t, float(2000 + 1500 * std::sin(1.5 * t + phase)), float(1500 + 1100 * std::sin(2.1 * t + 1)),
                    float(0.6 + 0.4 * std::sin(2.3 * t + phase))});
    }
    return ev;
  };
  const std::vector<PenEvent> ev = events(0), other = events(0.5);
  std::vector<double> lag;
  Stroke s(d, id, b);
  for (const PenEvent& e : ev) {
    const auto t0 = Clock::now();
    s.add(e);
    s.wait();
    lag.push_back(ms_since(t0));
  }
  auto t0 = Clock::now();
  s.finish();
  const auto st = s.wait();
  const double merge = ms_since(t0);
  std::sort(lag.begin(), lag.end());
  Stroke all(d, id, b);
  t0 = Clock::now();
  for (const PenEvent& e : other) all.add(e);
  all.wait();
  const double at_once = ms_since(t0);
  std::printf("test_stroke: 300 px, pressure on size: %zu dabs over %zu events; add() to state p50 %.3f ms, p95 %.3f ms, "
              "max %.3f ms; pen-up merge of %zu tiles %.1f ms; all at once %.3f ms a dab\n",
              all_dabs(b, ev).size(), ev.size(), lag[lag.size() / 2], lag[lag.size() * 95 / 100], lag.back(),
              st->buffer->tiles().size(), merge, at_once / double(all_dabs(b, other).size()));
}

}  // namespace

TEST(test_stroke) {
  check_reference();
  check_cover();
  check_erase();
  check_same();
  check_publish();
  check_time();
  check_lifetime();  // last: the dropped stroke's job may run on a while
}
