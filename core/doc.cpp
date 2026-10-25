// The document model (doc.h): layer properties <-> JSON, and commands that make a
// new state sharing every layer they leave alone.
#include "doc.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <locale>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <type_traits>

#include "profile.h"

namespace pb {
namespace {

// LayerProps' numbers and switches by JSON key, in the order to_json() writes them; numbers read clamped to 0..max.
struct FloatKey {
  const char* key;
  float LayerProps::*field;
  float max;
};
constexpr float kMaxFeather = 1000;  // px, as Properties' Feather
constexpr FloatKey kFloats[] = {{"opacity", &LayerProps::opacity, 1},
                                {"fill", &LayerProps::fill, 1},
                                {"maskDensity", &LayerProps::mask_density, 1},
                                {"maskFeather", &LayerProps::mask_feather, kMaxFeather}};
struct BoolKey {
  const char* key;
  bool LayerProps::*field;
};
constexpr BoolKey kBools[] = {{"visible", &LayerProps::visible},
                              {"lockTransparency", &LayerProps::lock_transparency},
                              {"lockPixels", &LayerProps::lock_pixels},
                              {"lockPosition", &LayerProps::lock_position},
                              {"lockAll", &LayerProps::lock_all},
                              {"clip", &LayerProps::clip},
                              {"maskOff", &LayerProps::mask_off},
                              {"maskLinked", &LayerProps::mask_linked},
                              {"background", &LayerProps::background}};
// Channels' check boxes by JSON key, as LayerStyleDialog names them.
struct ChannelKey {
  const char* key;
  bool Channels::*field;
};
constexpr ChannelKey kChannels[] = {
    {"r", &Channels::r}, {"g", &Channels::g}, {"b", &Channels::b}, {"gray", &Channels::gray}};
// The JSON objects kept as text.
struct ObjectKey {
  const char* key;
  JsonPtr LayerProps::*field;
};
constexpr ObjectKey kObjects[] = {{"vals", &LayerProps::vals},
                                  {"fx", &LayerProps::fx},
                                  {"content", &LayerProps::content},
                                  {"source", &LayerProps::source}};

std::string dump(const nlohmann::json& j) { return j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace); }

bool ordered(const std::array<uint8_t, 4>& v) { return v[0] <= v[1] && v[1] <= v[2] && v[2] <= v[3]; }
bool is_default(const BlendRange& r) {
  static const BlendRange none;
  return r.layer == none.layer && r.under == none.under;
}

}  // namespace

// Keys at their default are left out: a key added later changes no JSON of layers without it.
std::string to_json(const LayerProps& p) {
  static const LayerProps def;
  std::ostringstream s;
  s.imbue(std::locale::classic());  // '.' decimals whatever the UI locale (vi_VN uses ',')
  s.precision(9);                   // enough to round-trip any float
  s << "{\"version\":" << kLayerSchemaVersion;
  if (p.name != def.name) s << ",\"name\":" << dump(p.name);
  if (p.blend != def.blend) s << ",\"blend\":\"" << kBlendNames[size_t(p.blend)] << '"';
  for (const FloatKey& k : kFloats)
    if (p.*k.field != def.*k.field) s << ",\"" << k.key << "\":" << p.*k.field;
  for (const BoolKey& k : kBools)
    if (p.*k.field != def.*k.field) s << ",\"" << k.key << "\":" << (p.*k.field ? "true" : "false");
  if (p.link != def.link) s << ",\"link\":" << p.link;
  if (p.color != def.color)  // "" for none known, which props_from_json() refuses
    s << ",\"color\":\"" << (size_t(p.color) < std::size(kLabelNames) ? kLabelNames[size_t(p.color)] : "") << '"';
  // Blend If: the channels off their default, both sliders each; Channels: the boxes off.
  const auto slider = [&](const char* key, const std::array<uint8_t, 4>& v) {
    s << '"' << key << "\":[" << int(v[0]) << ',' << int(v[1]) << ',' << int(v[2]) << ',' << int(v[3]) << ']';
  };
  bool any = false;
  for (size_t c = 0; c < p.blend_if.size(); c++) {
    if (is_default(p.blend_if[c])) continue;
    s << (any ? "," : ",\"blendIf\":{") << '"' << kBlendIfNames[c] << "\":{";
    slider("layer", p.blend_if[c].layer);
    s << ',';
    slider("under", p.blend_if[c].under);
    s << '}';
    any = true;
  }
  if (any) s << '}';
  any = false;
  for (const ChannelKey& k : kChannels)
    if (!(p.channels.*k.field)) s << (any ? "," : ",\"channels\":{") << '"' << k.key << "\":false", any = true;
  if (any) s << '}';
  if (!p.adj.empty()) s << ",\"adj\":" << dump(p.adj);
  for (const ObjectKey& k : kObjects)
    if (p.*k.field) s << ",\"" << k.key << "\":" << *(p.*k.field);
  if (p.extra.size() > 2) s << ',' << std::string_view(p.extra).substr(1, p.extra.size() - 2);  // its members
  s << '}';
  return s.str();
}

namespace {

// Blend If as LayerStyleDialog writes it, {"gray": {"layer": [4 numbers], "under": [...]}, ...}, a channel or slider
// left out at its default: the numbers rounded to 0..255, each slider's in order.
bool blend_if_from(const nlohmann::json& v, std::array<BlendRange, 4>& to) {
  if (v.is_null()) return true;
  if (!v.is_object()) return false;
  for (const auto& [name, channel] : v.items()) {
    const auto c = std::find(std::begin(kBlendIfNames), std::end(kBlendIfNames), name);
    if (c == std::end(kBlendIfNames) || !channel.is_object()) return false;
    BlendRange& r = to[size_t(c - std::begin(kBlendIfNames))];
    for (const auto& [which, a] : channel.items()) {
      if ((which != "layer" && which != "under") || !a.is_array() || a.size() != 4) return false;
      std::array<uint8_t, 4>& out = which == "layer" ? r.layer : r.under;
      for (size_t i = 0; i < 4; i++) {
        if (!a[i].is_number()) return false;
        out[i] = uint8_t(std::lround(std::clamp(a[i].get<double>(), 0., 255.)));
      }
      if (!ordered(out)) return false;
    }
  }
  return true;
}
bool channels_from(const nlohmann::json& v, Channels& to) {
  if (v.is_null()) return true;
  if (!v.is_object()) return false;
  for (const auto& [name, on] : v.items()) {
    const auto k =
        std::find_if(std::begin(kChannels), std::end(kChannels), [&](const ChannelKey& c) { return name == c.key; });
    if (k == std::end(kChannels) || !on.is_boolean()) return false;
    to.*k->field = on.get<bool>();
  }
  return true;
}

// Numbers are clamped to their range; null for one of P1's keys that takes an object or a name is its default, as the
// UI's mock writes them. extra gets the keys it does not know, members sorted, as do the JSON objects kept as text,
// so what to_json() wrote reads back and writes the same bytes.
std::optional<LayerProps> props_from(const nlohmann::json& j) {
  using nlohmann::json;
  const auto version = j.is_object() ? j.find("version") : j.end();
  if (!j.is_object() || version == j.end() || !version->is_number() ||
      !(*version >= 1 && *version <= kLayerSchemaVersion))
    return std::nullopt;
  LayerProps p;
  json extra = json::object();
  for (const auto& [key, v] : j.items()) {
    const auto is = [&](const auto& k) { return key == k.key; };
    const auto f = std::find_if(std::begin(kFloats), std::end(kFloats), is);
    const auto b = std::find_if(std::begin(kBools), std::end(kBools), is);
    const auto o = std::find_if(std::begin(kObjects), std::end(kObjects), is);
    if (f != std::end(kFloats)) {
      if (!v.is_number()) return std::nullopt;
      p.*f->field = float(std::clamp(v.get<double>(), 0., double(f->max)));
    } else if (b != std::end(kBools)) {
      if (!v.is_boolean()) return std::nullopt;
      p.*b->field = v.get<bool>();
    } else if (o != std::end(kObjects)) {
      if (!v.is_null() && !v.is_object()) return std::nullopt;
      p.*o->field = v.is_null() ? nullptr : std::make_shared<const std::string>(dump(v));
    } else if (key == "name") {
      if (!v.is_string()) return std::nullopt;
      p.name = v.get<std::string>();
    } else if (key == "blend") {
      const std::string name = v.is_string() ? v.get<std::string>() : "";
      const auto n = std::find(std::begin(kBlendNames), std::end(kBlendNames), name);
      if (n == std::end(kBlendNames)) return std::nullopt;
      p.blend = Blend(n - std::begin(kBlendNames));
    } else if (key == "link") {
      if (!v.is_number_unsigned() || v.get<uint64_t>() > std::numeric_limits<uint32_t>::max()) return std::nullopt;
      p.link = uint32_t(v.get<uint64_t>());
    } else if (key == "color") {  // null: none, as the mock writes it
      const std::string name = v.is_null() ? "none" : v.is_string() ? v.get<std::string>() : "";
      const auto n = std::find(std::begin(kLabelNames), std::end(kLabelNames), name);
      if (n == std::end(kLabelNames)) return std::nullopt;
      p.color = Label(n - std::begin(kLabelNames));
    } else if (key == "blendIf") {
      if (!blend_if_from(v, p.blend_if)) return std::nullopt;
    } else if (key == "channels") {
      if (!channels_from(v, p.channels)) return std::nullopt;
    } else if (key == "adj") {
      if (!v.is_null() && !v.is_string()) return std::nullopt;
      p.adj = v.is_null() ? std::string() : v.get<std::string>();
    } else if (key != "version") {
      extra[key] = v;
    }
  }
  if (!extra.empty()) p.extra = dump(extra);
  return p;
}

}  // namespace

std::optional<LayerProps> props_from_json(const std::string& text) {
  return props_from(nlohmann::json::parse(text, nullptr, false));  // discarded, not an object, when malformed
}

std::string to_json(const ChannelProps& p) {
  std::ostringstream s;
  s.imbue(std::locale::classic());
  s.precision(9);
  char color[8];
  std::snprintf(color, sizeof color, "#%02x%02x%02x", p.color[0], p.color[1], p.color[2]);
  s << "{\"name\":" << dump(p.name) << ",\"color\":\"" << color << "\",\"opacity\":" << p.opacity
    << ",\"masked\":" << (p.masked ? "true" : "false") << ",\"visible\":" << (p.visible ? "true" : "false") << '}';
  return s.str();
}

namespace {

// "#rrggbb", either case.
bool color_from(const std::string& s, std::array<uint8_t, 3>& to) {
  const auto digit = [](char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
  };
  if (s.size() != 7 || s[0] != '#') return false;
  for (size_t i = 0; i < 3; i++) {
    const int hi = digit(s[1 + 2 * i]), lo = digit(s[2 + 2 * i]);
    if (hi < 0 || lo < 0) return false;
    to[i] = uint8_t(hi * 16 + lo);
  }
  return true;
}

std::optional<ChannelProps> channel_props_from(const nlohmann::json& j) {
  if (!j.is_object()) return std::nullopt;
  ChannelProps p;
  for (const auto& [key, v] : j.items()) {
    if (key == "name") {
      if (!v.is_string()) return std::nullopt;
      p.name = v.get<std::string>();
    } else if (key == "color") {
      if (!v.is_string() || !color_from(v.get_ref<const std::string&>(), p.color)) return std::nullopt;
    } else if (key == "opacity") {
      if (!v.is_number()) return std::nullopt;
      p.opacity = float(std::clamp(v.get<double>(), 0., 1.));
    } else if (key == "masked" || key == "visible") {
      if (!v.is_boolean()) return std::nullopt;
      (key == "masked" ? p.masked : p.visible) = v.get<bool>();
    }
  }
  return p;
}

}  // namespace

std::optional<ChannelProps> channel_props_from_json(const std::string& text) {
  return channel_props_from(nlohmann::json::parse(text, nullptr, false));
}

namespace {

using Layers = std::vector<LayerPtr>;
// What a command does to one layer: nullopt keeps it (with what changed inside it), else the layers in its place.
using Edit = std::function<std::optional<Layers>(const LayerPtr&)>;

std::invalid_argument bad(const std::string& what) { return std::invalid_argument("pb: " + what); }

// Where layer `id` is in l's subtree (l included): its node, and its group and index there unless it is l.
struct Place {
  const LayerPtr* layer = nullptr;  // null: not there
  const Layer* parent = nullptr;
  size_t index = 0;
};
// ponytail: a walk of the tree, O(layers), fine at D4's 1000; an id index if the Layers panel finds it slow.
Place locate(const LayerPtr& l, LayerId id) {
  if (l->id == id) return {&l};
  for (size_t i = 0; i < l->children.size(); i++)
    if (Place p = locate(l->children[i], id); p.layer) {
      if (!p.parent) p.parent = l.get(), p.index = i;
      return p;
    }
  return {};
}

Place need(const DocState& d, LayerId id) {  // a layer, not the root
  if (id == 0) throw bad("the root is not a layer to change");
  const Place p = locate(d.root, id);
  if (!p.layer) throw bad("no layer " + std::to_string(id));
  return p;
}
const Layer& need_group(const DocState& d, LayerId id) {  // the root too
  const Place p = locate(d.root, id);
  if (!p.layer || (*p.layer)->kind != Kind::Group) throw bad("no group " + std::to_string(id));
  return **p.layer;
}

void collect(const Layer& l, std::vector<LayerId>& out) {  // l's id and all below
  out.push_back(l.id);
  for (const LayerPtr& c : l.children) collect(*c, out);
}

// The layers of ids, once each, but for those inside another of them (they go along with it), in the order of the
// tree: bottom first, a group before what it holds. Throws for the root or a layer not there.
Layers top_level(const DocState& d, std::vector<LayerId> ids) {
  std::sort(ids.begin(), ids.end());
  ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
  if (!ids.empty() && ids[0] == 0) throw bad("the root is not a layer to change");
  Layers out;
  size_t found = 0;
  const std::function<void(const Layer&, bool)> walk = [&](const Layer& g, bool inside) {
    for (const LayerPtr& c : g.children) {
      const bool listed = std::binary_search(ids.begin(), ids.end(), c->id);
      found += listed;
      if (listed && !inside) out.push_back(c);
      walk(*c, inside || listed);
    }
  };
  walk(*d.root, false);
  if (found != ids.size()) throw bad("a layer not there");
  return out;
}

// Appends what l becomes to out: edit applied to everything in l first and then to l, so a group's edit sees its
// children's changes. Layers edit leaves alone, and everything in them, stay shared with the old tree. True when out
// got anything but l.
bool rebuild(const LayerPtr& l, const Edit& edit, Layers& out) {
  LayerPtr now = l;
  if (!l->children.empty()) {
    Layers kids;
    kids.reserve(l->children.size());
    bool changed = false;
    for (const LayerPtr& c : l->children) changed |= rebuild(c, edit, kids);
    if (changed) {
      auto c = std::make_shared<Layer>(*l);  // the rest as it was
      c->children = std::move(kids);
      now = std::move(c);
    }
  }
  if (std::optional<Layers> e = edit(now)) {
    out.insert(out.end(), e->begin(), e->end());
    return true;
  }
  const bool changed = now != l;
  out.push_back(std::move(now));
  return changed;
}
LayerPtr rebuilt(const DocState& d, const Edit& edit) {
  Layers root;
  rebuild(d.root, edit, root);
  return root.at(0);  // commands never delete or duplicate the root
}

// g with `layers` put in among its children at index `at`.
LayerPtr inserted(const LayerPtr& g, size_t at, const Layers& layers) {
  auto c = std::make_shared<Layer>(*g);
  c->children.insert(c->children.begin() + std::ptrdiff_t(at), layers.begin(), layers.end());
  return c;
}

// The tree with a copy of layer id changed.
LayerPtr changed(const DocState& d, LayerId id, const std::function<void(Layer&)>& change) {
  return rebuilt(d, [&](const LayerPtr& l) -> std::optional<Layers> {
    if (l->id != id) return std::nullopt;
    auto c = std::make_shared<Layer>(*l);
    change(*c);
    return Layers{std::move(c)};
  });
}

// JSON text of an object, checked without building it.
bool is_object(const std::string& text) {
  const size_t at = text.find_first_not_of(" \t\n\r");
  return at != std::string::npos && text[at] == '{' && nlohmann::json::accept(text);
}

// p for a layer of `kind`. `was`: the props the layer has, whose JSON objects need no second look (a slider dragged
// on a layer with a 3D LUT parses nothing). The Background's place is its command's to check.
const LayerProps& checked(const LayerProps& p, Kind kind, const LayerProps* was = nullptr) {
  if (size_t(kind) >= std::size(kKindNames)) throw bad("no such kind of layer");
  if (size_t(p.blend) >= std::size(kBlendNames)) throw bad("no such blend mode");
  if (p.blend == Blend::PassThrough && kind != Kind::Group) throw bad("Pass Through is for groups");
  if (!(p.opacity >= 0 && p.opacity <= 1 && p.fill >= 0 && p.fill <= 1)) throw bad("opacity and fill go 0..1");
  if (!(p.mask_density >= 0 && p.mask_density <= 1 && p.mask_feather >= 0 && p.mask_feather <= kMaxFeather))
    throw bad("mask density goes 0..1, feather 0..1000 px");
  for (const BlendRange& r : p.blend_if)
    if (!ordered(r.layer) || !ordered(r.under)) throw bad("Blend If's sliders out of order");
  if (size_t(p.color) >= std::size(kLabelNames)) throw bad("no such colour label");
  if (p.background && (kind != Kind::Pixel || p.blend != Blend::Normal || p.opacity != 1 || p.fill != 1))
    throw bad("the Background is a pixel layer in Normal at full opacity and fill");
  for (const ObjectKey& k : kObjects) {
    const JsonPtr& j = p.*k.field;
    if (j && !(was && was->*k.field == j) && !is_object(*j)) throw bad(std::string(k.key) + " is not a JSON object");
  }
  return p;
}

// An operation's JSON (SetPixels, SetState): an object or none.
void check_action(const JsonPtr& action) {
  if (action && !is_object(*action)) throw bad("an action that is not a JSON object");
}

// The Background, if d has one: the bottom layer of its root.
const Layer* background_of(const DocState& d) {
  const auto& bottom = d.root->children;
  return !bottom.empty() && bottom[0]->props.background ? bottom[0].get() : nullptr;
}
// What may become the Background, a layer at place p (its props' kind, blend, opacity and fill checked) with mask m:
// the bottom layer of the root, without a mask.
void check_background(const DocState& d, const Place& p, const std::shared_ptr<const TileGrid>& m) {
  if (p.parent != d.root.get() || p.index != 0) throw bad("the Background is the bottom layer of the root");
  if (m) throw bad("the Background takes no mask");
}

// A selection's, Reselect's or an alpha channel's grid in d: one channel at its depth, 0 where empty, no tile wholly
// past the canvas.
void check_area(const DocState& d, const TileGrid& g, const char* what) {
  if (g.format() != mask_format(d) || g.fill()[0] != 0)
    throw bad(std::string(what) + " not in the document's mask format, or not 0 where empty");
  const int64_t across = (int64_t(d.width) + kTileSize - 1) >> kTileLog2;
  const int64_t down = (int64_t(d.height) + kTileSize - 1) >> kTileLog2;
  for (const auto& [xy, t] : g.tiles())
    if (xy.x < 0 || xy.y < 0 || xy.x >= across || xy.y >= down) throw bad(std::string(what) + " past the canvas");
}

const ChannelProps& checked(const ChannelProps& p) {
  if (!(p.opacity >= 0 && p.opacity <= 1)) throw bad("a channel's opacity goes 0..1");
  return p;
}
size_t channel_at(const DocState& d, ChannelId id) {  // its index in d.channels; throws when not there
  for (size_t i = 0; i < d.channels.size(); i++)
    if (d.channels[i].id == id) return i;
  throw bad("no channel " + std::to_string(id));
}

void check_pixels(const DocState& d, const TileGrid& g) {
  const Format f = pixel_format(d);
  if (g.format() != f || g.fill()[size_t(f.channels - 1)] != 0)
    throw bad("pixels not in the document's format, or not transparent where empty");
}
// A layer's pixels in d: a pixel, text or shape layer's grid g, checked, or an empty one for null; other kinds have
// none. Its kind checked first.
std::shared_ptr<const TileGrid> layer_pixels(const DocState& d, Kind kind, std::shared_ptr<const TileGrid> g) {
  if (!has_pixels(kind)) {
    if (g) throw bad(std::string("a layer of kind ") + kKindNames[size_t(kind)] + " has no pixels");
    return nullptr;
  }
  if (!g) return std::make_shared<const TileGrid>(pixel_format(d));
  check_pixels(d, *g);
  return g;
}

void check_mask(const DocState& d, const TileGrid& g) {
  const int fill = g.fill()[0];
  if (g.format() != mask_format(d) || (fill != 0 && fill != max_value(d.depth)))
    throw bad("a mask not in the document's mask format, or neither white nor black where empty");
}
std::shared_ptr<const TileGrid> layer_mask(const DocState& d, std::shared_ptr<const TileGrid> g) {
  if (g) check_mask(d, *g);
  return g;
}

// l and everything in it again, under new ids from `next` on (each before what it holds), sharing their pixels,
// masks, props' JSON and kept blocks; a copy of the Background is none.
LayerPtr copy_layer(const Layer& l, LayerId& next) {
  auto c = std::make_shared<Layer>(l);
  c->id = next++;
  c->props.background = false;
  for (LayerPtr& k : c->children) k = copy_layer(*k, next);
  return c;
}

// Each command: checks against d, then writes the new state into n (a copy of d), which is dropped if it throws.
void perform(const DocState& d, DocState& n, const AddLayer& c) {
  const Layer& parent = need_group(d, c.parent);
  if (c.index < 0 || size_t(c.index) > parent.children.size()) throw bad("no index " + std::to_string(c.index));
  if (c.parent == 0 && c.index == 0 && background_of(d)) throw bad("nothing goes below the Background");
  if (c.props.background && (c.parent != 0 || c.index != 0 || c.mask))
    throw bad("the Background goes at the bottom of the root, without a mask");
  auto l = std::make_shared<Layer>();
  l->kind = c.kind;
  l->props = checked(c.props, c.kind);
  l->pixels = layer_pixels(d, c.kind, c.pixels);
  l->mask = layer_mask(d, c.mask);
  l->id = n.next_id++;
  n.root = rebuilt(d, [&](const LayerPtr& x) -> std::optional<Layers> {
    if (x->id != c.parent) return std::nullopt;
    return Layers{inserted(x, size_t(c.index), {l})};
  });
  n.selected = {l->id};
}

void perform(const DocState& d, DocState& n, const DeleteLayers& c) {
  std::vector<LayerId> gone;  // and everything in them
  for (const LayerPtr& l : top_level(d, c.layers)) collect(*l, gone);
  std::sort(gone.begin(), gone.end());
  const auto is_gone = [&](LayerId id) { return std::binary_search(gone.begin(), gone.end(), id); };
  n.root = rebuilt(d, [&](const LayerPtr& x) -> std::optional<Layers> {
    if (!is_gone(x->id)) return std::nullopt;
    return Layers{};
  });
  n.selected.erase(std::remove_if(n.selected.begin(), n.selected.end(), is_gone), n.selected.end());
}

void perform(const DocState& d, DocState& n, const DuplicateLayers& c) {
  std::map<LayerId, LayerPtr> copies;
  n.selected.clear();
  for (const LayerPtr& l : top_level(d, c.layers)) {
    LayerPtr k = copy_layer(*l, n.next_id);
    n.selected.push_back(k->id);
    copies.emplace(l->id, std::move(k));
  }
  n.root = rebuilt(d, [&](const LayerPtr& x) -> std::optional<Layers> {
    const auto it = copies.find(x->id);
    if (it == copies.end()) return std::nullopt;
    return Layers{x, it->second};
  });
}

void perform(const DocState& d, DocState& n, const MoveLayer& c) {
  const Place l = need(d, c.layer);
  const Layer& parent = need_group(d, c.parent);
  if (locate(*l.layer, c.parent).layer) throw bad("a group inside itself");
  const size_t count = parent.children.size() - (l.parent->id == c.parent);
  if (c.index < 0 || size_t(c.index) > count) throw bad("no index " + std::to_string(c.index));
  const bool bottom = c.parent == 0 && c.index == 0;  // the Background's place: it may only stay there
  if ((*l.layer)->props.background ? !bottom : bottom && background_of(d))
    throw bad((*l.layer)->props.background ? "the Background does not move" : "nothing goes below the Background");
  const LayerPtr moved = *l.layer;
  n.root = rebuilt(d, [&](const LayerPtr& x) -> std::optional<Layers> {
    if (x->id == c.layer) return Layers{};
    if (x->id == c.parent) return Layers{inserted(x, size_t(c.index), {moved})};
    return std::nullopt;
  });
}

void perform(const DocState& d, DocState& n, const GroupLayers& c) {
  const Layers top = top_level(d, c.layers);
  if (top.empty()) throw bad("no layers to group");
  if (std::any_of(top.begin(), top.end(), [](const LayerPtr& l) { return l->props.background; }))
    throw bad("the Background goes in no group");
  auto g = std::make_shared<Layer>();
  g->kind = Kind::Group;
  g->props = checked(c.props, Kind::Group);
  g->children = top;
  g->id = n.next_id++;
  std::vector<LayerId> moved;
  for (const LayerPtr& l : top) moved.push_back(l->id);
  std::sort(moved.begin(), moved.end());
  const auto is_moved = [&](LayerId id) { return std::binary_search(moved.begin(), moved.end(), id); };
  const Place t = locate(d.root, top.back()->id);  // the topmost
  size_t at = 0;                                  // its index among the layers that stay
  for (size_t i = 0; i < t.index; i++) at += !is_moved(t.parent->children[i]->id);
  n.root = rebuilt(d, [&](const LayerPtr& x) -> std::optional<Layers> {
    if (is_moved(x->id)) return Layers{};
    if (x->id == t.parent->id) return Layers{inserted(x, at, {g})};
    return std::nullopt;
  });
  n.selected = {g->id};
}

void perform(const DocState& d, DocState& n, const Ungroup& c) {
  const LayerPtr& g = *need(d, c.group).layer;
  if (g->kind != Kind::Group) throw bad("layer " + std::to_string(c.group) + " is not a group");
  n.root = rebuilt(d, [&](const LayerPtr& x) -> std::optional<Layers> {
    if (x->id != c.group) return std::nullopt;
    return x->children;
  });
  n.selected.clear();
  for (const LayerPtr& k : g->children) n.selected.push_back(k->id);
}

void perform(const DocState& d, DocState& n, const SetProps& c) {
  const Place at = need(d, c.layer);
  const Layer& l = **at.layer;
  const LayerProps& p = checked(c.props, l.kind, &l.props);
  if (p.background) check_background(d, at, l.mask);  // Background from Layer (T31), or the Background still
  n.root = changed(d, c.layer, [&](Layer& x) { x.props = p; });
}

void perform(const DocState& d, DocState& n, const SetPixels& c) {
  if ((*need(d, c.layer).layer)->kind != Kind::Pixel)
    throw bad("layer " + std::to_string(c.layer) + " is not a pixel layer");
  const auto pixels = layer_pixels(d, Kind::Pixel, c.pixels);
  check_action(c.action);
  n.root = changed(d, c.layer, [&](Layer& l) { l.pixels = pixels; });
}

void perform(const DocState& d, DocState& n, const SelectLayers& c) {
  std::vector<LayerId> all;
  collect(*d.root, all);
  std::sort(all.begin(), all.end());
  std::vector<bool> taken(all.size());
  n.selected.clear();  // in the order given, once each
  for (const LayerId id : c.layers) {
    const auto it = std::lower_bound(all.begin(), all.end(), id);
    if (id == 0 || it == all.end() || *it != id) throw bad("no layer " + std::to_string(id) + " to select");
    if (!taken[size_t(it - all.begin())]) taken[size_t(it - all.begin())] = true, n.selected.push_back(id);
  }
}

void perform(const DocState& d, DocState& n, const SetMask& c) {
  const Place at = need(d, c.layer);
  if (c.mask && (*at.layer)->props.background) throw bad("the Background takes no mask");
  const auto mask = layer_mask(d, c.mask);
  n.root = changed(d, c.layer, [&](Layer& l) { l.mask = mask; });
}

void perform(const DocState& d, DocState& n, const ReplaceLayer& c) {
  const Place at = need(d, c.layer);
  const Layer& old = **at.layer;
  if (c.kind == Kind::Group) throw bad("ReplaceLayer makes no group");
  auto l = std::make_shared<Layer>();
  l->id = c.layer;
  l->kind = c.kind;
  l->props = checked(c.props, c.kind);
  if (c.props.background) check_background(d, at, c.mask);
  l->pixels = layer_pixels(d, c.kind, c.pixels);
  l->mask = layer_mask(d, c.mask);
  l->kept = c.kept;
  std::vector<LayerId> gone;  // a group's layers go with it
  for (const LayerPtr& k : old.children) collect(*k, gone);
  std::sort(gone.begin(), gone.end());
  n.root = rebuilt(d, [&](const LayerPtr& x) -> std::optional<Layers> {
    if (x->id != c.layer) return std::nullopt;
    return Layers{l};
  });
  const auto is_gone = [&](LayerId id) { return std::binary_search(gone.begin(), gone.end(), id); };
  n.selected.erase(std::remove_if(n.selected.begin(), n.selected.end(), is_gone), n.selected.end());
}

void perform(const DocState& d, DocState& n, const SetSelection& c) {
  if (c.selection)
    check_area(d, *c.selection, "a selection");
  else if (d.selection)
    n.reselect = d.selection;  // Deselect: for Reselect
  n.selection = c.selection;
}

void perform(const DocState& d, DocState& n, const AddChannel& c) {
  if (c.index < 0 || size_t(c.index) > d.channels.size()) throw bad("no channel index " + std::to_string(c.index));
  if (c.grid) check_area(d, *c.grid, "a channel");
  Channel added{n.next_id++, checked(c.props), c.grid ? c.grid : std::make_shared<const TileGrid>(mask_format(d))};
  n.channels.insert(n.channels.begin() + std::ptrdiff_t(c.index), std::move(added));
}

void perform(const DocState& d, DocState& n, const DeleteChannels& c) {
  std::vector<ChannelId> gone = c.channels;
  std::sort(gone.begin(), gone.end());
  gone.erase(std::unique(gone.begin(), gone.end()), gone.end());
  for (const ChannelId id : gone) channel_at(d, id);
  n.channels.erase(std::remove_if(n.channels.begin(), n.channels.end(),
                                  [&](const Channel& x) { return std::binary_search(gone.begin(), gone.end(), x.id); }),
                   n.channels.end());
}

void perform(const DocState& d, DocState& n, const SetChannel& c) {
  const size_t at = channel_at(d, c.channel);
  if (c.grid) check_area(d, *c.grid, "a channel");
  Channel& x = n.channels[at];
  x.props = checked(c.props);
  if (c.grid) x.grid = c.grid;
}

void perform(const DocState& d, DocState& n, const MoveChannel& c) {
  const size_t at = channel_at(d, c.channel);
  if (c.index < 0 || size_t(c.index) >= d.channels.size()) throw bad("no channel index " + std::to_string(c.index));
  Channel moved = std::move(n.channels[at]);
  n.channels.erase(n.channels.begin() + std::ptrdiff_t(at));
  n.channels.insert(n.channels.begin() + std::ptrdiff_t(c.index), std::move(moved));
}

void perform(const DocState&, DocState& n, const SetGuides& c) {
  for (const Guide& g : c.guides)
    if (!std::isfinite(g.position)) throw bad("a guide at no position");
  n.guides = c.guides;
}

void perform(const DocState&, DocState& n, const SetResolution& c) {
  if (!(c.ppi > 0) || !std::isfinite(c.ppi)) throw bad("a resolution not above 0");
  n.ppi = c.ppi;
}

// SetState's: `to` itself, made from doc.
DocPtr state_from(const DocState& doc, const SetState& c) {
  if (c.from.get() != &doc) throw bad("SetState from another state: the document changed since its job began");
  if (!c.to) throw bad("SetState without a state");
  check_action(c.action);
  validate(*c.to);
  if (c.to->next_id < doc.next_id) throw bad("SetState would give ids again");
  return c.to;
}

}  // namespace

DocPtr new_document(int width, int height, Mode mode, Depth depth, std::shared_ptr<const std::vector<uint8_t>> icc) {
  auto root = std::make_shared<Layer>();
  root->kind = Kind::Group;
  auto d = std::make_shared<DocState>();
  d->width = width;
  d->height = height;
  d->mode = mode;
  d->depth = depth;
  d->icc = std::move(icc);
  d->root = std::move(root);
  return d;
}

const Layer* find_layer(const DocState& d, LayerId id) {
  const Place p = d.root ? locate(d.root, id) : Place{};
  return p.layer ? p.layer->get() : nullptr;
}

DocPtr run(const DocState& doc, const Command& cmd) {
  PROFILE_ZONE("doc_run");
  if (!doc.root) throw bad("a document without its root");
  if (const auto* s = std::get_if<SetState>(&cmd)) return state_from(doc, *s);
  auto next = std::make_shared<DocState>(doc);
  std::visit(
      [&](const auto& c) {
        if constexpr (!std::is_same_v<std::decay_t<decltype(c)>, SetState>) perform(doc, *next, c);
      },
      cmd);
  if (next->next_id < doc.next_id) throw bad("out of ids");  // wrapped: only a damaged file's next_id gets there
  return next;
}

void validate(const DocState& d) {
  if (d.width < 1 || d.height < 1) throw bad("a canvas without pixels");
  if ((d.mode != Mode::Rgb && d.mode != Mode::Gray) || (d.depth != Depth::U8 && d.depth != Depth::U16))
    throw bad("no such mode or depth");
  if (!(d.ppi > 0) || !std::isfinite(d.ppi)) throw bad("a resolution not above 0");
  if (!d.root || d.root->id != 0 || d.root->kind != Kind::Group || d.root->pixels || d.root->mask)
    throw bad("no root group");
  if (d.next_id == 0) throw bad("next_id 0, the root's");
  std::vector<LayerId> ids;
  const std::function<void(const Layer&)> walk = [&](const Layer& g) {
    for (size_t i = 0; i < g.children.size(); i++) {
      const LayerPtr& l = g.children[i];
      if (!l) throw bad("a null layer");
      if (l->id == 0 || l->id >= d.next_id) throw bad("layer id " + std::to_string(l->id) + " not in 1 to next_id");
      checked(l->props, l->kind);
      if (has_pixels(l->kind)) {
        if (!l->pixels) throw bad("layer " + std::to_string(l->id) + " without its pixels");
        check_pixels(d, *l->pixels);
      } else if (l->pixels) {
        throw bad("pixels on layer " + std::to_string(l->id) + ", of kind " + kKindNames[size_t(l->kind)]);
      }
      if (l->kind != Kind::Group && !l->children.empty())
        throw bad("layers inside layer " + std::to_string(l->id) + ", not a group");
      if (l->mask) check_mask(d, *l->mask);
      if (l->props.background) check_background(d, {&l, &g, i}, l->mask);
      ids.push_back(l->id);
      walk(*l);
    }
  };
  walk(*d.root);
  std::sort(ids.begin(), ids.end());
  if (const auto twice = std::adjacent_find(ids.begin(), ids.end()); twice != ids.end())
    throw bad("layer id " + std::to_string(*twice) + " twice");
  std::vector<LayerId> selected = d.selected;
  std::sort(selected.begin(), selected.end());
  if (std::adjacent_find(selected.begin(), selected.end()) != selected.end() ||
      !std::all_of(selected.begin(), selected.end(),
                   [&](LayerId id) { return std::binary_search(ids.begin(), ids.end(), id); }))
    throw bad("a selection of layers not there, or twice");
  // P1 (A40): the selection, Reselect's, the alpha channels, the guides.
  if (d.selection) check_area(d, *d.selection, "the selection");
  if (d.reselect) check_area(d, *d.reselect, "Reselect's selection");
  std::vector<ChannelId> channels;
  for (const Channel& c : d.channels) {
    if (c.id == 0 || c.id >= d.next_id) throw bad("channel id " + std::to_string(c.id) + " not in 1 to next_id");
    if (!c.grid) throw bad("channel " + std::to_string(c.id) + " without its grid");
    check_area(d, *c.grid, "a channel");
    checked(c.props);
    channels.push_back(c.id);
  }
  std::sort(channels.begin(), channels.end());
  if (const auto twice = std::adjacent_find(channels.begin(), channels.end()); twice != channels.end())
    throw bad("channel id " + std::to_string(*twice) + " twice");
  for (const Guide& g : d.guides)
    if (!std::isfinite(g.position)) throw bad("a guide at no position");
}

std::string to_json(const Command& cmd) {
  std::ostringstream s;
  s.imbue(std::locale::classic());
  const auto list = [&](const char* key, const std::vector<uint32_t>& v) {
    s << ",\"" << key << "\":[";
    for (size_t i = 0; i < v.size(); i++) s << (i ? "," : "") << v[i];
    s << ']';
  };
  const auto ids = [&](const std::vector<LayerId>& v) { list("layers", v); };
  const auto kind = [&](Kind k) {  // "" for none known, which command_from_json() refuses
    s << ",\"kind\":\"" << (size_t(k) < std::size(kKindNames) ? kKindNames[size_t(k)] : "") << '"';
  };
  const auto action = [&](const JsonPtr& a) {
    if (a) s << ",\"action\":" << *a;
  };
  const auto number = [&](double x) { s << dump(nlohmann::json(x)); };  // the shortest that reads back the same
  const auto grid = [&](const std::shared_ptr<const TileGrid>& g) { s << ",\"grid\":" << (g ? "true" : "false"); };
  std::visit(
      [&](const auto& c) {
        using T = std::decay_t<decltype(c)>;
        if constexpr (std::is_same_v<T, AddLayer>) {
          s << "{\"op\":\"addLayer\",\"parent\":" << c.parent << ",\"index\":" << c.index;
          kind(c.kind);
          s << ",\"props\":" << to_json(c.props);
        } else if constexpr (std::is_same_v<T, DeleteLayers>) {
          s << "{\"op\":\"deleteLayers\"";
          ids(c.layers);
        } else if constexpr (std::is_same_v<T, DuplicateLayers>) {
          s << "{\"op\":\"duplicateLayers\"";
          ids(c.layers);
        } else if constexpr (std::is_same_v<T, MoveLayer>) {
          s << "{\"op\":\"moveLayer\",\"layer\":" << c.layer << ",\"parent\":" << c.parent << ",\"index\":" << c.index;
        } else if constexpr (std::is_same_v<T, GroupLayers>) {
          s << "{\"op\":\"groupLayers\"";
          ids(c.layers);
          s << ",\"props\":" << to_json(c.props);
        } else if constexpr (std::is_same_v<T, Ungroup>) {
          s << "{\"op\":\"ungroup\",\"group\":" << c.group;
        } else if constexpr (std::is_same_v<T, SetProps>) {
          s << "{\"op\":\"setProps\",\"layer\":" << c.layer << ",\"props\":" << to_json(c.props);
        } else if constexpr (std::is_same_v<T, SetPixels>) {
          s << "{\"op\":\"setPixels\",\"layer\":" << c.layer;
          action(c.action);
        } else if constexpr (std::is_same_v<T, SelectLayers>) {
          s << "{\"op\":\"selectLayers\"";
          ids(c.layers);
        } else if constexpr (std::is_same_v<T, SetMask>) {  // whether there is one: its grid is left out
          s << "{\"op\":\"setMask\",\"layer\":" << c.layer << ",\"mask\":" << (c.mask ? "true" : "false");
        } else if constexpr (std::is_same_v<T, ReplaceLayer>) {
          s << "{\"op\":\"replaceLayer\",\"layer\":" << c.layer;
          kind(c.kind);
          s << ",\"props\":" << to_json(c.props);
        } else if constexpr (std::is_same_v<T, SetSelection>) {  // as SetMask
          s << "{\"op\":\"setSelection\",\"selection\":" << (c.selection ? "true" : "false");
        } else if constexpr (std::is_same_v<T, AddChannel>) {
          s << "{\"op\":\"addChannel\",\"index\":" << c.index << ",\"props\":" << to_json(c.props);
          grid(c.grid);
        } else if constexpr (std::is_same_v<T, DeleteChannels>) {
          s << "{\"op\":\"deleteChannels\"";
          list("channels", c.channels);
        } else if constexpr (std::is_same_v<T, SetChannel>) {
          s << "{\"op\":\"setChannel\",\"channel\":" << c.channel << ",\"props\":" << to_json(c.props);
          grid(c.grid);
        } else if constexpr (std::is_same_v<T, MoveChannel>) {
          s << "{\"op\":\"moveChannel\",\"channel\":" << c.channel << ",\"index\":" << c.index;
        } else if constexpr (std::is_same_v<T, SetGuides>) {  // [{"x": a vertical one's}, {"y": a horizontal one's}]
          s << "{\"op\":\"setGuides\",\"guides\":[";
          for (size_t i = 0; i < c.guides.size(); i++) {
            s << (i ? "," : "") << (c.guides[i].vertical ? "{\"x\":" : "{\"y\":");
            number(c.guides[i].position);
            s << '}';
          }
          s << ']';
        } else if constexpr (std::is_same_v<T, SetResolution>) {
          s << "{\"op\":\"setResolution\",\"ppi\":";
          number(c.ppi);
        } else {
          static_assert(std::is_same_v<T, SetState>, "a command to_json() does not write");
          s << "{\"op\":\"setState\"";
          action(c.action);
        }
      },
      cmd);
  s << '}';
  return s.str();
}

namespace {

// JSON fields as command members: false when missing or of another type, or an integer out of the member's range.
template <class T>
bool integer(const nlohmann::json& j, const char* key, T& to) {
  const auto it = j.find(key);
  if (it == j.end() || !it->is_number_integer()) return false;
  if (it->is_number_unsigned()) {
    const auto v = it->get<uint64_t>();
    if (v > uint64_t(std::numeric_limits<T>::max())) return false;
    to = T(v);
    return true;
  }
  const auto v = it->get<int64_t>();
  if (v < int64_t(std::numeric_limits<T>::min()) || v > int64_t(std::numeric_limits<T>::max())) return false;
  to = T(v);
  return true;
}
bool ids(const nlohmann::json& j, std::vector<uint32_t>& to, const char* key = "layers") {  // layers' or channels'
  const auto it = j.find(key);
  if (it == j.end() || !it->is_array()) return false;
  to.clear();
  for (const auto& v : *it) {
    if (!v.is_number_unsigned() || v.get<uint64_t>() > std::numeric_limits<uint32_t>::max()) return false;
    to.push_back(uint32_t(v.get<uint64_t>()));
  }
  return true;
}
bool props(const nlohmann::json& j, LayerProps& to) {
  const auto it = j.find("props");
  if (it == j.end()) return false;
  std::optional<LayerProps> p = props_from(*it);
  if (!p) return false;
  to = std::move(*p);
  return true;
}
bool props(const nlohmann::json& j, ChannelProps& to) {
  const auto it = j.find("props");
  if (it == j.end()) return false;
  std::optional<ChannelProps> p = channel_props_from(*it);
  if (!p) return false;
  to = std::move(*p);
  return true;
}
bool is_false(const nlohmann::json& j, const char* key) {  // a grid left out: true for none
  const auto it = j.find(key);
  return it != j.end() && it->is_boolean() && !it->get<bool>();
}
bool guides(const nlohmann::json& j, std::vector<Guide>& to) {  // [{"x": 10.5}, {"y": 3}]
  const auto it = j.find("guides");
  if (it == j.end() || !it->is_array()) return false;
  for (const auto& g : *it) {
    const auto x = g.is_object() ? g.find("x") : g.end(), y = g.is_object() ? g.find("y") : g.end();
    const auto at = x != g.end() ? x : y;
    if ((x == g.end()) == (y == g.end()) || !at->is_number()) return false;
    to.push_back({x != g.end(), at->get<double>()});
  }
  return true;
}
bool kind(const nlohmann::json& j, Kind& to) {
  const auto it = j.find("kind");
  if (it == j.end() || !it->is_string()) return false;
  const auto n = std::find(std::begin(kKindNames), std::end(kKindNames), it->get_ref<const std::string&>());
  if (n == std::end(kKindNames)) return false;
  to = Kind(n - std::begin(kKindNames));
  return true;
}

}  // namespace

std::optional<Command> command_from_json(const std::string& text) {
  using nlohmann::json;
  const json j = json::parse(text, nullptr, false);
  const auto op = j.is_object() ? j.find("op") : j.end();
  if (op == j.end() || !op->is_string()) return std::nullopt;
  const std::string& name = op->get_ref<const std::string&>();
  if (name == "addLayer") {
    AddLayer c;
    if (kind(j, c.kind) && integer(j, "parent", c.parent) && integer(j, "index", c.index) && props(j, c.props))
      return c;
  } else if (name == "deleteLayers") {
    DeleteLayers c;
    if (ids(j, c.layers)) return c;
  } else if (name == "duplicateLayers") {
    DuplicateLayers c;
    if (ids(j, c.layers)) return c;
  } else if (name == "moveLayer") {
    MoveLayer c;
    if (integer(j, "layer", c.layer) && integer(j, "parent", c.parent) && integer(j, "index", c.index)) return c;
  } else if (name == "groupLayers") {
    GroupLayers c;
    if (ids(j, c.layers) && props(j, c.props)) return c;
  } else if (name == "ungroup") {
    Ungroup c;
    if (integer(j, "group", c.group)) return c;
  } else if (name == "setProps") {
    SetProps c;
    if (integer(j, "layer", c.layer) && props(j, c.props)) return c;
  } else if (name == "selectLayers") {
    SelectLayers c;
    if (ids(j, c.layers)) return c;
  } else if (name == "setMask") {
    SetMask c;  // the mask deleted; one set has its grid left out
    if (integer(j, "layer", c.layer) && is_false(j, "mask")) return c;
  } else if (name == "setSelection") {
    if (is_false(j, "selection")) return SetSelection{};  // Deselect; a selection has its grid left out
  } else if (name == "addChannel") {
    AddChannel c;  // a black one; a grid given is left out
    if (integer(j, "index", c.index) && props(j, c.props) && is_false(j, "grid")) return c;
  } else if (name == "deleteChannels") {
    DeleteChannels c;
    if (ids(j, c.channels, "channels")) return c;
  } else if (name == "setChannel") {
    SetChannel c;  // its props; a grid given is left out
    if (integer(j, "channel", c.channel) && props(j, c.props) && is_false(j, "grid")) return c;
  } else if (name == "moveChannel") {
    MoveChannel c;
    if (integer(j, "channel", c.channel) && integer(j, "index", c.index)) return c;
  } else if (name == "setGuides") {
    SetGuides c;
    if (guides(j, c.guides)) return c;
  } else if (name == "setResolution") {
    const auto ppi = j.find("ppi");
    if (ppi != j.end() && ppi->is_number()) return SetResolution{ppi->get<double>()};
  }
  return std::nullopt;  // setPixels, replaceLayer, setState too: their JSON lacks the grids, the states
}

}  // namespace pb
