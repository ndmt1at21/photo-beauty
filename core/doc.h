#pragma once
// The document: an immutable tree of layers over the tile store. A command takes a state and gives a new one that
// shares everything it left alone, so the history is a list of states and undo swaps a pointer. Declared here,
// implemented by T1 (doc.cpp).
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "tile.h"

namespace pb {

enum class Mode : uint8_t { Rgb, Gray };

// Photoshop's blend modes in its menu's order, and Pass Through (groups only). P0 composites Normal, Darken,
// Multiply, Color Burn, Lighten, Screen, Color Dodge, Overlay, Soft Light, Linear Light, Difference (B1).
enum class Blend : uint8_t {
  PassThrough, Normal, Dissolve,
  Darken, Multiply, ColorBurn, LinearBurn, DarkerColor,
  Lighten, Screen, ColorDodge, LinearDodge, LighterColor,
  Overlay, SoftLight, HardLight, VividLight, LinearLight, PinLight, HardMix,
  Difference, Exclusion, Subtract, Divide,
  Hue, Saturation, Color, Luminosity,
};
inline constexpr const char* kBlendNames[] = {  // JSON, by value
    "passThrough", "normal",     "dissolve",   "darken",     "multiply",  "colorBurn", "linearBurn",
    "darkerColor", "lighten",    "screen",     "colorDodge", "linearDodge", "lighterColor", "overlay",
    "softLight",   "hardLight",  "vividLight", "linearLight", "pinLight", "hardMix",   "difference",
    "exclusion",   "subtract",   "divide",     "hue",        "saturation", "color",    "luminosity"};
static_assert(std::size(kBlendNames) == size_t(Blend::Luminosity) + 1);

// Fixed for a layer's life, unique in its document (DocState::next_id); 0 is the root.
using LayerId = uint32_t;

// What the Layers panel sets on a layer. JSON: the members' names in camelCase and the schema version; keys it does
// not know go to `extra` and are written back as they were.
struct LayerProps {
  std::string name;
  bool visible = true;
  float opacity = 1;  // 0..1
  float fill = 1;     // 0..1: opacity of the pixels without the layer style (P1)
  Blend blend = Blend::Normal;
  // Photoshop's four locks: kept, not enforced before P1.
  bool lock_transparency = false, lock_pixels = false, lock_position = false, lock_all = false;
  std::string extra;  // unknown keys: a JSON object as written back, "" for none
};
inline constexpr int kLayerSchemaVersion = 1;  // bump only when a key changes meaning, then migrate older ones
std::string to_json(const LayerProps&);
std::optional<LayerProps> props_from_json(const std::string&);  // nullopt when malformed or of a newer schema

enum class Kind : uint8_t { Pixel, Group };

// A node of the tree, shared by every state that has it; never changed once made.
struct Layer {
  LayerId id = 0;
  Kind kind = Kind::Pixel;
  LayerProps props;
  std::shared_ptr<const TileGrid> pixels;  // a pixel layer's: pixel_format() of its document, fill transparent
  std::vector<std::shared_ptr<const Layer>> children;  // a group's, bottom first (the order they composite in)
};
using LayerPtr = std::shared_ptr<const Layer>;

// One state of a document. Immutable once shared: the UI thread keeps the current one and the history; jobs (a
// frame, a stroke, a save) get a DocPtr and read it on any thread while the UI goes on.
struct DocState {
  int width = 0, height = 0;  // the canvas, pixels; layers may hold tiles past it
  Mode mode = Mode::Rgb;
  Depth depth = Depth::U8;                          // 8 or 16 bits
  std::shared_ptr<const std::vector<uint8_t>> icc;  // the document's ICC profile, shared by its states
  LayerPtr root;                  // a group of id 0 holding the layers; its props are unused
  std::vector<LayerId> selected;  // the Layers panel's selection; painting needs one pixel layer
  LayerId next_id = 1;            // the next new layer's id
};
using DocPtr = std::shared_ptr<const DocState>;

// The pixels of a document's layers: RGB or grey, and alpha, at its depth.
inline Format pixel_format(const DocState& d) { return {d.depth, d.mode == Mode::Rgb ? 4 : 2}; }

// A blank document: no layers. Any thread.
DocPtr new_document(int width, int height, Mode, Depth, std::shared_ptr<const std::vector<uint8_t>> icc);
const Layer* find_layer(const DocState&, LayerId);  // null when not there; valid while the state lives

// Commands are values: run() applies one, to_json() writes it ({"op": "addLayer", ...}) for the history and for
// Actions (P2) to replay. A command that adds layers gives them ids from next_id on and selects them. `index` counts
// among the parent group's children, 0 at the bottom.
struct AddLayer {
  LayerId parent = 0;
  int index = 0;
  Kind kind = Kind::Pixel;
  LayerProps props;
  std::shared_ptr<const TileGrid> pixels;  // a pixel layer's; null: empty
};
struct DeleteLayers {
  std::vector<LayerId> layers;
};
struct DuplicateLayers {  // each copy right above its layer, sharing its tiles
  std::vector<LayerId> layers;
};
struct MoveLayer {  // reorders, or moves into another group: `index` counts without the layer
  LayerId layer = 0, parent = 0;
  int index = 0;
};
struct GroupLayers {  // into a new group where the topmost of them was
  std::vector<LayerId> layers;
  LayerProps props;
};
struct Ungroup {  // the group's children take its place
  LayerId group = 0;
};
struct SetProps {
  LayerId layer = 0;
  LayerProps props;
};
struct SetPixels {  // a pixel layer's new tiles (pen-up, a filter); to_json() leaves the pixels out
  LayerId layer = 0;
  std::shared_ptr<const TileGrid> pixels;
};
struct SelectLayers {
  std::vector<LayerId> layers;
};
using Command = std::variant<AddLayer, DeleteLayers, DuplicateLayers, MoveLayer, GroupLayers, Ungroup, SetProps,
                            SetPixels, SelectLayers>;

// The state after cmd. Throws std::invalid_argument (doc stays as it was) for a layer that is not there, an index out
// of range, or what would break an invariant: ids unique, Pass Through on groups only, pixels in pixel_format(doc), no
// group inside itself, the root kept, the rest of what validate() checks. Any thread.
DocPtr run(const DocState& doc, const Command& cmd);
std::string to_json(const Command&);

// Throws std::invalid_argument unless d keeps what run() keeps: a canvas of at least a pixel, a known mode and depth;
// the root a group of id 0 without pixels; layer ids unique, from 1 to below next_id; Pass Through on groups only,
// opacity and fill 0..1, a known blend and kind; a pixel layer's pixels in pixel_format(d), transparent where empty,
// no layers in it; no pixels on a group; the selection of layers there, once each. For a state not made by run(), as
// M2 builds one from a file. Any thread.
void validate(const DocState& d);

}  // namespace pb
