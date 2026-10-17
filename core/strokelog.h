#pragma once
// Strokes recorded and played again (C4): a stroke's pen events as JSON lines, written as the UI paints it
// (PB_RECORD_STROKES, C3) or made up (pb-bench make-strokes), played again to the same pixels bit for bit whatever the
// platform, the threads or how events batch (tests compare the hashes a log ends with), or at its own pace (pb-bench
// strokes, E3's latency bench). One file a stroke, one JSON object a line:
//
//   {"strokeLog":1,"width":700,"height":500,"mode":"rgb","depth":16,"layer":1,"zoom":0.5,"doc":"plain",
//    "brush":{"size":30.0,"hardness":0.5,...,"color":[0.9,0.1,0.1],"erase":false}}       StrokeRecord's
//   {"t":0.005,"x":120.5,"y":64.25,"pressure":0.6000000238418579,"tiltX":0.0,"tiltY":0.0,"rotation":0.0}
//   ...                                                                                   a PenEvent a line, in order
//   {"buffer":"9f0c33b1e2d4a817","pixels":"52aa0e9d6b1c07f3"}                             StrokeHash after pen-up
//
// Numbers read back to the bit: a float is written as the double equal to it, every double as a decimal that reads
// back as that double.
#include <cstdint>
#include <future>
#include <memory>
#include <string>
#include <vector>

#include "stroke.h"

namespace pb {

// What a stroke made, over the tiles its buffer has, row by row, each tile's pixels as read_rect() gives them (empty
// and solid tiles hash as the data tiles of their pixels would; samples little-endian): `buffer` its coverage, which
// its events, its brush and the canvas decide alone; `pixels` its layer's after pen-up, which the layer's pixels before
// it decide too.
struct StrokeHash {
  uint64_t buffer = 0, pixels = 0;
};
inline bool operator==(StrokeHash a, StrokeHash b) { return a.buffer == b.buffer && a.pixels == b.pixels; }
inline bool operator!=(StrokeHash a, StrokeHash b) { return !(a == b); }
// Of a state pen-up merged (StrokeState::pixels set; std::invalid_argument otherwise). Any thread.
StrokeHash stroke_hash(const StrokeState&);

// A stroke as its log holds it: enough to paint it again on a document of the same canvas.
struct StrokeRecord {
  int width = 0, height = 0;  // the canvas, pixels
  Mode mode = Mode::Rgb;
  Depth depth = Depth::U8;
  LayerId layer = 0;  // the pixel layer painted
  BrushParams brush;
  double zoom = 1;  // View::zoom as it was painted: the mip level frames showed it at (E3)
  std::string doc;  // what the document was, to build it again: "plain" for replay_doc()'s, else a name, "" unknown
  std::vector<PenEvent> events;
  StrokeHash hash;  // after pen-up
};

std::string to_jsonl(const StrokeRecord&);  // the log's text, as above
// The log at `path`. Throws std::runtime_error ("<path>:<line>: <why>") for a file that is not one, one of a later
// version, or one cut short (no hash line). Keys it does not know are skipped. Any thread.
StrokeRecord read_stroke_log(const std::string& path);

// A document to play a log on away from the one it was painted on: its canvas, mode and depth, and one pixel layer
// (selected) of one colour at half alpha over every canvas tile, so that painting blends and erasing shows. The logs
// pb-bench make-strokes writes were painted on it (doc "plain"). Any thread.
DocPtr replay_doc(const StrokeRecord&);

// Records a stroke as the UI paints it (C3): record() at pen down, add() beside each Stroke::add(), finish() once the
// stroke merged.
class StrokeLog {
 public:
  // A log of the stroke about to be painted on `layer` of `doc`, its file to go into `dir`, by default the directory
  // PB_RECORD_STROKES names: null when that is unset or empty. `name` is the log's "doc". UI thread.
  static std::unique_ptr<StrokeLog> record(const DocState& doc, LayerId layer, const BrushParams&, double zoom,
                                           std::string name = {}, std::string dir = {});
  void add(const PenEvent&);  // UI thread, as Stroke::add(): kept in memory
  // Pen up, with the merged state (Stroke::state() once it has pixels; std::invalid_argument otherwise): a Background
  // job hashes it and writes the log, stroke-<seconds since 1970>-<n>.jsonl in the directory (made if need be), and the
  // future gives that path or what stopped it. Returns at once. UI thread, once.
  std::future<std::string> finish(std::shared_ptr<const StrokeState> merged);

 private:
  StrokeLog() = default;
  std::string dir_;
  StrokeRecord rec_;
};

}  // namespace pb
