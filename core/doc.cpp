// The document model (doc.h): layer properties <-> JSON, and commands that make a
// new state sharing every layer they leave alone.
#include "doc.h"

#include <nlohmann/json.hpp>

#include <algorithm>
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

// LayerProps' numbers and switches by JSON key, in the order to_json() writes them.
struct FloatKey {
  const char* key;
  float LayerProps::*field;
};
constexpr FloatKey kFloats[] = {{"opacity", &LayerProps::opacity}, {"fill", &LayerProps::fill}};
struct BoolKey {
  const char* key;
  bool LayerProps::*field;
};
constexpr BoolKey kBools[] = {{"visible", &LayerProps::visible},
                              {"lockTransparency", &LayerProps::lock_transparency},
                              {"lockPixels", &LayerProps::lock_pixels},
                              {"lockPosition", &LayerProps::lock_position},
                              {"lockAll", &LayerProps::lock_all}};

std::string dump(const nlohmann::json& j) { return j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace); }

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
  if (p.extra.size() > 2) s << ',' << std::string_view(p.extra).substr(1, p.extra.size() - 2);  // its members
  s << '}';
  return s.str();
}

namespace {

// Numbers are clamped to 0..1; extra gets the keys it does not know, members sorted, so what to_json() wrote reads
// back and writes the same bytes.
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
    if (f != std::end(kFloats)) {
      if (!v.is_number()) return std::nullopt;
      p.*f->field = float(std::clamp(v.get<double>(), 0., 1.));
    } else if (b != std::end(kBools)) {
      if (!v.is_boolean()) return std::nullopt;
      p.*b->field = v.get<bool>();
    } else if (key == "name") {
      if (!v.is_string()) return std::nullopt;
      p.name = v.get<std::string>();
    } else if (key == "blend") {
      const std::string name = v.is_string() ? v.get<std::string>() : "";
      const auto n = std::find(std::begin(kBlendNames), std::end(kBlendNames), name);
      if (n == std::end(kBlendNames)) return std::nullopt;
      p.blend = Blend(n - std::begin(kBlendNames));
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
    if (changed) now = std::make_shared<const Layer>(Layer{l->id, l->kind, l->props, l->pixels, std::move(kids)});
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
  Layers kids = g->children;
  kids.insert(kids.begin() + std::ptrdiff_t(at), layers.begin(), layers.end());
  return std::make_shared<const Layer>(Layer{g->id, g->kind, g->props, g->pixels, std::move(kids)});
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

const LayerProps& checked(const LayerProps& p, Kind kind) {
  if (size_t(p.blend) >= std::size(kBlendNames)) throw bad("no such blend mode");
  if (p.blend == Blend::PassThrough && kind != Kind::Group) throw bad("Pass Through is for groups");
  if (!(p.opacity >= 0 && p.opacity <= 1 && p.fill >= 0 && p.fill <= 1)) throw bad("opacity and fill go 0..1");
  return p;
}

void check_pixels(const DocState& d, const TileGrid& g) {
  const Format f = pixel_format(d);
  if (g.format() != f || g.fill()[size_t(f.channels - 1)] != 0)
    throw bad("pixels not in the document's format, or not transparent where empty");
}
// A pixel layer's grid in d: g, checked, or an empty one for null.
std::shared_ptr<const TileGrid> layer_pixels(const DocState& d, std::shared_ptr<const TileGrid> g) {
  if (!g) return std::make_shared<const TileGrid>(pixel_format(d));
  check_pixels(d, *g);
  return g;
}

// l and everything in it again, under new ids from `next` on (each before what it holds), sharing their pixels.
LayerPtr copy_layer(const Layer& l, LayerId& next) {
  auto c = std::make_shared<Layer>();
  c->id = next++;
  c->kind = l.kind;
  c->props = l.props;
  c->pixels = l.pixels;
  for (const LayerPtr& k : l.children) c->children.push_back(copy_layer(*k, next));
  return c;
}

// Each command: checks against d, then writes the new state into n (a copy of d), which is dropped if it throws.
void perform(const DocState& d, DocState& n, const AddLayer& c) {
  const Layer& parent = need_group(d, c.parent);
  if (c.index < 0 || size_t(c.index) > parent.children.size()) throw bad("no index " + std::to_string(c.index));
  auto l = std::make_shared<Layer>();
  l->kind = c.kind;
  l->props = checked(c.props, c.kind);
  if (c.kind == Kind::Pixel)
    l->pixels = layer_pixels(d, c.pixels);
  else if (c.pixels)
    throw bad("a group has no pixels");
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
  const LayerProps& p = checked(c.props, (*need(d, c.layer).layer)->kind);
  n.root = changed(d, c.layer, [&](Layer& l) { l.props = p; });
}

void perform(const DocState& d, DocState& n, const SetPixels& c) {
  if ((*need(d, c.layer).layer)->kind != Kind::Pixel) throw bad("layer " + std::to_string(c.layer) + " is a group");
  const auto pixels = layer_pixels(d, c.pixels);
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
  auto next = std::make_shared<DocState>(doc);
  std::visit([&](const auto& c) { perform(doc, *next, c); }, cmd);
  if (next->next_id < doc.next_id) throw bad("out of layer ids");  // wrapped: only a damaged file's next_id gets there
  return next;
}

void validate(const DocState& d) {
  if (d.width < 1 || d.height < 1) throw bad("a canvas without pixels");
  if ((d.mode != Mode::Rgb && d.mode != Mode::Gray) || (d.depth != Depth::U8 && d.depth != Depth::U16))
    throw bad("no such mode or depth");
  if (!d.root || d.root->id != 0 || d.root->kind != Kind::Group || d.root->pixels) throw bad("no root group");
  if (d.next_id == 0) throw bad("next_id 0, the root's");
  std::vector<LayerId> ids;
  const std::function<void(const Layer&)> walk = [&](const Layer& g) {
    for (const LayerPtr& l : g.children) {
      if (!l) throw bad("a null layer");
      if (l->id == 0 || l->id >= d.next_id) throw bad("layer id " + std::to_string(l->id) + " not in 1 to next_id");
      checked(l->props, l->kind);
      if (l->kind == Kind::Pixel) {
        if (!l->pixels) throw bad("pixel layer " + std::to_string(l->id) + " without pixels");
        check_pixels(d, *l->pixels);
        if (!l->children.empty()) throw bad("layers inside pixel layer " + std::to_string(l->id));
      } else if (l->kind != Kind::Group || l->pixels) {
        throw bad("layer " + std::to_string(l->id) + " neither a pixel layer nor a group");
      }
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
}

std::string to_json(const Command& cmd) {
  std::ostringstream s;
  s.imbue(std::locale::classic());
  const auto ids = [&](const std::vector<LayerId>& v) {
    s << ",\"layers\":[";
    for (size_t i = 0; i < v.size(); i++) s << (i ? "," : "") << v[i];
    s << ']';
  };
  std::visit(
      [&](const auto& c) {
        using T = std::decay_t<decltype(c)>;
        if constexpr (std::is_same_v<T, AddLayer>) {
          s << "{\"op\":\"addLayer\",\"parent\":" << c.parent << ",\"index\":" << c.index << ",\"kind\":\""
            << (c.kind == Kind::Group ? "group" : "pixel") << "\",\"props\":" << to_json(c.props);
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
        } else {
          static_assert(std::is_same_v<T, SelectLayers>, "a command to_json() does not write");
          s << "{\"op\":\"selectLayers\"";
          ids(c.layers);
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
bool ids(const nlohmann::json& j, std::vector<LayerId>& to) {
  const auto it = j.find("layers");
  if (it == j.end() || !it->is_array()) return false;
  to.clear();
  for (const auto& v : *it) {
    if (!v.is_number_unsigned() || v.get<uint64_t>() > std::numeric_limits<LayerId>::max()) return false;
    to.push_back(LayerId(v.get<uint64_t>()));
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

}  // namespace

std::optional<Command> command_from_json(const std::string& text) {
  using nlohmann::json;
  const json j = json::parse(text, nullptr, false);
  const auto op = j.is_object() ? j.find("op") : j.end();
  if (op == j.end() || !op->is_string()) return std::nullopt;
  const std::string& name = op->get_ref<const std::string&>();
  if (name == "addLayer") {
    AddLayer c;
    const auto kind = j.find("kind");
    if (kind == j.end() || !kind->is_string() || !(*kind == "pixel" || *kind == "group")) return std::nullopt;
    c.kind = *kind == "group" ? Kind::Group : Kind::Pixel;
    if (integer(j, "parent", c.parent) && integer(j, "index", c.index) && props(j, c.props)) return c;
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
  }
  return std::nullopt;  // setPixels too: its JSON lacks the pixels
}

}  // namespace pb
