#pragma once
// A document's history: its current state and the steps that led to it, as Edit › Undo and the History panel show
// them. A step is a state (doc.h), so undo, redo and jumping swap a pointer and copy no pixel, and steps share every
// layer and tile they did not change. The UI thread owns a Document and alone calls it; jobs (a frame, a stroke, a
// save) take state() and read it on any thread meanwhile.
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "doc.h"

namespace pb {

class Document {
 public:
  // What the History panel lists, and Edit › Undo names.
  struct Step {
    std::string name;  // the caller's: "Open", "Brush", "Opacity"
    // What made it, as to_json() writes the command; several (a gesture's, layers chosen since), a JSON array of
    // them in order; "" where no command made it: the first step, a snapshot's.
    std::string json;
    DocPtr state;  // the document after it
    // Unique in the process, new whenever a command makes or changes the step but for layers chosen: keep the current
    // step's at a save, and the document has unsaved changes (the tab's dot) while it is another.
    uint64_t serial = 0;
  };
  // A state kept by name beside the steps, as Photoshop's snapshots: never dropped, its tiles counted nowhere.
  struct Snapshot {
    std::string name;
    DocPtr state;
  };

  // A history of one step, `name` ("Open", "New"), at `state`: walks its layers and tiles once. Throws
  // std::invalid_argument for a null state or one without its root.
  Document(DocPtr state, std::string name);

  const DocPtr& state() const { return steps_[current_].state; }  // the current step's
  size_t steps() const { return steps_.size(); }                  // how many, at least 1
  // Oldest first, valid until the history next changes; throws std::out_of_range past the last.
  const Step& step(size_t i) const { return steps_.at(i); }
  size_t current() const { return current_; }  // state() is step(current()); the steps after it are what redo gives

  // cmd (doc.h) on state(), as a new step named `name` after the steps past the current one go, as in Photoshop; but
  // SelectLayers joins the current step and keeps those after it, as Photoshop records no step for choosing layers.
  // Throws as run() does (std::invalid_argument) and changes nothing then.
  void run(const Command& cmd, std::string name);
  // Between begin_gesture() and end_gesture() (a slider dragged, a layer duplicated and renamed) the commands make one
  // step: the first names it and each changes its state. undo(), redo(), jump(), delete_step(), restore() end that
  // step: the gesture's next command makes another. They nest; an end without a begin does nothing.
  void begin_gesture();
  void end_gesture();

  bool undo();             // to the step before; false at the first
  bool redo();             // to the step after; false at the last
  void jump(size_t step);  // to step(step), as a click on it in the History panel; throws std::out_of_range
  void delete_step();      // drops the current step and those after it, back to the one before; not the first

  void snapshot(std::string name);  // keeps state() as a snapshot
  const std::vector<Snapshot>& snapshots() const { return snapshots_; }  // oldest first
  // Shows snapshot i: jumps to the step that still has its state (the one it was taken on, unless that changed or
  // went since), else makes a step of its own at it, named as the snapshot, the steps past the current one gone as
  // with run(). Throws std::out_of_range.
  void restore(size_t i);

  // Tile memory that only steps other than the current one hold: the raw size of their data tiles (kTileSize² pixels
  // in the tile's format, wherever the store keeps them: RAM, zstd, scratch, a .pbd), each tile once however many
  // steps or layers hold it; solid and empty tiles count nothing, snapshots' only while a step holds them too. Kept up
  // as each step is made, by walking the two states' trees together past what they share, so a stroke's step costs
  // the tiles of the layer it painted, not the document's. Once a new step takes it past a quarter of tile_budget()
  // (tilecache.h) the oldest steps go until it is within it again, never the current one nor, in a gesture, the one
  // before it (the store pushes old steps' tiles out to zstd and scratch first, being least recently used).
  // ponytail: each document's history gets the quarter to itself (P0 opens one at a time); share it when tabs come.
  // ponytail: a .pbd's tiles (M2) count their raw size though RAM holds them for free; weigh them less if documents
  // read from files run out of steps early.
  size_t history_bytes() const { return kept_bytes_ - now_bytes_; }

 private:
  struct Entry : Step {
    std::vector<uint64_t> came, gone;  // data tiles (ids) its state holds that the step before's does not, and back
  };
  // A data tile that a step holds: in how many of state()'s layers, in how many steps' `came` (and 1 if the first
  // step's state holds it), its raw size. So a step holds it while `kept` is not 0.
  struct Held {
    uint32_t now = 0, kept = 0, bytes = 0;
  };
  void count(const Layer& from, const Layer& to, Entry* into);  // state() was `from`, is `to`: `now` follows
  void keep(const std::vector<uint64_t>& ids, int by);
  void cut();                                                  // drops the steps past the current one
  void add(DocPtr state, std::string name, std::string json);  // a new step after the current one
  void trim();

  std::vector<Entry> steps_;
  size_t current_ = 0;
  int gesture_ = 0;     // begin_gesture()s not ended
  bool merge_ = false;  // the current step is the open gesture's: its next command goes in it
  std::vector<Snapshot> snapshots_;
  std::unordered_map<uint64_t, Held> tiles_;  // by id
  size_t now_bytes_ = 0, kept_bytes_ = 0;     // of the tiles state() holds, and of those any step holds
};

}  // namespace pb
