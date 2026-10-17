// The history (history.h): 1000 random commands among undos, redos, jumps, gestures, snapshots, restores and deleted
// steps against a reference that runs every command again (layers, props, tile ids, names, JSON, serials), and the
// tile bytes against a count over every step afresh, with the oldest steps dropped past a quarter of the budget and no
// more; a slider dragged through 100 values one step; layers chosen joining a step; steps leaving the store with their
// tiles, shared tiles counted once; a real brush stroke undone to the layer's very tiles; the cost of a step on a tree
// shaped as D4.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <set>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "check.h"
#include "history.h"
#include "stroke.h"
#include "tilecache.h"

using namespace pb;

namespace {

constexpr int S = kTileSize;
using Ids = std::vector<LayerId>;

// Everything a state holds but pointers: layers, props, tiles' ids, and the layers chosen unless told not. Equal dumps,
// equal states.
void dump(std::ostream& s, const Layer& l) {
  const LayerProps& p = l.props;
  s << '[' << l.id << ' ' << int(l.kind) << ' ' << p.name << '|' << p.visible << p.lock_transparency << p.lock_pixels
    << p.lock_position << p.lock_all << ' ' << p.opacity << ' ' << p.fill << ' ' << int(p.blend) << ' ' << p.extra;
  if (l.pixels)
    for (const auto& [xy, t] : l.pixels->tiles()) s << ' ' << xy.x << ',' << xy.y << '=' << t->id();
  for (const LayerPtr& c : l.children) dump(s, *c);
  s << ']';
}
std::string dump(const DocState& d, bool selected = true) {
  std::ostringstream s;
  s.precision(9);
  s << d.width << 'x' << d.height << ' ' << int(d.mode) << int(d.depth) << ' ' << d.icc.get() << ' ' << d.next_id
    << " {";
  for (const LayerId id : d.selected) s << ' ' << (selected ? id : 0);
  s << " } ";
  dump(s, *d.root);
  return s.str();
}

size_t raw_size(const Tile& t) { return size_t(S) * S * size_t(t.format().bytes()); }

// The data tiles of each state, once each, as the history weighs them: by id, their raw size.
std::map<uint64_t, size_t> tiles_of(const std::vector<DocPtr>& states) {
  std::map<uint64_t, size_t> out;
  std::unordered_set<const Layer*> seen;  // shared nodes once
  const auto walk = [&](const Layer& l, const auto& self) -> void {
    if (!seen.insert(&l).second) return;
    if (l.pixels)
      for (const auto& [xy, t] : l.pixels->tiles())
        if (!t->is_solid()) out[t->id()] = raw_size(*t);
    for (const LayerPtr& c : l.children) self(*c, self);
  };
  for (const DocPtr& d : states) walk(*d->root, walk);
  return out;
}
// What only states other than current hold, counted afresh.
size_t held_elsewhere(const std::vector<DocPtr>& states, const DocPtr& current) {
  const std::map<uint64_t, size_t> all = tiles_of(states), now = tiles_of({current});
  size_t bytes = 0;
  for (const auto& [id, b] : all) bytes += now.count(id) ? 0 : b;
  return bytes;
}
std::vector<DocPtr> states_of(const Document& doc) {
  std::vector<DocPtr> v;
  for (size_t i = 0; i < doc.steps(); i++) v.push_back(doc.step(i).state);
  return v;
}

// A grid of f with one to three fresh data tiles (never pinned: no pixels to allocate) and maybe a solid one.
std::shared_ptr<const TileGrid> fresh(Format f, std::mt19937& rng) {
  auto g = std::make_shared<TileGrid>(f);
  for (unsigned n = 1 + rng() % 3; n > 0; n--) g->set(int(rng() % 4) - 1, int(rng() % 3) - 1, Tile::make(f));
  if (rng() % 3 == 0) g->set(3, 2, Tile::solid(f, {9, 8, 7, 255}));
  return g;
}

// A random command on d, some refused: layers not there, a parent not a group, an index out of range, Pass Through on
// a pixel layer, pixels of another format. Pixels are fresh tiles, another layer's grid or none.
Command random_command(const DocState& d, std::mt19937& rng) {
  std::vector<const Layer*> all, groups{d.root.get()}, pixel;  // all: but the root
  const auto walk = [&](const Layer& g, const auto& self) -> void {
    for (const LayerPtr& l : g.children) {
      all.push_back(l.get());
      (l->kind == Kind::Group ? groups : pixel).push_back(l.get());
      self(*l, self);
    }
  };
  walk(*d.root, walk);
  const auto from = [&](const std::vector<const Layer*>& v) {
    return rng() % 24 && !v.empty() ? v[rng() % v.size()]->id : LayerId(rng() % (d.next_id + 3));
  };
  const auto pick = [&] { return from(all); };
  const auto group = [&] { return from(groups); };
  const auto index = [&](LayerId g) {
    const Layer* l = find_layer(d, g);
    return int(rng() % ((l ? l->children.size() : 0) + 2)) - int(rng() % 16 == 0);
  };
  const auto some = [&] {
    Ids v(1 + rng() % 3);
    for (LayerId& id : v) id = pick();
    return v;
  };
  const auto props = [&] {
    LayerProps p;
    p.name = "l" + std::to_string(rng() % 100);
    p.opacity = float(rng() % 11) / 10;
    p.blend = rng() % 30 ? Blend(1 + rng() % 27) : Blend::PassThrough;
    return p;
  };
  const auto pixels = [&]() -> std::shared_ptr<const TileGrid> {
    const unsigned k = rng() % 6;
    if (k == 0) return nullptr;
    if (k == 1 && !pixel.empty()) return pixel[rng() % pixel.size()]->pixels;  // another layer's: its tiles shared
    if (k == 2 && rng() % 3 == 0) return std::make_shared<const TileGrid>(Format{d.depth, 1});
    return fresh(pixel_format(d), rng);
  };
  if (all.size() > 40) return DeleteLayers{some()};
  switch (all.size() < 4 ? 0 : rng() % 10) {
    case 0:
    case 1: {
      const LayerId g = group();
      const Kind kind = rng() % 4 ? Kind::Pixel : Kind::Group;
      return AddLayer{g, index(g), kind, props(), kind == Kind::Pixel || rng() % 8 == 0 ? pixels() : nullptr};
    }
    case 2: return DeleteLayers{some()};
    case 3: return DuplicateLayers{some()};
    case 4: {
      const LayerId g = group();
      return MoveLayer{pick(), g, index(g)};
    }
    case 5: return GroupLayers{some(), props()};
    case 6: return rng() % 2 ? Ungroup{group()} : Command(SelectLayers{some()});
    case 7: return SetProps{pick(), props()};
  }
  return SetPixels{from(pixel), pixels()};
}

// The history as plainly as it goes: states run again from the commands, the oldest dropped as Document drops them.
struct Reference {
  struct Step {
    std::string name;
    std::vector<Command> cmds;  // none: the first step, a snapshot's
    std::vector<std::string> json;
    DocPtr state;
    uint64_t serial = 0;  // Document's, as it gave it when it made or changed the step
  };
  std::vector<Step> steps;
  size_t cur = 0;
  int gesture = 0;
  bool merge = false;
  std::vector<std::pair<std::string, DocPtr>> snapshots;

  const DocPtr& state() const { return steps[cur].state; }
  std::string json(size_t i) const {
    const std::vector<std::string>& j = steps[i].json;
    if (j.size() < 2) return j.empty() ? "" : j[0];
    std::string s = "[";
    for (const std::string& x : j) s += (s.size() > 1 ? "," : "") + x;
    return s + "]";
  }
  void add(Step s) {
    steps.resize(cur + 1);
    steps.push_back(std::move(s));
    cur++;
  }
  bool run(const Command& cmd, const std::string& name, DocPtr next) {  // false: layers chosen, joining the step
    Step& s = steps[cur];
    const bool select = std::holds_alternative<SelectLayers>(cmd);
    if (select || merge) {
      if (!s.cmds.empty() || !select) s.cmds.push_back(cmd), s.json.push_back(to_json(cmd));
      s.state = next;
      return !select;
    }
    add({name, {cmd}, {to_json(cmd)}, next});
    merge = gesture > 0;
    return true;
  }
};

// Every step of doc against the reference's.
void check_steps(const Document& doc, const Reference& ref) {
  CHECK(doc.steps() == ref.steps.size() && doc.current() == ref.cur);
  for (size_t i = 0; i < doc.steps(); i++)
    CHECK(doc.step(i).name == ref.steps[i].name && doc.step(i).json == ref.json(i) &&
          doc.step(i).serial == ref.steps[i].serial && dump(*doc.step(i).state) == dump(*ref.steps[i].state));
}

// Random commands (some refused) among undos, redos, jumps, gestures, snapshots, restores and deleted steps, under a
// budget small enough that steps keep going: after each, the current step as the reference has it, a new serial where
// it was made or changed, the tile bytes as counted afresh, steps dropped only while a quarter of the budget was
// passed; every step every so often; at the end the commands since the last step made otherwise, run again from
// there, give the current state (but the layers chosen: those joined steps that later ones had been run on).
void check_random() {
  const size_t old = tile_budget();
  set_tile_budget(size_t(16) << 20);  // a quarter: 4 MB, 16 tiles of 8-bit RGBA
  const size_t cap = tile_budget() / 4;
  std::mt19937 rng(7);
  DocPtr d = new_document(700, 500, Mode::Rgb, Depth::U8, nullptr);
  d = run(*d, AddLayer{0, 0, Kind::Pixel, {}, fresh(pixel_format(*d), rng)});
  Document doc(d, "Open");
  Reference ref;
  ref.steps.push_back({"Open", {}, {}, d, doc.step(0).serial});
  std::set<uint64_t> serials{doc.step(0).serial};
  int ran = 0, refused = 0, undone = 0, redone = 0, jumped = 0, gestures = 0, restored = 0, deleted = 0, dropped = 0;
  size_t most = 0;
  for (int i = 0; i < 1000; i++) {
    const unsigned op = rng() % 100;
    bool added = false;    // a step may have been made or changed: Document may drop the oldest
    bool changed = false;  // the current step made or changed: a new serial
    if (op < 58) {
      const Command cmd = random_command(*ref.state(), rng);
      const std::string name = "step " + std::to_string(i);
      DocPtr next;
      try {
        next = run(*ref.state(), cmd);
      } catch (const std::invalid_argument&) {
      }
      const DocPtr before = doc.state();
      const size_t steps = doc.steps();
      bool threw = false;
      try {
        doc.run(cmd, name);
      } catch (const std::invalid_argument&) {
        threw = true;
      }
      CHECK(threw == !next);
      if (threw) {
        CHECK(doc.state() == before && doc.steps() == steps);
        refused++;
      } else {
        added = changed = ref.run(cmd, name, next);
        ran++;
      }
    } else if (op < 74) {  // undos, then maybe redos
      const auto step = [&](bool back) {
        const bool can = back ? ref.cur > 0 : ref.cur + 1 < ref.steps.size();
        CHECK((back ? doc.undo() : doc.redo()) == can);
        if (can) ref.cur += back ? -1 : 1, ref.merge = false, (back ? undone : redone)++;
      };
      for (unsigned n = op < 68 ? 1 + rng() % 2 : 0; n > 0; n--) step(true);
      for (unsigned n = op >= 68 || rng() % 2 ? 1 + rng() % 2 : 0; n > 0; n--) step(false);
    } else if (op < 79) {  // a few steps either way, or past the last
      const size_t to = std::min(ref.steps.size(), ref.cur + rng() % 4 - std::min<size_t>(ref.cur, rng() % 6));
      bool threw = false;
      try {
        doc.jump(to);
      } catch (const std::out_of_range&) {
        threw = true;
      }
      CHECK(threw == (to == ref.steps.size()));
      if (!threw) ref.cur = to, ref.merge = false, jumped++;
    } else if (op < 89) {  // into a gesture, now and then two deep, or out of one
      if (ref.gesture && (ref.gesture == 2 || rng() % 3)) {
        doc.end_gesture();
        if (!--ref.gesture) ref.merge = false;
      } else {
        doc.begin_gesture();
        if (!ref.gesture++) ref.merge = false, gestures++;
      }
    } else if (op < 93) {
      const std::string name = "snapshot " + std::to_string(i);
      doc.snapshot(name);
      ref.snapshots.push_back({name, ref.state()});
    } else if (op < 98) {  // to the step it was taken on while that is as it was, else a step of its own
      const size_t k = rng() % (ref.snapshots.size() + 1);
      bool threw = false;
      try {
        doc.restore(k);
      } catch (const std::out_of_range&) {
        threw = true;
      }
      CHECK(threw == (k == ref.snapshots.size()));
      if (!threw) {
        const auto at = std::find_if(ref.steps.begin(), ref.steps.end(),
                                     [&](const Reference::Step& s) { return s.state == ref.snapshots[k].second; });
        if (at != ref.steps.end()) {
          ref.cur = size_t(at - ref.steps.begin());
        } else {
          ref.add({ref.snapshots[k].first, {}, {}, ref.snapshots[k].second});
          added = changed = true;
        }
        ref.merge = false;
        restored++;
      }
    } else {
      doc.delete_step();
      if (ref.cur > 0) ref.steps.resize(ref.cur), ref.cur--, deleted++;
      ref.merge = false;
    }
    if (added) {  // the oldest dropped, as few as bring the history within a quarter of the budget
      CHECK(doc.steps() <= ref.steps.size());
      const size_t n = ref.steps.size() - doc.steps();
      const size_t keep = ref.merge ? 1 : 0;  // the gesture's step and the one before it stay
      if (n) {
        CHECK(n <= ref.cur - keep);
        std::vector<DocPtr> kept;
        for (size_t k = n - 1; k < ref.steps.size(); k++) kept.push_back(ref.steps[k].state);
        CHECK(held_elsewhere(kept, ref.state()) > cap);  // one fewer would not do
        ref.steps.erase(ref.steps.begin(), ref.steps.begin() + std::ptrdiff_t(n));
        ref.cur -= n;
        dropped += int(n);
      }
      CHECK(doc.history_bytes() <= cap || ref.cur == keep);
    }
    if (changed) {
      ref.steps[ref.cur].serial = doc.step(ref.cur).serial;
      CHECK(serials.insert(ref.steps[ref.cur].serial).second);
    }
    CHECK(doc.steps() == ref.steps.size() && doc.current() == ref.cur);
    CHECK(doc.step(ref.cur).name == ref.steps[ref.cur].name && doc.step(ref.cur).json == ref.json(ref.cur) &&
          doc.step(ref.cur).serial == ref.steps[ref.cur].serial);
    CHECK(dump(*doc.state()) == dump(*ref.state()));
    CHECK(doc.history_bytes() == held_elsewhere(states_of(doc), doc.state()));
    most = std::max(most, doc.history_bytes());
    if (i % 50 == 0) check_steps(doc, ref);
  }
  check_steps(doc, ref);
  CHECK(doc.snapshots().size() == ref.snapshots.size());
  for (size_t k = 0; k < ref.snapshots.size(); k++)
    CHECK(doc.snapshots()[k].name == ref.snapshots[k].first &&
          dump(*doc.snapshots()[k].state) == dump(*ref.snapshots[k].second));
  // Run again: the commands of the steps since the last one no command made, from its state.
  size_t from = ref.cur;
  while (from > 0 && !ref.steps[from].cmds.empty()) from--;
  DocPtr again = doc.step(from).state;
  for (size_t k = from + 1; k <= ref.cur; k++)
    for (const Command& c : ref.steps[k].cmds) again = run(*again, c);
  CHECK(dump(*again, false) == dump(*doc.state(), false));
  std::printf("test_history: %d commands (%d refused), %d undos, %d redos, %d jumps, %d gestures, %d restores, %d "
              "steps deleted; %d dropped, %zu left, history up to %zu MB of %zu\n",
              ran, refused, undone, redone, jumped, gestures, restored, deleted, dropped, doc.steps(), most >> 20,
              cap >> 20);
  CHECK(ran >= 400 && refused >= 30 && undone >= 50 && redone >= 30 && jumped >= 20 && gestures >= 15 &&
        restored >= 15 && deleted >= 5 && dropped >= 50 && most > cap / 2);
  set_tile_budget(old);
}

// A slider dragged through 100 values is one step, named as the first, its JSON every command in order; undo goes back
// to the state before the drag. A gesture without a command makes no step, a refused command changes nothing, nested
// gestures end with the outer one, and an undo inside one ends its step.
void check_gesture() {
  DocPtr d = new_document(64, 64, Mode::Rgb, Depth::U8, nullptr);
  d = run(*d, AddLayer{0, 0, Kind::Pixel, {}, nullptr});
  const LayerId id = d->selected[0];
  Document doc(d, "New");
  const auto opacity = [&](float o) {
    LayerProps p;
    p.opacity = o;
    return SetProps{id, p};
  };
  doc.begin_gesture();
  doc.end_gesture();
  doc.end_gesture();  // without a begin
  CHECK(doc.steps() == 1 && doc.step(0).name == "New" && doc.step(0).json.empty());
  std::string json = "[";
  doc.begin_gesture();
  for (int i = 0; i < 100; i++) {
    const SetProps c = opacity(float(i) / 99);
    doc.run(c, i ? "later" : "Opacity");
    json += (i ? "," : "") + to_json(c);
    CHECK(doc.steps() == 2 && doc.current() == 1);
    if (i == 50) {
      bool threw = false;
      const DocPtr before = doc.state();
      try {
        doc.run(SetProps{77, {}}, "nothing");
      } catch (const std::invalid_argument&) {
        threw = true;
      }
      CHECK(threw && doc.state() == before && doc.steps() == 2);
    }
  }
  doc.end_gesture();
  CHECK(doc.step(1).name == "Opacity" && doc.step(1).json == json + "]");
  CHECK(find_layer(*doc.state(), id)->props.opacity == 1);
  CHECK(doc.undo() && doc.state() == d && !doc.undo());
  CHECK(doc.redo() && find_layer(*doc.state(), id)->props.opacity == 1 && !doc.redo());
  // Nested: the inner end leaves the step open. One command alone: its JSON as to_json() writes it.
  doc.begin_gesture();
  doc.begin_gesture();
  doc.run(opacity(0.5f), "Opacity");
  doc.end_gesture();
  doc.run(opacity(0.25f), "Opacity");
  doc.end_gesture();
  doc.run(opacity(0.125f), "Opacity");
  CHECK(doc.steps() == 4 && doc.step(2).json == "[" + to_json(opacity(0.5f)) + "," + to_json(opacity(0.25f)) + "]");
  CHECK(doc.step(3).json == to_json(opacity(0.125f)));
  // An undo inside a gesture: the next command makes a step of its own, the redo gone, and later ones join it.
  doc.begin_gesture();
  doc.run(opacity(0.75f), "Opacity");
  CHECK(doc.undo() && doc.current() == 3);
  doc.run(opacity(0.375f), "Opacity");
  doc.run(opacity(0.875f), "Opacity");
  doc.end_gesture();
  CHECK(doc.steps() == 5 && doc.step(4).json == "[" + to_json(opacity(0.375f)) + "," + to_json(opacity(0.875f)) + "]");
  CHECK(doc.undo() && find_layer(*doc.state(), id)->props.opacity == 0.125f);
}

// Layers chosen join the current step, those after it kept and its serial too; a snapshot shows the step it was taken
// on while that has its state still, else comes back as a step; deleting a step takes those after it, not the first.
void check_select() {
  DocPtr d = new_document(64, 64, Mode::Rgb, Depth::U8, nullptr);
  d = run(*d, AddLayer{0, 0, Kind::Pixel, {}, nullptr});
  d = run(*d, AddLayer{0, 1, Kind::Pixel, {}, nullptr});  // layers 1 and 2, 2 chosen
  Document doc(d, "Open");
  const uint64_t open = doc.step(0).serial;
  doc.run(SelectLayers{{1}}, "Select");
  CHECK(doc.steps() == 1 && doc.state()->selected == Ids{1} && doc.step(0).serial == open && doc.step(0).json.empty());
  LayerProps a;
  a.name = "a";
  doc.run(SetProps{1, a}, "Rename");
  const uint64_t renamed = doc.step(1).serial;
  CHECK(doc.steps() == 2 && renamed != open);
  doc.snapshot("renamed");
  doc.run(SetProps{2, a}, "Rename");
  CHECK(doc.undo());
  doc.run(SelectLayers{{2}}, "Select");  // joins step 1, the redo kept
  CHECK(doc.steps() == 3 && doc.current() == 1 && doc.step(1).serial == renamed && doc.state()->selected == Ids{2});
  CHECK(doc.step(1).json == "[" + to_json(SetProps{1, a}) + "," + to_json(SelectLayers{{2}}) + "]");
  CHECK(doc.redo() && doc.current() == 2);
  doc.restore(0);  // step 1 has another state now: a step of its own
  CHECK(doc.steps() == 4 && doc.current() == 3 && doc.step(3).name == "renamed" && doc.step(3).json.empty() &&
        doc.state() == doc.snapshots()[0].state && doc.state()->selected == Ids{1});
  doc.snapshot("as it was");
  doc.run(SetProps{1, LayerProps{}}, "Reset");
  doc.restore(1);  // the step it was taken on, unchanged: there
  CHECK(doc.steps() == 5 && doc.current() == 3);
  doc.jump(2);
  doc.delete_step();
  CHECK(doc.steps() == 2 && doc.current() == 1 && doc.step(1).serial == renamed);
  CHECK(doc.undo() && !doc.undo());
  doc.delete_step();
  CHECK(doc.steps() == 2 && doc.current() == 0);
}

// Under a small budget the oldest steps go once what only they hold passes a quarter of it, and their tiles leave the
// store with them; a tile that three steps hold counts once, a layer duplicated and its copy deleted holds nothing
// back, and a step that frees more than it takes leaves the store's raw pixels fewer.
void check_budget() {
  const size_t old = tile_budget();
  set_tile_budget(size_t(64) << 20);  // a quarter: 16 MB, 64 tiles
  trim_tiles();                       // what other tests left: out of RAM, the raw count ours
  const Format f{Depth::U8, 4};
  const size_t tile = size_t(S) * S * 4;
  std::vector<std::weak_ptr<const Tile>> made;  // every data tile of ours
  const auto painted = [&](int v) {  // 4 x 2 data tiles of v, their pixels in RAM
    auto g = std::make_shared<TileGrid>(f);
    for (int ty = 0; ty < 2; ty++)
      for (int tx = 0; tx < 4; tx++) std::memset(g->write(tx, ty).data(), v, tile);
    for (const auto& [xy, t] : g->tiles()) made.push_back(t);
    return g;
  };
  const auto alive = [&] { return std::count_if(made.begin(), made.end(), [](const auto& w) { return !w.expired(); }); };
  DocPtr d = new_document(1024, 512, Mode::Rgb, Depth::U8, nullptr);
  d = run(*d, AddLayer{0, 0, Kind::Pixel, {}, painted(1)});
  const LayerId id = d->selected[0];
  Document doc(std::move(d), "Open");  // the history alone holds its states: their tiles go with them
  CHECK(doc.history_bytes() == 0);
  doc.run(DuplicateLayers{{id}}, "Duplicate");
  CHECK(doc.history_bytes() == 0);
  doc.run(DeleteLayers{{doc.state()->selected[0]}}, "Delete");
  CHECK(doc.history_bytes() == 0);
  auto one = std::make_shared<TileGrid>(*find_layer(*doc.state(), id)->pixels);
  std::memset(one->write(0, 0).data(), 2, tile);
  doc.run(SetPixels{id, std::move(one)}, "Brush");
  CHECK(doc.history_bytes() == tile);  // the tile replaced, though three steps hold it
  CHECK(doc.undo() && doc.history_bytes() == tile && doc.redo());  // the redo's new tile, then the old one again
  for (int i = 0; i < 12; i++) {  // 2 MB a step: 8 steps behind the current fit in 16 MB
    doc.run(SetPixels{id, painted(10 + i)}, "Fill");
    CHECK(doc.history_bytes() <= tile_budget() / 4);
  }
  CHECK(doc.steps() == 9 && doc.current() == 8 && doc.history_bytes() == 16 * 4 * tile);
  CHECK(alive() == 9 * 8);  // the first 8 went with their steps (and the brushed one), and 3 fills' tiles
  for (size_t k = 0; k < 4 * 8; k++) CHECK(made[k].expired());
  const TileStats s0 = tile_stats();
  doc.run(SetPixels{id, nullptr}, "Clear");  // takes nothing, leaves 8 tiles behind: a step of them goes
  const TileStats s1 = tile_stats();
  CHECK(doc.steps() == 9 && alive() == 8 * 8 && s1.raw + 8 * tile == s0.raw);
  // A lower budget: the next step leaves fewer behind.
  set_tile_budget(size_t(32) << 20);
  doc.run(SetPixels{id, painted(99)}, "Fill");
  CHECK(doc.steps() == 6 && doc.history_bytes() == 8 * 4 * tile && alive() == 5 * 8);
  // In a gesture the step before its own stays however far past the budget, as its commands start from it; the next
  // step out of it lets that go.
  set_tile_budget(size_t(4) << 20);  // a quarter: 1 MB
  doc.begin_gesture();
  doc.run(SetPixels{id, painted(1)}, "Paint");
  doc.run(SetPixels{id, painted(2)}, "Paint");
  CHECK(doc.steps() == 2 && doc.current() == 1 && doc.history_bytes() == 8 * tile);
  doc.end_gesture();
  doc.run(SetPixels{id, nullptr}, "Clear");
  CHECK(doc.steps() == 1 && doc.history_bytes() == 0);
  set_tile_budget(old);
}

// A brush stroke (C2) is one SetPixels at pen-up: one step. Undo gives the layer its own grid back, the very tiles it
// had (ids), no pixel copied, and redo the stroke's; the history holds what the stroke replaced and no more.
void check_stroke() {
  std::mt19937 rng(3);
  const Format f{Depth::U8, 4};
  DocPtr d = new_document(1500, 1000, Mode::Rgb, Depth::U8, nullptr);
  auto g = std::make_shared<TileGrid>(f);
  for (int ty = 0; ty * S < 1000; ty++)
    for (int tx = 0; tx * S < 1500; tx++) {
      if (rng() % 3 == 0) {
        g->set(tx, ty, Tile::solid(f, {200, 100, 50, 255}));
        continue;
      }
      const WritePin w = g->write(tx, ty);
      for (size_t i = 0; i < size_t(S) * S * 4; i++) w.data()[i] = uint8_t(rng());
    }
  d = run(*d, AddLayer{0, 0, Kind::Pixel, {}, g});
  const LayerId id = d->selected[0];
  Document doc(d, "Open");
  const std::shared_ptr<const TileGrid> was = find_layer(*doc.state(), id)->pixels;
  std::map<TileXY, uint64_t> ids;
  for (const auto& [xy, t] : was->tiles()) ids[xy] = t->id();

  BrushParams b;
  b.size = 90, b.hardness = 0.5f, b.color = {0.1f, 0.6f, 0.9f};
  Stroke s(doc.state(), id, b);
  for (int i = 0; i < 120; i++) s.add({i / 200., float(200 + 9 * i), float(300 + 300 * std::sin(i / 15.)), 0.8f});
  s.finish();
  const std::shared_ptr<const TileGrid> painted = s.wait()->pixels;
  CHECK(painted && painted != was);
  doc.run(SetPixels{id, painted}, "Brush");
  CHECK(doc.steps() == 2 && doc.step(1).name == "Brush" && doc.step(1).json == to_json(SetPixels{id, painted}));
  size_t replaced = 0, made = 0;  // data tiles' bytes: the layer's the stroke replaced, and the stroke's new ones
  for (const auto& [xy, t] : was->tiles())
    if (painted->get(xy.x, xy.y) != t.get() && !t->is_solid()) replaced += raw_size(*t);
  for (const auto& [xy, t] : painted->tiles())
    if (was->get(xy.x, xy.y) != t.get() && !t->is_solid()) made += raw_size(*t);
  CHECK(replaced > 0 && made > 0 && doc.history_bytes() == replaced);
  const size_t raw = tile_stats().raw;
  CHECK(doc.undo() && doc.state() == d && find_layer(*doc.state(), id)->pixels == was);
  for (const auto& [xy, t] : find_layer(*doc.state(), id)->pixels->tiles()) CHECK(ids.at(xy) == t->id());
  CHECK(was->tiles().size() == ids.size() && doc.history_bytes() == made && tile_stats().raw == raw);
  CHECK(doc.redo() && find_layer(*doc.state(), id)->pixels == painted && doc.history_bytes() == replaced);
  CHECK(tile_stats().raw == raw);
}

double ms(std::chrono::steady_clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

// D4's shape (1000 layers, groups 6 deep, a tenth of the pixel layers with tiles) and a layer of D1's 33 x 22 tiles:
// each command as a step, undone to run again, against run() alone; undo and redo of a stroke's 40 tiles there.
void check_d4() {
  std::mt19937 rng(4);
  DocPtr d = new_document(8256, 5504, Mode::Rgb, Depth::U8, nullptr);
  const Format f = pixel_format(*d);
  const auto tiles = [&](int nx, int ny) {
    auto g = std::make_shared<TileGrid>(f);
    for (int ty = 0; ty < ny; ty++)
      for (int tx = 0; tx < nx; tx++) g->set(tx, ty, Tile::make(f));
    return g;
  };
  Ids groups{0};
  std::vector<int> depth{0};
  for (int i = 1; i <= 6; i++) {
    d = run(*d, AddLayer{groups.back(), 0, Kind::Group, {}, nullptr});
    groups.push_back(d->selected[0]), depth.push_back(i);
  }
  d = run(*d, AddLayer{6, 0, Kind::Pixel, {}, tiles(33, 22)});
  const LayerId big = 7;
  while (d->next_id <= 1000) {
    const size_t g = rng() % groups.size();
    const bool group = depth[g] < 6 && rng() % 7 == 0;
    const int index = int(rng() % (find_layer(*d, groups[g])->children.size() + 1));
    d = run(*d, AddLayer{groups[g], index, group ? Kind::Group : Kind::Pixel, {}, !group && rng() % 10 == 0 ? tiles(2, 2) : nullptr});
    if (group) groups.push_back(d->selected[0]), depth.push_back(depth[g] + 1);
  }
  Ids all, hundred(100), fifty(50);
  for (LayerId id = 1; id < d->next_id; id++) all.push_back(id);
  for (LayerId& id : hundred) id = all[rng() % all.size()];
  for (LayerId& id : fifty) id = all[rng() % all.size()];
  auto stroked = std::make_shared<TileGrid>(*find_layer(*d, big)->pixels);  // 40 tiles anew, as a stroke leaves them
  for (int i = 0; i < 40; i++) stroked->set(10 + i % 8, 5 + i / 8, Tile::make(f));
  LayerProps renamed;
  renamed.name = "renamed";
  const std::pair<const char*, Command> cmds[] = {
      {"addLayer", AddLayer{6, 0, Kind::Pixel, {}, nullptr}},
      {"delete100", DeleteLayers{hundred}},
      {"duplicateChain", DuplicateLayers{{1}}},
      {"move", MoveLayer{big, 0, 0}},
      {"group50", GroupLayers{fifty, {}}},
      {"ungroup", Ungroup{1}},
      {"setProps", SetProps{big, renamed}},
      {"setPixels40", SetPixels{big, stroked}},
      {"select1000", SelectLayers{all}},  // last: it joins the first step, which the others start from
  };
  auto t0 = std::chrono::steady_clock::now();
  Document doc(d, "Open");
  std::printf("test_history: D4, 1000 layers 6 deep, %zu MB of tiles: a history of it in %.2f ms; ms a step, median of "
              "21, run() alone / as a step:",
              (tiles_of({d}).size() * size_t(S) * S * 4) >> 20, ms(t0));
  double worst = 0;
  const auto median = [](std::vector<double> v) {
    std::nth_element(v.begin(), v.begin() + 10, v.end());
    return v[10];
  };
  for (const auto& [name, cmd] : cmds) {
    std::vector<double> alone, step;
    for (int i = 0; i < 21; i++) {
      t0 = std::chrono::steady_clock::now();
      const DocPtr r = run(*d, cmd);
      alone.push_back(ms(t0));
      t0 = std::chrono::steady_clock::now();
      doc.run(cmd, name);
      step.push_back(ms(t0));
      CHECK(std::holds_alternative<SelectLayers>(cmd) ? doc.current() == 0 : doc.undo() && doc.state() == d);
    }
    worst = std::max(worst, median(step));
    std::printf(" %s %.3f/%.3f", name, median(alone), median(step));
  }
  doc.run(SetPixels{big, stroked}, "Brush");
  std::vector<double> undo, redo;
  for (int i = 0; i < 21; i++) {
    t0 = std::chrono::steady_clock::now();
    doc.undo();
    undo.push_back(ms(t0));
    t0 = std::chrono::steady_clock::now();
    doc.redo();
    redo.push_back(ms(t0));
  }
  worst = std::max({worst, median(undo), median(redo)});
  std::printf("; the stroke's undo %.3f, redo %.3f\n", median(undo), median(redo));
  CHECK(doc.steps() == 2 && doc.history_bytes() == 40 * size_t(S) * S * 4);
#ifdef NDEBUG  // the UI thread's 1 ms a command; Debug with sanitizers takes tens of times as long
  // 1 ms is the target, printed above; 5 spares a shared machine under load (17/10: 1.03-1.18 ms at a load of 15-17)
  // and still catches a command copying pixels.
  CHECK(worst <= 5);
#endif
}

}  // namespace

TEST(test_history) {
  check_gesture();
  check_select();
  check_stroke();  // before the checks that lower the budget: its raw bytes stay put
  check_budget();
  check_random();
  check_d4();
}
