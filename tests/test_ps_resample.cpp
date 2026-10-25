// T38: Photoshop's images of Image Size, Free Transform, Warp, Image Rotation and Canvas Size
// (tools/ps-golden/resample.jsx; tests/golden/ps/resample/ once B33 brought them). The inputs as pb-bench ps-resample
// writes them (tests/ps_resample_inputs.h); the index as the tests of T33, X30, X31, X32 and T34 read it, on the
// hand-written sample (tests/golden/ps/resample/sample/index.json: lines as PB.offline writes them) and on the set;
// what Photoshop's ramps say of the matrices and meshes it read back. The comparisons of those tasks wait for their
// code: each test says so ("chưa có <task>") and, while the images are not there, that it is skipped, as B4's.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "check.h"
#include "golden.h"
#include "ps_resample_inputs.h"
#include "render.h"

using namespace pb;
namespace fs = std::filesystem;
namespace rs = golden::resample;
using nlohmann::json;

namespace {

const fs::path kSet = golden::kRoot / "resample";
const fs::path kIn = kSet / "in";
const fs::path kPhoto = fs::exists(PB_SOURCE_DIR "/ui/mock/photos/monan.jpg") ? fs::path(PB_SOURCE_DIR "/ui/mock/photos/monan.jpg")
                                                                              : fs::path("ui/mock/photos/monan.jpg");
constexpr uintmax_t kBudget = 3u << 20;  // the set's share of tests/golden/ps (decision 6 of docs/p1-tasks.md)

// Whether tests/golden/ps and the photo are here: in the source tree always, beside a Windows .exe only when copied.
bool have_tree(const char* test) {
  if (fs::exists(kIn) && fs::exists(kPhoto)) return true;
  std::printf("%s: skipped, no %s or %s here (a Windows .exe: copy tests/golden/ps and ui/mock/photos/monan.jpg "
              "beside it)\n", test, kIn.generic_string().c_str(), kPhoto.generic_string().c_str());
  return false;
}

std::string bytes(const fs::path& path) {
  std::ifstream f(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

// A folder of its own, removed with it.
struct TempDir {
  fs::path path = fs::temp_directory_path() / ("pb-resample-" + std::to_string(std::random_device{}()));
  TempDir() { fs::create_directories(path); }
  ~TempDir() {
    std::error_code e;
    fs::remove_all(path, e);
  }
};

uintmax_t folder_size(const fs::path& dir) {
  uintmax_t n = 0;
  for (const auto& f : fs::recursive_directory_iterator(dir))
    if (f.is_regular_file()) n += f.file_size();
  return n;
}

// ---- An entry as T33, X30, X31, X32 and T34 read it ---------------------------------------------------------------

// Interp of an index's name: LayerWarp's and the ops' interp, Image Size's method (but preserveDetails, P3's).
std::optional<Interp> interp_of(const std::string& name) {
  static const std::pair<const char*, Interp> kNames[] = {
      {"nearestNeighbor", Interp::Nearest}, {"bilinear", Interp::Bilinear},
      {"bicubic", Interp::Bicubic},         {"bicubicSmoother", Interp::BicubicSmoother},
      {"bicubicSharper", Interp::BicubicSharper}, {"automatic", Interp::Automatic}};
  for (const auto& n : kNames)
    if (name == n.first) return n.second;
  return std::nullopt;
}

using Matrix = std::array<double, 9>;  // by rows, x' = (m0 x + m1 y + m2) / (m6 x + m7 y + m8)

std::optional<Matrix> matrix_of(const json& j) {
  if (!j.is_array() || j.size() != 9) return std::nullopt;
  Matrix m{};
  for (size_t i = 0; i < 9; i++) {
    if (!j[i].is_number()) return std::nullopt;
    m[i] = j[i].get<double>();
  }
  return m;
}

std::array<double, 2> apply(const Matrix& m, double x, double y) {
  const double w = m[6] * x + m[7] * y + m[8];
  return {(m[0] * x + m[1] * y + m[2]) / w, (m[3] * x + m[4] * y + m[5]) / w};
}

Matrix multiply(const Matrix& p, const Matrix& q) {
  Matrix out{};
  for (int r = 0; r < 3; r++)
    for (int c = 0; c < 3; c++) out[size_t(3 * r + c)] = p[size_t(3 * r)] * q[size_t(c)] + p[size_t(3 * r + 1)] * q[size_t(3 + c)] + p[size_t(3 * r + 2)] * q[size_t(6 + c)];
  return out;
}

// numbers: a non-empty array of numbers, n long when n.
bool numbers(const json& j, size_t n = 0) {
  if (!j.is_array() || j.empty() || (n && j.size() != n)) return false;
  return std::all_of(j.begin(), j.end(), [](const json& v) { return v.is_number(); });
}

// An op's warp as A30's LayerWarp, its layers left to the caller: matrix, mesh (A30's BezierMesh) when there, interp.
// nullopt when it is not one.
std::optional<LayerWarp> warp_of(const json& j) {
  if (!j.is_object() || !j.contains("matrix") || !j.contains("interp") || !j["interp"].is_string()) return std::nullopt;
  const auto m = matrix_of(j["matrix"]);
  const auto interp = interp_of(j["interp"].get<std::string>());
  if (!m || !interp) return std::nullopt;
  LayerWarp w;
  w.matrix = *m;
  w.interp = *interp;
  if (j.contains("mesh")) {
    const json& mesh = j["mesh"];
    if (!mesh.is_object() || !mesh.value("cols", json()).is_number_integer() || !mesh.value("rows", json()).is_number_integer())
      return std::nullopt;
    BezierMesh& b = w.mesh;
    b.cols = mesh["cols"].get<int>(), b.rows = mesh["rows"].get<int>();
    if (b.cols < 1 || b.rows < 1 || !numbers(mesh.value("xs", json()), size_t(b.cols + 1)) ||
        !numbers(mesh.value("ys", json()), size_t(b.rows + 1)) || !mesh.value("points", json()).is_array() ||
        mesh["points"].size() != size_t(3 * b.cols + 1) * size_t(3 * b.rows + 1))
      return std::nullopt;
    for (const json& x : mesh["xs"]) b.xs.push_back(x.get<double>());
    for (const json& y : mesh["ys"]) b.ys.push_back(y.get<double>());
    if (!std::is_sorted(b.xs.begin(), b.xs.end(), std::less_equal<double>()) ||
        !std::is_sorted(b.ys.begin(), b.ys.end(), std::less_equal<double>()))
      return std::nullopt;
    for (const json& p : mesh["points"]) {
      if (!numbers(p, 2)) return std::nullopt;
      b.points.push_back({p[0].get<double>(), p[1].get<double>()});
    }
  }
  return w;
}

// Where patch (0, 0) of a mesh takes (s, t), 0..1 across and down: its bicubic Bezier surface.
std::array<double, 2> bezier(const BezierMesh& b, double s, double t) {
  const auto basis = [](double u, double* k) {
    const double v = 1 - u;
    k[0] = v * v * v, k[1] = 3 * u * v * v, k[2] = 3 * u * u * v, k[3] = u * u * u;
  };
  double ks[4], kt[4];
  basis(s, ks), basis(t, kt);
  std::array<double, 2> p{0, 0};
  const size_t across = size_t(3 * b.cols + 1);
  for (size_t j = 0; j < 4; j++)
    for (size_t i = 0; i < 4; i++)
      for (size_t c = 0; c < 2; c++) p[c] += ks[i] * kt[j] * b.points[j * across + i][c];
  return p;
}

// The box of the padded inputs, the transforms' and warps': [left, top, right, bottom].
constexpr double kBox[4] = {rs::kPadX, rs::kPadY, rs::kPadX + rs::kW, rs::kPadY + rs::kH};

// The ops' names and keys as resample.jsx writes them, in order.
const std::vector<std::pair<std::string, std::vector<std::string>>> kOps = {
    {"imageSize", {"op", "w", "h", "method"}},
    {"rotateCanvas", {"op", "angle", "interp", "color"}},
    {"canvasSize", {"op", "w", "h", "anchor", "color"}},
    {"freeTransform", {"op", "w", "h", "angle", "skew", "corners", "interp", "transform", "warp"}},
    {"warp", {"op", "style", "bend", "h", "v", "orientation", "interp", "warp"}}};
const char* const kStyles[] = {"arc",  "arcLower", "arcUpper", "arch",    "bulge",   "shellLower", "shellUpper", "flag",
                               "wave", "fish",     "rise",     "fisheye", "inflate", "squeeze",    "twist",      "custom"};

bool colour(const json& j) {  // null or '#rrggbb'
  if (j.is_null()) return true;
  if (!j.is_string()) return false;
  const std::string s = j.get<std::string>();
  return s.size() == 7 && s[0] == '#' && s.find_first_not_of("0123456789abcdef", 1) == std::string::npos;
}

// What is wrong with an entry, empty when nothing: its keys, ops and their types as resample.jsx writes them; a
// transform's matrix and LayerWarp's inverse of each other; a custom warp's mesh over the box; the size the ops leave
// (Image Rotation's is the script's guess: Photoshop's own, after B33, is said where it differs, not wrong).
std::vector<std::string> problems_of(const golden::Entry& e) {
  std::vector<std::string> out;
  const auto bad = [&](const std::string& why) { out.push_back(e.name + ": " + why); };
  const json& j = e.json;
  if (!numbers(j.value("canvas", json()), 2) || !numbers(j.value("size", json()), 2) || !j.contains("ops") ||
      !j["ops"].is_array() || j["ops"].empty() || !j.value("layers", json()).is_array() || j["layers"].size() != 1)
    return bad("not a case of resample.jsx: " + j.dump().substr(0, 200)), out;
  const int bits = j.value("bits", 0);
  if (bits != 8 && bits != 16) bad("bits " + std::to_string(bits));
  int w = j["canvas"][0].get<int>(), h = j["canvas"][1].get<int>();
  for (const json& op : j["ops"]) {
    const std::string name = op.value("op", std::string());
    const auto known = std::find_if(kOps.begin(), kOps.end(), [&](const auto& k) { return k.first == name; });
    if (known == kOps.end()) {
      bad("no op " + name);
      continue;
    }
    std::vector<std::string> keys;
    for (const auto& kv : op.items()) keys.push_back(kv.key());
    for (const std::string& k : keys)
      if (std::find(known->second.begin(), known->second.end(), k) == known->second.end()) bad(name + ": key " + k);
    const auto interp = [&](const char* key) {
      return op.value(key, json()).is_string() && interp_of(op[key].get<std::string>());
    };
    if (name == "imageSize") {
      const std::string method = op.value("method", std::string());
      if (!op.value("w", json()).is_number_integer() || !op.value("h", json()).is_number_integer() ||
          (!interp_of(method) && method != "preserveDetails"))
        bad("imageSize " + op.dump());
      else
        w = op["w"].get<int>(), h = op["h"].get<int>();
    } else if (name == "canvasSize") {
      if (!op.value("w", json()).is_number_integer() || !op.value("h", json()).is_number_integer() ||
          !op.value("anchor", json()).is_number_integer() || op["anchor"].get<int>() < 0 || op["anchor"].get<int>() > 8 ||
          !op.contains("color") || !colour(op["color"]))
        bad("canvasSize " + op.dump());
      else
        w = op["w"].get<int>(), h = op["h"].get<int>();
    } else if (name == "rotateCanvas") {
      if (!op.value("angle", json()).is_number() || !interp("interp") || !op.contains("color") || !colour(op["color"])) {
        bad("rotateCanvas " + op.dump());
      } else {
        const double a = op["angle"].get<double>() * 3.14159265358979323846 / 180, c = std::abs(std::cos(a)), s = std::abs(std::sin(a));
        const int w2 = int(std::ceil(w * c + h * s - 1e-9));
        h = int(std::ceil(w * s + h * c - 1e-9)), w = w2;
      }
    } else {
      const auto warp = warp_of(op.value("warp", json()));
      if (!warp || !interp("interp") || op["warp"]["interp"] != op["interp"]) bad(name + ": warp " + op.value("warp", json()).dump());
      if (name == "freeTransform") {
        const auto m = matrix_of(op.value("transform", json()));
        if (!m || !warp) {
          bad("freeTransform: transform " + op.value("transform", json()).dump());
          continue;
        }
        const Matrix one = multiply(*m, warp->matrix);
        double off = 0;
        for (size_t i = 0; i < 9; i++) off = std::max(off, std::abs(one[i] / one[8] - (i % 4 == 0 ? 1 : 0)));
        if (off > 1e-6) bad("freeTransform: warp's matrix is not transform's inverse (" + std::to_string(off) + ")");
        if (op.contains("corners") && !numbers(op["corners"], 8)) bad("freeTransform: corners " + op["corners"].dump());
        if (op.contains("skew") && !numbers(op["skew"], 2)) bad("freeTransform: skew " + op["skew"].dump());
        if (warp && warp->mesh.cols) bad("freeTransform: a mesh");
      } else {
        const std::string style = op.value("style", std::string());
        if (std::find(std::begin(kStyles), std::end(kStyles), style) == std::end(kStyles)) bad("warp: style " + style);
        const bool custom = style == "custom";
        if (!custom && (!op.value("bend", json()).is_number() || !op.value("h", json()).is_number() ||
                        !op.value("v", json()).is_number() ||
                        (op.value("orientation", std::string()) != "horizontal" && op.value("orientation", std::string()) != "vertical")))
          bad("warp: " + op.dump());
        if (warp) {
          if (warp->matrix != Matrix{1, 0, 0, 0, 1, 0, 0, 0, 1}) bad("warp: a matrix");
          if (custom && !warp->mesh.cols) bad("warp: custom without a mesh");
          const BezierMesh& b = warp->mesh;
          const auto far = [](double v, double want) { return std::abs(v - want) > 1e-3; };  // Photoshop's numbers
          if (b.cols && (b.cols != 1 || b.rows != 1 || far(b.xs[0], kBox[0]) || far(b.xs[1], kBox[2]) ||
                         far(b.ys[0], kBox[1]) || far(b.ys[1], kBox[3])))
            bad("warp: a mesh not of one patch over the box");
        }
      }
    }
  }
  const int sw = j["size"][0].get<int>(), sh = j["size"][1].get<int>();
  const bool turned = std::any_of(j["ops"].begin(), j["ops"].end(), [](const json& op) { return op.value("op", "") == "rotateCanvas"; });
  if ((sw != w || sh != h) && !turned) bad("size " + j["size"].dump() + ", not [" + std::to_string(w) + ", " + std::to_string(h) + "]");
  if ((sw != w || sh != h) && turned && out.empty())
    std::printf("%s: Photoshop's canvas %d x %d, not %d x %d as resample.jsx supposed\n", e.name.c_str(), sw, sh, w, h);
  return out;
}

// How many of a set's entries have an op of that name.
size_t count(const golden::Set& ps, const char* op) {
  size_t n = 0;
  for (const auto& e : ps.entries())
    if (e.json.contains("ops"))
      for (const json& o : e.json["ops"])
        if (o.value("op", "") == op) {
          n++;
          break;
        }
  return n;
}

// A test that waits for a task's code: says so, how many of the set's (else the sample's) cases it will compare, and
// that it is skipped while Photoshop's images are not there.
void waits(const char* test, const char* task, const char* code, const std::vector<const char*>& ops) {
  if (!have_tree(test)) return;
  golden::Set set("resample"), sample("resample/sample");
  const golden::Set& ps = set.has_index() ? set : sample;
  std::string what;
  for (const char* op : ops) what += (what.empty() ? "" : ", ") + std::to_string(count(ps, op)) + " " + op;
  std::printf("%s: chưa có %s (%s): %s cases of %s to compare\n", test, task, code, what.c_str(), ps.name().c_str());
  set.ready(test);
}

// Where a pixel of Photoshop's image of a padded ramp (16 bits) was taken from, in the document's pixels of the input
// (its R and G: (x + 0.5) / kW and (y + 0.5) / kH of the ramp's pixel (x, y), at (kPadX, kPadY)); nullopt where the
// pixel or a neighbour is not opaque (an edge mixes in the transparency around).
std::optional<std::array<double, 2>> source(const golden::Image& im, int x, int y) {
  for (int dy = -1; dy <= 1; dy++)
    for (int dx = -1; dx <= 1; dx++) {
      const int u = x + dx, v = y + dy;
      if (u < 0 || v < 0 || u >= im.w || v >= im.h || im.at(u, v)[3] != 65535) return std::nullopt;
    }
  const uint16_t* p = im.at(x, y);
  return std::array<double, 2>{rs::kPadX + golden::ps15(p[0]) / 32768. * rs::kW, rs::kPadY + golden::ps15(p[1]) / 32768. * rs::kH};
}

struct Spread {
  size_t n = 0;
  double mean = 0, p99 = 0, max = 0;
};
Spread spread(std::vector<double> d) {
  Spread s;
  s.n = d.size();
  if (d.empty()) return s;
  for (const double v : d) s.mean += v / double(d.size()), s.max = std::max(s.max, v);
  const auto k = d.begin() + std::ptrdiff_t(0.99 * double(d.size() - 1));
  std::nth_element(d.begin(), k, d.end());
  s.p99 = *k;
  return s;
}

// How far from each opaque pixel's centre of Photoshop's image of a padded ramp the op's map puts where the pixel says
// it was taken from: the transform's (Photoshop's read back) of it, or the mesh's (its patch at the point's place in
// the box at rest).
Spread map_spread(const golden::Image& im, const json& op) {
  const auto warp = warp_of(op.value("warp", json()));
  const auto m = matrix_of(op.value("transform", json()));
  std::vector<double> d;
  for (int y = 0; y < im.h; y++)
    for (int x = 0; x < im.w; x++) {
      const auto q = source(im, x, y);
      if (!q) continue;
      std::array<double, 2> p;
      if (m) {
        p = apply(*m, (*q)[0], (*q)[1]);
      } else if (warp && warp->mesh.cols) {
        const BezierMesh& b = warp->mesh;
        p = bezier(b, ((*q)[0] - b.xs[0]) / (b.xs[1] - b.xs[0]), ((*q)[1] - b.ys[0]) / (b.ys[1] - b.ys[0]));
      } else {
        continue;
      }
      d.push_back(std::hypot(p[0] - (x + .5), p[1] - (y + .5)));
    }
  return spread(d);
}

}  // namespace

namespace {

Matrix invert(const Matrix& m) {
  const double a = m[0], b = m[1], c = m[2], d = m[3], e = m[4], f = m[5], g = m[6], h = m[7], k = m[8];
  Matrix out{e * k - f * h, c * h - b * k, b * f - c * e, f * g - d * k, a * k - c * g, c * d - a * f,
             d * h - e * g, b * g - a * h, a * e - b * d};
  const double w = out[8];
  for (double& v : out) v /= w;
  return out;
}

bool near(const std::array<double, 2>& p, double x, double y, double tolerance = 1e-6) {
  return std::abs(p[0] - x) <= tolerance && std::abs(p[1] - y) <= tolerance;
}

bool same(const uint16_t* p, const double* rgb, uint16_t a = 65535) {
  return p[0] == golden::ps_level(rgb[0]) && p[1] == golden::ps_level(rgb[1]) && p[2] == golden::ps_level(rgb[2]) && p[3] == a;
}

// A padded ramp as Photoshop would write it transformed by forward map m exactly (each pixel the point m's inverse
// takes its centre to; nothing where that is not inside the box, a pixel in from its edge): 16 bits.
golden::Image ramp_through(const Matrix& m) {
  const Matrix back = invert(m);
  golden::Image im(rs::kPadW, rs::kPadH, 4);
  for (int y = 0; y < im.h; y++)
    for (int x = 0; x < im.w; x++) {
      const auto q = apply(back, x + .5, y + .5);
      if (q[0] < kBox[0] + 1 || q[0] > kBox[2] - 1 || q[1] < kBox[1] + 1 || q[1] > kBox[3] - 1) continue;
      uint16_t* p = im.at(x, y);
      p[0] = golden::ps_level((q[0] - rs::kPadX) / rs::kW), p[1] = golden::ps_level((q[1] - rs::kPadY) / rs::kH);
      p[2] = golden::ps_level(.5), p[3] = 65535;
    }
  return im;
}

}  // namespace

// The files of tests/golden/ps/resample/in as pb-bench ps-resample writes them (golden::resample): the same samples
// and bytes, every sample a level Photoshop reads back exactly; what each holds.
TEST(test_ps_resample_inputs) {
  if (!have_tree("test_ps_resample_inputs")) return;
  const auto files = rs::all(kPhoto);
  CHECK(files.size() == 6);
  TempDir tmp;
  for (const auto& f : files) {
    const golden::Image got = golden::read_png(kIn / f.name);
    if (!got || got.px != f.image.px)
      std::printf("tests/golden/ps/resample/in/%s %s: run pb-bench ps-resample tests/golden/ps/resample/in\n",
                  f.name.c_str(), got ? "is not as golden::resample makes it" : "is not there");
    CHECK(got && got.bits == 16 && got.channels == f.image.channels && got.w == f.image.w && got.h == f.image.h);
    CHECK(got.px == f.image.px);
    for (const uint16_t s : got.px) CHECK(golden::is_ps_level(s));
    golden::write_png(tmp.path / f.name, f.image);
    CHECK(bytes(tmp.path / f.name) == bytes(kIn / f.name));  // what pb-bench ps-resample writes, byte for byte
  }
  const auto file = [&](const char* name) -> const golden::Image& {
    for (const auto& f : files)
      if (f.name == name) return f.image;
    CHECK(false);
    return files[0].image;
  };
  using golden::ps_level;
  const golden::Image& checker = file("checker.png");
  CHECK(checker.w == rs::kW && checker.h == rs::kH && checker.channels == 3);
  CHECK(same(checker.at(0, 0), rs::kLight) && same(checker.at(9, 9), rs::kLight) && same(checker.at(10, 0), rs::kDark));
  CHECK(same(checker.at(10, 10), rs::kLight) && same(checker.at(69, 69), rs::kLight) &&
        same(checker.at(59, 69), rs::kDark));
  CHECK(same(checker.at(70, 0), rs::kLight) && same(checker.at(71, 0), rs::kDark) && same(checker.at(70, 1), rs::kDark));  // 1 px
  CHECK(same(checker.at(70, 20), rs::kDark) && same(checker.at(72, 20), rs::kLight) && same(checker.at(73, 21), rs::kLight));  // 2
  CHECK(same(checker.at(70, 40), rs::kLight) && same(checker.at(75, 40), rs::kDark) && same(checker.at(99, 69), rs::kLight));  // 5
  const golden::Image& ramp = file("ramp.png");
  for (const auto& xy : {std::make_pair(0, 0), std::make_pair(99, 69), std::make_pair(37, 50)}) {
    const double u = (xy.first + .5) / rs::kW, v = (xy.second + .5) / rs::kH;
    const uint16_t* p = ramp.at(xy.first, xy.second);
    CHECK(p[0] == ps_level(u) && p[1] == ps_level(v) && p[2] == ps_level((u * u + v * v) / 2) && p[3] == 65535);
  }
  const golden::Image& photo = file("photo.png");  // B30's photo.png, cut
  const golden::Image b30 = golden::read_png(golden::kRoot / "p1" / "in" / "photo.png");
  CHECK(photo.w == rs::kW && photo.h == rs::kH && photo.channels == 3 && b30.w == 256);
  for (int y = 0; y < rs::kH; y++)
    for (int x = 0; x < rs::kW; x++) CHECK(std::equal(photo.at(x, y), photo.at(x, y) + 4, b30.at(rs::kPhotoX + x, rs::kPhotoY + y)));
  for (const char* stem : {"checker", "ramp", "photo"}) {
    const golden::Image& in = file((std::string(stem) + ".png").c_str());
    const golden::Image& pad = file((std::string(stem) + "-pad.png").c_str());
    CHECK(pad.w == rs::kPadW && pad.h == rs::kPadH && pad.channels == 4);
    for (int y = 0; y < pad.h; y++)
      for (int x = 0; x < pad.w; x++) {
        const uint16_t* p = pad.at(x, y);
        const int u = x - rs::kPadX, v = y - rs::kPadY;
        if (u < 0 || v < 0 || u >= rs::kW || v >= rs::kH)
          CHECK(p[0] == 0 && p[1] == 0 && p[2] == 0 && p[3] == 0);
        else if (std::string(stem) == "ramp")  // ramp-pad.png's B is 0.5: no curve to warp
          CHECK(p[0] == in.at(u, v)[0] && p[1] == in.at(u, v)[1] && p[2] == ps_level(.5) && p[3] == 65535);
        else
          CHECK(std::equal(p, p + 4, in.at(u, v)));
      }
  }
}

// The hand-written sample of the index (sample/index.json: one case of each kind, the lines PB.offline writes) as the
// tasks will read the set: every scene readable, its input of the canvas's size, every op well formed (problems_of);
// and resample.jsx's numbers worked out again here: each Free Transform's matrix (its guess of how Photoshop composes
// the options bar: scale, skew V then H, turn clockwise, about the box's centre), the corners of Perspective and
// Distort, a custom mesh, the sizes.
TEST(test_ps_resample_sample) {
  if (!have_tree("test_ps_resample_sample")) return;
  golden::Set ps("resample/sample");
  CHECK(ps.has_index() && ps.entries().size() == 21 && ps.index().value("inputs", "") == "../in");
  for (const auto& e : ps.entries()) {
    const auto problems = problems_of(e);
    for (const auto& p : problems) std::printf("%s\n", p.c_str());
    CHECK(problems.empty());
    const auto layers = ps.layers(e);
    CHECK(layers.size() == 1 && layers[0].kind == "pixel" && layers[0].pixels);
    CHECK(layers[0].pixels->w == e.json["canvas"][0].get<int>() && layers[0].pixels->h == e.json["canvas"][1].get<int>());
    const json& l = *layers[0].json;
    const bool eight = l.value("src", "").rfind("photo", 0) == 0 || e.name.rfind("warp-checker", 0) == 0;
    CHECK(e.json["bits"].get<int>() == (eight ? 8 : 16));
    const bool background = l["props"].value("background", false);
    CHECK(background == (l["props"].value("name", "") == "Background"));
    for (const json& op : e.json["ops"])  // a colour only for a Background
      if (op.contains("color")) CHECK(op["color"].is_null() != background);
  }
  // problems_of sees each way an entry can be wrong: entries of the sample broken one way at a time.
  const auto broken = [&](const char* name, const std::function<void(json&)>& edit) {
    const golden::Entry* found = ps.find(name);
    if (!found) return false;
    golden::Entry e = *found;
    edit(e.json);
    return !problems_of(e).empty();
  };
  CHECK(broken("transform-ramp-combo-bilinear", [](json& j) {
    json& m = j["ops"][0]["warp"]["matrix"];
    m[2] = m[2].get<double>() + .01;
  }));
  CHECK(broken("transform-ramp-combo-bilinear", [](json& j) { j["ops"][0]["warp"]["interp"] = "bicubic"; }));
  CHECK(broken("transform-ramp-distort-bilinear", [](json& j) { j["ops"][0]["corners"].erase(7); }));
  CHECK(broken("size-checker-bicubic-200", [](json& j) { j["ops"][0]["method"] = "lanczos"; }));
  CHECK(broken("size-checker-bicubic-200", [](json& j) { j["size"][0] = 199; }));
  CHECK(broken("size-checker-bicubic-200", [](json& j) { j["bits"] = 12; }));
  CHECK(broken("canvas-ramp-141x101-center", [](json& j) { j["ops"][0]["anchor"] = 9; }));
  CHECK(broken("canvas-ramp-141x101-center", [](json& j) { j["ops"][0]["colour"] = nullptr; }));  // no key of the op
  CHECK(broken("rotate-ramp-m15-bicubic", [](json& j) { j["ops"][0]["color"] = "#2B6CB0"; }));
  CHECK(broken("warp-ramp-arc-50", [](json& j) { j["ops"][0]["style"] = "arcs"; }));
  CHECK(broken("warp-ramp-arc-50", [](json& j) { j["ops"][0]["warp"]["matrix"][2] = 1; }));
  CHECK(broken("warp-ramp-custom-b", [](json& j) { j["ops"][0]["warp"]["mesh"]["xs"][0] = 31; }));
  CHECK(broken("warp-ramp-custom-b", [](json& j) { j["ops"][0]["warp"]["mesh"]["points"].erase(15); }));
  const auto op = [&](const char* name) -> const json& {
    const golden::Entry* e = ps.find(name);
    CHECK(e);
    return e->json["ops"][0];
  };
  const auto transform = [&](const char* name) { return *matrix_of(op(name)["transform"]); };
  const double deg = 3.14159265358979323846 / 180, cx = (kBox[0] + kBox[2]) / 2, cy = (kBox[1] + kBox[3]) / 2;
  Matrix m = transform("transform-checker-rotate30-nearestNeighbor");  // clockwise, y down
  CHECK(near(apply(m, cx, cy), cx, cy) && near(apply(m, kBox[2], cy), cx + 50 * std::cos(30 * deg), cy + 50 * std::sin(30 * deg)));
  m = transform("transform-checker-scale37-bicubicSharper");
  CHECK(near(apply(m, kBox[0], kBox[1]), cx - 50 * .37, cy - 35 * .37) && near(apply(m, kBox[2], kBox[3]), cx + 50 * .37, cy + 35 * .37));
  m = transform("transform-ramp-skew20-bilinear");  // x moves by tan 20 of y
  CHECK(near(apply(m, kBox[0], kBox[1]), kBox[0] - 35 * std::tan(20 * deg), kBox[1]) && near(apply(m, cx, cy), cx, cy));
  CHECK(near(apply(m, kBox[2], kBox[3]), kBox[2] + 35 * std::tan(20 * deg), kBox[3]));
  for (const char* name : {"transform-ramp-perspective-bilinear", "transform-ramp-distort-bilinear"}) {
    m = transform(name);
    const json& to = op(name)["corners"];
    const double from[8] = {kBox[0], kBox[1], kBox[2], kBox[1], kBox[2], kBox[3], kBox[0], kBox[3]};
    // 1e-4 px: index.json's 9 decimals of a perspective term move the far corner by up to 2e-5 px
    for (size_t i = 0; i < 4; i++)
      CHECK(near(apply(m, from[2 * i], from[2 * i + 1]), to[2 * i].get<double>(), to[2 * i + 1].get<double>(), 1e-4));
  }
  CHECK(op("transform-ramp-perspective-bilinear")["corners"] == json::parse("[46, 29, 114, 29, 130, 99, 30, 99]"));
  CHECK(transform("transform-ramp-perspective-bilinear")[6] == 0);  // the keystone: perspective in y alone
  {
    const json& f = op("transform-ramp-combo-bilinear");
    const double a = f["angle"].get<double>() * deg, kh = std::tan(f["skew"][0].get<double>() * deg), kv = std::tan(f["skew"][1].get<double>() * deg);
    const Matrix s{f["w"].get<double>() / 100, 0, 0, 0, f["h"].get<double>() / 100, 0, 0, 0, 1};
    const Matrix all = multiply(Matrix{1, 0, cx, 0, 1, cy, 0, 0, 1},
                                multiply(Matrix{std::cos(a), -std::sin(a), 0, std::sin(a), std::cos(a), 0, 0, 0, 1},
                                         multiply(Matrix{1, kh, 0, 0, 1, 0, 0, 0, 1},
                                                  multiply(Matrix{1, 0, 0, kv, 1, 0, 0, 0, 1}, multiply(s, Matrix{1, 0, -cx, 0, 1, -cy, 0, 0, 1})))));
    m = *matrix_of(f["transform"]);
    for (size_t i = 0; i < 9; i++) CHECK(std::abs(m[i] - all[i]) < 1e-8);
  }
  const auto custom = warp_of(op("warp-ramp-custom-b")["warp"]);
  CHECK(custom && custom->mesh.cols == 1 && custom->mesh.points.size() == 16 && custom->interp == Interp::Bicubic);
  CHECK(custom->mesh.points[0] == (std::array<double, 2>{kBox[0] - 8, kBox[1] - 6}) && custom->mesh.points[15] == (std::array<double, 2>{kBox[2] - 6, kBox[3] + 10}));
  CHECK(near(bezier(custom->mesh, 0, 0), kBox[0] - 8, kBox[1] - 6) && near(bezier(custom->mesh, 1, 1), kBox[2] - 6, kBox[3] + 10));
  const auto preset = warp_of(op("warp-ramp-arc-50")["warp"]);
  CHECK(preset && !preset->mesh.cols && op("warp-ramp-arc-50")["bend"] == 50 && op("warp-ramp-twist-m50")["bend"] == -50);
  CHECK(ps.find("rotate-ramp-m15-bicubic")->json["size"] == json::parse("[115, 94]"));  // 114.7 x 93.5, whole pixels up
  CHECK(ps.find("canvas-ramp-bg-61x41-back")->json["size"] == json::parse("[100, 70]"));
  CHECK(ps.find("size-photo-preserveDetails-37")->json["size"] == json::parse("[37, 26]"));
  CHECK(ps.find("size-checker-pad-automatic-37")->json["size"] == json::parse("[59, 47]"));
  CHECK(!interp_of("preserveDetails") && interp_of("automatic") == Interp::Automatic && interp_of("nearestNeighbor") == Interp::Nearest);
}

// Photoshop's set, once B33 brought it: every image index.json lists there at the size and depth it says, every scene
// readable and every op well formed; the set within its 3 MB (inputs and sample until then).
TEST(test_ps_resample_set) {
  if (!have_tree("test_ps_resample_set")) return;
  const uintmax_t size = folder_size(kSet);
  std::printf("tests/golden/ps/resample: %.2f MB of %.0f\n", double(size) / (1 << 20), double(kBudget) / (1 << 20));
  CHECK(size <= kBudget);
  golden::Set ps("resample");
  if (!ps.ready("test_ps_resample_set")) return;
  const auto problems = ps.problems();
  for (const auto& p : problems) std::printf("tests/golden/ps/resample: %s\n", p.c_str());
  CHECK(problems.empty());
  std::printf("tests/golden/ps/resample: %zu images of resample.jsx's 177\n", ps.entries().size());
  for (const auto& e : ps.entries()) {
    const auto wrong = problems_of(e);
    for (const auto& p : wrong) std::printf("%s\n", p.c_str());
    CHECK(wrong.empty());
    const golden::Image im = ps.image(e);
    CHECK(im.w == e.json["size"][0].get<int>() && im.h == e.json["size"][1].get<int>() && im.bits == e.json["bits"].get<int>());
  }
}

// What Photoshop's ramps say of the maps index.json has for them. Each opaque pixel of a padded ramp transformed or
// warped holds where it was taken from (its R, G); the op's transform (Photoshop's, read back off a smart object's
// copy) or mesh (a custom one, or one Photoshop kept for a style) takes that point back to the pixel's centre: how far
// it misses, printed (a hypothesis of the script until B33: a read back that is not what the pixels show, or half a
// pixel's convention, is for X30 to know). First through maps known here: a matrix and an affine mesh read back to the
// levels' precision, a shift or a moved point by about what it is.
TEST(test_ps_resample_maps) {
  const double deg = 3.14159265358979323846 / 180, cx = (kBox[0] + kBox[2]) / 2, cy = (kBox[1] + kBox[3]) / 2;
  const Matrix turn{std::cos(30 * deg), -std::sin(30 * deg), 0, std::sin(30 * deg), std::cos(30 * deg), 0, 0, 0, 1};
  const Matrix m = multiply(Matrix{1, 0, cx + 3, 0, 1, cy - 2, 0, 0, 1}, multiply(turn, Matrix{1, 0, -cx, 0, 1, -cy, 0, 0, 1}));
  const golden::Image made = ramp_through(m);
  json op = {{"op", "freeTransform"}, {"interp", "bilinear"}, {"transform", m},
             {"warp", {{"matrix", invert(m)}, {"interp", "bilinear"}}}};
  Spread s = map_spread(made, op);
  CHECK(s.n > 4000 && s.max < 0.01);
  op["transform"][2] = m[2] + .5;
  s = map_spread(made, op);
  CHECK(s.n > 4000 && std::abs(s.mean - .5) < 0.01 && s.max < 0.51);
  json mesh = json::array();  // m as a mesh: an affine map's patch is the map
  for (int j = 0; j < 4; j++)
    for (int i = 0; i < 4; i++) {
      const auto p = apply(m, kBox[0] + (kBox[2] - kBox[0]) * i / 3, kBox[1] + (kBox[3] - kBox[1]) * j / 3);
      mesh.push_back({p[0], p[1]});
    }
  json warp = {{"op", "warp"}, {"style", "custom"}, {"interp", "bicubic"},
               {"warp", {{"matrix", Matrix{1, 0, 0, 0, 1, 0, 0, 0, 1}},
                         {"mesh", {{"cols", 1}, {"rows", 1}, {"xs", {kBox[0], kBox[2]}}, {"ys", {kBox[1], kBox[3]}}, {"points", mesh}}},
                         {"interp", "bicubic"}}}};
  s = map_spread(made, warp);
  CHECK(s.n > 4000 && s.max < 0.01);
  warp["warp"]["mesh"]["points"][5][0] = mesh[5][0].get<double>() + 9;  // an inner point: its patch bulges, the edges stay
  s = map_spread(made, warp);
  CHECK(s.max > 1 && s.max < 9 && s.mean < s.max / 2);

  if (!have_tree("test_ps_resample_maps")) return;
  golden::Set ps("resample");
  if (!ps.ready("test_ps_resample_maps")) return;
  for (const auto& e : ps.entries()) {
    if (!e.json.contains("layers") || e.json["layers"].empty() || !e.json.contains("ops") || e.json["ops"].empty() ||
        e.json["layers"][0].value("src", "") != "ramp-pad.png")
      continue;
    const json& o = e.json["ops"][0];
    const auto w = warp_of(o.value("warp", json()));
    if (o.value("op", "") != "freeTransform" && !(w && w->mesh.cols)) continue;
    const golden::Image im = ps.image(e);
    if (!im) continue;
    const Spread d = map_spread(im, o);
    std::printf("ps golden %s: Photoshop's %s takes %zu pixels' points to their centres within %.3f px on average, "
                "%.3f at p99, %.3f at most\n", e.name.c_str(), o.value("op", "") == "warp" ? "mesh" : "matrix", d.n,
                d.mean, d.p99, d.max);
  }
  ps.skipped("test_ps_resample_maps");
}

// T33's, once its resample() is there: each Image Size case (its input; the op's w, h, method) against Photoshop's
// image, golden::compare printed; within a level of 8 bits for Nearest Neighbor and Bilinear (public formulas), dE2000
// mean < 1 and p99 < 3 for Bicubic once its a is fitted to these, the rest printed (Smoother, Sharper, Automatic:
// hypotheses; Preserve Details is P3's); the ramp at 200% says where each pixel samples, the ramp as a Background how
// the edges go.
TEST(test_ps_resample_t33) { waits("test_ps_resample_t33", "T33", "resample() of core/resample.h", {"imageSize"}); }

// X30's, once its warp_tile() is there: each Free Transform and Warp case's LayerWarp (warp_of(op["warp"]) on its one
// layer: Photoshop's matrix read back, a custom mesh, a style's mesh where Photoshop kept one) composited against
// Photoshop's image; the alpha at the edges of the box (alpha weighted), Scale 37% (prefiltered by the mip).
TEST(test_ps_resample_x30) {
  waits("test_ps_resample_x30", "X30", "warp_tile() of core/warp.h", {"freeTransform", "warp"});
}

// X31's, once its TransformSession is there: the matrix it makes of each Free Transform's fields (w, h, angle, skew;
// corners) against the op's transform (Photoshop's, read back: the combo case says how Photoshop composes them), and
// the layer it applies against Photoshop's image.
TEST(test_ps_resample_x31) {
  waits("test_ps_resample_x31", "X31", "TransformSession of core/transform.h", {"freeTransform"});
}

// X32's, once its styles are there: each Warp style's mesh at the op's bend, h, v, orientation against Photoshop's own
// (op["warp"]["mesh"] where it kept one) or against the ramp's map (each pixel's source), its warped ramp and checks
// against Photoshop's images; the custom meshes as X30's.
TEST(test_ps_resample_x32) { waits("test_ps_resample_x32", "X32", "core/meshwarp.h", {"warp"}); }

// T34's, once Image Rotation and Canvas Size are there: the canvas a rotation leaves (the op's size, Photoshop's),
// its Bicubic against Bicubic Automatic, a Background's colour where it turned from; Canvas Size's anchors and odd
// margins, its extension colours, the pixels past the canvas a layer keeps and a Background loses.
TEST(test_ps_resample_t34) {
  waits("test_ps_resample_t34", "T34", "core/canvasops.cpp", {"rotateCanvas", "canvasSize"});
}
