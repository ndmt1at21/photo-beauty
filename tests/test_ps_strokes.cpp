// C41: Photoshop's images of its painting and retouching tools (tools/ps-golden/strokes.jsx, README-strokes.md). The
// inputs and paths pb-bench ps-strokes wrote into tests/golden/ps/strokes/in/ against tests/ps_strokes_inputs.h; the
// index the script writes, as the hand-made tests/golden/ps/strokes/sample/index.json has it (lines of PB.offline's,
// one of each form: read before Photoshop's images are there); and once B33 brought them into tests/golden/ps/strokes/,
// photo-beauty against them: P0's brush and eraser (dab.h, stroke.h) where a case is theirs, the statistics of the
// retouching tools' areas, and a place for each tool C30-C38 and H30-H35 bring, which says it is not there yet.
// Deviations are printed, not checked (C1's fall-off and the rest are hypotheses: docs/p1-tasks.md, "Khớp Photoshop
// bằng ảnh mẫu"); missing images are skipped.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "check.h"
#include "dab.h"
#include "golden.h"
#include "gradient.h"
#include "ps_strokes_inputs.h"
#include "render.h"
#include "stroke.h"

using namespace pb;
namespace fs = std::filesystem;
namespace in = golden::strokes_in;
using nlohmann::json;

namespace {

const fs::path kIn = in::dir(golden::kRoot);
const fs::path kSample = golden::kRoot / "strokes" / "sample";
constexpr uintmax_t kBudget = 3u << 20;  // bytes of Photoshop's images in tests/golden/ps/strokes/ (C41's share)

struct TempDir {
  fs::path path = fs::temp_directory_path() / ("pb-ps-strokes-" + std::to_string(std::random_device{}()));
  TempDir() { fs::create_directories(path); }
  ~TempDir() {
    std::error_code e;
    fs::remove_all(path, e);
  }
};

bool have_tree(const char* test) {
  if (fs::exists(kIn)) return true;
  std::printf("%s: skipped, no %s (a copy of the tree beside it)\n", test, kIn.generic_string().c_str());
  return false;
}

std::string bytes(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

// ---- index.json's paint (strokes.jsx's C.TIP and C.SPEC) -----------------------------------------------------------

// Each tool's keys besides "tool" and the task whose test compares its images: the one that brings the tool, or what
// a case of it needs (task_of()).
struct Tool {
  std::vector<std::string> keys;
  const char* task;
  const char* what;
};
const std::map<std::string, Tool>& tools() {
  static const std::map<std::string, Tool> all = [] {
    const auto tip = [](std::vector<std::string> k) {
      k.insert(k.begin(), {"size", "hardness", "spacing", "angle", "roundness"});
      return k;
    };
    return std::map<std::string, Tool>{
        {"brush", {tip({"mode", "opacity", "flow", "color", "wetEdges", "noise", "paths"}), "C33", "the Brush's modes"}},
        {"pencil", {tip({"mode", "opacity", "color", "paths"}), "C30", "the Pencil"}},
        {"eraser", {tip({"eraser", "opacity", "flow", "paths"}), "C33", "the Eraser's Pencil and Block"}},
        {"clone", {tip({"mode", "opacity", "flow", "source", "aligned", "paths", "areas"}), "H30", "the Clone Stamp"}},
        {"heal", {tip({"mode", "source", "aligned", "diffusion", "paths", "areas"}), "H32", "the Healing Brush"}},
        {"spot", {tip({"mode", "type", "paths", "areas"}), "H33", "the Spot Healing Brush"}},
        {"dodge", {tip({"range", "exposure", "protectTones", "paths"}), "H35", "Dodge"}},
        {"burn", {tip({"range", "exposure", "protectTones", "paths"}), "H35", "Burn"}},
        {"sponge", {tip({"sponge", "flow", "vibrance", "paths"}), "H35", "the Sponge"}},
        {"gradient", {{"gradient", "method", "style", "mode", "opacity", "reverse", "dither", "transparency", "line"}, "C36",
                      "the Gradient tool"}},
        {"bucket", {{"color", "mode", "opacity", "tolerance", "antiAlias", "contiguous", "allLayers", "at"}, "C38",
                    "the Paint Bucket"}}};
  }();
  return all;
}
const std::set<std::string> kModes = {"normal", "dissolve", "behind", "clear", "darken", "multiply", "colorBurn",
                                      "linearBurn", "darkerColor", "lighten", "screen", "colorDodge", "linearDodge",
                                      "lighterColor", "overlay", "softLight", "hardLight", "vividLight", "linearLight",
                                      "pinLight", "hardMix", "difference", "exclusion", "subtract", "divide", "hue",
                                      "saturation", "color", "luminosity"};  // ToolSpec's "paint"
const std::set<std::string> kHealModes = {"normal", "replace", "multiply", "screen", "darken", "lighten", "color",
                                          "luminosity"};

bool round_tip(const json& p) { return p["roundness"] == 1 && p["angle"] == 0; }
// The task an entry's image waits for: "P0" where photo-beauty paints it now (C1-C3: the Brush in Normal with a round
// tip and neither Wet Edges nor Noise; the Eraser's Brush), else the one that brings its tool or what it needs.
std::string task_of(const json& p) {
  const std::string tool = p["tool"];
  if (tool == "brush") {
    if (p["wetEdges"] || p["noise"]) return "C31";
    if (!round_tip(p)) return "C30";
    return p["mode"] == "normal" ? "P0" : "C33";
  }
  if (tool == "eraser") return p["eraser"] == "brush" && round_tip(p) ? "P0" : "C33";
  return tools().at(tool).task;
}
const char* what_of(const json& p) {
  const std::string tool = p["tool"];
  if (tool == "brush" && (p["wetEdges"] || p["noise"])) return "Wet Edges, Noise";
  if (tool == "brush" && !round_tip(p)) return "a brush tip not round";
  return tools().at(tool).what;
}

// ---- photo-beauty's images -----------------------------------------------------------------------------------------

std::array<float, 3> colour(const std::string& hex) {
  const long v = std::stol(hex.substr(1), nullptr, 16);
  return {float((v >> 16) & 255) / 255, float((v >> 8) & 255) / 255, float(v & 255) / 255};
}
// The brush of a paint as BrushParams (stroke.h): the tip, opacity, flow (the Eraser's too), colour; no pressure
// (Photoshop strokes paths without Simulate Pressure).
BrushParams brush_of(const json& p) {
  BrushParams b;
  b.size = p["size"].get<float>(), b.hardness = p["hardness"].get<float>(), b.spacing = p["spacing"].get<float>();
  b.opacity = p.value("opacity", 1.f), b.flow = p.value("flow", 1.f);
  b.pressure_size = b.pressure_opacity = b.pressure_flow = false;
  b.color = colour(p.value("color", std::string("#000000")));
  b.erase = p["tool"] == "eraser";
  return b;
}

// photo-beauty's image of an entry P0 paints (task_of() "P0"): its pixel layer, each path a Stroke (stroke.h) of the
// paint's brush, merged in turn, the document composited (composite_tile()).
golden::Image render_p0(golden::Set& set, const golden::Entry& e) {
  const auto scene = set.layers(e);
  const json& p = e.json["paint"];
  const int w = e.json["canvas"][0], h = e.json["canvas"][1];
  CHECK(scene.size() == 1 && scene[0].kind == "pixel");
  DocPtr d = new_document(w, h, Mode::Rgb, Depth::U16, nullptr);
  d = run(*d, AddLayer{0, 0, Kind::Pixel, {}, golden::grid(*scene[0].pixels, pixel_format(*d))});
  const LayerId id = d->selected[0];
  const BrushParams b = brush_of(p);
  for (const json& name : p["paths"]) {
    Stroke s(d, id, b);
    for (const PenEvent& ev : in::events(in::path(name.get<std::string>()))) s.add(ev);
    s.finish();
    const auto st = s.wait();
    CHECK(st->pixels);
    d = run(*d, SetPixels{id, st->pixels});
  }
  TileGrid out(Format{Depth::U16, 4});
  for (int ty = 0; ty * kTileSize < h; ty++)
    for (int tx = 0; tx * kTileSize < w; tx++) out.set(tx, ty, composite_tile({d}, 0, tx, ty));
  golden::Image im = golden::image(out, w, h);
  im.channels = scene[0].pixels->channels == 4 ? 4 : 3;
  return im;
}

// ---- index.json's paint, checked -----------------------------------------------------------------------------------

// What is wrong with an entry's paint, empty when nothing: a tool strokes.jsx has not; keys not its tool's (the script
// writes every one, defaults too); a path strokes_in::paths() has not; areas not strokes_in::area() of the paths at the
// tip's size; a source that takes an area off the canvas; a mode, gradient, method or style photo-beauty does not read
// (kModes, kHealModes, gradient.h); a gradient's line or the bucket's point off the canvas; a scene not of one pixel
// layer of the canvas' size.
std::vector<std::string> paint_problems(golden::Set& set, const golden::Entry& e) {
  std::vector<std::string> out;
  const auto bad = [&](const std::string& why) { out.push_back(e.name + ": " + why); };
  try {
    if (!e.json.contains("paint") || !e.json["paint"].is_object()) return bad("no paint"), out;
    const json& p = e.json["paint"];
    const std::string tool = p.value("tool", std::string());
    const auto t = tools().find(tool);
    if (t == tools().end()) return bad("no tool \"" + tool + "\""), out;
    std::set<std::string> want(t->second.keys.begin(), t->second.keys.end()), got;
    want.insert("tool");
    std::string keys;
    for (const auto& kv : p.items()) got.insert(kv.key()), keys += " " + kv.key();
    if (got != want) bad("keys" + keys + ", not the " + tool + "'s");
    const int w = e.json.at("canvas").at(0), h = e.json.at("canvas").at(1);
    const auto scene = set.layers(e);
    if (scene.size() != 1 || scene[0].kind != "pixel")
      bad("a scene not of one pixel layer");
    else if (scene[0].pixels->w != w || scene[0].pixels->h != h)
      bad("canvas " + e.json["canvas"].dump() + ", not its input's size");
    if (p.contains("mode") && !(tool == "heal" || tool == "spot" ? kHealModes : kModes).count(p["mode"].get<std::string>()))
      bad("no mode " + p["mode"].dump());
    const auto inside = [&](const json& q) {
      return q.at(0).get<double>() >= 0 && q.at(1).get<double>() >= 0 && q.at(0).get<double>() <= w &&
             q.at(1).get<double>() <= h;
    };
    if (p.contains("paths")) {
      const json& names = p["paths"];
      if (p.contains("areas") && p["areas"].size() != names.size()) bad("not an area for each path");
      for (size_t i = 0; i < names.size(); i++) {
        const std::string name = names[i].get<std::string>();
        const auto it = std::find_if(in::paths().begin(), in::paths().end(), [&](const in::Path& q) { return q.name == name; });
        if (it == in::paths().end()) {
          bad("no path " + name);
          continue;
        }
        if (!p.contains("areas") || i >= p["areas"].size()) continue;
        const json a = in::area(*it, p["size"].get<double>(), w, h);
        if (p["areas"][i] != a) bad("area " + p["areas"][i].dump() + " of " + name + ", not " + a.dump());
        if (!p.contains("source")) continue;
        const int x = a[0].get<int>() + p["source"][0].get<int>(), y = a[1].get<int>() + p["source"][1].get<int>();
        if (!inside(json{x, y}) || !inside(json{x + a[2].get<int>(), y + a[3].get<int>()}))
          bad("the source of " + name + " off the canvas");
      }
    }
    if (tool == "gradient") {
      if (!gradient_from(p["gradient"])) bad("a gradient D30 does not read: " + p["gradient"].dump());
      if (!gradient_method(p["method"].get<std::string>())) bad("no method " + p["method"].dump());
      if (!gradient_style(p["style"].get<std::string>())) bad("no style " + p["style"].dump());
      if (!inside(p["line"].at(0)) || !inside(p["line"].at(1))) bad("the line off the canvas");
    }
    if (tool == "bucket" && !inside(p["at"])) bad("the point off the canvas");
  } catch (const std::exception& x) {
    bad(x.what());
  }
  return out;
}

// ---- Comparing ------------------------------------------------------------------------------------------------------

// Where the dab of the path "dot" (a quarter pixel off the grid) lies on white: the centroid of how much darker than
// white each pixel within 16 px of the path's point is, pixel (i, j) at (i + 0.5, j + 0.5). Nothing else is painted
// there (strokes_in::paths()). NaN where nothing is darker.
std::array<double, 2> dot_centre(const golden::Image& im) {
  const in::Point& d = in::path("dot").points.at(0);
  double sw = 0, sx = 0, sy = 0;
  for (int y = int(d.y) - 16; y <= int(d.y) + 16; y++)
    for (int x = int(d.x) - 16; x <= int(d.x) + 16; x++) {
      const uint16_t* q = im.at(x, y);
      const double v = 1 - (double(q[0]) + q[1] + q[2]) / (3 * 65535.);
      sw += v, sx += v * (x + .5), sy += v * (y + .5);
    }
  if (sw <= 0) return {NAN, NAN};
  return {sx / sw, sy / sw};
}

// The mean and standard deviation of each colour channel, 0..1, over [x, y, w, h] of an image.
struct Stats {
  std::array<double, 3> mean{}, sd{};
};
Stats stats(const golden::Image& im, const std::array<int, 4>& r) {
  std::array<double, 3> sum{}, sq{};
  for (int y = r[1]; y < r[1] + r[3]; y++)
    for (int x = r[0]; x < r[0] + r[2]; x++)
      for (int c = 0; c < 3; c++) {
        const double v = im.at(x, y)[c] / 65535.;
        sum[c] += v, sq[c] += v * v;
      }
  const double n = double(r[2]) * r[3];
  Stats s;
  for (int c = 0; c < 3; c++)
    s.mean[c] = sum[c] / n, s.sd[c] = std::sqrt(std::max(0., sq[c] / n - s.mean[c] * s.mean[c]));
  return s;
}
std::string str(const Stats& s) {
  char t[96];
  std::snprintf(t, sizeof t, "%.4f %.4f %.4f sd %.4f %.4f %.4f", s.mean[0], s.mean[1], s.mean[2], s.sd[0], s.sd[1],
                s.sd[2]);
  return t;
}
bool same(const Stats& a, const Stats& b) { return a.mean == b.mean && a.sd == b.sd; }

// The retouching tools' areas ("areas": the Clone Stamp's, the Healing Brushes'), what H30, H32 and H33 compare
// theirs with by statistics (a heal's pixels are its algorithm's): for each, those of Photoshop's image there, of the
// input and of where the source is (the area moved by "source"; the Spot Healing Brush has none). Printed; returned
// as {Photoshop's, the input's, the source's or the input's}.
std::vector<std::array<Stats, 3>> area_stats(golden::Set& set, const golden::Entry& e, const golden::Image& theirs) {
  const json& p = e.json["paint"];
  const golden::Image& input = *set.layers(e).at(0).pixels;
  std::vector<std::array<Stats, 3>> out;
  for (size_t i = 0; i < p["areas"].size(); i++) {
    const auto a = p["areas"][i].get<std::array<int, 4>>();
    std::array<Stats, 3> s = {stats(theirs, a), stats(input, a), {}};
    std::string source;
    if (p.contains("source")) {
      const int dx = p["source"][0], dy = p["source"][1];
      s[2] = stats(input, {a[0] + dx, a[1] + dy, a[2], a[3]});
      source = "; source " + str(s[2]);
    } else {
      s[2] = s[1];
    }
    std::printf("ps strokes %s %s %s: Photoshop %s; input %s%s\n", e.name.c_str(), p["paths"][i].get<std::string>().c_str(),
                p["areas"][i].dump().c_str(), str(s[0]).c_str(), str(s[1]).c_str(), source.c_str());
    out.push_back(s);
  }
  return out;
}

// The places of the tasks that bring the other tools (task_of()): each will compare an image of its tool with
// photo-beauty's, as check_set() does for P0's, and return true; until then false: the image waits for it. C30: the
// Pencil, tips not round; C31: Wet Edges, Noise; C33: the Brush's modes, the Eraser's Pencil and Block; C36: the
// Gradient tool (method, style); C38: the Paint Bucket; H30: the Clone Stamp; H32: the Healing Brush; H33: the Spot
// Healing Brush (the three by area_stats()); H35: Dodge, Burn, Sponge.
using Check = bool (*)(golden::Set&, const golden::Entry&, const golden::Image&);
bool check_c30(golden::Set&, const golden::Entry&, const golden::Image&) { return false; }
bool check_c31(golden::Set&, const golden::Entry&, const golden::Image&) { return false; }
bool check_c33(golden::Set&, const golden::Entry&, const golden::Image&) { return false; }
bool check_c36(golden::Set&, const golden::Entry&, const golden::Image&) { return false; }
bool check_c38(golden::Set&, const golden::Entry&, const golden::Image&) { return false; }
bool check_h30(golden::Set&, const golden::Entry&, const golden::Image&) { return false; }
bool check_h32(golden::Set&, const golden::Entry&, const golden::Image&) { return false; }
bool check_h33(golden::Set&, const golden::Entry&, const golden::Image&) { return false; }
bool check_h35(golden::Set&, const golden::Entry&, const golden::Image&) { return false; }
const std::map<std::string, Check> kChecks = {{"C30", check_c30}, {"C31", check_c31}, {"C33", check_c33},
                                              {"C36", check_c36}, {"C38", check_c38}, {"H30", check_h30},
                                              {"H32", check_h32}, {"H33", check_h33}, {"H35", check_h35}};

// What check_set() found.
struct Summary {
  int images = 0, p0 = 0, dots = 0, areas = 0;
  double worst = 0;                    // P0's largest ΔE2000
  std::map<std::string, int> waiting;  // images by the task they wait for
};

// A set of strokes.jsx's images (Photoshop's, or test_ps_strokes_stand_in's): whole (every image index.json lists,
// its scene readable, its paint right, within C41's budget), each image of its canvas and bits. P0's images compared
// with photo-beauty's (render_p0()), the deviation printed (`exact`: none); the dot's dab centre and the areas'
// statistics printed (`exact`: the dab where the path has it, the areas their input's); the rest waiting for their
// tasks, said by task.
Summary check_set(golden::Set& set, const char* test, bool exact) {
  Summary sum;
  const auto problems = set.problems();
  for (const auto& p : problems) std::printf("%s: %s\n", set.name().c_str(), p.c_str());
  CHECK(problems.empty());
  uintmax_t size = 0;
  for (const auto& f : fs::directory_iterator(set.dir()))
    if (f.is_regular_file() && f.path().extension() == ".png") size += f.file_size();
  std::printf("%s: %zu images, %.2f MB\n", test, set.entries().size(), double(size) / (1 << 20));
  CHECK(size <= kBudget);
  std::map<std::string, std::set<std::string>> what;  // task: what the images that wait for it need
  for (const golden::Entry& e : set.entries()) {
    const auto pp = paint_problems(set, e);
    for (const auto& p : pp) std::printf("%s: %s\n", test, p.c_str());
    CHECK(pp.empty());
    const golden::Image theirs = set.image(e);
    if (!theirs) continue;
    sum.images++;
    CHECK(theirs.w == e.json["canvas"][0].get<int>() && theirs.h == e.json["canvas"][1].get<int>());
    CHECK(theirs.bits == e.json["bits"].get<int>());
    const json& p = e.json["paint"];
    const std::string task = task_of(p);
    const json& names = p.value("paths", json::array());
    if (std::find(names.begin(), names.end(), "dot") != names.end() && e.json["layers"][0]["src"] == "strokes/in/white.png") {
      const auto c = dot_centre(theirs);
      const in::Point& d = in::path("dot").points[0];
      if (std::isnan(c[0])) {
        std::printf("ps strokes %s: no dab at the dot\n", e.name.c_str());
      } else {
        std::printf("ps strokes %s: the dot's dab centred at (%.3f, %.3f), the path's point (%g, %g)\n", e.name.c_str(),
                    c[0], c[1], d.x, d.y);
        if (exact) CHECK(std::abs(c[0] - d.x) < .02 && std::abs(c[1] - d.y) < .02);
        sum.dots++;
      }
    }
    if (p.contains("areas"))
      for (const auto& s : area_stats(set, e, theirs)) {
        if (exact) CHECK(same(s[0], s[1]));
        sum.areas++;
      }
    if (task == "P0") {
      const golden::Diff d = golden::compare(render_p0(set, e), theirs, e.name);
      golden::print("strokes " + e.name, d);
      if (exact) CHECK(d.max == 0 && d.levels == 0);
      sum.p0++, sum.worst = std::max(sum.worst, d.max);
    } else if (!kChecks.at(task)(set, e, theirs)) {
      what[task].insert(what_of(p)), sum.waiting[task]++;
    }
  }
  if (sum.p0)
    std::printf("%s: P0's brush and eraser (dab.h, stroke.h) against %d images: largest ΔE2000 %.2f (hypotheses: "
                "C30-C35 tune them)\n", test, sum.p0, sum.worst);
  for (const auto& kv : what) {
    std::string names;
    for (const auto& w : kv.second) names += (names.empty() ? "" : ", ") + w;
    std::printf("%s: %d images wait for %s (%s): not in photo-beauty yet\n", test, sum.waiting[kv.first],
                kv.first.c_str(), names.c_str());
  }
  set.skipped(test);
  return sum;
}

}  // namespace

// The inputs in the repo as pb-bench ps-strokes writes them (golden::strokes_in), levels Photoshop reads back exactly;
// what they hold; paths.json; the paths as pen events, where P0's dabs fall along one, and the areas of strokes.
TEST(test_ps_strokes_inputs) {
  if (!have_tree("test_ps_strokes_inputs")) return;
  const auto files = in::all();
  CHECK(files.size() == size_t(in::kFiles));
  for (const auto& f : files) {
    const golden::Image got = golden::read_png(kIn / f.name);
    if (!got || got.px != f.image.px || got.channels != f.image.channels)
      std::printf("tests/golden/ps/strokes/in/%s %s: run pb-bench ps-strokes tests/golden/ps\n", f.name.c_str(),
                  got ? "is not as golden::strokes_in makes it" : "is not there");
    CHECK(got && got.bits == 16 && got.channels == f.image.channels && got.px == f.image.px);
    for (const uint16_t s : got.px) CHECK(golden::is_ps_level(s));
  }
  if (bytes(kIn / "paths.json") != in::paths_json())
    std::printf("tests/golden/ps/strokes/in/paths.json is not strokes_in::paths_json(): run pb-bench ps-strokes "
                "tests/golden/ps\n");
  CHECK(bytes(kIn / "paths.json") == in::paths_json());

  const auto px = [](const golden::Image& im, int x, int y) {
    const uint16_t* p = im.at(x, y);
    return std::array<int, 4>{p[0], p[1], p[2], p[3]};
  };
  const auto level = [](double v) { return int(golden::ps_level(v)); };
  const golden::Image white = in::white(), patches = in::patches(), tones = in::tones(), skin = in::skin();
  CHECK(white.w == in::kW && white.h == in::kH && white.channels == 3);
  for (const uint16_t s : white.px) CHECK(s == 65535);
  CHECK(patches.w == in::kW && patches.h == in::kH && patches.channels == 4);
  CHECK((px(patches, 16, 16) == std::array<int, 4>{65535, 65535, 65535, 65535}));
  CHECK((px(patches, 5 * 32 + 3, 30) == std::array<int, 4>{65535, 0, 0, 65535}));  // red, row 0
  CHECK((px(patches, 4 * 32 + 9, 2 * 32 + 9) == std::array<int, 4>{0, 0, level(.5), 65535}));  // navy, row 2
  CHECK((px(patches, 200, 100) == std::array<int, 4>{0, 65535, 0, level(200 / 255.)}));  // row 3: row 0's, alpha x / 255
  CHECK((px(patches, 77, 150) == std::array<int, 4>{0, 0, 0, 0}));                             // row 4: transparent
  CHECK(tones.w == in::kTonesW && tones.h == in::kTonesH && tones.channels == 3);
  CHECK((px(tones, 100, 8) == std::array<int, 4>{level(100 / 255.), level(100 / 255.), level(100 / 255.), 65535}));
  CHECK((px(tones, 100, 24) == std::array<int, 4>{level(100 / 255.), 0, 0, 65535}));
  CHECK((px(tones, 37, 56) == std::array<int, 4>{0, 0, level(37 / 255.), 65535}));
  CHECK((px(tones, 0, 72) == std::array<int, 4>{65535, 0, 0, 65535} && px(tones, 128, 88)[2] == level(.5)));  // hues
  CHECK(skin.w == in::kSkin && skin.h == in::kSkin && skin.channels == 3);
  for (const in::Spot& s : in::spots()) {  // darker than the clean skin 64 px right, where the strokes' source is
    CHECK(s.x + s.rx <= 52 && s.x + s.rx + 64 <= in::kSkin);
    CHECK(px(skin, int(s.x), int(s.y))[1] < px(skin, int(s.x) + 64, int(s.y))[1] - 4000);
  }
  int textured = 0;  // the clean skin is not flat
  for (int x = 64; x < 127; x++) textured += px(skin, x, 10)[0] != px(skin, x + 1, 10)[0];
  CHECK(textured > 50);

  const json paths = json::parse(in::paths_json())["paths"];
  CHECK(paths.size() == in::paths().size());
  for (const in::Path& p : in::paths()) {
    CHECK(paths.contains(p.name) && paths[p.name].size() == p.points.size());
    for (size_t i = 0; i < p.points.size(); i++) {
      const in::Point& q = p.points[i];
      const json want = q.corner() ? json{q.x, q.y} : json{q.x, q.y, q.ix, q.iy, q.ox, q.oy};
      CHECK(paths[p.name][i] == want);
    }
  }

  const auto line = in::events(in::path("line"));
  CHECK(line.front().x == 16 && line.front().y == 20 && line.back().x == 240 && line.back().y == 20);
  for (size_t i = 1; i < line.size(); i++) {
    CHECK(std::hypot(line[i].x - line[i - 1].x, line[i].y - line[i - 1].y) <= .5f + 1e-4f);
    CHECK(line[i].t > line[i - 1].t && line[i].pressure == 1);
  }
  const auto dot = in::events(in::path("dot"));
  CHECK(dot.size() == 1 && dot[0].x == 28.25f && dot[0].y == 64.75f);
  const auto curve = in::events(in::path("curve"));
  CHECK(curve.front().x == 136 && curve.front().y == 48 && curve.back().x == 240 && curve.back().y == 104);
  double near = 1e9;  // the S passes its Bezier's point at a quarter: (170.25, 56.75), off the chord
  for (const PenEvent& ev : curve) near = std::min(near, std::hypot(ev.x - 170.25, ev.y - 56.75));
  CHECK(near < .5);
  BrushParams b;  // P0's dabs along the line: 24 px, spacing 25%, every 6 px from its start
  b.size = 24, b.spacing = .25f, b.pressure_size = false;
  DabPlacer placer(b);
  std::vector<Dab> dabs;
  for (const PenEvent& ev : line)
    for (const Dab& d : placer.add(ev)) dabs.push_back(d);
  for (const Dab& d : placer.finish()) dabs.push_back(d);
  CHECK(dabs.size() == 38);
  for (size_t i = 0; i < dabs.size(); i++) CHECK(std::abs(dabs[i].x - (16 + 6. * i)) < .01 && std::abs(dabs[i].y - 20) < .01);

  CHECK((in::area(in::path("spot-a"), 20, in::kSkin, in::kSkin) == std::array<int, 4>{9, 13, 22, 22}));
  CHECK((in::area(in::path("spot-b"), 20, in::kSkin, in::kSkin) == std::array<int, 4>{17, 49, 38, 22}));
  CHECK((in::area(in::path("h16"), 24, in::kW, in::kH) == std::array<int, 4>{0, 3, 256, 26}));  // clipped
  CHECK((in::area(in::path("curve"), 24, in::kW, in::kH) == std::array<int, 4>{123, 35, 130, 82}));
}

// The hand-made index (tests/golden/ps/strokes/sample/index.json: lines of strokes.jsx's PB.offline, one of each form):
// it reads as golden.h reads it, every paint right (paint_problems()), each tool and each task there, P0's four
// painted here (no Photoshop images beside the sample).
TEST(test_ps_strokes_index) {
  if (!have_tree("test_ps_strokes_index")) return;
  golden::Set sample(kSample);
  CHECK(sample.has_index());
  CHECK(sample.index()["script"] == "strokes.jsx" && sample.index()["inputs"] == "../..");
  std::set<std::string> tools_in, tasks_in;
  for (const golden::Entry& e : sample.entries()) {
    const auto pp = paint_problems(sample, e);
    for (const auto& p : pp) std::printf("test_ps_strokes_index: %s\n", p.c_str());
    CHECK(pp.empty());
    const json& p = e.json["paint"];
    tools_in.insert(p["tool"].get<std::string>()), tasks_in.insert(task_of(p));
    if (task_of(p) == "P0") {
      const golden::Image im = render_p0(sample, e);
      CHECK(im.w == e.json["canvas"][0].get<int>() && im.h == e.json["canvas"][1].get<int>());
    }
    CHECK(!sample.image(e));
  }
  for (const auto& kv : tools())
    if (!tools_in.count(kv.first)) std::printf("test_ps_strokes_index: the sample has no %s\n", kv.first.c_str());
  CHECK(tools_in.size() == tools().size());
  for (const char* t : {"P0", "C30", "C31", "C33", "C36", "C38", "H30", "H32", "H33", "H35"}) CHECK(tasks_in.count(t));
  for (const char* n : {"brush-h100", "brush-h50-o50", "brush-h50-flow30", "eraser-brush"}) CHECK(sample.find(n));
  CHECK(sample.problems().size() == sample.entries().size());  // only the images missing
}

// check_set() on a stand-in for Photoshop's set: the sample's entries in a folder of their own, P0's images painted
// here, the others their input untouched. Every image read and compared: P0's the same as painted again, the dot's dab
// where the path has it, the areas' statistics their input's; the rest waiting for their tasks. And P0's images as
// they must be: the hard brush black on the line, white past it; Opacity 50% half; Flow 30% 70% of white at the dot;
// the eraser through on its row and nowhere between rows.
TEST(test_ps_strokes_stand_in) {
  if (!have_tree("test_ps_strokes_stand_in")) return;
  golden::Set sample(kSample);
  TempDir tmp;
  json index = sample.index();
  index["inputs"] = golden::kRoot.string();  // the inputs where they are
  std::ofstream(tmp.path / "index.json") << index.dump();
  std::map<std::string, golden::Image> made;
  int p0 = 0, others = 0, areas = 0;
  for (const golden::Entry& e : sample.entries()) {
    const json& p = e.json["paint"];
    golden::Image im;
    if (task_of(p) == "P0")
      im = render_p0(sample, e), p0++;
    else
      im = sample.input(e.json["layers"][0]["src"].get<std::string>()), others++;
    areas += int(p.value("areas", json::array()).size());
    golden::write_png(tmp.path / (e.name + ".png"), im);
    made[e.name] = im;
  }
  CHECK(p0 == 4 && others >= 12 && areas >= 12);
  const auto v = [&](const char* name, int x, int y, int c) { return int(made.at(name).at(x, y)[c]); };
  CHECK(v("brush-h100", 128, 20, 0) == 0 && v("brush-h100", 128, 36, 0) == 65535);
  CHECK(std::abs(v("brush-h50-o50", 128, 20, 0) - 32768) <= 2);
  CHECK(std::abs(v("brush-h50-flow30", 28, 64, 0) - 45875) <= 2);
  CHECK(v("eraser-brush", 128, 16, 3) == 0 && v("eraser-brush", 128, 32, 3) == 65535);
  golden::Set stand_in(tmp.path);
  CHECK(stand_in.ready("test_ps_strokes_stand_in"));
  const Summary sum = check_set(stand_in, "test_ps_strokes_stand_in", true);
  CHECK(sum.images == int(sample.entries().size()) && sum.p0 == p0 && sum.worst == 0);
  CHECK(sum.dots == 3 && sum.areas == areas);  // the dot of brush-h100, -h50-o50, -h50-flow30; the rest not painted
  int waiting = 0;
  for (const auto& kv : sum.waiting) waiting += kv.second;
  CHECK(waiting == others && sum.waiting.size() == kChecks.size());
}

// Photoshop's images, once B33 has brought them (until then said to be skipped): check_set() on them.
TEST(test_ps_strokes) {
  if (!have_tree("test_ps_strokes")) return;
  golden::Set set("strokes");
  if (!set.ready("test_ps_strokes")) return;
  check_set(set, "test_ps_strokes", false);
}
