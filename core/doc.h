#pragma once
// The document: an immutable tree of layers over the tile store. A command takes a state and gives a new one that
// shares everything it left alone, so the history is a list of states and undo swaps a pointer. Declared here,
// implemented by T1 (doc.cpp); A30 adds P1's kinds of layer, masks and the props the composite reads from B34 on; A40
// the selection, alpha channels, guides, resolution, the Background, metadata and the blocks of a file kept as read.
#include <array>
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

// A JSON object as text, shared: copying props copies no text, which can run to megabytes (a 3D LUT's). Members
// sorted, no spaces, as props_from_json() writes it; null for none.
using JsonPtr = std::shared_ptr<const std::string>;

// Blend If (Layer Style, Blending Options) on one channel: This Layer's slider and Underlying Layer's, each {black
// left, black right, white left, white right} of 0..255 in that order. The layer shows where the value lies from black
// right to white left, fading out across each split half (left = right: a hard edge), nothing past them.
struct BlendRange {
  std::array<uint8_t, 4> layer{0, 0, 255, 255}, under{0, 0, 255, 255};
};
inline constexpr const char* kBlendIfNames[] = {"gray", "red", "green", "blue"};  // LayerProps::blend_if's, in order

// Advanced Blending's Channels: those the layer changes, the others keeping what is below. An RGB document reads r, g,
// b, a grey one gray.
struct Channels {
  bool r = true, g = true, b = true, gray = true;
};

// The Layers panel's colour labels (and the New Layer dialog's Color), as Photoshop's; by value as PSD's `lclr`.
enum class Label : uint8_t { None, Red, Orange, Yellow, Green, Blue, Violet, Gray };
inline constexpr const char* kLabelNames[] = {"none",  "red",    "orange", "yellow",
                                              "green", "blue",   "violet", "gray"};  // JSON, by value
static_assert(std::size(kLabelNames) == size_t(Label::Gray) + 1);

// What the Layers panel and the layer's dialogs set on a layer. JSON: the members' names in camelCase, as the UI's
// (ui/qml/Mock.js's PROPS, LayerStyleDialog, AdjustSpec), and the schema version; keys it does not know go to `extra`
// and are written back as they were. The core keeps P1's (A30) and composites them from B34 (mask, clip, Blend If,
// Channels), D40 (adjustment and fill layers) and L34 (fx) on.
struct LayerProps {
  std::string name;
  bool visible = true;
  float opacity = 1;  // 0..1
  float fill = 1;     // 0..1: opacity of the pixels without the layer style (P1)
  Blend blend = Blend::Normal;
  // Photoshop's four locks: kept, not enforced before P1.
  bool lock_transparency = false, lock_pixels = false, lock_position = false, lock_all = false;
  bool clip = false;  // a clipping mask onto the layer below; none below in its group: no effect, as in Photoshop
  // Layer::mask's, as Properties sets it: off (Shift click), moving with the layer (the chain), Density 0..1, Feather
  // 0..1000 px.
  bool mask_off = false, mask_linked = true;
  float mask_density = 1, mask_feather = 0;
  // The Background (A40): the bottom layer of the root, a pixel layer in Normal at full opacity and fill without a
  // mask, so one at most; nothing goes below it and it does not move. Its pixels are opaque, as the commands that
  // make it and paint it keep (T31, B35): validate() checks the structure only.
  bool background = false;
  uint32_t link = 0;          // the linked layers' group (the chain): layers of one number move together; 0 for none
  Label color = Label::None;  // the colour label (JSON "color"; a fill layer's colour is in `content`)
  std::array<BlendRange, 4> blend_if{};  // by channel, as kBlendIfNames: gray (the composite's), red, green, blue
  Channels channels;
  std::string adj;  // an adjustment layer's adjustment, a name of AdjustSpec.ORDER ("Curves"); "" for none
  JsonPtr vals;     // its values off their defaults, by AdjustSpec's keys (D31)
  JsonPtr fx;       // the layer style: effects by LayerStyleDialog's keys (L30)
  JsonPtr content;  // a fill layer's Solid Color or Gradient (D40)
  JsonPtr source;   // a text or shape layer's: what its pixels are drawn from (Y30, Y32); the core does not read it
  std::string extra;  // unknown keys: a JSON object as written back, "" for none
};
inline constexpr int kLayerSchemaVersion = 1;  // bump only when a key changes meaning, then migrate older ones
std::string to_json(const LayerProps&);
std::optional<LayerProps> props_from_json(const std::string&);  // nullopt when malformed or of a newer schema

// Pixel, text and shape layers hold pixels, which text and shape layers have drawn from props.source (Y30, Y32): the
// core composites them as a pixel layer's. A group holds layers. Adjustment and fill layers hold neither: their props
// say what they are (adj and vals, content). Any of them may have a mask; the root has none.
enum class Kind : uint8_t { Pixel, Group, Adj, Fill, Text, Shape };
inline constexpr const char* kKindNames[] = {"pixel", "group", "adj", "fill", "text", "shape"};  // JSON, by value
static_assert(std::size(kKindNames) == size_t(Kind::Shape) + 1);
inline bool has_pixels(Kind k) { return k == Kind::Pixel || k == Kind::Text || k == Kind::Shape; }

// Blocks of a file's format that the app does not understand, kept so that a writer of that format writes them back as
// they were (plan.md: PSD's blocks it cannot show are kept): a document's (PSD's image resources) or a layer's (its
// tagged blocks). Its readers fill it (M40), its writers read it (M41); nothing else looks inside.
struct KeptBlock {
  std::string format;  // whose: "psd", "tiff", "png", "jpeg"...
  std::string key;     // its key there, as its reader names it: a PSD image resource's id ("1057"), a tagged block's
  std::shared_ptr<const std::vector<uint8_t>> bytes;  // as in the file; copies of the document share them
};
struct Kept {
  std::vector<KeptBlock> blocks;  // in the file's order
  // A layer's pixels as read. Once the layer holds another grid (painted, moved, transformed) or it is gone, a writer
  // drops the blocks drawn from the pixels (a PSD's text and shapes): it compares pointers, so a grid that only went
  // through a copy of the layer counts as the same.
  std::weak_ptr<const TileGrid> pixels;
};

// A node of the tree, shared by every state that has it; never changed once made.
struct Layer {
  LayerId id = 0;
  Kind kind = Kind::Pixel;
  LayerProps props;
  std::shared_ptr<const TileGrid> pixels;  // has_pixels(kind)'s: pixel_format() of its document, fill transparent
  std::vector<std::shared_ptr<const Layer>> children;  // a group's, bottom first (the order they composite in)
  // The layer mask: mask_format() of its document, white (Reveal All) or black (Hide All) where empty; null for none.
  // The layer's alpha times 1 - maskDensity x (1 - m), from B34 on.
  std::shared_ptr<const TileGrid> mask;
  // Its file's blocks not understood; null for none. Commands keep it, copies of the layer share it; ReplaceLayer
  // sets what it is given.
  std::shared_ptr<const Kept> kept;
};
using LayerPtr = std::shared_ptr<const Layer>;

// An alpha channel's Channel Options: how it shows over the image when visible.
struct ChannelProps {
  std::string name;                         // "Alpha 1"
  std::array<uint8_t, 3> color{255, 0, 0};  // the overlay's, sRGB: Photoshop's red
  float opacity = 0.5f;                     // the overlay's, 0..1
  bool masked = true;  // Color Indicates: Masked Areas, the colour where it is 0, as Photoshop's default; else Selected
  bool visible = false;  // its eye in the Channels panel
};
// Unique among a document's channels, from 1 to below its next_id: a new channel takes the id a new layer would, so
// an id never comes back once its channel is deleted (a job or step aimed at it finds nothing).
using ChannelId = uint32_t;
// An alpha channel: a saved selection (S37), painted on as one (S36).
struct Channel {
  ChannelId id = 0;
  ChannelProps props;
  std::shared_ptr<const TileGrid> grid;  // as DocState::selection's, never null: an empty grid is black
};

// A ruler guide: a vertical line at x = position, or a horizontal one at y = position, in document pixels.
struct Guide {
  bool vertical = true;
  double position = 0;
};

// The metadata of the file a document came from, for writers and Export As to write out again (M33-M35, M40, M48).
struct Meta {
  std::vector<uint8_t> exif;  // a TIFF block from its byte order mark on, without JPEG's "Exif\0\0"; empty for none
  std::string xmp;            // the XMP packet, UTF-8
  std::vector<uint8_t> iptc;  // IPTC-IIM records
};

// One state of a document. Immutable once shared: the UI thread keeps the current one and the history; jobs (a
// frame, a stroke, a save) get a DocPtr and read it on any thread while the UI goes on.
struct DocState {
  int width = 0, height = 0;  // the canvas, pixels; layers may hold tiles past it
  Mode mode = Mode::Rgb;
  Depth depth = Depth::U8;                          // 8 or 16 bits
  std::shared_ptr<const std::vector<uint8_t>> icc;  // the document's ICC profile, shared by its states
  LayerPtr root;                  // a group of id 0 holding the layers; its props are unused
  std::vector<LayerId> selected;  // the Layers panel's selection; painting needs one pixel layer
  LayerId next_id = 1;            // the next new layer's or alpha channel's id: one counter, no id given twice
  // P1 (A40). Until M31 a .pbd keeps none of these: saving one loses them.
  double ppi = 72;  // pixels per inch, as Image Size's Resolution: one value (New's default); a file's horizontal one
  // What is selected (Edit, Select): mask_format(), 0 (not selected) up to the depth's maximum (selected), 0 where
  // empty, no tile wholly past the canvas; what an edge tile holds past the canvas means nothing (Select All's solid
  // tiles), so readers stop at the canvas. Null when nothing is.
  std::shared_ptr<const TileGrid> selection;
  std::shared_ptr<const TileGrid> reselect;  // the selection the last Deselect dropped, for Reselect; null for none
  std::vector<Channel> channels;             // the alpha channels, as the Channels panel lists them below RGB's
  std::vector<Guide> guides;
  std::shared_ptr<const Meta> meta;  // null for none; commands keep both
  std::shared_ptr<const Kept> kept;  // the document's blocks of its file not understood (its `pixels` unused)
};
using DocPtr = std::shared_ptr<const DocState>;

// The pixels of a document's layers: RGB or grey, and alpha, at its depth.
inline Format pixel_format(const DocState& d) { return {d.depth, d.mode == Mode::Rgb ? 4 : 2}; }
// Its masks, selection and alpha channels: a value without alpha, at its depth.
inline Format mask_format(const DocState& d) { return {d.depth, 1}; }

// ChannelProps <-> JSON: {"name":..,"color":"#ff0000","opacity":0.5,"masked":true,"visible":false}, every key and in
// that order; reading takes them in any order, missing ones at their default, unknown ones skipped; the opacity is
// clamped to 0..1. nullopt when malformed or of the wrong type. For commands' JSON and M31's .pbd.
std::string to_json(const ChannelProps&);
std::optional<ChannelProps> channel_props_from_json(const std::string&);

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
  std::shared_ptr<const TileGrid> pixels;  // a pixel, text or shape layer's; null: empty. None for other kinds.
  std::shared_ptr<const TileGrid> mask;    // its mask; null: none
};
struct DeleteLayers {
  std::vector<LayerId> layers;
};
// Each copy right above its layer, sharing its tiles, mask, props' JSON and kept blocks (a copy of a PSD's text layer
// is one too, as in Photoshop); a copy of the Background is a layer as others.
struct DuplicateLayers {
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
  // What made them, as a JSON object of the operation's name and parameters for Actions (P2) to replay, as the task
  // that runs it writes it ({"op":"gaussianBlur","radius":5}); null for none. to_json() writes it as given.
  JsonPtr action;
};
struct SelectLayers {
  std::vector<LayerId> layers;
};
struct SetMask {  // adds, replaces or (null) deletes a layer's mask; to_json() leaves the grid out
  LayerId layer = 0;
  std::shared_ptr<const TileGrid> mask;
};
// Another layer in the place and id of `layer`, as given (Rasterize, a text layer drawn again): nothing else of the
// old one stays, a group's layers neither. Not a group. to_json() leaves pixels and mask out.
struct ReplaceLayer {
  LayerId layer = 0;
  Kind kind = Kind::Pixel;
  LayerProps props;
  std::shared_ptr<const TileGrid> pixels;  // as AddLayer's
  std::shared_ptr<const TileGrid> mask;
  // Its file's blocks: the old layer's for a text layer drawn again (a writer drops those drawn from the old pixels),
  // none or some for Rasterize (a pixel layer has no text block).
  std::shared_ptr<const Kept> kept;
};
// The selection, alpha channels, guides and resolution (A40). Grids are as DocState::selection's; a command's step
// name is Photoshop's ("Rectangular Marquee", "Save Selection"), given to the history with it.
struct SetSelection {  // a new selection; null: Deselect, which keeps the one it drops as `reselect`
  std::shared_ptr<const TileGrid> selection;
};
struct AddChannel {  // at `index` of channels, under next_id, as a new layer takes it; a null grid: black
  int index = 0;
  ChannelProps props;
  std::shared_ptr<const TileGrid> grid;
};
struct DeleteChannels {
  std::vector<ChannelId> channels;
};
struct SetChannel {  // a channel's props (as given), and its grid unless null
  ChannelId channel = 0;
  ChannelProps props;
  std::shared_ptr<const TileGrid> grid;
};
struct MoveChannel {  // reorders: `index` counts without the channel
  ChannelId channel = 0;
  int index = 0;
};
struct SetGuides {  // all of them, as given
  std::vector<Guide> guides;
};
struct SetResolution {  // the ppi alone, no pixel resampled: Image Size with Resample off
  double ppi = 72;
};
// The state `to` that a job computed from `from` (Image Size, Merge, Crop with an angle, Convert to Profile: heavy
// operations, run in a Background job while the UI takes no command on the document): to_json() leaves both out.
struct SetState {
  DocPtr from, to;
  JsonPtr action;  // as SetPixels'
};
using Command = std::variant<AddLayer, DeleteLayers, DuplicateLayers, MoveLayer, GroupLayers, Ungroup, SetProps,
                            SetPixels, SelectLayers, SetMask, ReplaceLayer, SetSelection, AddChannel, DeleteChannels,
                            SetChannel, MoveChannel, SetGuides, SetResolution, SetState>;

// The state after cmd. Throws std::invalid_argument (doc stays as it was) for a layer or channel that is not there, an
// index out of range, or what would break an invariant: ids unique, Pass Through on groups only, pixels in
// pixel_format(doc) and masks in mask_format(doc), no group inside itself, the root kept, the Background where it is,
// the rest of what validate() checks. A copy of the Background (DuplicateLayers) is no Background. SetState gives `to`
// itself, and throws unless doc is `from` itself (nothing ran on the document since the job took it) and `to` passes
// validate() and gives no id again. Any thread.
DocPtr run(const DocState& doc, const Command& cmd);
std::string to_json(const Command&);
// The command to_json() wrote, read back: the same op, layers, ids, indexes, kinds and props, so
// command_from_json(to_json(c)) writes the JSON c does. Keys it does not use are skipped. nullopt for JSON that is
// malformed, an unknown op, a field missing or of the wrong type (ids and indexes are integers in range, props as
// props_from_json() reads them), and for commands that carry grids or states, which their JSON leaves out: SetPixels,
// SetMask with a mask, ReplaceLayer, SetSelection with a selection, AddChannel and SetChannel with a grid, SetState.
// AddLayer comes back without pixels or mask (an empty layer), as its JSON says nothing of them. For Actions (P2) and
// the UI's commands (DocModel, N1). Any thread.
std::optional<Command> command_from_json(const std::string&);

// Throws std::invalid_argument unless d keeps what run() keeps: a canvas of at least a pixel, a known mode and depth,
// a resolution above 0; the root a group of id 0 without pixels or mask; layer ids unique, from 1 to below next_id; a
// known kind, Pass Through on groups only, opacity, fill and mask density 0..1, feather 0..1000, Blend If's sliders in
// order, props' JSON objects whole (a clip on the bottom layer of a group stands, without effect); a pixel, text or
// shape layer's pixels in pixel_format(d), transparent where empty; no pixels on other kinds, no layers but in groups;
// masks in mask_format(d), white or black where empty; the Background only at the bottom of the root, a pixel layer in
// Normal at full opacity and fill without a mask; the selection of layers there, once each; the selection, Reselect's
// and the channels' grids in mask_format(d), 0 where empty, no tile past the canvas; channel ids unique, from 1 to
// below next_id, channel opacity 0..1; guides at finite positions. For a state not made by run(), as M2 builds one
// from a file. Any thread.
void validate(const DocState& d);

}  // namespace pb
