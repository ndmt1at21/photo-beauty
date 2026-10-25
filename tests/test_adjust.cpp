// Adjustments (adjust.h, D31): the sixteen types' JSON against ui/qml/AdjustSpec.js itself (its keys, defaults, ranges
// and lists, read from the file: what the UI writes the core reads and writes back byte for byte) and README-psd.md's
// sets; each type the identity wherever it says so, and the GPU (adjust_check.h) the CPU's to the bit, the test matrix
// too; apply_adjustment() against a reference written pixel by pixel here (soft selections stopping at the canvas,
// alpha, empty and unchanged tiles keeping their ids, U8, U16, grey, masks and their fill, cancelling, progress);
// preview_state(); histogram() at levels 0 and 2, in a region, with a selection.
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <random>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "adjust.h"
#include "adjust_check.h"
#include "check.h"
#include "hash.h"
#include "histogram.h"
#include "mip.h"

using namespace pb;
using nlohmann::json;

namespace {

constexpr int S = kTileSize;

// ---- AdjustSpec.js: what the UI keeps of each adjustment --------------------------------------------------------

std::string spec_text() {
  std::ifstream f(PB_SOURCE_DIR "/ui/qml/AdjustSpec.js", std::ios::binary);
  CHECK(f);
  std::ostringstream s;
  s << f.rdbuf();
  return s.str();
}

// The JS string literals in s, in order.
std::vector<std::string> literals(const std::string& s) {
  std::vector<std::string> out;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] != '"') continue;
    std::string v;
    for (i++; i < s.size() && s[i] != '"'; i++) {
      if (s[i] == '\\' && i + 1 < s.size()) i++;
      v += s[i];
    }
    out.push_back(v);
  }
  return out;
}

// From "var <name> = " to the line "};" (or to the end of the line for one that fits on it).
std::string block(const std::string& js, const std::string& name) {
  const size_t at = js.find("var " + name + " = ");
  CHECK(at != std::string::npos);
  size_t end = js.find("\n};", at);
  const size_t eol = js.find('\n', at);
  if (js.compare(eol - 1, 1, ";") == 0) end = eol;  // one line
  CHECK(end != std::string::npos);
  return js.substr(at, end - at);
}

// "key: [...]" lines of a block: each key's literals.
std::map<std::string, std::vector<std::string>> lists(const std::string& b) {
  std::map<std::string, std::vector<std::string>> out;
  std::istringstream in(b);
  const std::regex line(R"(^\s*(\w+): \[(.*)\],?\s*$)");
  std::smatch m;
  for (std::string l; std::getline(in, l);)
    if (std::regex_match(l, m, line)) out[m[1]] = literals(m[2]);
  return out;
}

double pnum(std::string s) {  // Fmt.pnum(): a decimal comma
  std::replace(s.begin(), s.end(), ',', '.');
  return std::stod(s);
}

// One key of an adjustment's vals as AdjustSpec's defaults() makes it.
struct Key {
  enum Kind { Number, Flag, Choice, Index, Color, Text, Object, Span } kind = Number;
  std::string key;
  json def;
  double lo = 0, hi = 0;  // Number; Index: 0..hi
  int decimals = 0;
  std::vector<std::string> list;  // Choice's names; Text's to pick a sample from
};
struct Spec {
  std::vector<std::string> order;            // ORDER
  std::map<std::string, std::vector<Key>> keys;  // by name
};

Spec read_spec() {
  const std::string js = spec_text();
  Spec spec;
  spec.order = literals(block(js, "ORDER"));
  const auto lst = lists(block(js, "LISTS")), presets = lists(block(js, "PRESETS"));
  // FIELDS: [min, max, decimals, name]; POINT: the fields that are a curve's point, no values.
  std::map<std::string, std::array<double, 3>> fields;
  const std::string fb = block(js, "FIELDS");
  const std::regex field(R"((\w+): \[(-?[0-9.]+), (-?[0-9.]+), (\d+))");
  for (auto it = std::sregex_iterator(fb.begin(), fb.end(), field); it != std::sregex_iterator(); ++it)
    fields[(*it)[1]] = {std::stod((*it)[2]), std::stod((*it)[3]), std::stod((*it)[4])};
  const std::string pb_ = block(js, "POINT");
  const std::regex word(R"((\w+): \d)");
  std::vector<std::string> point;
  for (auto it = std::sregex_iterator(pb_.begin(), pb_.end(), word); it != std::sregex_iterator(); ++it)
    point.push_back((*it)[1]);
  const auto list_of = [&](const std::string& n, const std::string& def) {
    const auto it = lst.find(n);
    return it != lst.end() ? it->second : std::vector<std::string>{def};
  };

  std::istringstream in(block(js, "ADJ"));
  for (std::string l; std::getline(in, l);) {
    const size_t rows_at = l.find("rows: [");
    if (rows_at == std::string::npos) continue;
    const std::string name = literals(l.substr(0, rows_at)).at(0);
    std::vector<Key>& keys = spec.keys[name];
    std::map<std::string, int> radios;  // radio groups' sizes
    for (const std::string& row : literals(l.substr(rows_at))) {
      std::vector<std::string> a;
      std::stringstream rs(row);
      for (std::string p; std::getline(rs, p, '~');) a.push_back(p);
      const std::string& k = a[0];
      Key key;
      if (k == "PRE") {
        key.kind = Key::Text, key.key = "pre", key.def = "Default", key.list = presets.at(a[1]);
      } else if (k == "DDL") {
        key.kind = Key::Choice, key.key = a[1], key.def = a[3], key.list = list_of(a[4], a[3]);
      } else if (k == "S") {
        key.kind = Key::Number, key.key = a[1], key.lo = pnum(a[3]), key.hi = pnum(a[4]), key.def = pnum(a[5]);
        key.decimals = a.size() > 6 && !a[6].empty() ? std::stoi(a[6]) : 0;
      } else if (k == "CHK") {
        key.kind = Key::Flag, key.key = a[1], key.def = a[3] == "1";
      } else if (k == "SEG") {
        key.kind = Key::Index, key.key = a[1], key.def = std::stoi(a[3]);
        key.hi = double(std::count(a[2].begin(), a[2].end(), '|'));
      } else if (k == "RADIO") {
        radios[a[1]]++;
        if (a[4][0] == '#')
          key.kind = Key::Color, key.def = a[4];
        else
          key.kind = Key::Text, key.def = a[5], key.list = list_of(a[4], a[5]);
        key.key = a[1] + a[2];
      } else if (k == "TINT") {
        keys.push_back({Key::Flag, "tint", a[1] == "1"});
        key.kind = Key::Color, key.key = "tintColor", key.def = a[2];
      } else if (k == "FLDS" || k == "LF") {
        std::stringstream fs(a[1]);
        for (std::string f; std::getline(fs, f, '|');) {
          const std::string fk = f.substr(0, f.find('='));
          if (std::find(point.begin(), point.end(), fk) != point.end()) continue;
          const std::array<double, 3>& r = fields.at(fk);
          Key n;
          n.kind = Key::Number, n.key = fk, n.def = pnum(f.substr(f.rfind('=') + 1)), n.lo = r[0], n.hi = r[1];
          n.decimals = int(r[2]);
          keys.push_back(n);
        }
        continue;
      } else if (k == "CURVE") {
        key.kind = Key::Object, key.key = "pts", key.def = json::object();
      } else if (k == "RANGE") {
        key.kind = Key::Span, key.key = "span", key.def = nullptr;
      } else {
        continue;  // BTN, TOOLS, HIST, OUT, TOT, NOTE, GRAD: no values
      }
      keys.push_back(key);
    }
    for (const auto& [group, n] : radios) keys.push_back({Key::Index, group, 0, 0, double(n - 1)});
  }
  return spec;
}

double rounded(double v, int decimals) {
  const double k = std::pow(10., decimals);
  return std::round(v * k) / k;
}
// A number as the UI's JSON.stringify() writes it.
json js_number(double v) { return v == std::floor(v) ? json(int64_t(v)) : json(v); }

// A value of the key off its default, as the UI could keep it.
json sample(const Key& k) {
  switch (k.kind) {
    case Key::Number: {
      const double d = k.def.get<double>();
      double v = rounded(k.hi != d ? d + (k.hi - d) / 4 : d - (d - k.lo) / 4, k.decimals);
      if (v == d) v = k.hi != d ? k.hi : k.lo;
      return js_number(v);
    }
    case Key::Flag: return !k.def.get<bool>();
    case Key::Index: return (k.def.get<int>() + 1) % (int(k.hi) + 1);
    case Key::Color: return "#1a2b3c";
    case Key::Choice:
    case Key::Text:
      for (auto it = k.list.rbegin(); it != k.list.rend(); ++it)
        if (*it != k.def.get<std::string>()) return *it;
      return k.def.get<std::string>() + " 2";
    case Key::Object: return json::parse(R"({"RGB": [[0, 0], [128, 140], [255, 255]], "Red": [[0, 10], [255, 245]]})");
    case Key::Span: return json::array({10, 40, 80, 110});
  }
  return nullptr;
}

// Values of the wrong type for the key.
std::vector<json> wrong(const Key& k) {
  switch (k.kind) {
    case Key::Number: case Key::Index: return {"5", true, json::array({1})};
    case Key::Flag: return {1, "true"};
    case Key::Choice: return {3, "Zzz", ""};
    case Key::Color: return {3, "red", "#12345", "#12345g"};
    case Key::Text: return {3, false};
    case Key::Object: return {json::array({1}), "x"};
    case Key::Span: return {json::array({1, 2}), json::array({1, 2, 3, "4"}), 7};
  }
  return {};
}

std::string dump(const json& j) { return j.dump(); }

}  // namespace

// What AdjustSpec.js keeps for each adjustment (its rows' keys, defaults, ranges, lists) the core reads, and writes
// back as the UI wrote it: the defaults as {}, values off them byte for byte, numbers past a range at its end; a key of
// the wrong type or a name off its list does not read. The names are AdjustSpec.ORDER's, in its order.
TEST(test_adjust_spec) {
  const Spec spec = read_spec();
  CHECK(spec.order.size() == size_t(kAdjPhotoshop));
  for (int i = 0; i < kAdjPhotoshop; i++) {
    CHECK(spec.order[size_t(i)] == kAdjNames[i]);
    CHECK(adj_type(kAdjNames[i]) == AdjType(i));
  }
  CHECK(!adj_type("") && !adj_type("Shadows/Highlights"));
  // The test's type is no user's: by name (a file's props) only once a test turns it on.
  LayerProps tm;
  tm.adj = "Test Matrix";
  enable_test_matrix(false);
  CHECK(!adj_type("Test Matrix") && !adjustment("Test Matrix") && !adjustment(tm) && adjustment(AdjType::TestMatrix));
  enable_test_matrix();
  CHECK(adj_type("Test Matrix") == AdjType::TestMatrix && adjustment(tm) == adjustment(AdjType::TestMatrix));
  enable_test_matrix(false);
  size_t keys = 0, failed = 0;
  for (const std::string& name : spec.order) {
    const std::vector<Key>& ks = spec.keys.at(name);
    keys += ks.size();
    json defaults = json::object(), samples = json::object();
    for (const Key& k : ks) defaults[k.key] = k.def, samples[k.key] = sample(k);
    const auto vals = [&](const json& j) {
      const AdjustmentPtr a = adjustment(name, dump(j));
      return a ? a->vals() : std::string("null");
    };
    // The defaults (AdjustSpec's full()), none at all, keys it does not know.
    CHECK(vals(defaults) == "{}" && vals(json::object()) == "{}");
    CHECK(vals(json::parse(R"({"zzz": 1, "x": [1]})")) == "{}");
    // Every key off its default, and read back.
    const std::string all = vals(samples);
    if (all != dump(samples)) {
      std::printf("test_adjust_spec: %s: %s written as %s\n", name.c_str(), dump(samples).c_str(), all.c_str());
      failed++;
    }
    CHECK(vals(json::parse(all)) == all);
    for (const Key& k : ks) {
      // One key alone, and past its range.
      json one = json::object();
      one[k.key] = sample(k);
      CHECK(vals(one) == dump(one));
      if (k.kind == Key::Number || k.kind == Key::Index) {
        const bool up = k.hi != k.def.get<double>();
        json past = json::object(), at = json::object();
        past[k.key] = up ? k.hi + 1000 : k.lo - 1000;
        at[k.key] = js_number(up ? k.hi : k.lo);
        if (vals(past) != dump(at)) {
          std::printf("test_adjust_spec: %s: %s read as %s\n", name.c_str(), dump(past).c_str(), vals(past).c_str());
          failed++;
        }
      }
      for (const json& w : wrong(k)) {
        json bad = json::object();
        bad[k.key] = w;
        if (k.kind == Key::Text && w.is_string()) continue;
        if (adjustment(name, dump(bad))) {
          std::printf("test_adjust_spec: %s: %s reads\n", name.c_str(), dump(bad).c_str());
          failed++;
        }
      }
      // null is the default (the mock writes null for some).
      json nul = json::object();
      nul[k.key] = nullptr;
      CHECK(vals(nul) == "{}");
    }
  }
  CHECK(failed == 0);
  // Not JSON, not an object; a layer's props (vals null: the defaults); the cache.
  CHECK(!adjustment("Levels", "{") && !adjustment("Levels", "[1]") && !adjustment("Levels", "") &&
        !adjustment("Nothing", "{}"));
  LayerProps p;
  p.adj = "Curves";
  CHECK(adjustment(p) && adjustment(p)->vals() == "{}" && adjustment(p)->type() == AdjType::Curves);
  p.vals = std::make_shared<const std::string>(R"({"ch":"Red"})");
  CHECK(adjustment(p)->vals() == R"({"ch":"Red"})");
  CHECK(adjustment("Levels", R"({"gam":2})") == adjustment(AdjType::Levels, R"({"gam":2})"));
  CHECK(adjustment("Levels", R"({"gam":2})") != adjustment("Levels", R"({"gam":2})", {Mode::Gray, nullptr}));
  // Grey documents offer those that need no colour.
  for (int i = 0; i < kAdjTypes; i++) CHECK(available(AdjType(i), Mode::Rgb));
  CHECK(available(AdjType::Levels, Mode::Gray) && available(AdjType::Invert, Mode::Gray) &&
        !available(AdjType::HueSaturation, Mode::Gray) && !available(AdjType::Vibrance, Mode::Gray));
  std::printf("test_adjust_spec: %d adjustments, %zu keys of AdjustSpec.js\n", kAdjPhotoshop, keys);
}

namespace {

// The colours a type is checked on: the 33³ lattice and the grey 16-bit ramp, 4 floats each (alpha apart).
std::vector<Float4> colours() {
  std::vector<Float4> px;
  for (int b = 0; b <= 32; b++)
    for (int g = 0; g <= 32; g++)
      for (int r = 0; r <= 32; r++) px.push_back({r / 32.f, g / 32.f, b / 32.f, 0.25f});
  for (int v = 0; v < 65536; v++) {
    const float u = float(double(v) / 65535);
    px.push_back({u, u, u, 0.75f});
  }
  return px;
}

// Every type at AdjustSpec's defaults and with every value off them, and the test matrix: some, some not identities.
std::vector<AdjustmentPtr> every_type() {
  const Spec spec = read_spec();
  std::vector<AdjustmentPtr> out;
  for (const std::string& name : spec.order) {
    json samples = json::object();
    for (const Key& k : spec.keys.at(name)) samples[k.key] = sample(k);
    out.push_back(adjustment(name, "{}"));
    out.push_back(adjustment(name, dump(samples)));
  }
  for (const char* vals : {"{}", R"({"m":[0.9,0.2,-0.15,0,0.1,0.7,0.25,0,-0.05,0.3,0.8,0]})",
                           R"({"m":[-1,0,0,1,0,-1,0,1,0,0,-1,1]})",
                           R"({"m":[1.2,-0.3,0.4,-0.1,0.25,0.5,0.5,0.03,0.1,0.2,0.3,0.4],"power":[2,1,0.5]})",
                           R"({"power":[0.3,3,1.7]})"})
    out.push_back(adjustment(AdjType::TestMatrix, vals));
  for (const AdjustmentPtr& a : out) CHECK(a);
  return out;
}

}  // namespace

// Each type is the identity wherever identity() says so (D31 left all sixteen the identity: every one says so then);
// apply() leaves alpha alone and clamps; the test matrix computes what it says, its curves' table included.
TEST(test_adjust_frame) {
  const std::vector<Float4> px = colours();
  int identities = 0, photoshop = 0;
  for (const AdjustmentPtr& a : every_type()) {
    std::vector<Float4> out = px;
    a->apply(out[0].data(), int(out.size()), -5, 3);
    size_t moved = 0;
    for (size_t i = 0; i < px.size(); i++) {
      CHECK(out[i][3] == px[i][3]);
      for (int c = 0; c < 3; c++) CHECK(out[i][size_t(c)] >= 0 && out[i][size_t(c)] <= 1);
      if (std::memcmp(&out[i], &px[i], sizeof(Float4)) != 0) moved++;
    }
    if (a->identity()) {
      if (moved) std::printf("test_adjust_frame: %s %s moves %zu colours\n", kAdjNames[size_t(a->type())],
                             a->vals().c_str(), moved);
      CHECK(moved == 0);
      identities++;
      photoshop += a->type() != AdjType::TestMatrix;
    }
    // Packed: the type and the base; the words agree with the tables' presence for the test matrix.
    const AdjustParams p = a->pack(77);
    CHECK(p.w[0] == uint32_t(a->type()) && p.w[1] == 77);
  }
  std::printf("test_adjust_frame: %d of %d settings of the sixteen are identities\n", photoshop, 2 * kAdjPhotoshop);

  // The test matrix against its formula in double precision (its curves through the table: within 1e-6).
  const AdjustmentPtr m = adjustment(
      AdjType::TestMatrix, R"({"m":[1.2,-0.3,0.4,-0.1,0.25,0.5,0.5,0.03,0.1,0.2,0.3,0.4],"power":[2,1,3]})");
  CHECK(m && !m->identity() && m->tables().size() == size_t(kCurveSteps) + 2);
  CHECK(m->vals() == R"({"m":[1.2,-0.3,0.4,-0.1,0.25,0.5,0.5,0.03,0.1,0.2,0.3,0.4],"power":[2,1,3]})");
  const double M[12] = {1.2, -0.3, 0.4, -0.1, 0.25, 0.5, 0.5, 0.03, 0.1, 0.2, 0.3, 0.4}, P[3] = {2, 1, 3};
  double worst = 0;
  for (const Float4& c : px) {
    const std::array<float, 3> got = m->apply({c[0], c[1], c[2]}, 0, 0);
    for (int k = 0; k < 3; k++) {
      const double v = M[4 * k] * c[0] + M[4 * k + 1] * c[1] + M[4 * k + 2] * c[2] + M[4 * k + 3];
      const double want = std::pow(std::clamp(v, 0., 1.), P[k]);
      worst = std::max(worst, std::fabs(got[size_t(k)] - want));
    }
  }
  std::printf("test_adjust_frame: test matrix within %.3g of its formula\n", worst);
  CHECK(worst < 1e-6);
  CHECK(adjustment(AdjType::TestMatrix, "{}")->identity() && adjustment(AdjType::TestMatrix, "{}")->tables().empty());
  CHECK(!adjustment(AdjType::TestMatrix, R"({"m":[1,2]})"));
  // Clamped, NaN to 0.
  const AdjustmentPtr big = adjustment(AdjType::TestMatrix, R"({"m":[3,0,0,0.5,0,-2,0,0,0,0,0,0]})");
  float nan[4] = {std::nanf(""), 0.5f, 0.5f, 0.5f};
  big->apply(nan, 1, 0, 0);
  CHECK(nan[0] == 0 && nan[1] == 0 && nan[2] == 0 && nan[3] == 0.5f);
  CHECK(big->apply({0.5f, 0.5f, 0.5f}) == (std::array<float, 3>{1, 0, 0}));
}

// adjust_pixel() of pack() and tables() on lavapipe, against apply() to the bit, for every type at AdjustSpec's
// defaults and off them, and the test matrix with its table and without.
TEST(test_gpu_adjust) {
  test::AdjustCheck check;
  if (!check) return;
  const auto t0 = std::chrono::steady_clock::now();
  int n = 0;
  for (const AdjustmentPtr& a : every_type()) {
    CHECK(check(*a));
    n++;
  }
  enable_test_matrix();  // by name, as props name it (D40's tests)
  CHECK(check("Test Matrix", R"({"m":[0.5,0.5,0,0,0,1,0,0,0,0,1,0],"power":[1,1,0.25]})"));
  enable_test_matrix(false);
  CHECK(!check("Levels", "{"));
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  std::printf("test_gpu_adjust: %d adjustments x %zu colours alike, %.0f ms\n", n + 1, check.pixels(), ms);
}

namespace {

// A data tile of format f at tile (tx, ty) whose pixel at document (x, y) is px(x, y).
TilePtr data_tile(Format f, int tx, int ty, const std::function<Pixel(int, int)>& px) {
  const std::shared_ptr<Tile> t = Tile::make(f);
  {
    const WritePin w = t->write();
    for (int y = 0; y < S; y++)
      for (int x = 0; x < S; x++) {
        const Pixel p = px(tx * S + x, ty * S + y);
        for (int c = 0; c < f.channels; c++) {
          const size_t at = (size_t(y) * S + size_t(x)) * size_t(f.channels) + size_t(c);
          if (f.depth == Depth::U16)
            reinterpret_cast<uint16_t*>(w.data())[at] = p[size_t(c)];
          else
            w.data()[at] = uint8_t(p[size_t(c)]);
        }
      }
  }
  return t;
}

// Every sample of tiles r of g, row by row, as numbers.
std::vector<uint16_t> samples(const TileGrid& g, TileRect r) {
  const Format f = g.format();
  const int w = (r.x1 - r.x0) * S, h = (r.y1 - r.y0) * S;
  std::vector<uint8_t> raw(size_t(w) * size_t(h) * size_t(f.bytes()));
  g.read_rect(r.x0 * S, r.y0 * S, w, h, raw.data(), size_t(w) * size_t(f.bytes()));
  std::vector<uint16_t> out(size_t(w) * size_t(h) * size_t(f.channels));
  for (size_t i = 0; i < out.size(); i++)
    out[i] = f.depth == Depth::U16 ? reinterpret_cast<const uint16_t*>(raw.data())[i] : raw[i];
  return out;
}

int floor_div(int v, int d) { return v >= 0 ? v / d : -((-v + d - 1) / d); }

// What apply_adjustment() should give over tiles r, pixel by pixel: `in`'s samples as floats (to_unit()), adjusted by
// the selection's value there (null: everywhere; past a canvas W x H: nowhere), v (1 - s) + A(v) s to the nearest
// level, alpha kept. Where `in` has no tile, a layer's pixels stay (transparent: no colour); a mask's are its fill,
// adjusted where empty_changes(tx, ty).
std::vector<uint16_t> reference(const TileGrid& in, const Adjustment& a, const TileGrid* sel, int W, int H, TileRect r,
                                const std::function<bool(int, int)>& empty_changes) {
  const Format f = in.format();
  const int w = (r.x1 - r.x0) * S, h = (r.y1 - r.y0) * S, ch = f.channels, colors = ch == 4 ? 3 : 1;
  const int g = ch == 4 ? 1 : 0, b = ch == 4 ? 2 : 0;
  std::vector<uint16_t> v = samples(in, r), s;
  if (sel) s = samples(*sel, r);
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      const int dx = r.x0 * S + x, dy = r.y0 * S + y, tx = floor_div(dx, S), ty = floor_div(dy, S);
      if (!in.get(tx, ty) && (f.alpha() || !empty_changes(tx, ty))) continue;
      const bool past = W > 0 && (dx < 0 || dy < 0 || dx >= W || dy >= H);
      const float sw = !sel ? 1 : past ? 0 : to_unit(s[size_t(y) * size_t(w) + size_t(x)], sel->format().depth);
      if (!(sw > 0)) continue;
      uint16_t* p = &v[(size_t(y) * size_t(w) + size_t(x)) * size_t(ch)];
      const std::array<float, 3> c = {to_unit(p[0], f.depth), to_unit(p[g], f.depth), to_unit(p[b], f.depth)};
      const std::array<float, 3> o = a.apply(c, dx, dy);
      for (int k = 0; k < colors; k++)
        p[k] = from_unit(sw < 1 ? c[size_t(k)] * (1 - sw) + o[size_t(k)] * sw : o[size_t(k)], f.depth);
    }
  return v;
}

// Random pixels of format f: colours anything, alpha a quarter each 0 and full, the rest anything.
Pixel random_pixel(Format f, std::mt19937& rng) {
  const int top = max_value(f.depth);
  std::uniform_int_distribution<int> any(0, top);
  Pixel p{};
  for (int c = 0; c < f.channels; c++) p[size_t(c)] = uint16_t(any(rng));
  if (f.alpha()) {
    const int k = int(rng() % 4);
    p[size_t(f.channels - 1)] = uint16_t(k == 0 ? 0 : k == 1 ? top : any(rng));
  }
  return p;
}

// A layer over a 900 x 600 canvas: tiles of data (two past the canvas), solid ones (opaque, opaque black,
// half-transparent) and empty ones.
TileGrid layer(Format f, uint32_t seed) {
  std::mt19937 rng(seed);
  const int top = max_value(f.depth);
  const auto scale = [&](int v) { return uint16_t(int64_t(v) * top / 65535); };
  TileGrid g(f);
  const auto data = [&](int tx, int ty) {
    g.set(tx, ty, data_tile(f, tx, ty, [&](int, int) { return random_pixel(f, rng); }));
  };
  const auto solid = [&](int tx, int ty, int r, int gr, int b, int a) {
    const Pixel p = f.channels == 4 ? Pixel{scale(r), scale(gr), scale(b), scale(a)}
                                    : Pixel{scale(r), uint16_t(f.alpha() ? scale(a) : 0), 0, 0};
    g.set(tx, ty, Tile::solid(f, p));
  };
  data(-1, 0), data(0, 0), solid(1, 0, 1000, 30000, 60000, 65535), data(3, 0);
  solid(0, 1, 0, 0, 0, 65535), data(1, 1), solid(2, 1, 40000, 20000, 10000, 30000);
  data(0, 2), data(1, 2), data(4, 2);
  return g;
}

constexpr int kW = 900, kH = 600;
const TileRect kAll{-1, -1, 5, 4};  // every tile of layer() and around

// A soft round selection on the canvas (full within 250 px of (450, 300), none past 330), what lies past the canvas
// in its edge tiles all selected (meaning nothing: A40), tile (1, 0) a third selected, (2, 1) none.
TileGrid round_selection(Depth d) {
  const Format f{d, 1};
  const int top = max_value(d);
  TileGrid sel(f);
  for (int ty = 0; ty * S < kH; ty++)
    for (int tx = 0; tx * S < kW; tx++) {
      const TilePtr t = data_tile(f, tx, ty, [&](int x, int y) {
        if (x >= kW || y >= kH) return Pixel{uint16_t(top)};
        const double dist = std::hypot(x + 0.5 - 450, y + 0.5 - 300);
        return Pixel{uint16_t(std::lround(std::clamp((330 - dist) / 80, 0., 1.) * top))};
      });
      sel.set(tx, ty, compact(t, sel.fill()));
    }
  sel.set(1, 0, Tile::solid(f, {uint16_t(top / 3)}));
  sel.set(2, 1, nullptr);
  return sel;
}

const char* const kMatrix = R"({"m":[0.9,0.2,-0.15,0,0.1,0.7,0.25,0,-0.05,0.3,0.8,0],"power":[2,1,0.5]})";

bool same(const TileGrid& a, const TileGrid& b, int tx, int ty) { return a.get(tx, ty) == b.get(tx, ty); }

}  // namespace

// apply_adjustment() with the test matrix against reference(): RGBA and grey layers in U8 and U16, everywhere and in a
// soft selection (stopping at the canvas, or not without one); the tiles nothing changes keep their ids, empty ones
// stay empty, a solid one comes out solid, alpha stays.
TEST(test_adjust_apply) {
  const AdjustmentPtr m = adjustment(AdjType::TestMatrix, kMatrix);
  const auto never = [](int, int) { return false; };
  for (Format f : {Format{Depth::U16, 4}, Format{Depth::U8, 4}, Format{Depth::U16, 2}, Format{Depth::U8, 2}}) {
    const TileGrid in = layer(f, uint32_t(f.channels * 10 + int(f.depth)));
    const std::vector<uint16_t> before = samples(in, kAll);
    // Everywhere.
    TileGrid out = apply_adjustment(in, *m, {nullptr, kW, kH});
    CHECK(samples(out, kAll) == reference(in, *m, nullptr, kW, kH, kAll, never));
    CHECK(out.fill() == in.fill() && out.tiles().size() == in.tiles().size());
    CHECK(!out.get(2, 0) && !out.get(3, 1));  // empty: transparent
    CHECK(same(out, in, 0, 1));                // opaque black: the matrix leaves it
    CHECK(!same(out, in, 0, 0) && !same(out, in, -1, 0) && !same(out, in, 4, 2));  // past the canvas too
    CHECK(out.get(1, 0)->is_solid() && out.get(2, 1)->is_solid());
    // In a soft selection, stopping at the canvas: past it (and where nothing is selected) the tiles stay.
    const TileGrid sel = round_selection(f.depth);
    out = apply_adjustment(in, *m, {&sel, kW, kH});
    CHECK(samples(out, kAll) == reference(in, *m, &sel, kW, kH, kAll, never));
    CHECK(same(out, in, -1, 0) && same(out, in, 4, 2) && same(out, in, 2, 1) && same(out, in, 0, 1));
    CHECK(!same(out, in, 1, 1) && !same(out, in, 3, 0));
    CHECK(out.get(1, 0)->is_solid() && out.get(1, 0)->value() != in.get(1, 0)->value());  // a third of the way
    // Without a canvas the selection reads as it is: its edge tiles' pixels past the canvas select.
    out = apply_adjustment(in, *m, {&sel});
    CHECK(samples(out, kAll) == reference(in, *m, &sel, 0, 0, kAll, never));
    // The identity shares every tile; the grid given never changes.
    out = apply_adjustment(in, *adjustment(AdjType::TestMatrix, "{}"), {&sel, kW, kH});
    for (const auto& [xy, t] : in.tiles()) CHECK(out.get(xy.x, xy.y) == t.get());
    CHECK(samples(in, kAll) == before);
  }
  const TileGrid rgba(Format{Depth::U8, 4});
  bool threw = false;
  try {
    apply_adjustment(rgba, *m, {&rgba});
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  CHECK(threw);

  // A larger layer, for the time it takes: 2048 x 2048 RGBA U16, every pixel.
  TileGrid big(Format{Depth::U16, 4});
  std::mt19937 rng(7);
  for (int ty = 0; ty < 8; ty++)
    for (int tx = 0; tx < 8; tx++)
      big.set(tx, ty, data_tile(big.format(), tx, ty, [&](int, int) { return random_pixel(big.format(), rng); }));
  const auto t0 = std::chrono::steady_clock::now();
  const TileGrid done = apply_adjustment(big, *m);
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  CHECK(done.tiles().size() == 64);
  std::printf("test_adjust_apply: 2048 x 2048 RGBA U16 in %.1f ms (%.0f Mpx/s)\n", ms, 4.194304 / ms * 1000);
}

// On a mask (1 channel): stored tiles as a layer's; its empty ones are its fill, white here. With nothing selected
// the fill becomes the adjusted one where that is black or white (Invert) or no canvas is given, else the canvas's
// empty tiles become tiles of it and past the canvas it stays white; with a selection, the empty tiles under it.
TEST(test_adjust_mask) {
  const char* const invert = R"({"m":[-1,0,0,1,0,-1,0,1,0,0,-1,1]})";
  const char* const half = R"({"m":[0.5,0,0,0,0,0.5,0,0,0,0,0.5,0]})";
  for (Depth d : {Depth::U16, Depth::U8}) {
    const Format f{d, 1};
    const uint16_t white = uint16_t(max_value(d));
    std::mt19937 rng(int(d) + 5);
    TileGrid in(f, {white});
    in.set(0, 0, data_tile(f, 0, 0, [&](int, int) { return random_pixel(f, rng); }));
    in.set(1, 0, Tile::solid(f, {uint16_t(white / 2 - 7)}));
    in.set(2, 1, Tile::solid(f, {0}));
    in.set(5, 5, data_tile(f, 5, 5, [&](int, int) { return random_pixel(f, rng); }));  // past the canvas
    const TileRect r{-1, -1, 6, 6};
    const auto all = [](int, int) { return true; };
    const auto on_canvas = [](int tx, int ty) { return tx >= 0 && ty >= 0 && tx * S < kW && ty * S < kH; };
    // Invert: the fill turns black, the empty tiles stay empty.
    const AdjustmentPtr inv = adjustment(AdjType::TestMatrix, invert);
    TileGrid out = apply_adjustment(in, *inv, {nullptr, kW, kH});
    CHECK(out.fill()[0] == 0 && out.tiles().size() == in.tiles().size());
    CHECK(samples(out, r) == reference(in, *inv, nullptr, kW, kH, r, all));
    CHECK(out.get(2, 1) == nullptr || out.get(2, 1)->value()[0] == white);
    // Half: grey is no fill for a mask: the canvas's empty tiles become grey, past it white stays.
    const AdjustmentPtr h = adjustment(AdjType::TestMatrix, half);
    out = apply_adjustment(in, *h, {nullptr, kW, kH});
    CHECK(out.fill()[0] == white);
    CHECK(samples(out, r) == reference(in, *h, nullptr, kW, kH, r, on_canvas));
    CHECK(out.get(3, 2) && out.get(3, 2)->is_solid() && !out.get(4, 0) && !out.get(-1, 0));
    // Without a canvas the plane goes grey.
    out = apply_adjustment(in, *h);
    CHECK(out.fill()[0] == from_unit(0.5f, d) && samples(out, r) == reference(in, *h, nullptr, 0, 0, r, all));
    // In a selection: the fill stays, the empty tiles it touches change by it.
    const TileGrid sel = round_selection(d);
    out = apply_adjustment(in, *h, {&sel, kW, kH});
    CHECK(out.fill()[0] == white && samples(out, r) == reference(in, *h, &sel, kW, kH, r, all));
    CHECK(same(out, in, 5, 5) && !out.get(-1, 0));
    out = apply_adjustment(in, *inv, {&sel, kW, kH});
    CHECK(out.fill()[0] == white && samples(out, r) == reference(in, *inv, &sel, kW, kH, r, all));
  }
}

// Cancelled from the progress callback after the first batch: Cancelled, nothing given back; set before: at once.
// Uncancelled, the progress rises to 1.
TEST(test_adjust_cancel) {
  const Format f{Depth::U8, 2};
  TileGrid in(f);
  for (int i = 0; i < 200; i++) in.set(i % 20, i / 20, Tile::solid(f, {uint16_t(i), 255}));
  const AdjustmentPtr m = adjustment(AdjType::TestMatrix, kMatrix);
  std::vector<double> seen;
  AdjustOptions o;
  o.cancel = make_token();
  o.progress = [&](double p) {
    seen.push_back(p);
    o.cancel->store(true);
  };
  bool cancelled = false;
  try {
    apply_adjustment(in, *m, o);
  } catch (const Cancelled&) {
    cancelled = true;
  }
  CHECK(cancelled && seen.size() == 1 && seen[0] > 0 && seen[0] < 1);
  for (int i = 0; i < 200; i++) CHECK(in.get(i % 20, i / 20)->value()[0] == i);
  seen.clear();
  cancelled = false;
  try {
    apply_adjustment(in, *m, o);  // the token still set
  } catch (const Cancelled&) {
    cancelled = true;
  }
  CHECK(cancelled && seen.empty());
  o.cancel = make_token();
  o.progress = [&](double p) { seen.push_back(p); };
  const TileGrid out = apply_adjustment(in, *m, o);
  CHECK(seen.size() == 4 && std::is_sorted(seen.begin(), seen.end()) && seen.back() == 1);
  CHECK(out.get(0, 0) == in.get(0, 0) && out.get(19, 9)->value()[0] != in.get(19, 9)->value()[0]);
}

namespace {

LayerProps named(const char* name) {
  LayerProps p;
  p.name = name;
  return p;
}

template <class F>
bool throws(const F& f) {
  try {
    f();
  } catch (const std::invalid_argument&) {
    return true;
  }
  return false;
}

}  // namespace

// The dialog's preview: an adjustment layer clipped right above the layer, its mask the selection, the next id, the
// layers selected as they were; the document given unchanged; only pixel layers, the selection in the mask format, the
// adjustment of the document's mode.
TEST(test_adjust_preview) {
  DocPtr doc = new_document(kW, kH, Mode::Rgb, Depth::U16, nullptr);
  doc = run(*doc, AddLayer{0, 0, Kind::Pixel, named("A")});  // 1
  doc = run(*doc, AddLayer{0, 1, Kind::Group, named("G")});  // 2
  doc = run(*doc, AddLayer{2, 0, Kind::Pixel, named("B")});  // 3, in G
  doc = run(*doc, AddLayer{2, 1, Kind::Pixel, named("C")});  // 4, in G above B
  doc = run(*doc, SelectLayers{{1, 4}});
  const auto sel = std::make_shared<const TileGrid>(round_selection(Depth::U16));
  const AdjustmentPtr a = adjustment("Levels", R"({"gam":2})");
  const DocPtr p = preview_state(*doc, 3, *a, sel);
  validate(*p);
  const Layer& g = *find_layer(*p, 2);
  CHECK(g.children.size() == 3 && g.children[0]->id == 3 && g.children[2]->id == 4);
  const Layer& l = *g.children[1];
  CHECK(l.kind == Kind::Adj && l.id == doc->next_id && l.props.adj == "Levels" && l.props.clip && l.mask == sel);
  CHECK(l.props.vals && *l.props.vals == a->vals() && adjustment(l.props) == a);
  CHECK(p->selected == doc->selected && p->next_id == doc->next_id + 1);
  CHECK(find_layer(*doc, 2)->children.size() == 2);  // the document as it was
  // No selection: no mask; at the root.
  const DocPtr q = preview_state(*doc, 1, *a);
  CHECK(q->root->children.size() == 3 && q->root->children[1]->kind == Kind::Adj && !q->root->children[1]->mask);
  // Refused.
  CHECK(throws([&] { preview_state(*doc, 2, *a); }));   // a group
  CHECK(throws([&] { preview_state(*doc, 99, *a); }));  // none
  CHECK(throws([&] { preview_state(*doc, 3, *a, std::make_shared<const TileGrid>(round_selection(Depth::U8))); }));
  DocPtr grey = new_document(kW, kH, Mode::Gray, Depth::U8, nullptr);
  grey = run(*grey, AddLayer{0, 0, Kind::Pixel, named("A")});
  CHECK(throws([&] { preview_state(*grey, 1, *adjustment("Hue/Saturation")); }));
  const auto grey_sel = std::make_shared<const TileGrid>(round_selection(Depth::U8));
  validate(*preview_state(*grey, 1, *adjustment("Curves"), grey_sel));
}

namespace {

// The histogram written out over pixels x, y, w, h of g (level 0): each pixel's levels, its weight alpha times the
// selection, as histogram.h says, apart from tiles and mips.
Histogram reference_histogram(const TileGrid& g, int x, int y, int w, int h, const TileGrid* sel) {
  const Format f = g.format();
  const int ch = f.channels;
  std::vector<uint8_t> raw(size_t(w) * size_t(h) * size_t(f.bytes()));
  g.read_rect(x, y, w, h, raw.data(), size_t(w) * size_t(f.bytes()));
  std::vector<uint8_t> sraw;
  if (sel) {
    sraw.resize(size_t(w) * size_t(h) * size_t(sel->format().bytes()));
    sel->read_rect(x, y, w, h, sraw.data(), size_t(w) * size_t(sel->format().bytes()));
  }
  const auto at = [](const std::vector<uint8_t>& v, Depth d, size_t i) -> unsigned {
    return d == Depth::U16 ? reinterpret_cast<const uint16_t*>(v.data())[i] : v[i];
  };
  const auto level = [&](unsigned v) {
    return f.depth == Depth::U16 ? unsigned(std::lround(v * 255.0 / 65535)) : v;
  };
  std::array<uint64_t, 1024> sums{};
  uint64_t total = 0;
  for (size_t i = 0; i < size_t(w) * size_t(h); i++) {
    const unsigned r = at(raw, f.depth, i * size_t(ch)), gr = ch == 4 ? at(raw, f.depth, i * 4 + 1) : r,
                   b = ch == 4 ? at(raw, f.depth, i * 4 + 2) : r;
    const uint64_t wt = uint64_t(f.alpha() ? at(raw, f.depth, i * size_t(ch) + size_t(ch) - 1) : 1) *
                        (sel ? at(sraw, sel->format().depth, i) : 1);
    const unsigned lum = ch == 4 ? (30 * r + 59 * gr + 11 * b + 50) / 100 : r;
    sums[level(r)] += wt, sums[256 + level(gr)] += wt, sums[512 + level(b)] += wt, sums[768 + level(lum)] += wt;
    total += wt;
  }
  const double unit = double(f.alpha() ? max_value(f.depth) : 1) * double(sel ? max_value(sel->format().depth) : 1);
  Histogram out;
  for (size_t c = 0; c < 4; c++)
    for (size_t v = 0; v < 256; v++) out.counts[c][v] = double(sums[c * 256 + v]) / unit;
  out.total = double(total) / unit;
  return out;
}

bool equal(const Histogram& a, const Histogram& b) { return a.counts == b.counts && a.total == b.total; }

// A grid whose pixel (x, y) is block (x >> shift, y >> shift)'s value, over tiles r (document pixels of the grid).
TileGrid blocks(Format f, Pixel fill, TileRect r, int shift, const std::function<Pixel(int, int)>& block) {
  TileGrid g(f, fill);
  for (int ty = r.y0; ty < r.y1; ty++)
    for (int tx = r.x0; tx < r.x1; tx++)
      g.set(tx, ty, data_tile(f, tx, ty, [&](int x, int y) { return block(x >> shift, y >> shift); }));
  return g;
}

}  // namespace

// histogram() against reference_histogram(): at level 0 over the canvas; at level 2 of blocks of 4 x 4 one colour
// (each block a pixel there, exactly) against the grid of the blocks, and 16 times fewer; a region off the level's
// grid; a soft selection; transparent pixels; 16-bit levels at their edges; grey layers and masks with their fill.
TEST(test_histogram) {
  const Format f{Depth::U16, 4};
  const auto block = [](int bx, int by) {
    const std::array<uint32_t, 4> h = hash4(uint32_t(bx), uint32_t(by), 31, 0);
    const uint16_t a = uint16_t(h[3] % 3 == 0 ? 0 : h[3] % 3 == 1 ? 65535 : h[3] >> 16);
    // Colours at the 8-bit levels' edges (257 k + 128, 257 k + 129) half the time.
    const auto c = [&](uint32_t v) {
      return uint16_t(v & 1 ? std::min(65535u, ((v >> 1) % 256) * 257 + 128 + (v >> 9) % 2) : v >> 16);
    };
    return Pixel{c(h[0]), c(h[1]), c(h[2]), a};
  };
  const auto sel_block = [](int bx, int by) {
    const uint32_t h = hash3(uint32_t(bx), uint32_t(by), 77);
    return Pixel{uint16_t(h % 4 == 0 ? 0 : h % 4 == 1 ? 65535 : h >> 16)};
  };
  // Level 0: blocks of 4 over tiles (-1..2, 0..1), the canvas 500 x 300; level 2 is the grid of the blocks.
  const TileGrid g = blocks(f, {}, {-1, 0, 3, 2}, 2, block);
  const TileGrid g2 = blocks(f, {}, {-1, 0, 1, 1}, 0, block);
  const TileGrid sel = blocks({Depth::U16, 1}, {}, {0, 0, 3, 2}, 2, sel_block);
  const TileGrid sel2 = blocks({Depth::U16, 1}, {}, {0, 0, 1, 1}, 0, sel_block);
  for (const TileGrid* s : {static_cast<const TileGrid*>(nullptr), &sel}) {
    const Histogram h0 = histogram(g, 0, 0, 0, 500, 300, s), h2 = histogram(g, 2, 0, 0, 500, 300, s);
    CHECK(equal(h0, reference_histogram(g, 0, 0, 500, 300, s)));
    CHECK(equal(h2, reference_histogram(g2, 0, 0, 125, 75, s ? &sel2 : nullptr)));
    for (size_t c = 0; c < 4; c++)
      for (size_t v = 0; v < 256; v++) CHECK(h0.counts[c][v] == 16 * h2.counts[c][v]);
    CHECK(h0.total > 0 && h0.total < 500 * 300);  // transparent pixels and the selection count in part
    // A region off the level's grid: the level's pixels partly in it count.
    CHECK(equal(histogram(g, 2, 2, 5, 13, 9, s), reference_histogram(g2, 0, 1, 4, 3, s ? &sel2 : nullptr)));
    // Past the canvas, to the left, and in the empty tiles to the right (transparent): what is there.
    CHECK(equal(histogram(g, 0, -300, 10, 1200, 40, s), reference_histogram(g, -300, 10, 1200, 40, s)));
  }
  double sum = 0;
  const Histogram all = histogram(g, 1, 0, 0, 500, 300);
  for (double n : all.counts[Histogram::Luminosity]) sum += n;
  CHECK(std::fabs(sum - all.total) <= 1e-9 * all.total && all.total > 0);  // the levels add up to the total
  // A grey layer (U8) and a mask (U16, white where empty): their grey in all four.
  const TileGrid grey = blocks({Depth::U8, 2}, {}, {0, 0, 2, 2}, 2, [&](int bx, int by) {
    const Pixel p = block(bx, by);
    return Pixel{uint16_t(p[0] >> 8), uint16_t(p[3] >> 8), 0, 0};
  });
  const Histogram gh = histogram(grey, 0, 0, 0, 500, 300, &sel);
  CHECK(equal(gh, reference_histogram(grey, 0, 0, 500, 300, &sel)));
  CHECK(gh.counts[0] == gh.counts[1] && gh.counts[0] == gh.counts[3]);
  TileGrid mask = blocks({Depth::U16, 1}, {65535}, {0, 0, 1, 1}, 2, sel_block);
  mask.set(2, 0, Tile::solid({Depth::U16, 1}, {1000}));
  const Histogram mh = histogram(mask, 0, 0, 0, 900, 300);
  CHECK(equal(mh, reference_histogram(mask, 0, 0, 900, 300, nullptr)) && mh.total == 900 * 300);
  // Its empty tiles count white: tile (1, 0), the canvas's part of (3, 0) and the rows below tile row 0; the solid
  // tile its level 4 (1000 x 255 / 65535 = 3.9).
  CHECK(mh.counts[0][255] >= 256 * 256 + 132 * 256 + 900 * 44 && mh.counts[0][4] >= 256 * 256);
  CHECK(throws([&] { histogram(g, 32, 0, 0, 1, 1); }) && throws([&] { histogram(g, 0, 0, 0, 1, 1, &g); }));
  CHECK(histogram(g, 0, 0, 0, 0, 10).total == 0);
}

// tools/ps-golden/README-psd.md's sets (what E33's and D32's index.json and M43 write): each channel's, range's,
// tone's or colour's values in an object by its selector's name, kept as they are for D33-D37 to read; Color Lookup's
// .cube text; Gradient Map's gradient (gradient.h's JSON, read to check it). Written back byte for byte.
TEST(test_adjust_sets) {
  const char* const gradient = R"({"alphas":[{"a":1,"m":0.5,"p":0},{"a":0.5,"m":0.5,"p":1}],)"
                               R"("colors":[{"c":[10,20,30],"m":0.3,"p":0},{"c":[255,255,255],"m":0.5,"p":1}],)"
                               R"("smoothness":100})";
  const std::string map = std::string(R"({"gradient":)") + gradient + R"(,"method":"Linear","rev":true})";
  const std::pair<const char*, std::string> good[] = {
      {"Levels", R"({"Blue":{"in1":240},"RGB":{"gam":1.2,"in0":5},"Red":{"in0":10,"out1":250}})"},
      {"Curves", R"({"pts":{"RGB":[[0,0],[128,140],[255,255]],"Red":[[0,10],[255,245]]}})"},
      {"Hue/Saturation", R"({"Master":{"h":20,"l":0,"s":10},"Reds":{"h":-15,"s":30,"span":[300,330,20,50]},)"
                         R"("colorize":true})"},
      {"Color Balance", R"({"Highlights":{"cr":-10},"Midtones":{"cr":20,"mg":-5,"yb":7},"Shadows":{"yb":30},)"
                        R"("pl":false})"},
      {"Channel Mixer", R"({"Blue":{"b":80,"k":5},"Green":{"g":90},"Red":{"b":10,"g":-20,"r":110}})"},
      {"Channel Mixer", R"({"Gray":{"b":20,"g":40,"r":40},"mono":true})"},
      {"Selective Color", R"({"Blacks":{"k":15},"Neutrals":{"c":-5},"Reds":{"c":-20,"m":10,"y":5},"rel":1})"},
      {"Color Lookup", R"({"dither":true,"lf0":"identity 17.cube","lut":"LUT_3D_SIZE 2\n0 0 0\n1 0 0\n"})"},
      {"Gradient Map", map},
  };
  for (const auto& [name, vals] : good) {
    const AdjustmentPtr a = adjustment(name, vals);
    if (!a || a->vals() != vals)
      std::printf("test_adjust_sets: %s %s: %s\n", name, vals.c_str(), a ? a->vals().c_str() : "null");
    CHECK(a && a->vals() == vals);
  }
  const std::pair<const char*, const char*> bad[] = {
      {"Levels", R"({"Red":3})"},       {"Hue/Saturation", R"({"Reds":[1]})"},
      {"Color Balance", R"({"Shadows":"x"})"}, {"Channel Mixer", R"({"Gray":1})"},
      {"Selective Color", R"({"Whites":true})"}, {"Color Lookup", R"({"lut":5})"},
      {"Gradient Map", R"({"gradient":{"colors":[]}})"}, {"Gradient Map", R"({"gradient":7})"},
  };
  for (const auto& [name, vals] : bad) CHECK(!adjustment(name, vals));
}
