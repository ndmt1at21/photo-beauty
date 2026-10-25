#pragma once
// Adjustments (D31): Photoshop's sixteen of Image › Adjustments and of adjustment layers, by AdjustSpec.js's names and
// keys. An Adjustment is one of them with its values, made from a layer's props (adj, vals: A30) once and cached by
// content, shared, never changed. It adjusts colours on the CPU (apply()) and packs what the GPU's adjust_pixel()
// (shaders/lib/adjust.slang) needs to do the same to the bit: 32 words and its tables (pack(), tables()).
// apply_adjustment() runs one over a layer's or a mask's tiles (Image › Adjustments), preview_state() shows one in the
// dialog's preview; histogram.h counts what Levels, Curves and Threshold show.
//
// A type is a pair of files that D33-D39 own, one type's task each, nothing shared between them:
//   - core/adj_<type>.cpp: a struct of its values whose fields() lists AdjustSpec's keys (AdjFields reads and writes
//     them alike), a class over Adjustment with identity(), adjust_row() and pack_words(), its tables made in its
//     constructor, and the maker adjustment() calls (adj::<type>(), below), which make_adjustment() writes;
//   - shaders/lib/adj_<type>.slang: float3 adj_<type>(uint w[32], StructuredBuffer<float4> tables, float3 c, int x,
//     int y), adjust_row()'s pixel line by line: the same float operations in the same order, each `precise`, division
//     and square root through blend.slang's div() and sqrt_rn(), no pow, exp, log, sin or cos (tables, made on the
//     CPU), so both give the same bits (tests/adjust_check.h checks it on lavapipe). Helpers get the type's name in
//     front (adjust.slang imports every type's module, so two `lum()` would clash where it calls them).
// The table of types (names, makers, grey documents) is adjust.cpp's, its switch adjust.slang's: nothing there
// changes when a type's body does. D31 left every type the identity, its values read and written; AdjType::TestMatrix
// (adjust.cpp, adjust.slang) is a worked example of fields, words and a table, for tests (D31, D40).
//
// pack()'s 32 words (AdjustParams): w[0] the type, w[1] where its tables start in the frame's `tables` buffer
// (float4s; whoever lays the buffer out sets it), both the frame's; w[2..31] the type's, floats as their bits (word(),
// asfloat() in Slang), each type's file saying which is which. More than 30 values (Hue/Saturation's six ranges,
// Selective Color's nine colours) go in its tables.
//
// tables(): float4 entries the type makes once, in its constructor, into tables_; the GPU reads entry i at
// tables[w[1] + i], several tables one after another. A curve along a value (Levels, Curves, Exposure...: what pow or
// exp make) is 4,098 entries, entry i its value at i / 4096 and entry 4097 repeating 4096, R, G, B in x, y, z (a fourth
// in w): adjust_curve() reads it on the CPU, gradient.slang's gradient_lookup(tables, w[1] + offset, t) on the GPU
// (`import gradient;`), linear between entries, the same bits (GradientLut's layout). A 3D table (D39) interpolates
// tetrahedrally, its own code on both sides.
#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "doc.h"
#include "jobs.h"
#include "tile.h"

namespace pb {

// AdjustSpec.ORDER's sixteen in its order (the Adjustments panel's), then one for tests: a colour matrix and curves
// (adjust.cpp). That one is no user's: the UI never makes it, lists of what a user picks or a file holds (the UI's,
// .pbd's, PSD's: M43) stop at kAdjPhotoshop, and a file's props.adj naming it is a name adj_type() does not know
// (kept as it is, as A30 keeps those) unless a test has called enable_test_matrix().
enum class AdjType : uint8_t {
  BrightnessContrast, Levels, Curves, Exposure, Vibrance, HueSaturation, ColorBalance, BlackWhite, PhotoFilter,
  ChannelMixer, ColorLookup, Invert, Posterize, Threshold, GradientMap, SelectiveColor,
  TestMatrix,
};
// props.adj's names, by value: AdjustSpec.ORDER's, and the test's.
inline constexpr const char* kAdjNames[] = {
    "Brightness/Contrast", "Levels",          "Curves",        "Exposure",       "Vibrance",
    "Hue/Saturation",      "Color Balance",   "Black & White", "Photo Filter",   "Channel Mixer",
    "Color Lookup",        "Invert",          "Posterize",     "Threshold",      "Gradient Map",
    "Selective Color",     "Test Matrix"};
inline constexpr int kAdjTypes = int(std::size(kAdjNames));
static_assert(kAdjTypes == int(AdjType::TestMatrix) + 1);
inline constexpr int kAdjPhotoshop = int(AdjType::TestMatrix);  // the sixteen of the UI, of .pbd and PSD files
// props.adj's; nullopt for "" and names it does not know, "Test Matrix" among them but after enable_test_matrix().
std::optional<AdjType> adj_type(const std::string& name);
// For tests only (D31's, D40's composite through props): from now on, until false, adj_type() and so adjustment() by
// name and of props take "Test Matrix". adjustment(AdjType::TestMatrix, ...) makes it whatever this says. Any thread.
void enable_test_matrix(bool on = true);

// Whether Photoshop offers it in a document of this mode: on a grey one, only those that need no colour (Cần bạn chốt
// 13; which those are is adjust.cpp's, a hypothesis until checked in Photoshop). Masks take the same ones.
bool available(AdjType, Mode);

// What an adjustment's tables depend on besides its values: the document's colours.
struct AdjustContext {
  Mode mode = Mode::Rgb;  // a grey document's pixels come as R = G = B (apply()'s R is read back)
  // The document's profile (DocState::icc): what Exposure's linear light and Gradient Map's methods are of (D34, D38;
  // source_profile() of it, color.h). Null or empty: sRGB's.
  std::shared_ptr<const std::vector<uint8_t>> icc;
};
inline AdjustContext adjust_context(const DocState& d) { return {d.mode, d.icc}; }

using Float4 = std::array<float, 4>;

// What adjust.slang's adjust_pixel() reads of an adjustment: 32 words, 128 bytes, a GPU buffer's element as it is
// (the header above says which word is whose).
struct AdjustParams {
  std::array<uint32_t, 32> w{};
};
inline uint32_t word(float f) {
  uint32_t u;
  std::memcpy(&u, &f, 4);
  return u;
}
inline float word_float(uint32_t u) {
  float f;
  std::memcpy(&f, &u, 4);
  return f;
}

// A curve's table (the header above) at v, as gradient.slang's gradient_lookup() takes it: v clamped to 0..1 (NaN 0),
// linear between the entries around it; `table` holds 4,098 entries.
Float4 adjust_curve(const Float4* table, float v);
inline constexpr int kCurveSteps = 4096;  // a curve's entry i is at i / kCurveSteps

// One adjustment with its values: made by adjustment() (below), shared, never changed, any thread.
class Adjustment {
 public:
  virtual ~Adjustment() = default;
  Adjustment(const Adjustment&) = delete;
  Adjustment& operator=(const Adjustment&) = delete;

  AdjType type() const { return type_; }
  const AdjustContext& context() const { return context_; }
  // Its values as props keep them (vals): AdjustSpec's keys of those off their defaults, members sorted, no spaces,
  // whole numbers without a fraction: "{}" at the defaults. adjustment() of it makes the same adjustment.
  const std::string& vals() const { return vals_; }
  // Leaves every colour as it is (Levels at 0, 1.00, 255): a composite and apply_adjustment() may skip it. Only when
  // sure: false costs time, true where it is not changes pixels.
  virtual bool identity() const = 0;
  // Its result depends on the pixel as well as on the colour (a dither): a solid tile comes out of data.
  virtual bool dithered() const { return false; }

  // n pixels of row y from column x (pixels of the mip level worked on: what a dither hashes), 4 floats each: R, G, B,
  // the document's encoded values 0..1 (a grey document's and a mask's R = G = B: R is the result), adjusted in place
  // and clamped to 0..1 (NaN 0); the fourth (alpha) neither read nor written. adjust_pixel() of pack() gives the same
  // bits.
  void apply(float* px, int n, int x, int y) const;
  std::array<float, 3> apply(const std::array<float, 3>& rgb, int x = 0, int y = 0) const;

  // The words adjust_pixel() reads, its tables at `base` (float4s) in the frame's `tables` buffer.
  AdjustParams pack(uint32_t base) const;
  const std::vector<Float4>& tables() const { return tables_; }
  size_t bytes() const;  // about what it holds: its tables, its JSON

 protected:
  Adjustment(AdjType type, const AdjustContext& context, std::string vals)
      : type_(type), context_(context), vals_(std::move(vals)) {}
  // The type's: apply() before clamping. Within 0..1 it gives what it likes past them, apply() clamps.
  virtual void adjust_row(float* px, int n, int x, int y) const = 0;
  // The type's words, w[2..31] (pack() sets w[0], w[1]).
  virtual void pack_words(AdjustParams& p) const = 0;
  std::vector<Float4> tables_;  // made by the type's constructor

 private:
  AdjType type_;
  AdjustContext context_;
  std::string vals_;
};
using AdjustmentPtr = std::shared_ptr<const Adjustment>;

// The adjustment of `type` with `vals` (props' JSON, AdjustSpec's keys): keys it leaves out at their defaults, numbers
// out of range clamped, keys a type does not know skipped. Null for JSON that is malformed or not an object, a key of
// the wrong type, a name not in its list. Cached by what made it (type, vals, mode, profile), within 64 MB of tables:
// the same arguments give the same adjustment while it stays, its tables made once. Any thread.
AdjustmentPtr adjustment(AdjType type, const std::string& vals = "{}", const AdjustContext& = {});
// By props.adj's name; null too for a name it does not know.
AdjustmentPtr adjustment(const std::string& name, const std::string& vals = "{}", const AdjustContext& = {});
// An adjustment layer's: props.adj and props.vals (null: the defaults).
AdjustmentPtr adjustment(const LayerProps& props, const AdjustContext& = {});

// Image › Adjustments on a grid (a pixel layer's, a mask's).
struct AdjustOptions {
  // Where: mask_format() of the document (DocState::selection), its values weighing the adjustment (0 none, the
  // maximum all of it); outside the canvas nothing is selected. Null: everywhere.
  const TileGrid* selection = nullptr;
  int width = 0, height = 0;  // the canvas: where a selection stops, and where a mask's empty tiles change (below)
  CancelToken cancel;         // once set, it stops soon, throwing Cancelled
  std::function<void(double)> progress;  // the part done, 0 to 1, after each batch of tiles, on the calling thread
};
// `grid` adjusted: a new grid sharing every tile it leaves alone (empty ones, those it does not change or where nothing
// is selected keep their ids). Samples go to apply() as floats (v / max_value()) with a grey one as R = G = B, then
// back mixed with the old by the selection there, v (1 - s) + A(v) s, to the nearest level; alpha stays as it is (so
// Lock Transparent Pixels changes nothing; Lock Image Pixels refusing is the command's, B35). A layer's empty tiles
// stay empty: transparent, they have no colour. A mask's (1 channel) are its fill f: with nothing selected the fill
// becomes A(f) where that is black or white or no canvas is given, else the canvas's empty tiles become tiles of A(f)
// (a mask keeps black or white past them: doc.h); with a selection, the empty tiles it touches become tiles. Tile by
// tile on the pool (parallel_for), the calling thread waiting: from a Background job (Image › Adjustments, D41's
// Auto), a Latest one for a preview. Throws Cancelled, nothing changed; std::invalid_argument for a selection of
// more than 1 channel. Any thread.
TileGrid apply_adjustment(const TileGrid& grid, const Adjustment& adj, const AdjustOptions& = {});

// What an Image › Adjustments dialog shows while it is open (N37): doc with an adjustment layer of adj right above
// `layer`, clipped to it, its mask the selection (DocState::selection's; null: none), so that frames composite what
// apply_adjustment() of the layer would give (from D40 on: until then adjustment layers composite nothing). For frames
// only: not a command, no step of the history. The layer takes doc.next_id, the selected layers stay. A layer that
// clips onto another itself shows its clipping group adjusted up to it. Throws std::invalid_argument unless `layer` is
// a pixel layer of doc, the adjustment available() in its mode and the selection in mask_format(doc). Any thread.
DocPtr preview_state(const DocState& doc, LayerId layer, const Adjustment& adj,
                     std::shared_ptr<const TileGrid> selection = nullptr);

// ---- For the types (core/adj_<type>.cpp) ----------------------------------------------------------------------------

// Reads or writes a type's values by AdjustSpec's keys: its fields() lists each key once, and the same list reads them
// (from vals) and writes them back (vals()), so what it reads it writes back alike, and the UI's vals (AdjustSpec's
// changed()) come back byte for byte. Reading: a key left out or null keeps the default, a number out of range is
// clamped, a key of the wrong type or a name outside its list fails (ok() false). Writing: only the keys off their
// default. Defaults and ranges are AdjustSpec's: its rows' (S: number, CHK: flag, DDL: choice, SEG and RADIO's group:
// index, RADIO's value and TINT's colour, PRE: text), FIELDS' for Levels' and Threshold's fields.
class AdjFields {
 public:
  explicit AdjFields(const nlohmann::json* vals);  // reading vals; null: writing
  ~AdjFields();
  AdjFields(const AdjFields&) = delete;
  AdjFields& operator=(const AdjFields&) = delete;

  bool reading() const { return in_ != nullptr; }
  bool ok() const { return ok_; }
  void number(const char* key, double& v, double def, double lo, double hi);
  void flag(const char* key, bool& v, bool def);
  void index(const char* key, int& v, int def, int count);  // 0..count - 1, a segment's or a radio group's
  void choice(const char* key, std::string& v, std::initializer_list<const char*> names);  // the first is the default
  void text(const char* key, std::string& v, const char* def);   // any string (a preset's or a file's name)
  void color(const char* key, std::string& v, const char* def);  // "#rrggbb", either case, kept as written
  // n numbers in an array of exactly n, each clamped to lo..hi.
  void numbers(const char* key, double* v, int n, const double* def, double lo, double hi);
  // Anything `valid` takes, kept as JSON (Curves' points, Hue/Saturation's span): written when not `def`.
  void value(const char* key, nlohmann::json& v, const nlohmann::json& def, bool (*valid)(const nlohmann::json&));
  // An object kept as JSON, null by default: tools/ps-golden/README-psd.md's sets by a selector's name (each channel's,
  // range's, tone's, colour's: what E33's and D32's index.json and M43 write), for the type to read inside.
  void object(const char* key, nlohmann::json& v);
  std::string written() const;  // writing: the object, as vals() keeps it

 private:
  void fail() { ok_ = false; }
  const nlohmann::json* in_;
  std::unique_ptr<nlohmann::json> out_;
  bool ok_ = true;
};

// What a type's maker does: V's fields() read from vals, then T(v, context, vals as written); null when they do not
// read. V: the values, default-constructed at AdjustSpec's defaults, with void fields(AdjFields&).
template <class V, class T>
AdjustmentPtr make_adjustment(const nlohmann::json& vals, const AdjustContext& context) {
  V v;
  AdjFields in(&vals);
  v.fields(in);
  if (!in.ok()) return nullptr;
  V copy = v;  // fields() takes its values by reference, both ways
  AdjFields out(nullptr);
  copy.fields(out);
  return std::make_shared<const T>(v, context, out.written());
}

// Each type's maker, core/adj_<type>.cpp's: make_adjustment<its values, its class>(vals, context). adjustment()
// calls them through adjust.cpp's table.
namespace adj {
AdjustmentPtr brightness_contrast(const nlohmann::json&, const AdjustContext&);
AdjustmentPtr levels(const nlohmann::json&, const AdjustContext&);
AdjustmentPtr curves(const nlohmann::json&, const AdjustContext&);
AdjustmentPtr exposure(const nlohmann::json&, const AdjustContext&);
AdjustmentPtr vibrance(const nlohmann::json&, const AdjustContext&);
AdjustmentPtr hue_saturation(const nlohmann::json&, const AdjustContext&);
AdjustmentPtr color_balance(const nlohmann::json&, const AdjustContext&);
AdjustmentPtr black_white(const nlohmann::json&, const AdjustContext&);
AdjustmentPtr photo_filter(const nlohmann::json&, const AdjustContext&);
AdjustmentPtr channel_mixer(const nlohmann::json&, const AdjustContext&);
AdjustmentPtr color_lookup(const nlohmann::json&, const AdjustContext&);
AdjustmentPtr invert(const nlohmann::json&, const AdjustContext&);
AdjustmentPtr posterize(const nlohmann::json&, const AdjustContext&);
AdjustmentPtr threshold(const nlohmann::json&, const AdjustContext&);
AdjustmentPtr gradient_map(const nlohmann::json&, const AdjustContext&);
AdjustmentPtr selective_color(const nlohmann::json&, const AdjustContext&);
}  // namespace adj

}  // namespace pb
