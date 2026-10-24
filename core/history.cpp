// The history (history.h): a list of states, and what tiles they hold, kept up from what each step changed. A data
// tile's `kept` counts the step it entered the history at (the first step for those its state starts with), so a step
// holds it while `kept` is not 0, whichever way it left and came back meanwhile: dropping the oldest step forgets what
// the next one let go of, dropping steps past the current one what they took on.
#include "history.h"

#include <algorithm>
#include <atomic>
#include <stdexcept>
#include <utility>
#include <variant>

#include "profile.h"
#include "tilecache.h"

namespace pb {
namespace {

std::atomic<uint64_t> g_serial{0};

// One more command in a step's JSON: from the second on, an array of them.
void append(std::string& json, const std::string& cmd) {
  if (json[0] == '[')
    json.pop_back();
  else
    json.insert(0, 1, '[');
  json += ',' + cmd + ']';
}

// The data tiles that layers of one state hold (pixels and masks) and the same layers (by id) of another do not, and
// the other way round: each placement +1 or -1 in `net`. The trees are walked together past the nodes and grids they
// share, so it costs what changed between them, not what they hold.
struct Diff {
  std::unordered_map<uint64_t, std::pair<int, const Tile*>> net;  // by id
  std::unordered_map<LayerId, const Layer*> left, entered;  // layers out of their group or into one, and all in them

  void tile(const TilePtr& t, int n) {
    if (t->is_solid()) return;
    auto& e = net[t->id()];
    e.first += n, e.second = t.get();
  }
  void grids(const TileGrid* a, const TileGrid* b) {
    if (a == b) return;
    if (!a || !b) {
      for (const auto& [xy, t] : (a ? a : b)->tiles()) tile(t, a ? -1 : 1);
      return;
    }
    const auto& x = a->tiles();
    const auto& y = b->tiles();
    for (auto i = x.begin(), j = y.begin(); i != x.end() || j != y.end();) {
      if (j == y.end() || (i != x.end() && i->first < j->first)) {
        tile((i++)->second, -1);
      } else if (i == x.end() || j->first < i->first) {
        tile((j++)->second, 1);
      } else {
        if (i->second != j->second) tile(i->second, -1), tile(j->second, 1);
        ++i, ++j;
      }
    }
  }
  static void all(const Layer& l, std::unordered_map<LayerId, const Layer*>& into) {
    into[l.id] = &l;
    for (const LayerPtr& c : l.children) all(*c, into);
  }
  // a and b: one layer in the two states. Children are matched by id; those of one only went elsewhere or for good.
  void layers(const Layer& a, const Layer& b) {
    if (&a == &b) return;
    grids(a.pixels.get(), b.pixels.get());
    grids(a.mask.get(), b.mask.get());
    const auto& x = a.children;
    const auto& y = b.children;
    const auto same = [](const LayerPtr& p, const LayerPtr& q) { return p->id == q->id; };
    if (x.size() == y.size() && std::equal(x.begin(), x.end(), y.begin(), same)) {  // the order kept: most commands
      for (size_t i = 0; i < x.size(); i++) layers(*x[i], *y[i]);
      return;
    }
    std::unordered_map<LayerId, const Layer*> there;
    for (const LayerPtr& l : y) there.emplace(l->id, l.get());
    for (const LayerPtr& l : x) {
      const auto it = there.find(l->id);
      if (it == there.end()) {
        all(*l, left);
      } else {
        layers(*l, *it->second);
        there.erase(it);
      }
    }
    for (const LayerPtr& l : y)
      if (there.count(l->id)) all(*l, entered);
  }
  // After layers(): a layer that left one group and entered another moved, its tiles changed where its grid did.
  void moved() {
    for (const auto& [id, l] : left) {
      const auto it = entered.find(id);
      const Layer* now = it == entered.end() ? nullptr : it->second;
      grids(l->pixels.get(), now ? now->pixels.get() : nullptr);
      grids(l->mask.get(), now ? now->mask.get() : nullptr);
    }
    for (const auto& [id, l] : entered)
      if (!left.count(id)) grids(nullptr, l->pixels.get()), grids(nullptr, l->mask.get());
  }
};

}  // namespace

Document::Document(DocPtr state, std::string name) {
  if (!state || !state->root) throw std::invalid_argument("pb: Document: no state");
  Entry tiles;
  count(Layer{}, *state->root, &tiles);
  keep(tiles.came, 1);  // the first step's
  steps_.push_back({{std::move(name), "", std::move(state), ++g_serial}, {}, {}});
}

void Document::count(const Layer& from, const Layer& to, Entry* into) {
  PROFILE_ZONE("history_count");
  Diff d;
  d.layers(from, to);
  d.moved();
  for (const auto& [id, n] : d.net) {
    if (!n.first) continue;  // moved between layers
    Held& h = tiles_[id];
    if (!h.bytes) h.bytes = uint32_t(size_t(kTileSize) * kTileSize * size_t(n.second->format().bytes()));
    const uint32_t was = h.now;
    h.now = uint32_t(int64_t(was) + n.first);
    if (!was) {
      now_bytes_ += h.bytes;
      if (into) into->came.push_back(id);
    } else if (!h.now) {
      now_bytes_ -= h.bytes;
      if (into) into->gone.push_back(id);
      if (!h.kept) tiles_.erase(id);
    }
  }
}

void Document::keep(const std::vector<uint64_t>& ids, int by) {
  for (const uint64_t id : ids) {
    const auto it = tiles_.find(id);  // there: a step holds it, or state() does
    Held& h = it->second;
    if (by > 0 && !h.kept++) kept_bytes_ += h.bytes;
    if (by < 0 && !--h.kept) {
      kept_bytes_ -= h.bytes;
      if (!h.now) tiles_.erase(it);
    }
  }
}

void Document::cut() {
  for (size_t i = current_ + 1; i < steps_.size(); i++) keep(steps_[i].came, -1);
  steps_.resize(current_ + 1);
}

void Document::add(DocPtr state, std::string name, std::string json) {
  cut();
  Entry e;
  e.name = std::move(name), e.json = std::move(json), e.state = std::move(state), e.serial = ++g_serial;
  count(*this->state()->root, *e.state->root, &e);
  keep(e.came, 1);
  steps_.push_back(std::move(e));
  current_++;
}

void Document::trim() {
  const size_t cap = tile_budget() / 4;
  while (history_bytes() > cap && current_ > (merge_ ? 1u : 0u)) {
    keep(steps_[1].gone, -1);  // what only the oldest held
    steps_.erase(steps_.begin());
    steps_[0].came = std::vector<uint64_t>(), steps_[0].gone = std::vector<uint64_t>();  // no step before it now
    current_--;
  }
}

void Document::run(const Command& cmd, std::string name) {
  PROFILE_ZONE("history_run");
  DocPtr next = pb::run(*state(), cmd);  // throws before anything changed
  std::string json = to_json(cmd);
  Entry& e = steps_[current_];
  if (std::holds_alternative<SelectLayers>(cmd)) {  // the same layers: no tile to count
    e.state = std::move(next);
    if (!e.json.empty()) append(e.json, json);
    return;
  }
  if (!merge_) {
    add(std::move(next), std::move(name), std::move(json));
    merge_ = gesture_ > 0;
  } else {  // the gesture's step anew from the one before: tiles that came and went within it are no step's
    const Layer& before = *steps_[current_ - 1].state->root;
    keep(e.came, -1);
    count(*e.state->root, before, nullptr);
    e.came.clear(), e.gone.clear();
    count(before, *next->root, &e);
    keep(e.came, 1);
    e.state = std::move(next);
    e.serial = ++g_serial;
    append(e.json, json);
  }
  trim();
}

void Document::begin_gesture() {
  if (!gesture_++) merge_ = false;
}

void Document::end_gesture() {
  if (gesture_ && !--gesture_) merge_ = false;
}

bool Document::undo() {
  if (!current_) return false;
  jump(current_ - 1);
  return true;
}

bool Document::redo() {
  if (current_ + 1 >= steps_.size()) return false;
  jump(current_ + 1);
  return true;
}

void Document::jump(size_t i) {
  if (i >= steps_.size()) throw std::out_of_range("pb: Document::jump: no step " + std::to_string(i));
  count(*state()->root, *steps_[i].state->root, nullptr);
  current_ = i;
  merge_ = false;
}

void Document::delete_step() {
  if (!current_) return;
  jump(current_ - 1);
  cut();
}

void Document::snapshot(std::string name) { snapshots_.push_back({std::move(name), state()}); }

void Document::restore(size_t i) {
  const Snapshot& s = snapshots_.at(i);
  for (size_t k = 0; k < steps_.size(); k++)
    if (steps_[k].state == s.state) return jump(k);
  add(s.state, s.name, "");
  merge_ = false;
  trim();
}

}  // namespace pb
