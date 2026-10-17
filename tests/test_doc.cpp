// The document model (doc.h): layer properties <-> JSON byte for byte, unknown keys included; every command on a
// small tree and what each rejects; states left as they were; random commands keeping the invariants; the time of
// each command on a tree shaped as D4 (1000 layers, groups 6 deep).
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <iterator>
#include <locale>
#include <map>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "check.h"
#include "doc.h"

using namespace pb;

namespace {

using Ids = std::vector<LayerId>;

LayerProps named(const char* name, Blend blend = Blend::Normal) {
  LayerProps p;
  p.name = name;
  p.blend = blend;
  return p;
}

bool same(const LayerProps& a, const LayerProps& b) {
  return a.name == b.name && a.visible == b.visible && a.opacity == b.opacity && a.fill == b.fill &&
         a.blend == b.blend && a.lock_transparency == b.lock_transparency && a.lock_pixels == b.lock_pixels &&
         a.lock_position == b.lock_position && a.lock_all == b.lock_all && a.extra == b.extra;
}

// The tree as ids, bottom first, what a group holds in parentheses: "5() 1 2(4 3)".
std::string shape(const Layer& g) {
  std::string s;
  for (const LayerPtr& l : g.children) {
    if (!s.empty()) s += ' ';
    s += std::to_string(l->id);
    if (l->kind == Kind::Group) s += '(' + shape(*l) + ')';
  }
  return s;
}
std::string shape(const DocPtr& d) { return shape(*d->root); }

void collect(const Layer& l, Ids& out) {  // what l holds, l not included
  for (const LayerPtr& c : l.children) out.push_back(c->id), collect(*c, out);
}
Ids ids(const DocState& d) {  // its layers', sorted
  Ids v;
  collect(*d.root, v);
  std::sort(v.begin(), v.end());
  return v;
}

std::map<TileXY, uint64_t> tile_ids(const TileGrid& g) {
  std::map<TileXY, uint64_t> out;
  for (const auto& [xy, t] : g.tiles()) out[xy] = t->id();
  return out;
}

// Everything a state holds, down to its tiles' ids: an equal dump, an unchanged state.
void dump(std::ostream& s, const Layer& l) {
  const LayerProps& p = l.props;
  s << '[' << l.id << ' ' << int(l.kind) << ' ' << p.name << '|' << p.visible << p.lock_transparency << p.lock_pixels
    << p.lock_position << p.lock_all << ' ' << p.opacity << ' ' << p.fill << ' ' << int(p.blend) << ' ' << p.extra
    << ' ' << l.pixels.get();
  if (l.pixels)
    for (const auto& [xy, id] : tile_ids(*l.pixels)) s << ' ' << xy.x << ',' << xy.y << '=' << id;
  for (const LayerPtr& c : l.children) dump(s, *c);
  s << ']';
}
std::string dump(const DocState& d) {
  std::ostringstream s;
  s.precision(9);
  s << d.width << 'x' << d.height << ' ' << int(d.mode) << int(d.depth) << ' ' << d.icc.get() << ' ' << d.next_id
    << " {";
  for (const LayerId id : d.selected) s << ' ' << id;
  s << " } ";
  dump(s, *d.root);
  return s.str();
}

// cmd throws std::invalid_argument and leaves d as it was.
bool rejects(const DocPtr& d, const Command& cmd) {
  const std::string before = dump(*d);
  try {
    run(*d, cmd);
  } catch (const std::invalid_argument&) {
    return dump(*d) == before;
  }
  return false;
}

// How many layers of b are not a's: another node for the id, or a new id.
int unshared(const DocState& a, const DocState& b) {
  std::map<LayerId, const Layer*> was;
  const std::function<void(const Layer&)> put = [&](const Layer& l) {
    was[l.id] = &l;
    for (const LayerPtr& c : l.children) put(*c);
  };
  put(*a.root);
  int n = 0;
  const std::function<void(const Layer&)> count = [&](const Layer& l) {
    const auto it = was.find(l.id);
    n += it == was.end() || it->second != &l;
    for (const LayerPtr& c : l.children) count(*c);
  };
  count(*b.root);
  return n;
}

// A grid of f with data tiles over x -10..290, y 5..205 (three tiles), pixels from seed on.
std::shared_ptr<const TileGrid> painted(Format f, unsigned seed) {
  auto g = std::make_shared<TileGrid>(f);
  const int w = 300, h = 200;
  std::vector<uint8_t> px(size_t(w) * h * size_t(f.bytes()));
  for (size_t i = 0; i < px.size(); i++) px[i] = uint8_t(i * 7 + seed);
  g->write_rect(-10, 5, w, h, px.data(), size_t(w) * size_t(f.bytes()));
  return g;
}
// A grid of f with one solid tile: nothing to allocate.
std::shared_ptr<const TileGrid> solid(Format f, unsigned v) {
  auto g = std::make_shared<TileGrid>(f);
  const auto s = uint16_t(v % 200 + 1);
  g->set(int(v % 5) - 2, int(v % 3) - 1, Tile::solid(f, {s, s, s, s}));
  return g;
}

LayerProps every_key() {
  LayerProps p = named("Lớp 1", Blend::Overlay);
  p.visible = false;
  p.opacity = 0.37f;
  p.fill = 0.5f;
  p.lock_transparency = p.lock_pixels = p.lock_position = p.lock_all = true;
  p.extra = R"({"zz":[1,2]})";
  return p;
}

LayerProps random_props(std::mt19937& rng) {
  static const char* const kBits[] = {"a", "Z", " ", "Lớp", "ảnh", "\"", "\\", "\n", "\t", "\x01", "/", "€", "😀", "{"};
  static const char* const kExtras[] = {"", R"({"a":1})", R"({"z":null,"é":"ü"})",
                                        R"({"blendIf":{"gray":[0,255,0,255]},"style":[{"fx":"stroke","size":3.5}]})"};
  static const float kValues[] = {0, 1, 0.5f, 0.37f, 1e-40f, 0.999999f};
  const auto value = [&] {
    return rng() % 2 ? kValues[rng() % std::size(kValues)] : std::uniform_real_distribution<float>(0, 1)(rng);
  };
  LayerProps p;
  for (int n = int(rng() % 6); n > 0; n--) p.name += kBits[rng() % std::size(kBits)];
  p.visible = rng() % 2;
  p.opacity = value();
  p.fill = value();
  p.blend = Blend(rng() % std::size(kBlendNames));
  p.lock_transparency = rng() % 2, p.lock_pixels = rng() % 2, p.lock_position = rng() % 2, p.lock_all = rng() % 2;
  p.extra = kExtras[rng() % std::size(kExtras)];
  return p;
}

void check_json() {
  CHECK(to_json(LayerProps{}) == R"({"version":1})");
  // Every key off its default and unknown keys of every kind, members sorted as written back: the same bytes.
  const std::string all =
      R"({"version":1,"name":"Lớp \"1\"\n\\","blend":"linearDodge","opacity":0.5,"fill":0.300000012,"visible":false,)"
      R"("lockTransparency":true,"lockPixels":true,"lockPosition":true,"lockAll":true,)"
      R"("blendIf":{"gray":[0,255,0,255]},"style":{"fx":[{"size":3.5,"type":"stroke"}],"on":true},"zz":null})";
  const std::optional<LayerProps> p = props_from_json(all);
  CHECK(p && p->name == "Lớp \"1\"\n\\" && p->blend == Blend::LinearDodge && p->opacity == 0.5f && p->fill == 0.3f);
  CHECK(!p->visible && p->lock_transparency && p->lock_pixels && p->lock_position && p->lock_all);
  CHECK(p->extra ==
        R"({"blendIf":{"gray":[0,255,0,255]},"style":{"fx":[{"size":3.5,"type":"stroke"}],"on":true},"zz":null})");
  CHECK(to_json(*p) == all);

  // Each blend mode by its name; Normal, the default, left out.
  for (size_t b = 0; b < std::size(kBlendNames); b++) {
    const std::string text = to_json(named("", Blend(b)));
    const auto q = props_from_json(text);
    CHECK(q && q->blend == Blend(b));
    CHECK(text == (Blend(b) == Blend::Normal ? std::string(R"({"version":1})")
                                             : R"({"version":1,"blend":")" + std::string(kBlendNames[b]) + "\"}"));
  }

  // Random properties read back as they were and write the same bytes.
  std::mt19937 rng(3);
  for (int i = 0; i < 3000; i++) {
    const LayerProps q = random_props(rng);
    const std::string text = to_json(q);
    const auto r = props_from_json(text);
    CHECK(r && same(*r, q) && to_json(*r) == text);
  }

  // Keys in any order, spaced: read alike, written in order. Out of range: clamped.
  const auto spaced = props_from_json(R"( { "zz" : [1, 2], "opacity": 0.25, "version": 1, "name": "a" } )");
  CHECK(spaced && to_json(*spaced) == R"({"version":1,"name":"a","opacity":0.25,"zz":[1,2]})");
  const auto clamped = props_from_json(R"({"version":1,"opacity":2,"fill":-1e300})");
  CHECK(clamped && clamped->opacity == 1 && clamped->fill == 0);

  // Broken, or of another schema: nothing.
  for (const char* text :
       {"", "[]", "1", "{", R"({"version":1)", R"({"name":"a"})", R"({"version":0})", R"({"version":2})",
        R"({"version":"1"})", R"({"version":1,"name":1})", R"({"version":1,"visible":1})",
        R"({"version":1,"opacity":"1"})", R"({"version":1,"fill":null})", R"({"version":1,"blend":"hardmix"})",
        R"({"version":1,"blend":3})", R"({"version":1,"lockAll":"true"})"})
    CHECK(!props_from_json(text));

  // A name of broken UTF-8 still writes JSON. Decimals with '.', no thousands separators, whatever the locale.
  const auto mended = props_from_json(to_json(named("a\xff" "b")));
  CHECK(mended && mended->name == "a\xef\xbf\xbd" "b");
  struct Comma : std::numpunct<char> {
    char do_decimal_point() const override { return ','; }
    char do_thousands_sep() const override { return '.'; }
    std::string do_grouping() const override { return "\3"; }
  };
  std::locale::global(std::locale(std::locale::classic(), new Comma));
  LayerProps half;
  half.opacity = 0.5f;
  const std::string text = to_json(half) + to_json(MoveLayer{1234, 0, 1000});
  std::locale::global(std::locale::classic());
  CHECK(text == R"({"version":1,"opacity":0.5}{"op":"moveLayer","layer":1234,"parent":0,"index":1000})");

  // Commands; pixels left out.
  const std::pair<Command, std::string> cmds[] = {
      {AddLayer{2, 1, Kind::Group, named("g", Blend::PassThrough), nullptr},
       R"({"op":"addLayer","parent":2,"index":1,"kind":"group",)"
       R"("props":{"version":1,"name":"g","blend":"passThrough"}})"},
      {AddLayer{0, 0, Kind::Pixel, LayerProps{}, painted({Depth::U8, 4}, 1)},
       R"({"op":"addLayer","parent":0,"index":0,"kind":"pixel","props":{"version":1}})"},
      {DeleteLayers{{3, 4}}, R"({"op":"deleteLayers","layers":[3,4]})"},
      {DuplicateLayers{{7}}, R"({"op":"duplicateLayers","layers":[7]})"},
      {MoveLayer{3, 2, 0}, R"({"op":"moveLayer","layer":3,"parent":2,"index":0})"},
      {GroupLayers{{1, 2}, named("G")}, R"({"op":"groupLayers","layers":[1,2],"props":{"version":1,"name":"G"}})"},
      {Ungroup{5}, R"({"op":"ungroup","group":5})"},
      {SetProps{9, *p}, R"({"op":"setProps","layer":9,"props":)" + all + "}"},
      {SetPixels{9, painted({Depth::U8, 4}, 1)}, R"({"op":"setPixels","layer":9})"},
      {SelectLayers{}, R"({"op":"selectLayers","layers":[]})"},
  };
  for (const auto& [cmd, want] : cmds) CHECK(to_json(cmd) == want);
}

void check_commands() {
  const auto icc = std::make_shared<const std::vector<uint8_t>>(std::vector<uint8_t>{1, 2, 3});
  const DocPtr d0 = new_document(300, 200, Mode::Rgb, Depth::U8, icc);
  CHECK(d0->width == 300 && d0->height == 200 && d0->mode == Mode::Rgb && d0->depth == Depth::U8 && d0->icc == icc);
  CHECK(d0->root && d0->root->id == 0 && d0->root->kind == Kind::Group && !d0->root->pixels && shape(d0).empty());
  CHECK(d0->selected.empty() && d0->next_id == 1 && find_layer(*d0, 0) == d0->root.get() && !find_layer(*d0, 1));
  const std::string blank = dump(*d0);

  // Adding: ids from next_id on, the new layer selected; a pixel layer without pixels gets an empty grid.
  DocPtr d = run(*d0, AddLayer{0, 0, Kind::Pixel, named("a"), nullptr});
  const Layer* a = find_layer(*d, 1);
  CHECK(shape(d) == "1" && d->selected == Ids{1} && d->next_id == 2 && a->kind == Kind::Pixel && a->props.name == "a");
  CHECK(a->pixels && a->pixels->format() == pixel_format(*d) && a->pixels->tiles().empty() && a->children.empty());
  CHECK(dump(*d0) == blank && d->icc == icc && d->width == 300 && d->height == 200);
  const auto pixels = painted(pixel_format(*d), 1);
  d = run(*d, SetPixels{1, pixels});
  d = run(*d, AddLayer{0, 1, Kind::Group, named("g", Blend::PassThrough), nullptr});
  d = run(*d, AddLayer{2, 0, Kind::Pixel, named("b"), nullptr});
  d = run(*d, AddLayer{2, 0, Kind::Pixel, named("c"), nullptr});
  d = run(*d, AddLayer{0, 0, Kind::Group, named("h"), nullptr});
  const DocPtr base = d;
  const std::string was = dump(*base);
  CHECK(shape(base) == "5() 1 2(4 3)" && base->selected == Ids{5} && base->next_id == 6);
  CHECK(find_layer(*base, 1)->pixels == pixels && find_layer(*base, 2)->props.blend == Blend::PassThrough);
  CHECK(!find_layer(*base, 2)->pixels && !find_layer(*base, 5)->pixels && find_layer(*base, 3)->pixels);
  CHECK(shape(run(*base, AddLayer{2, 2, Kind::Pixel, named("x"), nullptr})) == "5() 1 2(4 3 6)");
  CHECK(rejects(base, AddLayer{1, 0, Kind::Pixel, named("x"), nullptr}));  // into a pixel layer
  CHECK(rejects(base, AddLayer{9, 0, Kind::Pixel, named("x"), nullptr}));
  CHECK(rejects(base, AddLayer{2, 3, Kind::Pixel, named("x"), nullptr}));
  CHECK(rejects(base, AddLayer{2, -1, Kind::Pixel, named("x"), nullptr}));
  CHECK(rejects(base, AddLayer{0, 0, Kind::Pixel, named("x", Blend::PassThrough), nullptr}));
  CHECK(rejects(base, AddLayer{0, 0, Kind::Group, named("x"), painted(pixel_format(*base), 2)}));
  CHECK(rejects(base, AddLayer{0, 0, Kind::Pixel, named("x"), painted({Depth::U16, 4}, 2)}));

  // Moving: `index` counts without the layer; it moves as it was, nothing else changes.
  const auto moved = [&](const MoveLayer& m) {
    const DocPtr r = run(*base, m);
    CHECK(r->selected == base->selected && r->next_id == base->next_id);
    return shape(r);
  };
  CHECK(moved({1, 2, 2}) == "5() 2(4 3 1)");
  CHECK(moved({4, 2, 1}) == "5() 1 2(3 4)");
  CHECK(moved({4, 2, 0}) == "5() 1 2(4 3)");
  CHECK(moved({3, 0, 0}) == "3 5() 1 2(4)");
  CHECK(moved({1, 0, 2}) == "5() 2(4 3) 1");
  CHECK(moved({2, 5, 0}) == "5(2(4 3)) 1");
  CHECK(moved({5, 0, 2}) == "1 2(4 3) 5()");
  CHECK(unshared(*base, *run(*base, MoveLayer{1, 2, 2})) == 2);  // the root and 2
  CHECK(rejects(run(*base, MoveLayer{2, 5, 0}), MoveLayer{5, 2, 0}));  // into what it holds
  CHECK(rejects(base, MoveLayer{2, 2, 0}));                            // into itself
  CHECK(rejects(base, MoveLayer{1, 4, 0}));                            // into a pixel layer
  CHECK(rejects(base, MoveLayer{0, 2, 0}) && rejects(base, MoveLayer{9, 0, 0}) && rejects(base, MoveLayer{1, 9, 0}));
  CHECK(rejects(base, MoveLayer{1, 2, 3}) && rejects(base, MoveLayer{4, 2, 2}) && rejects(base, MoveLayer{1, 0, 3}));
  CHECK(rejects(base, MoveLayer{1, 0, -1}));

  // Deleting takes along what a group holds and drops them from the selection; ids are not given again.
  const DocPtr picked = run(*base, SelectLayers{{4, 1, 5}});
  CHECK(picked->selected == (Ids{4, 1, 5}));
  DocPtr r = run(*picked, DeleteLayers{{2}});
  CHECK(shape(r) == "5() 1" && r->selected == (Ids{1, 5}) && r->next_id == 6);
  CHECK(shape(run(*r, AddLayer{0, 0, Kind::Pixel, named("x"), nullptr})) == "6 5() 1");
  CHECK(shape(run(*base, DeleteLayers{{4, 2, 4}})) == "5() 1" && shape(run(*base, DeleteLayers{{1, 5}})) == "2(4 3)");
  CHECK(shape(run(*base, DeleteLayers{{3}})) == "5() 1 2(4)" && shape(run(*base, DeleteLayers{})) == shape(base));
  CHECK(rejects(base, DeleteLayers{{0}}) && rejects(base, DeleteLayers{{1, 9}}));

  // Duplicating: each copy right above its layer, selected, under new ids, sharing the tiles (no new ones).
  const size_t raw = tile_stats().raw;
  const DocPtr dup = run(*base, DuplicateLayers{{1}});
  CHECK(tile_stats().raw == raw);
  CHECK(shape(dup) == "5() 1 6 2(4 3)" && dup->selected == Ids{6} && dup->next_id == 7);
  const Layer* copy = find_layer(*dup, 6);
  CHECK(copy->kind == Kind::Pixel && same(copy->props, find_layer(*dup, 1)->props));
  CHECK(tile_ids(*copy->pixels) == tile_ids(*pixels) && tile_ids(*pixels).size() == 3);
  r = run(*base, DuplicateLayers{{2}});
  CHECK(shape(r) == "5() 1 2(4 3) 6(7 8)" && r->selected == Ids{6} && r->next_id == 9);
  CHECK(find_layer(*r, 6)->props.blend == Blend::PassThrough && find_layer(*r, 8)->props.name == "b");
  CHECK(find_layer(*r, 7)->pixels == find_layer(*r, 4)->pixels);
  CHECK(find_layer(*r, 8)->pixels == find_layer(*r, 3)->pixels);
  r = run(*base, DuplicateLayers{{3, 1}});
  CHECK(shape(r) == "5() 1 6 2(4 3 7)" && r->selected == (Ids{6, 7}) && r->next_id == 8);
  CHECK(shape(run(*base, DuplicateLayers{{2, 3}})) == "5() 1 2(4 3) 6(7 8)");  // 3 goes along with 2
  CHECK(rejects(base, DuplicateLayers{{0}}) && rejects(base, DuplicateLayers{{9}}));

  // Grouping: into a new group, selected, where the topmost of them was; they keep their order.
  r = run(*base, GroupLayers{{1, 4}, named("G")});
  CHECK(shape(r) == "5() 2(6(1 4) 3)" && r->selected == Ids{6} && r->next_id == 7);
  CHECK(find_layer(*r, 6)->kind == Kind::Group && find_layer(*r, 6)->props.name == "G" && !find_layer(*r, 6)->pixels);
  CHECK(shape(run(*base, GroupLayers{{4, 1}, named("G")})) == "5() 2(6(1 4) 3)");
  CHECK(shape(run(*base, GroupLayers{{2, 3}, named("G")})) == "5() 1 6(2(4 3))");
  CHECK(shape(run(*base, GroupLayers{{3, 5}, named("G")})) == "1 2(4 6(5() 3))");
  CHECK(shape(run(*base, GroupLayers{{1, 5}, named("G", Blend::PassThrough)})) == "6(5() 1) 2(4 3)");
  CHECK(rejects(base, GroupLayers{{}, named("G")}) && rejects(base, GroupLayers{{0}, named("G")}));
  CHECK(rejects(base, GroupLayers{{1, 9}, named("G")}));

  // Ungrouping: what the group held takes its place, selected.
  r = run(*base, Ungroup{2});
  CHECK(shape(r) == "5() 1 4 3" && r->selected == (Ids{4, 3}) && r->next_id == 6);
  r = run(*base, Ungroup{5});
  CHECK(shape(r) == "1 2(4 3)" && r->selected.empty());
  CHECK(rejects(base, Ungroup{1}) && rejects(base, Ungroup{0}) && rejects(base, Ungroup{9}));

  // Properties, unknown keys too: a new node for the layer and its groups, the rest shared.
  r = run(*base, SetProps{3, every_key()});
  CHECK(same(find_layer(*r, 3)->props, every_key()) && find_layer(*r, 3)->pixels == find_layer(*base, 3)->pixels);
  CHECK(shape(r) == shape(base) && r->selected == base->selected && r->next_id == 6 && unshared(*base, *r) == 3);
  CHECK(run(*base, SetProps{2, named("g2")})->root->children[2]->props.name == "g2");
  CHECK(rejects(base, SetProps{3, named("x", Blend::PassThrough)}));
  CHECK(rejects(base, SetProps{3, named("x", Blend(28))}));
  CHECK(rejects(base, SetProps{0, named("x")}) && rejects(base, SetProps{9, named("x")}));
  LayerProps off = named("x");
  off.opacity = 1.5f;
  CHECK(rejects(base, SetProps{3, off}));
  off.opacity = 1;
  off.fill = std::nanf("");
  CHECK(rejects(base, SetProps{3, off}));

  // Pixels: a grid of the document's format, transparent where empty.
  const auto other = painted(pixel_format(*base), 9);
  r = run(*base, SetPixels{3, other});
  CHECK(find_layer(*r, 3)->pixels == other && same(find_layer(*r, 3)->props, find_layer(*base, 3)->props));
  CHECK(unshared(*base, *r) == 3 && r->selected == base->selected);
  r = run(*base, SetPixels{1, nullptr});
  const auto& emptied = find_layer(*r, 1)->pixels;
  CHECK(emptied && emptied->tiles().empty() && emptied->format() == pixel_format(*r));
  for (const Format f : {Format{Depth::U16, 4}, Format{Depth::U8, 2}, Format{Depth::U8, 1}})
    CHECK(rejects(base, SetPixels{3, std::make_shared<const TileGrid>(f)}));
  CHECK(rejects(base, SetPixels{3, std::make_shared<const TileGrid>(pixel_format(*base), Pixel{0, 0, 0, 255})}));
  CHECK(rejects(base, SetPixels{2, other}) && rejects(base, SetPixels{0, other}) && rejects(base, SetPixels{9, other}));

  // Selecting: once each, in the order given.
  CHECK(run(*base, SelectLayers{{3, 1, 3}})->selected == (Ids{3, 1}) && run(*base, SelectLayers{})->selected.empty());
  CHECK(rejects(base, SelectLayers{{0}}) && rejects(base, SelectLayers{{1, 9}}));

  // A grey 16-bit document takes grey and alpha at 16 bits only.
  const DocPtr grey = new_document(10, 10, Mode::Gray, Depth::U16, nullptr);
  CHECK(pixel_format(*grey) == (Format{Depth::U16, 2}));
  const DocPtr g1 = run(*grey, AddLayer{0, 0, Kind::Pixel, named("x"), painted({Depth::U16, 2}, 3)});
  CHECK(find_layer(*g1, 1)->pixels->tiles().size() == 3);
  CHECK(rejects(grey, AddLayer{0, 0, Kind::Pixel, named("x"), painted({Depth::U16, 4}, 3)}));
  CHECK(rejects(grey, AddLayer{0, 0, Kind::Pixel, named("x"), painted({Depth::U8, 2}, 3)}));
  CHECK(rejects(g1, SetPixels{1, painted({Depth::U8, 4}, 3)}));

  // A state without its root (not one new_document() made) is refused, not followed.
  bool threw = false;
  try {
    run(DocState{}, SelectLayers{});
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  CHECK(threw && !find_layer(DocState{}, 0));

  // None of that changed a state it started from.
  CHECK(dump(*base) == was && dump(*d0) == blank);
}

void check_invariants(const DocState& d) {
  const Format f = pixel_format(d);
  CHECK(d.root && d.root->id == 0 && d.root->kind == Kind::Group && !d.root->pixels);
  Ids seen;
  const std::function<void(const Layer&)> walk = [&](const Layer& g) {
    for (const LayerPtr& l : g.children) {
      const LayerProps& p = l->props;
      seen.push_back(l->id);
      CHECK(l->id != 0 && l->id < d.next_id);
      CHECK((l->kind == Kind::Group || p.blend != Blend::PassThrough) && size_t(p.blend) < std::size(kBlendNames));
      CHECK(p.opacity >= 0 && p.opacity <= 1 && p.fill >= 0 && p.fill <= 1);
      if (l->kind == Kind::Pixel)
        CHECK(l->pixels && l->pixels->format() == f && l->pixels->fill()[size_t(f.channels - 1)] == 0 &&
              l->children.empty());
      else
        CHECK(!l->pixels);
      walk(*l);
    }
  };
  walk(*d.root);
  std::sort(seen.begin(), seen.end());
  CHECK(std::adjacent_find(seen.begin(), seen.end()) == seen.end());
  Ids selected = d.selected;
  std::sort(selected.begin(), selected.end());
  CHECK(std::adjacent_find(selected.begin(), selected.end()) == selected.end());
  for (const LayerId id : selected) CHECK(std::binary_search(seen.begin(), seen.end(), id));
}

// What cmd did, given b = run(a, cmd), past the invariants.
void check_effect(const Command& cmd, const DocState& a, const DocState& b) {
  const Ids x = ids(a), y = ids(b);
  Ids added, removed;
  std::set_difference(y.begin(), y.end(), x.begin(), x.end(), std::back_inserter(added));
  std::set_difference(x.begin(), x.end(), y.begin(), y.end(), std::back_inserter(removed));
  for (const LayerId id : added) CHECK(id >= a.next_id);
  CHECK(b.next_id == a.next_id + added.size());
  CHECK(b.width == a.width && b.height == a.height && b.mode == a.mode && b.depth == a.depth && b.icc == a.icc);
  const bool kept = x == y && b.selected == a.selected;
  if (const auto* c = std::get_if<AddLayer>(&cmd)) {
    CHECK(added == Ids{a.next_id} && removed.empty() && b.selected == added);
    CHECK(find_layer(b, c->parent)->children.at(size_t(c->index))->id == a.next_id);
  } else if (const auto* c = std::get_if<DeleteLayers>(&cmd)) {
    CHECK(added.empty());
    for (const LayerId id : c->layers) CHECK(!find_layer(b, id));
  } else if (std::holds_alternative<DuplicateLayers>(cmd)) {
    CHECK(removed.empty());
  } else if (const auto* c = std::get_if<MoveLayer>(&cmd)) {
    CHECK(kept && find_layer(b, c->parent)->children.at(size_t(c->index))->id == c->layer);
  } else if (std::holds_alternative<GroupLayers>(cmd)) {
    CHECK(added == Ids{a.next_id} && removed.empty() && b.selected == added);
  } else if (const auto* c = std::get_if<Ungroup>(&cmd)) {
    CHECK(added.empty() && removed == Ids{c->group});
  } else if (const auto* c = std::get_if<SetProps>(&cmd)) {
    CHECK(kept && same(find_layer(b, c->layer)->props, c->props));
  } else if (const auto* c = std::get_if<SetPixels>(&cmd)) {
    const auto& now = find_layer(b, c->layer)->pixels;
    CHECK(kept && (c->pixels ? now == c->pixels : now->tiles().empty()));
  } else {
    CHECK(x == y);
  }
}

Command random_command(const DocState& d, std::mt19937& rng) {
  Ids all{0};
  collect(*d.root, all);
  const auto pick = [&] { return rng() % 16 ? all[rng() % all.size()] : LayerId(rng() % (d.next_id + 2)); };
  const auto some = [&] {
    Ids v(rng() % 4);
    for (LayerId& id : v) id = pick();
    return v;
  };
  const auto index = [&](LayerId g) {
    const Layer* l = find_layer(d, g);
    return int(rng() % ((l ? l->children.size() : 0) + 3)) - 1;
  };
  const auto props = [&] {
    LayerProps p = named("x", Blend(rng() % std::size(kBlendNames)));
    p.opacity = rng() % 8 ? float(rng() % 11) / 10 : 1.5f;
    p.visible = rng() % 2;
    return p;
  };
  const auto grid = [&]() -> std::shared_ptr<const TileGrid> {
    Format f = pixel_format(d);
    if (rng() % 8 == 0) f.channels = f.channels == 4 ? 2 : 4;
    return rng() % 3 ? solid(f, rng()) : nullptr;
  };
  if (all.size() > 200) return DeleteLayers{some()};
  switch (rng() % 12) {
    case 0:
    case 1:
    case 2: {
      const LayerId g = pick();
      const Kind kind = rng() % 4 ? Kind::Pixel : Kind::Group;
      return AddLayer{g, index(g), kind, props(), rng() % 2 ? grid() : nullptr};
    }
    case 3: return DeleteLayers{some()};
    case 4: return DuplicateLayers{some()};
    case 5: {
      const LayerId g = pick();
      return MoveLayer{pick(), g, index(g)};
    }
    case 6: return GroupLayers{some(), props()};
    case 7: return Ungroup{pick()};
    case 8:
    case 9: return SetProps{pick(), props()};
    case 10: return SetPixels{pick(), grid()};
    default: return SelectLayers{some()};
  }
}

// Random commands, a tenth or so refused: the invariants hold, each does what it says, no state changes after.
void check_random() {
  for (const Mode mode : {Mode::Rgb, Mode::Gray}) {
    std::mt19937 rng(mode == Mode::Rgb ? 11 : 12);
    DocPtr d = new_document(500, 400, mode, mode == Mode::Rgb ? Depth::U8 : Depth::U16, nullptr);
    std::vector<std::pair<DocPtr, std::string>> left;  // states left behind, and what they held
    int done[std::variant_size_v<Command>] = {}, rejected = 0;
    for (int i = 0; i < 3000; i++) {
      const Command cmd = random_command(*d, rng);
      const std::string before = dump(*d);
      DocPtr next;
      try {
        next = run(*d, cmd);
      } catch (const std::invalid_argument&) {
        rejected++;
      }
      CHECK(dump(*d) == before);
      if (!next) continue;
      check_invariants(*next);
      check_effect(cmd, *d, *next);
      done[cmd.index()]++;
      if (i % 64 == 0) left.emplace_back(d, before);
      d = next;
    }
    for (const auto& [s, held] : left) CHECK(dump(*s) == held);
    for (const int n : done) CHECK(n >= 20);
    CHECK(rejected >= 200);
  }
}

double median_ms(const DocState& d, const Command& cmd) {
  double t[21];
  for (double& ms : t) {
    const auto t0 = std::chrono::steady_clock::now();
    const DocPtr r = run(d, cmd);
    ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  }
  std::nth_element(t, t + 10, t + 21);
  return t[10];
}

// D4's shape: 1000 layers, groups 6 deep. Each command well within the 1 ms a command may take on the UI thread.
void check_d4() {
  std::mt19937 rng(4);
  DocPtr d = new_document(6000, 4000, Mode::Rgb, Depth::U8, nullptr);
  // A chain of groups 6 deep (ids 1 to 6), a layer in the deepest (7), then layers in random groups to 1000, one in
  // seven a group while that is no deeper than 6.
  Ids groups{0};
  std::vector<int> depth{0};
  for (int i = 1; i <= 6; i++) {
    d = run(*d, AddLayer{groups.back(), 0, Kind::Group, named("chain"), nullptr});
    groups.push_back(d->selected[0]), depth.push_back(i);
  }
  d = run(*d, AddLayer{6, 0, Kind::Pixel, named("deepest"), nullptr});
  const LayerId leaf = 7;
  while (d->next_id <= 1000) {
    const size_t g = rng() % groups.size();
    const bool group = depth[g] < 6 && rng() % 7 == 0;
    const int index = int(rng() % (find_layer(*d, groups[g])->children.size() + 1));
    d = run(*d, AddLayer{groups[g], index, group ? Kind::Group : Kind::Pixel, named("layer"), nullptr});
    if (group) groups.push_back(d->selected[0]), depth.push_back(depth[g] + 1);
  }
  const Ids all = ids(*d);
  CHECK(all.size() == 1000 && *std::max_element(depth.begin(), depth.end()) == 6);
  Ids hundred(100), fifty(50);
  for (LayerId& id : hundred) id = all[rng() % all.size()];
  for (LayerId& id : fifty) id = all[rng() % all.size()];
  const std::string before = dump(*d);
  const std::pair<const char*, Command> cmds[] = {
      {"addLayer", AddLayer{6, 0, Kind::Pixel, named("new"), nullptr}},
      {"addGroup", AddLayer{0, 0, Kind::Group, named("new"), nullptr}},
      {"delete100", DeleteLayers{hundred}},
      {"duplicateChain", DuplicateLayers{{1}}},
      {"move", MoveLayer{leaf, 0, 0}},
      {"group50", GroupLayers{fifty, named("g")}},
      {"ungroup", Ungroup{1}},
      {"setProps", SetProps{leaf, named("renamed")}},
      {"setPixels", SetPixels{leaf, painted(pixel_format(*d), 5)}},
      {"select1000", SelectLayers{all}},
  };
  std::printf("test_doc: %zu layers, groups 6 deep; ms per command (median of 21):", all.size());
  double worst = 0;
  for (const auto& [name, cmd] : cmds) {
    const double ms = median_ms(*d, cmd);
    worst = std::max(worst, ms);
    std::printf(" %s %.3f", name, ms);
  }
  std::printf("\n");
#ifdef NDEBUG  // the KPI's builds: Debug with sanitizers takes some 30 times as long (seen 0.6-2.3 ms)
  CHECK(worst <= 1);
#endif
  CHECK(dump(*d) == before);
  CHECK(unshared(*d, *run(*d, SetProps{leaf, named("x")})) == 8);  // the leaf, its 6 groups, the root
}

}  // namespace

TEST(test_doc) {
  check_json();
  check_commands();
  check_random();
  check_d4();
}
