// S39: Photoshop's images of its selection commands (tools/ps-golden/select.jsx, README-select.md). The selections it
// loads, as pb-bench ps-select wrote them into tests/golden/ps/select/in/; the index it writes, as the hand-made
// tests/golden/ps/select/sample/index.json has it (lines of PB.offline's, one of each form: read before Photoshop's
// images are there); and once B33 brought them into tests/golden/ps/select/, photo-beauty against them: the way in
// and out (a selection loaded and saved as it was), Feather (S30) with sigma / Radius fitted from each image, and a
// place for each command S31-S34 brings, which says it is not there yet. Deviations are printed, not checked (sigma
// = Radius and the rest are hypotheses: docs/p1-tasks.md, "Khớp Photoshop bằng ảnh mẫu"); missing images are
// skipped.
#include <algorithm>
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
#include "gauss.h"
#include "golden.h"
#include "ps_select_inputs.h"

using namespace pb;
namespace fs = std::filesystem;
using nlohmann::json;

namespace {

const fs::path kSet = golden::kRoot / "select";
constexpr uintmax_t kBudget = 3u << 20;  // bytes of Photoshop's PNGs in tests/golden/ps/select/ (decision 6)

const fs::path kIn = golden::select_in::dir(golden::kRoot);

bool have_tree(const char* test) {
  if (fs::exists(kIn)) return true;
  std::printf("%s: skipped, no %s (a copy of the tree beside it)\n", test, kIn.generic_string().c_str());
  return false;
}

// ---- index.json's steps (select.jsx's S.SPEC) ----------------------------------------------------------------------

// Each step's keys, and the task whose command it is (S39 itself: the way in and out).
struct Spec {
  std::vector<std::string> keys;
  const char* task;
  const char* what;
};
const std::map<std::string, Spec>& specs() {
  static const std::map<std::string, Spec> all = {
      {"load", {{"src", "mode"}, "S31", "Load Selection with a mode"}},
      {"inverse", {{}, "S31", "Select > Inverse"}},
      {"feather", {{"px", "canvasBounds"}, "S30", "Feather"}},
      {"expand", {{"px", "canvasBounds"}, "S32", "Expand"}},
      {"contract", {{"px", "canvasBounds"}, "S32", "Contract"}},
      {"border", {{"px"}, "S32", "Border"}},
      {"smooth", {{"px", "canvasBounds"}, "S32", "Smooth"}},
      {"wand", {{"at", "color", "tolerance", "antiAlias", "contiguous", "sampleAll", "layer", "sampleSize", "mode"}, "S33",
                "Magic Wand"}},
      {"grow", {{"tolerance", "antiAlias"}, "S33", "Grow"}},
      {"similar", {{"tolerance", "antiAlias"}, "S33", "Similar"}},
      {"colorRange", {{"select", "fuzziness", "range", "at", "color", "lab", "detectFaces", "invert"}, "S34", "Color Range"}},
      {"lasso", {{"points", "antiAlias", "mode"}, "S31", "Lasso"}},
      {"ellipse", {{"rect", "antiAlias", "mode"}, "S31", "Elliptical Marquee"}},
      {"rect", {{"rect", "mode"}, "S31", "Rectangular Marquee"}}};
  return all;
}

// What is wrong with an entry's steps, empty when nothing: an unknown step or key, a mode, an input not in the tree.
std::vector<std::string> step_problems(golden::Set& set, const golden::Entry& e) {
  std::vector<std::string> out;
  const auto bad = [&](const std::string& why) { out.push_back(e.name + ": " + why); };
  if (!e.json.contains("select") || !e.json["select"].is_array() || e.json["select"].empty()) {
    bad("no steps");
    return out;
  }
  for (const json& s : e.json["select"]) {
    const std::string op = s.is_object() ? s.value("op", std::string()) : std::string();
    const auto spec = specs().find(op);
    if (spec == specs().end()) {
      bad("a step " + s.dump());
      continue;
    }
    for (const auto& kv : s.items())
      if (kv.key() != "op" && std::find(spec->second.keys.begin(), spec->second.keys.end(), kv.key()) == spec->second.keys.end())
        bad(op + " has a key " + kv.key());
    if (s.contains("mode")) {
      static const std::set<std::string> modes = {"new", "add", "subtract", "intersect"};
      if (!s["mode"].is_string() || !modes.count(s["mode"].get<std::string>())) bad(op + ": mode " + s["mode"].dump());
    }
    if (op == "load") {
      try {
        const golden::Image& in = set.input(s.at("src").get<std::string>());
        const auto canvas = e.json.at("canvas");
        if (in.w != canvas[0].get<int>() || in.h != canvas[1].get<int>()) bad("load: an input not of the canvas");
      } catch (const std::exception& x) {
        bad(x.what());
      }
    }
  }
  return out;
}

// The task whose command an entry is an image of: its last step's, but a lone Load Selection (the way in and out,
// S39's own check) and a load with a mode (combining, S31).
std::string task_of(const golden::Entry& e) {
  const json& steps = e.json["select"];
  if (steps.size() == 1 && steps[0]["op"] == "load" && steps[0].value("mode", std::string("new")) == "new") return "S39";
  for (const json& s : steps)
    if (s["op"] == "load" && s.value("mode", std::string("new")) != "new") return "S31";
  return specs().at(steps.back()["op"].get<std::string>()).task;
}

// ---- Comparing selections ------------------------------------------------------------------------------------------

// A grid of one channel at 16 bits from an input's grey (its red: the selection the script loads).
std::shared_ptr<TileGrid> selection_of(const golden::Image& im) { return golden::grid(im, Format{Depth::U16, 1}); }

// How far ours (0..65535) is from Photoshop's (its 16-bit PNG: levels 0..32768 written as round(v 65535 / 32768)), in
// levels of 8 bits: the largest and the mean.
struct Apart {
  double max = 0, mean = 0;
  int x = 0, y = 0;
};
Apart apart(const golden::Image& ours, const golden::Image& theirs) {
  Apart a;
  double sum = 0;
  for (int y = 0; y < ours.h; y++)
    for (int x = 0; x < ours.w; x++) {
      const double d = std::abs(ours.at(x, y)[0] / 65535. - golden::ps15(theirs.at(x, y)[0]) / 32768.) * 255;
      sum += d;
      if (d > a.max) a.max = d, a.x = x, a.y = y;
    }
  a.mean = sum / (double(ours.w) * double(ours.h));
  return a;
}

// The selection Gaussian-blurred by sigma with Feather's edges (feather_selection()'s), as an image.
golden::Image blurred(const TileGrid& sel, double sigma, int w, int h, bool at_canvas_bounds) {
  GaussEdges edges;
  edges.width = w, edges.height = h, edges.extend = !at_canvas_bounds;
  return golden::image(gauss(sel, sigma, edges), w, h);
}
double rms(const golden::Image& ours, const golden::Image& theirs) {  // levels of 8 bits
  double sum = 0;
  for (int y = 0; y < ours.h; y++)
    for (int x = 0; x < ours.w; x++) {
      const double d = (ours.at(x, y)[0] / 65535. - golden::ps15(theirs.at(x, y)[0]) / 32768.) * 255;
      sum += d * d;
    }
  return std::sqrt(sum / (double(ours.w) * double(ours.h)));
}

// sigma / Radius of the Gaussian that comes closest to Photoshop's Feather (least squares over the image): golden
// section over its logarithm, 1/4 to 4. *best_rms gets that fit's root mean square, levels of 8 bits.
double fit_sigma(const TileGrid& sel, const golden::Image& theirs, double radius, bool at_canvas_bounds, double* best_rms) {
  const auto cost = [&](double lk) { return rms(blurred(sel, std::exp(lk) * radius, theirs.w, theirs.h, at_canvas_bounds), theirs); };
  const double phi = (std::sqrt(5.) - 1) / 2;
  double a = std::log(.25), b = std::log(4.), c = b - phi * (b - a), d = a + phi * (b - a);
  double fc = cost(c), fd = cost(d);
  for (int i = 0; i < 28; i++) {
    if (fc < fd)
      b = d, d = c, fd = fc, c = b - phi * (b - a), fc = cost(c);
    else
      a = c, c = d, fc = fd, d = a + phi * (b - a), fd = cost(d);
  }
  const double k = std::exp((a + b) / 2);
  if (best_rms) *best_rms = cost(std::log(k));
  return k;
}

// Photoshop's 16-bit PNG of a selection of ours (as a test's stand-in for it): levels of 32768, written back to 16 bits.
golden::Image as_photoshop(const golden::Image& ours) {
  golden::Image out(ours.w, ours.h, 1, 16);
  for (size_t p = 0; p < ours.px.size(); p += 4) {
    const long v = std::lround(ours.px[p] / 65535. * 32768);
    out.px[p] = out.px[p + 1] = out.px[p + 2] = uint16_t(std::lround(v * 65535. / 32768)), out.px[p + 3] = 65535;
  }
  return out;
}

// Feather of e (its load, then feather) as S30 makes it, against Photoshop's theirs: prints how far apart, and with
// `fit` sigma / Radius fitted to theirs, which it returns (0 without theirs: ours is made all the same).
double check_feather(golden::Set& set, const golden::Entry& e, const golden::Image& theirs, bool fit) {
  const json& steps = e.json["select"];
  const json& f = steps.back();
  const int w = e.json["canvas"][0], h = e.json["canvas"][1];
  const auto sel = selection_of(set.input(steps[0]["src"].get<std::string>()));
  const double px = f["px"];
  const bool bounds = f.value("canvasBounds", false);
  const golden::Image ours = golden::image(feather_selection(*sel, px, w, h, bounds), w, h);
  if (!theirs) return 0;
  const golden::Diff d = golden::compare(ours, theirs, "select/" + e.name);
  const Apart a = apart(ours, theirs);
  double best = 0;
  const double k = fit ? fit_sigma(*sel, theirs, px, bounds, &best) : 0;
  char fitted[96] = "";
  if (fit) std::snprintf(fitted, sizeof fitted, "; sigma / Radius fitted %.3f (rms %.3f)", k, best);
  std::printf("ps select %s: S30's Feather (sigma %g x %g px) %.3f levels of 8 bits at most at (%d, %d), mean %.4f, "
              "rms %.4f%s; %s\n", e.name.c_str(), kSigmaPerRadius, px, a.max, a.x, a.y, a.mean, rms(ours, theirs), fitted,
              golden::str(d).c_str());
  std::fflush(stdout);
  return k;
}

// The places of S31-S34: each will compare an image of its commands (specs()' tasks) with photo-beauty's, as
// check_feather() does for S30, and return true; until then false: the image waits for it. S31: Lasso, the
// marquees, the four modes, Inverse (core/selection.h); S32: Expand, Contract, Border, Smooth (core/modify.h); S33:
// Magic Wand, Grow, Similar (core/flood.h); S34: Color Range (core/colorrange.h).
bool check_s31(golden::Set&, const golden::Entry&, const golden::Image&) { return false; }
bool check_s32(golden::Set&, const golden::Entry&, const golden::Image&) { return false; }
bool check_s33(golden::Set&, const golden::Entry&, const golden::Image&) { return false; }
bool check_s34(golden::Set&, const golden::Entry&, const golden::Image&) { return false; }

// What check_set() found.
struct Summary {
  int images = 0, loads_exact = 0, loads_off = 0, feathers = 0, same_bounds = 0;
  std::vector<double> sigmas;      // sigma / Radius fitted to each Feather of 1 px and more
  std::map<std::string, int> waiting;  // images by the task they wait for
};

// A set of select.jsx's images (Photoshop's, or test_ps_select_stand_in's): whole (every image index.json lists,
// every scene and step readable), within the budget; each image against photo-beauty's command where there is one,
// printed; the others counted for the task that brings it.
Summary check_set(golden::Set& set, const char* test) {
  Summary sum;
  const auto problems = set.problems();
  for (const auto& p : problems) std::printf("%s: %s\n", set.name().c_str(), p.c_str());
  CHECK(problems.empty());
  uintmax_t bytes = 0;
  for (const auto& f : fs::directory_iterator(set.dir()))
    if (f.is_regular_file() && f.path().extension() == ".png") bytes += f.file_size();
  std::printf("%s: %zu images, %.2f MB\n", test, set.entries().size(), double(bytes) / (1 << 20));
  CHECK(bytes <= kBudget);
  std::map<std::string, std::set<std::string>> what;  // task: the commands of the images that wait for it
  std::map<std::string, golden::Image> plain;          // Feather's images without Apply effect at canvas bounds
  for (const golden::Entry& e : set.entries()) {
    for (const auto& p : step_problems(set, e)) std::printf("%s: %s\n", test, p.c_str());
    CHECK(step_problems(set, e).empty());
    const golden::Image theirs = set.image(e);
    if (!theirs) continue;
    sum.images++;
    CHECK(theirs.w == e.json["canvas"][0].get<int>() && theirs.h == e.json["canvas"][1].get<int>());
    CHECK(theirs.bits == 16 && theirs.channels == 1);
    const std::string task = task_of(e);
    const json& steps = e.json["select"];
    bool done = true;
    if (task == "S39") {  // the way in and out: the input itself, to the level of 16 bits
      const golden::Image& in = set.input(steps[0]["src"].get<std::string>());
      int off = 0;
      for (size_t p = 0; p < in.px.size(); p += 4) off += golden::ps15(in.px[p]) != golden::ps15(theirs.px[p]);
      std::printf("ps select %s: the selection loaded and saved %s (%d pixels off)\n", e.name.c_str(),
                  off ? "differs from its input" : "is its input to the level", off);
      (off ? sum.loads_off : sum.loads_exact)++;
    } else if (task == "S30") {
      const double k = check_feather(set, e, theirs, true);
      sum.feathers++;
      if (steps.back()["px"].get<double>() >= 1) sum.sigmas.push_back(k);
      const bool bounds = steps.back().value("canvasBounds", false);
      const std::string base = bounds ? e.name.substr(0, e.name.rfind("-bounds")) : e.name;
      if (!bounds) {
        plain[e.name] = theirs;
      } else if (plain.count(base) && plain[base].px == theirs.px) {
        sum.same_bounds++;
        std::printf("ps select %s: the same as %s: Apply effect at canvas bounds made no difference (its key may not "
                    "be Photoshop's: README-select.md)\n", e.name.c_str(), base.c_str());
      }
    } else {
      done = task == "S31" ? check_s31(set, e, theirs) : task == "S32" ? check_s32(set, e, theirs)
           : task == "S33" ? check_s33(set, e, theirs) : check_s34(set, e, theirs);
    }
    if (!done) what[task].insert(specs().at(steps.back()["op"].get<std::string>()).what), sum.waiting[task]++;
  }
  if (!sum.sigmas.empty()) {
    std::vector<double> k = sum.sigmas;
    std::sort(k.begin(), k.end());
    std::printf("%s: Feather's sigma / Radius from the images of 1 px and more (%zu): %.3f to %.3f, median %.3f; "
                "kSigmaPerRadius %g\n", test, k.size(), k.front(), k.back(), k[k.size() / 2], kSigmaPerRadius);
  }
  for (const auto& kv : what) {
    std::string names;
    for (const auto& w : kv.second) names += (names.empty() ? "" : ", ") + w;
    std::printf("%s: %d images wait for %s (%s): not in photo-beauty yet\n", test, sum.waiting[kv.first], kv.first.c_str(),
                names.c_str());
  }
  set.skipped(test);
  return sum;
}

}  // namespace

// The inputs in the repo as pb-bench ps-select writes them, levels Photoshop reads back exactly, grey; what they hold.
TEST(test_ps_select_inputs) {
  if (!have_tree("test_ps_select_inputs")) return;
  const auto files = golden::select_in::all();
  CHECK(files.size() == size_t(golden::select_in::kFiles));
  for (const auto& f : files) {
    const golden::Image got = golden::read_png(kIn / f.name);
    if (!got || got.px != f.image.px)
      std::printf("tests/golden/ps/select/in/%s %s: run pb-bench ps-select tests/golden/ps\n", f.name.c_str(),
                  got ? "is not as golden::select_in makes it" : "is not there");
    CHECK(got && got.bits == 16 && got.channels == 3 && got.w == golden::select_in::kSize && got.h == got.w);
    CHECK(got.px == f.image.px);
    for (size_t p = 0; p < got.px.size(); p += 4)
      CHECK(golden::is_ps_level(got.px[p]) && got.px[p + 1] == got.px[p] && got.px[p + 2] == got.px[p] && got.px[p + 3] == 65535);
  }
  const auto file = [&](const char* name) -> const golden::Image& {
    for (const auto& f : files)
      if (f.name == name) return f.image;
    CHECK(false);
    return files[0].image;
  };
  const auto v = [](const golden::Image& im, int x, int y) { return im.at(x, y)[0]; };
  const golden::Image& hard = file("hard.png");
  CHECK(v(hard, 0, 0) == 65535 && v(hard, 89, 69) == 65535 && v(hard, 90, 0) == 0 && v(hard, 0, 70) == 0);
  CHECK(v(hard, 255, 20) == 65535 && v(hard, 235, 20) == 0);                            // on the right edge
  CHECK(v(hard, 120, 112) == 65535 && v(hard, 161, 151) == 0 && v(hard, 200, 200) == 0);  // the holes
  CHECK(v(hard, 110, 255) == 65535 && v(hard, 111, 255) == 0 && v(hard, 129, 49) == 65535);
  CHECK(v(hard, 60, 170) == 65535 && v(hard, 60, 131) == 65535 && v(hard, 75, 140) == 0);  // the star, a notch
  int n = 0;
  for (size_t p = 0; p < hard.px.size(); p += 4) n += hard.px[p] != 0 && hard.px[p] != 65535;
  CHECK(n == 0);
  const golden::Image& aa = file("aa.png");
  CHECK(v(aa, 64, 64) == 65535 && v(aa, 0, 0) == 0 && v(aa, 0, 200) == 65535);
  const uint16_t edge = v(aa, 64 + 28, 64 + 28);  // the disc's edge at 45 degrees: partly in
  CHECK(edge > 0 && edge < 65535);
  const golden::Image& soft = file("soft.png");
  CHECK(v(soft, 100, 220) == 32768 && v(soft, 50, 220) == golden::ps_level(.6) && v(soft, 150, 220) == golden::ps_level(.4));
  CHECK(v(soft, 64, 64) > 65000 && v(soft, 190, 66) == 65535 && v(soft, 255, 160) == 65535 && v(soft, 0, 160) == 0);
  const golden::Image &rx = file("ramp-x.png"), &ry = file("ramp-y.png");
  for (int i = 0; i < 256; i += 51) CHECK(v(rx, i, 7) == golden::ps_level(i / 255.) && v(ry, 7, i) == golden::ps_level(i / 255.));
}

// The hand-made index (tests/golden/ps/select/sample/index.json: lines of select.jsx's PB.offline): its scenes read
// as golden.h reads them, every step known and its inputs there, Magic Wand's and Color Range's colours the pixels'
// they name, Lab as select.jsx's S.lab makes it, one entry of each step at least; photo-beauty's Feather of its
// entries made (their Photoshop images are not there).
TEST(test_ps_select_index) {
  if (!have_tree("test_ps_select_index")) return;
  golden::Set sample("select/sample");
  CHECK(sample.has_index());
  CHECK(sample.index()["script"] == "select.jsx" && sample.index()["inputs"] == "../..");
  std::set<std::string> ops;
  for (const golden::Entry& e : sample.entries()) {
    for (const auto& p : step_problems(sample, e)) std::printf("test_ps_select_index: %s\n", p.c_str());
    CHECK(step_problems(sample, e).empty());
    const auto layers = sample.layers(e);
    CHECK(!layers.empty() && e.json["bits"] == 16);
    for (const auto& l : layers) CHECK(l.kind == "pixel" && l.pixels->w == e.json["canvas"][0].get<int>());
    for (const json& s : e.json["select"]) {
      ops.insert(s["op"].get<std::string>());
      if (s.contains("color")) {  // the pixel it picks: of its layer (the top one unless it names one), or with Sample
        const int x = s["at"][0], y = s["at"][1];  // All Layers the composite: here the bottom one, those over it clear
        const golden::Image* from = layers.back().pixels;
        for (const auto& l : layers)
          if (s.contains("layer") && (*l.json)["props"]["name"] == s["layer"]) from = l.pixels;
        if (s.value("sampleAll", false)) {
          for (size_t i = 1; i < layers.size(); i++) CHECK(layers[i].pixels->at(x, y)[3] == 0);
          from = layers[0].pixels;
        }
        for (int c = 0; c < 4; c++) CHECK(from->at(x, y)[c] == s["color"][c].get<int>());
      }
      if (s.contains("lab")) {  // sRGB IEC61966-2.1 (its D50 matrix) to CIELAB D50, as S.lab
        double lin[3], f[3];
        for (int c = 0; c < 3; c++) lin[c] = golden::srgb_linear(s["color"][c].get<int>() / 65535.);
        const double xyz[3] = {(.4360747 * lin[0] + .3850649 * lin[1] + .1430804 * lin[2]) / .9642,
                               .2225045 * lin[0] + .7168786 * lin[1] + .0606169 * lin[2],
                               (.0139322 * lin[0] + .0971045 * lin[1] + .7141733 * lin[2]) / .8249};
        for (int c = 0; c < 3; c++) f[c] = xyz[c] > 216. / 24389 ? std::cbrt(xyz[c]) : (24389. / 27 * xyz[c] + 16) / 116;
        const double lab[3] = {116 * f[1] - 16, 500 * (f[0] - f[1]), 200 * (f[1] - f[2])};
        for (int c = 0; c < 3; c++) CHECK(std::abs(s["lab"][c].get<double>() - lab[c]) <= .0051);
      }
    }
    const golden::Image none = sample.image(e);  // no Photoshop images beside the sample
    CHECK(!none);
    if (e.json["select"].back()["op"] == "feather") check_feather(sample, e, none, false);
  }
  for (const auto& kv : specs())
    if (!ops.count(kv.first)) std::printf("test_ps_select_index: the sample has no %s\n", kv.first.c_str());
  CHECK(ops.size() == specs().size());
  CHECK(sample.problems().size() == sample.entries().size());  // only the images missing
}

// The comparison and the fit on images made here as Photoshop would write them: S30's own Feather comes back within
// half a level of 16 bits, sigma / Radius 1; a Feather of 1.3 Radius (on, and off, the canvas bounds) comes back 1.3.
TEST(test_ps_select_fit) {
  if (!have_tree("test_ps_select_fit")) return;
  const auto sel = selection_of(golden::select_in::hard());
  for (const bool bounds : {false, true}) {
    const golden::Image same = as_photoshop(golden::image(feather_selection(*sel, 5, 256, 256, bounds), 256, 256));
    const golden::Image ours = golden::image(feather_selection(*sel, 5, 256, 256, bounds), 256, 256);
    CHECK(apart(ours, same).max <= .5 / 32768 * 255 + 1e-9);  // half a level of Photoshop's
    double r = 0;
    CHECK(std::abs(fit_sigma(*sel, same, 5, bounds, &r) - kSigmaPerRadius) < .002 && r < .01);
    const golden::Image wide = as_photoshop(blurred(*sel, 1.3 * 5, 256, 256, bounds));
    CHECK(std::abs(fit_sigma(*sel, wide, 5, bounds, &r) - 1.3) < .002 && r < .01);
    CHECK(apart(ours, wide).max > 1);  // and S30's 1.0 is seen to be off
    const golden::Image other = as_photoshop(blurred(*sel, 1.3 * 5, 256, 256, !bounds));
    double r2 = 0;
    fit_sigma(*sel, other, 5, bounds, &r2);
    CHECK(r2 > 5 * r);  // the other edges fit worse
  }
}

// check_set() on a stand-in for Photoshop's set: the sample's entries, each image made here as Photoshop would write
// it (the way in and out: the input; Feather: a Gaussian of 1.25 x Radius with its edges; the rest: the selection
// loaded, or the layer's grey), in a folder of its own. Everything read and compared, sigma / Radius 1.25 found, the
// way in and out exact, the other commands waiting for their tasks.
TEST(test_ps_select_stand_in) {
  if (!have_tree("test_ps_select_stand_in")) return;
  golden::Set sample("select/sample");
  struct TempDir {
    fs::path path = fs::temp_directory_path() / ("pb-ps-select-" + std::to_string(std::random_device{}()));
    TempDir() { fs::create_directories(path); }
    ~TempDir() {
      std::error_code e;
      fs::remove_all(path, e);
    }
  } tmp;
  json index = sample.index();
  index["inputs"] = golden::kRoot.string();  // the inputs where they are
  std::ofstream(tmp.path / "index.json") << index.dump();
  int loads = 0, feathers = 0, others = 0;
  for (const golden::Entry& e : sample.entries()) {
    const json& steps = e.json["select"];
    const auto sel = selection_of(sample.input(steps[0].value("src", e.json["layers"][0]["src"].get<std::string>())));
    const int w = e.json["canvas"][0], h = e.json["canvas"][1];
    golden::Image im = golden::image(*sel, w, h);
    if (task_of(e) == "S30") {
      im = blurred(*sel, 1.25 * steps.back()["px"].get<double>(), w, h, steps.back().value("canvasBounds", false));
      feathers++;
    } else {
      (task_of(e) == "S39" ? loads : others)++;
    }
    golden::write_png(tmp.path / (e.name + ".png"), as_photoshop(im));
  }
  CHECK(loads >= 1 && feathers >= 2 && others >= 10);
  golden::Set stand_in(tmp.path);
  CHECK(stand_in.ready("test_ps_select_stand_in"));
  const Summary sum = check_set(stand_in, "test_ps_select_stand_in");
  CHECK(sum.images == int(sample.entries().size()) && sum.loads_exact == loads && sum.loads_off == 0);
  CHECK(sum.feathers == feathers && !sum.sigmas.empty() && sum.same_bounds == 0);  // the sample has no pair
  for (const double k : sum.sigmas) CHECK(std::abs(k - 1.25) < .003);
  int waiting = 0;
  for (const auto& kv : sum.waiting) waiting += kv.second;
  CHECK(waiting == others && sum.waiting.count("S31") && sum.waiting.count("S32") && sum.waiting.count("S33") &&
        sum.waiting.count("S34"));
}

// Photoshop's images, once B33 has brought them (until then said to be skipped): check_set() on them.
TEST(test_ps_select) {
  if (!have_tree("test_ps_select")) return;
  golden::Set set("select");
  if (!set.ready("test_ps_select")) return;
  check_set(set, "test_ps_select");
}
