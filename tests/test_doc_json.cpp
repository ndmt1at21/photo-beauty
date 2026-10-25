// Commands read back from their JSON (doc.h: command_from_json()): every command without grids writes, read back, the
// JSON it wrote, its props byte for byte with P1's keys and unknown keys, layers of every kind, A40's commands of the
// selection, channels, guides and resolution; SetPixels, SetMask with a mask, ReplaceLayer, SetSelection with one,
// AddChannel and SetChannel with a grid, SetState and what is not a command give nullopt; a command read back runs to
// the same state.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <random>
#include <string>
#include <variant>
#include <vector>

#include "check.h"
#include "doc.h"

using namespace pb;

namespace {

// c's JSON read back writes the same JSON, as a command of the same kind.
void round_trip(const Command& c) {
  const std::string json = to_json(c);
  const std::optional<Command> back = command_from_json(json);
  if (!back || back->index() != c.index() || to_json(*back) != json) {
    std::fprintf(stderr, "not read back: %s\n", json.c_str());
    CHECK(false);
  }
}

LayerProps props_of(const std::string& json) {
  const std::optional<LayerProps> p = props_from_json(json);
  CHECK(p);
  return *p;
}

std::string random_name(std::mt19937& r) {
  static const char* const parts[] = {"Layer", " ", "bản sao", "\"", "\\", "\n", "\t", "/", "ảnh", "🙂", "{", "}", ",", "x"};
  std::string s;
  for (int n = int(r() % 6); n > 0; n--) s += parts[r() % std::size(parts)];
  return s;
}

LayerProps random_props(std::mt19937& r, bool group) {
  LayerProps p;
  p.name = random_name(r);
  p.visible = r() % 2;
  p.opacity = float(r() % 1001) / 1000;
  p.fill = std::uniform_real_distribution<float>(0, 1)(r);
  p.blend = Blend(1 + r() % (std::size(kBlendNames) - 1));
  if (group && r() % 2) p.blend = Blend::PassThrough;
  p.lock_transparency = r() % 2, p.lock_pixels = r() % 2, p.lock_position = r() % 2, p.lock_all = r() % 2;
  p.clip = r() % 2, p.mask_off = r() % 2, p.mask_linked = r() % 2;
  p.mask_density = std::uniform_real_distribution<float>(0, 1)(r), p.mask_feather = float(r() % 1001);
  if (r() % 2) p.blend_if[r() % 4] = {{0, uint8_t(r() % 100), uint8_t(100 + r() % 100), 255}, {9, 9, 9, 9}};
  p.channels.g = r() % 2;
  if (r() % 2) p.adj = random_name(r);
  if (r() % 2) p.vals = std::make_shared<const std::string>(R"x({"pre":"Lighter (RGB)","s":-30})x");
  if (r() % 2) p.fx = std::make_shared<const std::string>(R"( {"stroke": {"on": true, "size": 6}} )");  // as the UI may
  if (r() % 2) p.extra = R"x({"mask":null,"tag":"#e8664a"})x";
  p.background = !group && r() % 4 == 0;
  p.link = r() % 2 ? 0 : r() % 3 ? r() % 100 : std::numeric_limits<uint32_t>::max();
  p.color = Label(r() % std::size(kLabelNames));
  return props_of(to_json(p));  // as reading leaves them: members sorted, no spaces
}

ChannelProps random_channel(std::mt19937& r) {
  ChannelProps p;
  p.name = random_name(r);
  p.color = {uint8_t(r()), uint8_t(r()), uint8_t(r())};
  p.opacity = r() % 3 ? float(r() % 101) / 100 : std::uniform_real_distribution<float>(0, 1)(r);
  p.masked = r() % 2, p.visible = r() % 2;
  return p;
}

std::vector<Guide> random_guides(std::mt19937& r) {
  std::vector<Guide> v(r() % 5);
  for (Guide& g : v) {
    g.vertical = r() % 2;
    g.position = r() % 2 ? double(int(r() % 20000) - 5000) / 8 : std::uniform_real_distribution<double>(-1e6, 1e6)(r);
  }
  return v;
}

std::vector<LayerId> random_ids(std::mt19937& r) {
  std::vector<LayerId> v(r() % 5);
  for (LayerId& id : v) id = r() % 3 ? LayerId(r() % 2000) : std::numeric_limits<LayerId>::max() - LayerId(r() % 3);
  return v;
}

}  // namespace

TEST(test_doc_json) {
  // Each kind of command, edge values included.
  const LayerProps plain;
  const LayerProps full = props_of(
      R"({"version":1,"name":"Tách tần số \"Cao\"","blend":"linearLight","opacity":0.3,"fill":0.75,"visible":false,)"
      R"("lockTransparency":true,"lockPixels":true,"lockPosition":true,"lockAll":true,"clip":true,"fx":{"dropShadow":{"on":true}}})");
  const LayerProps p1 = props_of(
      R"({"version":1,"name":"Curves 1","maskDensity":0.5,"maskFeather":12.5,"clip":true,"maskOff":true,)"
      R"("maskLinked":false,"blendIf":{"gray":{"layer":[0,10,200,255],"under":[0,0,255,255]}},)"
      R"x("channels":{"r":false},"adj":"Curves","vals":{"pre":"Medium Contrast (RGB)"},"tag":"#e8664a"})x");
  const std::vector<Command> some = {
      AddLayer{0, 0, Kind::Pixel, plain, nullptr},
      AddLayer{7, 3, Kind::Group, full, nullptr},
      AddLayer{0, 1, Kind::Adj, p1, nullptr},
      AddLayer{0, 1, Kind::Fill, plain, nullptr},
      AddLayer{2, 0, Kind::Text, full, nullptr},
      AddLayer{2, 0, Kind::Shape, p1, nullptr},
      SetProps{4, p1},
      SetMask{3, nullptr},
      SetMask{std::numeric_limits<LayerId>::max(), nullptr},
      AddLayer{std::numeric_limits<LayerId>::max(), -1, Kind::Pixel, full, nullptr},  // run() refuses; JSON keeps it
      DeleteLayers{{}},
      DeleteLayers{{1, 2, 3}},
      DuplicateLayers{{9}},
      MoveLayer{4, 0, 2},
      MoveLayer{4, 12, std::numeric_limits<int>::max()},
      GroupLayers{{2, 5, 3}, full},
      GroupLayers{{}, plain},
      Ungroup{12},
      SetProps{3, full},
      SetProps{3, plain},
      SelectLayers{{}},
      SelectLayers{{5, 1, 5}},
      SetSelection{},
      AddChannel{0, ChannelProps{}, nullptr},
      AddChannel{-3, ChannelProps{"Kênh \"1\"\n🙂", {255, 0, 128}, 0.3f, false, true}, nullptr},
      DeleteChannels{{}},
      DeleteChannels{{1, 2, std::numeric_limits<ChannelId>::max()}},
      SetChannel{3, ChannelProps{"", {0, 0, 0}, 0, true, true}, nullptr},
      MoveChannel{2, 0},
      MoveChannel{std::numeric_limits<ChannelId>::max(), std::numeric_limits<int>::min()},
      SetGuides{{}},
      SetGuides{{{true, 0.1}, {false, -1e10}, {true, 1e-300}, {false, 0}}},
      SetResolution{72},
      SetResolution{299.99},
      SetResolution{-1},  // run() refuses; JSON keeps it
  };
  for (const Command& c : some) round_trip(c);

  // Random props and ids: names with quotes, escapes, Vietnamese, emoji; any float 0..1; unknown keys.
  std::mt19937 r(20261024);
  for (int i = 0; i < 2000; i++) {
    const bool group = r() % 2;
    switch (r() % 14) {
      case 0: {
        const Kind kind = group ? Kind::Group : Kind(r() % 2 ? 0 : 2 + r() % 4);  // pixel, or adj, fill, text, shape
        round_trip(AddLayer{LayerId(r() % 100), int(r() % 50), kind, random_props(r, group), nullptr});
        break;
      }
      case 1: round_trip(DeleteLayers{random_ids(r)}); break;
      case 2: round_trip(DuplicateLayers{random_ids(r)}); break;
      case 3: round_trip(MoveLayer{LayerId(r()), LayerId(r() % 100), int(r() % 1000) - 10}); break;
      case 4: round_trip(GroupLayers{random_ids(r), random_props(r, true)}); break;
      case 5: round_trip(Ungroup{LayerId(r())}); break;
      case 6: round_trip(SetProps{LayerId(r() % 100), random_props(r, group)}); break;
      case 7: round_trip(SetMask{LayerId(r()), nullptr}); break;
      case 8: round_trip(SelectLayers{random_ids(r)}); break;
      case 9: round_trip(r() % 2 ? Command(SetSelection{}) : Command(SetGuides{random_guides(r)})); break;
      case 10: round_trip(AddChannel{int(r() % 10) - 1, random_channel(r), nullptr}); break;
      case 11: round_trip(SetChannel{ChannelId(r()), random_channel(r), nullptr}); break;
      case 12:
        round_trip(r() % 2 ? Command(DeleteChannels{random_ids(r)}) : MoveChannel{ChannelId(r() % 9), int(r() % 9)});
        break;
      default:
        round_trip(SetResolution{r() % 2 ? double(r() % 1200) : std::uniform_real_distribution<double>(0, 1e4)(r)});
        break;
    }
  }

  // Grids are not in the JSON: SetPixels, SetMask with a mask, ReplaceLayer do not come back; AddLayer comes back
  // without pixels or mask.
  const DocPtr d0 = new_document(600, 400, Mode::Rgb, Depth::U8, nullptr);
  auto grid = std::make_shared<TileGrid>(pixel_format(*d0));
  grid->set(0, 0, Tile::solid(pixel_format(*d0), {10, 20, 30, 255}));
  const auto mask = std::make_shared<const TileGrid>(mask_format(*d0), Pixel{255});
  CHECK(!command_from_json(to_json(SetPixels{1, grid})));
  CHECK(!command_from_json(R"({"op":"setPixels","layer":1})"));
  CHECK(!command_from_json(to_json(SetMask{1, mask})));
  CHECK(!command_from_json(to_json(ReplaceLayer{1, Kind::Pixel, plain, grid, mask})));
  CHECK(!command_from_json(to_json(ReplaceLayer{1, Kind::Fill, p1, nullptr, nullptr})));
  const auto area = std::make_shared<const TileGrid>(mask_format(*d0));
  CHECK(!command_from_json(to_json(SetSelection{area})));
  CHECK(!command_from_json(to_json(AddChannel{0, {}, area})));
  CHECK(!command_from_json(to_json(SetChannel{1, {}, area})));
  CHECK(!command_from_json(to_json(SetState{d0, d0})));
  CHECK(!command_from_json(to_json(SetState{d0, d0, std::make_shared<const std::string>(R"({"op":"imageSize"})")})));
  CHECK(!command_from_json(to_json(SetPixels{1, grid, std::make_shared<const std::string>(R"({"op":"fill"})")})));
  {
    const std::optional<Command> a = command_from_json(to_json(AddLayer{0, 0, Kind::Text, plain, grid, mask}));
    CHECK(a && std::holds_alternative<AddLayer>(*a) && !std::get<AddLayer>(*a).pixels && !std::get<AddLayer>(*a).mask);
    CHECK(std::get<AddLayer>(*a).kind == Kind::Text);
  }

  // A command read back runs to the same state as the one that wrote it.
  {
    DocPtr d = run(*d0, AddLayer{0, 0, Kind::Pixel, props_of(R"({"version":1,"name":"Background","background":true})"), grid});
    d = run(*d, AddLayer{0, 1, Kind::Group, props_of(R"({"version":1,"name":"Group 1","blend":"passThrough"})"), nullptr});
    const std::vector<Command> steps = {
        AddLayer{2, 0, Kind::Pixel, full, nullptr}, DuplicateLayers{{3}}, MoveLayer{4, 0, 1}, SetProps{1, full},
        GroupLayers{{3, 4}, plain}, SelectLayers{{1, 5}}, Ungroup{5}, DeleteLayers{{2}},
        AddLayer{0, 0, Kind::Adj, p1, nullptr}, AddLayer{0, 2, Kind::Shape, p1, nullptr}, SetProps{6, full},
        SetMask{1, nullptr}};
    DocPtr a = d, b = d;
    for (const Command& c : steps) {
      a = run(*a, c);
      b = run(*b, *command_from_json(to_json(c)));
      CHECK(a->next_id == b->next_id && a->selected == b->selected);
    }
    // the same tree: ids, kinds, props, the same tiles
    std::vector<std::string> ta, tb;
    const auto walk = [](const Layer& g, std::vector<std::string>& out, const auto& self) -> void {
      for (const LayerPtr& l : g.children) {
        out.push_back(std::to_string(l->id) + kKindNames[size_t(l->kind)] + to_json(l->props) +
                      (l->pixels ? std::to_string(l->pixels->tiles().size()) : "-"));
        self(*l, out, self);
        out.emplace_back("}");
      }
    };
    walk(*a->root, ta, walk);
    walk(*b->root, tb, walk);
    CHECK(ta == tb && !ta.empty());
    // A40's: the same channels (ids, props, black grids), guides, resolution, no selection.
    const ChannelId c0 = a->next_id;  // the channels' ids: c0, c0 + 1, c0 + 2
    const std::vector<Command> more = {
        SetSelection{}, AddChannel{0, random_channel(r), nullptr}, AddChannel{1, random_channel(r), nullptr},
        AddChannel{0, random_channel(r), nullptr}, SetChannel{c0, random_channel(r), nullptr}, MoveChannel{c0 + 1, 0},
        DeleteChannels{{c0 + 2}}, SetGuides{random_guides(r)}, SetResolution{300}, SetSelection{}};
    for (const Command& c : more) {
      a = run(*a, c);
      b = run(*b, *command_from_json(to_json(c)));
      CHECK(a->next_id == b->next_id);
    }
    const auto areas = [](const DocState& s) {
      std::string out = std::to_string(s.ppi) + (s.selection ? "s" : "-") + (s.reselect ? "r" : "-");
      for (const Channel& c : s.channels)
        out += std::to_string(c.id) + to_json(c.props) + std::to_string(c.grid->tiles().size());
      for (const Guide& g : s.guides) out += (g.vertical ? "x" : "y") + std::to_string(g.position);
      return out;
    };
    CHECK(areas(*a) == areas(*b) && a->channels.size() == 2 && a->ppi == 300);
  }

  // Not commands: nullopt, never a throw.
  const char* const bad[] = {
      "", "{", "[]", "null", "3", R"("addLayer")", "{}", R"({"op":3})", R"({"op":"nope","layers":[]})",
      R"({"op":"AddLayer","parent":0,"index":0,"kind":"pixel","props":{"version":1}})",          // ops are camelCase
      R"({"op":"addLayer","parent":0,"index":0,"kind":"so","props":{"version":1}})",             // no such kind (yet)
      R"({"op":"addLayer","parent":0,"index":0,"kind":"Text","props":{"version":1}})",           // kinds are lower case
      R"({"op":"addLayer","parent":0,"index":0,"kind":2,"props":{"version":1}})",
      R"({"op":"addLayer","parent":0,"index":0,"props":{"version":1}})",                         // no kind
      R"({"op":"addLayer","parent":0,"kind":"pixel","props":{"version":1}})",                    // no index
      R"({"op":"addLayer","parent":0,"index":0,"kind":"pixel"})",                                // no props
      R"({"op":"addLayer","parent":0,"index":0,"kind":"pixel","props":{"name":"x"}})",           // props without version
      R"({"op":"addLayer","parent":0,"index":0,"kind":"pixel","props":{"version":2}})",          // a newer schema
      R"({"op":"addLayer","parent":0,"index":0,"kind":"pixel","props":{"version":1,"blend":"x"}})",
      R"({"op":"addLayer","parent":0,"index":0,"kind":"pixel","props":{"version":1,"opacity":"1"}})",
      R"({"op":"addLayer","parent":-1,"index":0,"kind":"pixel","props":{"version":1}})",         // ids from 0
      R"({"op":"addLayer","parent":4294967296,"index":0,"kind":"pixel","props":{"version":1}})",  // past 32 bits
      R"({"op":"addLayer","parent":0,"index":2147483648,"kind":"pixel","props":{"version":1}})",  // past int
      R"({"op":"addLayer","parent":0.5,"index":0,"kind":"pixel","props":{"version":1}})",
      R"({"op":"addLayer","parent":"0","index":0,"kind":"pixel","props":{"version":1}})",
      R"({"op":"deleteLayers"})", R"({"op":"deleteLayers","layers":3})", R"({"op":"deleteLayers","layers":[1,-2]})",
      R"({"op":"deleteLayers","layers":[1,2.5]})", R"({"op":"deleteLayers","layers":["1"]})",
      R"({"op":"duplicateLayers","layers":[4294967296]})", R"({"op":"moveLayer","layer":1,"parent":0})",
      R"({"op":"groupLayers","layers":[1]})", R"({"op":"ungroup"})", R"({"op":"ungroup","group":true})",
      R"({"op":"setProps","layer":1})", R"({"op":"setProps","props":{"version":1}})", R"({"op":"selectLayers"})",
      R"({"op":"setProps","layer":1,"props":{"version":1,"blendIf":{"gray":[0,255,0,255]}}})",
      R"({"op":"setMask","layer":1,"mask":true})", R"({"op":"setMask","layer":1})", R"({"op":"setMask","mask":false})",
      R"({"op":"setMask","layer":1,"mask":0})", R"({"op":"setMask","layer":1,"mask":null})",
      R"({"op":"replaceLayer","layer":1,"kind":"pixel","props":{"version":1}})",
      // A40's: grids and states are not in the JSON; fields missing, of the wrong type or out of range.
      R"({"op":"setSelection"})", R"({"op":"setSelection","selection":true})", R"({"op":"setSelection","selection":0})",
      R"({"op":"setSelection","selection":null})", R"({"op":"SetSelection","selection":false})",
      R"({"op":"addChannel","index":0,"props":{}})", R"({"op":"addChannel","index":0,"props":{},"grid":true})",
      R"({"op":"addChannel","props":{},"grid":false})", R"({"op":"addChannel","index":0,"grid":false})",
      R"({"op":"addChannel","index":0.5,"props":{},"grid":false})",
      R"({"op":"addChannel","index":0,"props":[],"grid":false})",
      R"({"op":"addChannel","index":0,"props":{"name":1},"grid":false})",
      R"({"op":"addChannel","index":0,"props":{"color":"red"},"grid":false})",
      R"({"op":"addChannel","index":0,"props":{"color":"#12345"},"grid":false})",
      R"({"op":"addChannel","index":0,"props":{"color":"#1234567"},"grid":false})",
      R"({"op":"addChannel","index":0,"props":{"color":"#gg0000"},"grid":false})",
      R"({"op":"addChannel","index":0,"props":{"color":[255,0,0]},"grid":false})",
      R"({"op":"addChannel","index":0,"props":{"opacity":"1"},"grid":false})",
      R"({"op":"addChannel","index":0,"props":{"masked":1},"grid":false})",
      R"({"op":"addChannel","index":0,"props":{"visible":null},"grid":false})",
      R"({"op":"deleteChannels"})", R"({"op":"deleteChannels","channels":[-1]})",
      R"({"op":"deleteChannels","channels":[4294967296]})", R"({"op":"deleteChannels","layers":[1]})",
      R"({"op":"setChannel","channel":1,"props":{},"grid":true})", R"({"op":"setChannel","props":{},"grid":false})",
      R"({"op":"setChannel","channel":-1,"props":{},"grid":false})", R"({"op":"setChannel","channel":1,"grid":false})",
      R"({"op":"moveChannel","channel":1})", R"({"op":"moveChannel","index":0})",
      R"({"op":"moveChannel","channel":1,"index":"0"})", R"({"op":"setGuides"})", R"({"op":"setGuides","guides":{}})",
      R"({"op":"setGuides","guides":[{"x":1,"y":2}]})", R"({"op":"setGuides","guides":[{}]})",
      R"({"op":"setGuides","guides":[{"x":"1"}]})", R"({"op":"setGuides","guides":[1]})",
      R"({"op":"setGuides","guides":[{"z":1}]})", R"({"op":"setResolution"})", R"({"op":"setResolution","ppi":"72"})",
      R"({"op":"setResolution","ppi":null})", R"({"op":"setState"})",
      R"({"op":"setState","action":{"op":"imageSize"}})", R"({"op":"setPixels","layer":1,"action":{}})",
  };
  for (const char* b : bad)
    if (command_from_json(b)) {
      std::fprintf(stderr, "read as a command: %s\n", b);
      CHECK(false);
    }
  // Keys it does not use are skipped; the order of keys does not matter.
  const std::optional<Command> u = command_from_json(R"({"group":3,"why":"Bỏ nhóm","op":"ungroup"})");
  CHECK(u && std::holds_alternative<Ungroup>(*u) && std::get<Ungroup>(*u).group == 3);
  const std::optional<Command> s = command_from_json(R"({"props":{"name":"x","version":1},"layer":2,"op":"setProps"})");
  CHECK(s && std::get<SetProps>(*s).layer == 2 && std::get<SetProps>(*s).props.name == "x");
  // Channel props: any order, missing keys at their default, unknown ones skipped, either case of hex, the opacity
  // clamped; guides at whole numbers too.
  const std::optional<Command> c = command_from_json(
      R"({"op":"addChannel","grid":false,"index":2,"props":{"zz":[1],"visible":true,"color":"#FFa0C1","opacity":7}})");
  CHECK(c && std::holds_alternative<AddChannel>(*c) && std::get<AddChannel>(*c).index == 2);
  const ChannelProps& cp = std::get<AddChannel>(*c).props;
  CHECK(cp.name.empty() && cp.visible && cp.masked && cp.opacity == 1);
  CHECK(cp.color == (std::array<uint8_t, 3>{255, 160, 193}));
  CHECK(to_json(*c) ==
        R"({"op":"addChannel","index":2,"props":{"name":"","color":"#ffa0c1","opacity":1,"masked":true,)"
        R"("visible":true},"grid":false})");
  CHECK(channel_props_from_json(R"({"opacity":-2})")->opacity == 0 && !channel_props_from_json("{") &&
        !channel_props_from_json("[]") && channel_props_from_json("{}"));
  const std::optional<Command> g = command_from_json(R"({"op":"setGuides","guides":[{"y":3},{"x":-2,"why":1}]})");
  CHECK(g && to_json(*g) == R"({"op":"setGuides","guides":[{"y":3.0},{"x":-2.0}]})");
  const std::optional<Command> ppi = command_from_json(R"({"ppi":300,"op":"setResolution"})");
  CHECK(ppi && std::get<SetResolution>(*ppi).ppi == 300 && to_json(*ppi) == R"({"op":"setResolution","ppi":300.0})");
  const std::optional<Command> drop = command_from_json(R"({"op":"setSelection","selection":false,"why":"Ctrl D"})");
  CHECK(drop && std::holds_alternative<SetSelection>(*drop) && !std::get<SetSelection>(*drop).selection);
}
