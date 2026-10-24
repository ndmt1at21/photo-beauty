// Gradients (gradient.h): their JSON, the table a method mixes along t, the shapes pixel by pixel, render_gradient().
// gradient_t(), GradientLut::at(), dither_noise() and to_level() are shaders/lib/gradient.slang's line by line: the
// same float operations in the same order (no fused multiply-add: CMakeLists.txt's -ffp-contract=off, the shader's
// `precise`), so both sides give the same bits.
#include "gradient.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <utility>

#include "hash.h"
#include "jobs.h"
#include "profile.h"

namespace pb {
namespace {

// ---- Hypotheses ----------------------------------------------------------------------------------------------------
// Photoshop publishes none of these. D32's images of its gradients (tools/ps-golden/p1/gradient.jsx, run at B33)
// settle them at B38; test_gradient_ps prints how far each one is.

// The mix weight u at x (0..1) of the way from a stop to the next, m their midpoint: half and half at m, linear on
// each side, through (0, 0), (m, 0.5), (1, 1). (GIMP's linear segments do so; Photoshop's may bend.)
double midpoint_weight(double x, double m) { return x < m ? 0.5 * x / m : 0.5 + 0.5 * (x - m) / (1 - m); }

// Classic's Smoothness s (0..1) eases u towards smoothstep, all the way at 100 %: flat at each stop, no kink there.
double smoothstep(double u) { return u * u * (3 - 2 * u); }
double smoothness_weight(double u, double s) { return u + s * (smoothstep(u) - u); }

// Smoothness eases Classic only: the 2023 methods leave it alone (else Smooth would be Perceptual at the default 100 %).
// Perceptual mixes in Oklab (Ottosson 2020) of the document's linear light; Smooth the same, by smoothstep(u).
// Stripes: a stop's colour and opacity hold while the mix weight is below kStripesAt (up to its midpoint), then the
// next stop's.
constexpr double kStripesAt = 0.5;

// A colour stop's grey in a grey document: Rec. 601's luma of its R, G, B. The UI hands greys (R = G = B) once it has
// converted a colour to the document's profile; this is for colours used as they are, as presets'.
constexpr double kGreyWeights[3] = {0.299, 0.587, 0.114};

// The dither's amplitude, in levels of the document's depth: ±0.5 of an 8-bit level at both depths (257 16-bit levels
// make one of 8), so a 16-bit gradient keeps from banding once shown or saved in 8 bits too. to_level() narrows it
// within that of 0 and the maximum (else rounding there would clamp, and dark and light 16-bit means drift).
constexpr float kDitherLevels8 = 1, kDitherLevels16 = 257;

// A fill's or an overlay's line (fill_line()): through the box's centre along the angle, as long as the box spans that
// way, |w cos a| + |h sin a| (CSS's linear-gradient(): the perpendiculars at the line's ends pass the far corners),
// times the scale; Linear from end to end, the other styles from the centre, half that.
double fill_span(double w, double h, double cos_a, double sin_a) { return std::fabs(w * cos_a) + std::fabs(h * sin_a); }

// Angle sweeps counterclockwise on screen from the line (Adobe: "shades in a counterclockwise sweep around the starting
// point"): gradient_t()'s v is the component a quarter turn that way.

// ---- JSON ------------------------------------------------------------------------------------------------------------

using nlohmann::json;

std::string dump(const json& j) { return j.dump(-1, ' ', false, json::error_handler_t::replace); }

// j[key] clamped to lo..hi into `to` (unchanged when there is none); false for one that is not a finite number.
bool number(const json& j, const char* key, double lo, double hi, double& to) {
  const auto it = j.find(key);
  if (it == j.end()) return true;
  if (!it->is_number() || !std::isfinite(it->get<double>())) return false;
  to = std::clamp(it->get<double>(), lo, hi);
  return true;
}
bool required(const json& j, const char* key, double lo, double hi, double& to) {
  return j.contains(key) && number(j, key, lo, hi, to);
}
bool boolean(const json& j, const char* key, bool& to) {
  const auto it = j.find(key);
  if (it == j.end()) return true;
  if (!it->is_boolean()) return false;
  to = it->get<bool>();
  return true;
}

template <size_t N>
int by_name(const char* const (&names)[N], std::string name) {
  for (char& c : name) c = char(std::tolower(static_cast<unsigned char>(c)));
  for (size_t i = 0; i < N; i++)
    if (name == names[i]) return int(i);
  return -1;
}

json gradient_json(const Gradient& g) {
  json colors = json::array(), alphas = json::array();
  for (const ColorStop& s : g.colors) {
    json o = json::object();
    o["p"] = s.p, o["c"] = json::array({s.c[0], s.c[1], s.c[2]}), o["m"] = s.m;
    if (s.type != ColorStop::Type::User) o["type"] = s.type == ColorStop::Type::Foreground ? "fg" : "bg";
    colors.push_back(std::move(o));
  }
  for (const AlphaStop& s : g.alphas) {
    json o = json::object();
    o["p"] = s.p, o["a"] = s.a, o["m"] = s.m;
    alphas.push_back(std::move(o));
  }
  json j = json::object();
  j["colors"] = std::move(colors), j["alphas"] = std::move(alphas), j["smoothness"] = g.smoothness;
  if (!g.name.empty()) j["name"] = g.name;
  return j;
}

// ---- Colour: the document's light, Oklab -------------------------------------------------------------------------

using Vec3 = std::array<double, 3>;
using Mat3 = std::array<double, 9>;  // rows

Vec3 times(const Mat3& m, const Vec3& v) {
  return {m[0] * v[0] + m[1] * v[1] + m[2] * v[2], m[3] * v[0] + m[4] * v[1] + m[5] * v[2],
          m[6] * v[0] + m[7] * v[1] + m[8] * v[2]};
}
Mat3 mul(const Mat3& a, const Mat3& b) {
  Mat3 r{};
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++)
      for (int k = 0; k < 3; k++) r[size_t(i * 3 + j)] += a[size_t(i * 3 + k)] * b[size_t(k * 3 + j)];
  return r;
}
Mat3 inverse(const Mat3& m) {
  const Mat3 c = {m[4] * m[8] - m[5] * m[7], m[2] * m[7] - m[1] * m[8], m[1] * m[5] - m[2] * m[4],
                  m[5] * m[6] - m[3] * m[8], m[0] * m[8] - m[2] * m[6], m[2] * m[3] - m[0] * m[5],
                  m[3] * m[7] - m[4] * m[6], m[1] * m[6] - m[0] * m[7], m[0] * m[4] - m[1] * m[3]};
  const double det = m[0] * c[0] + m[1] * c[3] + m[2] * c[6];
  Mat3 r;
  for (size_t i = 0; i < 9; i++) r[i] = c[i] / det;
  return r;
}

// Linear Rec. 2020 (D65) to CIE XYZ; Oklab's XYZ to LMS (M1) and LMS' to Lab (M2), Ottosson's "A perceptual color
// space for image processing" (2020).
constexpr Mat3 kRec2020ToXyz = {0.636958048, 0.144616904, 0.168880975, 0.262700212, 0.677998072,
                                0.0593017165, 0,          0.028072693, 1.06098506};
constexpr Mat3 kXyzToLms = {0.8189330101, 0.3618667424, -0.1288597137, 0.0329845436, 0.9293118715,
                            0.0361456387, 0.0482003018, 0.2643662691,  0.6338517070};
constexpr Mat3 kLmsToLab = {0.2104542553, 0.7936177850, -0.0040720468, 1.9779984951, -2.4285922050,
                            0.4505937099, 0.0259040371, 0.7827717662,  -0.8086757660};

const Source& srgb_source() {
  static const std::shared_ptr<const Source> s = source_profile(icc_profile(ColorSpace::Srgb));
  return *s;
}

// Encoded e (0..1) of channel c in linear light: the profile's table, linear between its entries.
double to_linear(const Source& s, int c, double e) {
  const double x = std::clamp(e, 0., 1.) * (Source::kLut - 1);
  const int i = std::min(int(x), Source::kLut - 2);
  return s.linear[c][i] + (double(s.linear[c][i + 1]) - s.linear[c][i]) * (x - i);
}
// Back: the encoded value of light y, through the same table (it rises).
double to_encoded(const Source& s, int c, double y) {
  const float* l = s.linear[c];
  if (!(y > l[0])) return 0;
  if (y >= l[Source::kLut - 1]) return 1;
  int lo = 0, hi = Source::kLut - 1;  // l[lo] <= y < l[hi]
  while (hi - lo > 1) {
    const int mid = (lo + hi) / 2;
    (l[mid] <= y ? lo : hi) = mid;
  }
  return (lo + (y - l[lo]) / (double(l[hi]) - l[lo])) / (Source::kLut - 1);
}

// A stop ready to mix: where it sits in the table's entries, its midpoint, and (colour stops) its colour encoded, in
// light, in Oklab.
struct Stop {
  int at = 0;
  double m = 0.5;
  Vec3 enc{}, lin{}, lab{};
  double a = 1;
};
int entry(double p) { return int(std::lround(std::clamp(p, 0., 1.) * GradientLut::kSteps)); }

// Where entry i falls among stops sorted by `at`: x (0..1) of the way from stop k to k + 1 (mix), or at stop k
// alone (before the first, past the last). Stops at one entry make an edge: the last of them starts what follows.
struct Where {
  size_t k = 0;
  double x = 0;
  bool mix = false;
};
Where where(const std::vector<Stop>& s, int i) {
  if (i < s.front().at) return {};
  size_t k = 0;
  while (k + 1 < s.size() && s[k + 1].at <= i) k++;
  if (k + 1 == s.size()) return {k, 0, false};
  return {k, double(i - s[k].at) / double(s[k + 1].at - s[k].at), true};
}

// ---- Angle's turn --------------------------------------------------------------------------------------------------

// Abramowitz and Stegun 4.4.49 (Hastings): atan(r) = r (1 + a2 r² + ... + a16 r^16), |error| <= 2e-8 for 0 <= r <= 1;
// and 1 / 2π. As float bits, gradient.slang's kAtan and kTurn: both sides the same floats.
constexpr uint32_t kAtanBits[9] = {0x3f800000, 0xbeaaaa6c, 0x3e4cbbe5, 0xbe117fc7, 0x3dda3d83,
                                   0xbd9a3174, 0x3d2fc1fe, 0xbc846e02, 0x3b3bd74a};
constexpr uint32_t kTurnBits = 0x3e22f983;
float from_bits(uint32_t u) {
  float f;
  std::memcpy(&f, &u, 4);
  return f;
}

// The turn from the u axis to (u, v), the v axis a quarter turn on: 0..1. atan of the smaller over the larger
// coordinate, then its octant.
float turns(float u, float v) {
  const float au = std::fabs(u), av = std::fabs(v);
  const float hi = au > av ? au : av, lo = au > av ? av : au;
  if (!(hi > 0)) return 0;
  const float r = lo / hi, s = r * r;
  float p = from_bits(kAtanBits[8]);
  for (int k = 7; k >= 0; k--) p = from_bits(kAtanBits[k]) + s * p;
  float a = r * p * from_bits(kTurnBits);
  if (av > au) a = 0.25f - a;
  if (u < 0) a = 0.5f - a;
  if (v < 0) a = 1 - a;
  return a;
}

}  // namespace

// ---- The gradient ----------------------------------------------------------------------------------------------------

std::optional<Gradient> gradient_from(const json& j) {
  if (!j.is_object()) return std::nullopt;
  Gradient g;
  const auto colors = j.find("colors");
  if (colors == j.end() || !colors->is_array() || colors->empty()) return std::nullopt;
  g.colors.clear();
  for (const json& s : *colors) {
    if (!s.is_object()) return std::nullopt;
    ColorStop c;
    if (!required(s, "p", 0, 1, c.p) || !number(s, "m", 0.05, 0.95, c.m)) return std::nullopt;
    if (const auto type = s.find("type"); type != s.end()) {
      const std::string t = type->is_string() ? type->get<std::string>() : "";
      if (t == "fg")
        c.type = ColorStop::Type::Foreground;
      else if (t == "bg")
        c.type = ColorStop::Type::Background;
      else if (t != "user")
        return std::nullopt;
    }
    if (const auto col = s.find("c"); col != s.end()) {
      if (!col->is_array() || col->size() != 3) return std::nullopt;
      for (size_t k = 0; k < 3; k++) {
        const json& v = (*col)[k];
        if (!v.is_number() || !std::isfinite(v.get<double>())) return std::nullopt;
        c.c[k] = std::clamp(v.get<double>(), 0., 255.);
      }
    } else if (c.type == ColorStop::Type::User) {
      return std::nullopt;
    } else if (c.type == ColorStop::Type::Background) {
      c.c = {255, 255, 255};  // Photoshop's default colours: black on white
    }
    g.colors.push_back(c);
  }
  if (const auto alphas = j.find("alphas"); alphas != j.end()) {
    if (!alphas->is_array()) return std::nullopt;
    if (!alphas->empty()) g.alphas.clear();
    for (const json& s : *alphas) {
      AlphaStop a;
      if (!s.is_object() || !required(s, "p", 0, 1, a.p) || !required(s, "a", 0, 1, a.a) ||
          !number(s, "m", 0.05, 0.95, a.m))
        return std::nullopt;
      g.alphas.push_back(a);
    }
  }
  if (!number(j, "smoothness", 0, 100, g.smoothness)) return std::nullopt;
  if (const auto name = j.find("name"); name != j.end()) {
    if (!name->is_string()) return std::nullopt;
    g.name = name->get<std::string>();
  }
  return g;
}

std::optional<Gradient> gradient_from_json(const std::string& text) {
  return gradient_from(json::parse(text, nullptr, false));  // discarded, not an object, when malformed
}

std::string to_json(const Gradient& g) { return dump(gradient_json(g)); }

Gradient with_colors(Gradient g, const std::array<double, 3>& fg, const std::array<double, 3>& bg) {
  for (ColorStop& s : g.colors) {
    if (s.type == ColorStop::Type::User) continue;
    s.c = s.type == ColorStop::Type::Foreground ? fg : bg;
    s.type = ColorStop::Type::User;
  }
  return g;
}

std::optional<GradientMethod> gradient_method(const std::string& name) {
  const int i = by_name(kGradientMethodNames, name);
  return i < 0 ? std::nullopt : std::optional<GradientMethod>(GradientMethod(i));
}
std::optional<GradientStyle> gradient_style(const std::string& name) {
  const int i = by_name(kGradientStyleNames, name);
  return i < 0 ? std::nullopt : std::optional<GradientStyle>(GradientStyle(i));
}

// ---- Mixing ----------------------------------------------------------------------------------------------------------

std::array<float, 4> GradientLut::at(float t) const {
  t = t > 0 ? (t < 1 ? t : 1) : 0;
  const float x = t * kSteps;  // exactly: kSteps is a power of 2
  const int i = int(x);        // 0..kSteps
  const float f = x - float(i);
  const std::array<float, 4>& a = rgba[size_t(i)];
  const std::array<float, 4>& b = rgba[size_t(i) + 1];
  return {a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f, a[2] + (b[2] - a[2]) * f, a[3] + (b[3] - a[3]) * f};
}

std::shared_ptr<const GradientLut> gradient_lut(const Gradient& g, GradientMethod method, const GradientSpace& space) {
  PROFILE_ZONE("gradient_lut");
  if (g.colors.empty() || g.alphas.empty()) throw std::invalid_argument("pb: a gradient without stops");
  const Source& src = space.profile ? *space.profile : srgb_source();
  Mat3 to2020;
  for (size_t i = 0; i < 9; i++) to2020[i] = src.to_rec2020[i];
  const Mat3 to_lms = mul(kXyzToLms, mul(kRec2020ToXyz, to2020)), from_lms = inverse(to_lms);
  const Mat3 from_lab = inverse(kLmsToLab);
  const auto light = [&](const Vec3& enc) {  // encoded -> linear light
    return Vec3{to_linear(src, 0, enc[0]), to_linear(src, 1, enc[1]), to_linear(src, 2, enc[2])};
  };
  const auto encode = [&](const Vec3& lin) {  // and back, clamped to the gamut
    Vec3 e{to_encoded(src, 0, lin[0]), to_encoded(src, 1, lin[1]), to_encoded(src, 2, lin[2])};
    if (space.grey) e[0] = e[1] = e[2] = kGreyWeights[0] * e[0] + kGreyWeights[1] * e[1] + kGreyWeights[2] * e[2];
    return e;
  };
  const auto from_oklab = [&](const Vec3& lab) {
    Vec3 lms = times(from_lab, lab);
    for (double& v : lms) v = v * v * v;
    return encode(times(from_lms, lms));
  };

  std::vector<Stop> cs, as;
  for (const ColorStop& c : g.colors) {
    Stop s;
    s.at = entry(c.p), s.m = std::clamp(c.m, 0.05, 0.95);
    for (size_t k = 0; k < 3; k++) s.enc[k] = std::clamp(c.c[k] / 255, 0., 1.);
    if (space.grey)
      s.enc[0] = s.enc[1] = s.enc[2] =
          kGreyWeights[0] * s.enc[0] + kGreyWeights[1] * s.enc[1] + kGreyWeights[2] * s.enc[2];
    s.lin = light(s.enc);
    Vec3 lms = times(to_lms, s.lin);
    for (double& v : lms) v = std::cbrt(v);
    s.lab = times(kLmsToLab, lms);
    cs.push_back(s);
  }
  for (const AlphaStop& a : g.alphas) {
    Stop s;
    s.at = entry(a.p), s.m = std::clamp(a.m, 0.05, 0.95), s.a = std::clamp(a.a, 0., 1.);
    as.push_back(s);
  }
  const auto by_place = [](const Stop& x, const Stop& y) { return x.at < y.at; };
  std::stable_sort(cs.begin(), cs.end(), by_place);
  std::stable_sort(as.begin(), as.end(), by_place);

  const double smooth = g.smoothness / 100;
  const auto lerp = [](const Vec3& a, const Vec3& b, double w) {
    return Vec3{a[0] + (b[0] - a[0]) * w, a[1] + (b[1] - a[1]) * w, a[2] + (b[2] - a[2]) * w};
  };
  auto lut = std::make_shared<GradientLut>();
  for (int i = 0; i <= GradientLut::kSteps; i++) {
    std::array<float, 4>& out = lut->rgba[size_t(i)];
    const Where wc = where(cs, i);
    Vec3 c = cs[wc.k].enc;  // at a stop: its own colour, whatever the method
    if (wc.mix && wc.x > 0) {
      const Stop &a = cs[wc.k], &b = cs[wc.k + 1];
      const double u = midpoint_weight(wc.x, a.m);
      switch (method) {
        case GradientMethod::Classic: c = lerp(a.enc, b.enc, smoothness_weight(u, smooth)); break;
        case GradientMethod::Stripes: c = u < kStripesAt ? a.enc : b.enc; break;
        case GradientMethod::Linear: c = encode(lerp(a.lin, b.lin, u)); break;
        case GradientMethod::Perceptual: c = from_oklab(lerp(a.lab, b.lab, u)); break;
        case GradientMethod::Smooth: c = from_oklab(lerp(a.lab, b.lab, smoothstep(u))); break;
      }
    }
    const Where wa = where(as, i);
    double alpha = as[wa.k].a;
    if (wa.mix && wa.x > 0) {
      const Stop &a = as[wa.k], &b = as[wa.k + 1];
      const double u = midpoint_weight(wa.x, a.m);
      const double w = method == GradientMethod::Classic  ? smoothness_weight(u, smooth)
                       : method == GradientMethod::Stripes ? (u < kStripesAt ? 0 : 1)
                       : method == GradientMethod::Smooth  ? smoothstep(u)
                                                           : u;
      alpha = a.a + (b.a - a.a) * w;
    }
    out = {float(c[0]), float(c[1]), float(c[2]), float(alpha)};
  }
  lut->rgba[GradientLut::kSteps + 1] = lut->rgba[GradientLut::kSteps];
  return lut;
}

// ---- Shapes ----------------------------------------------------------------------------------------------------------

GradientShape at_level(GradientShape s, int level) {
  const double k = std::ldexp(1., -level);
  s.line = {s.line.x0 * k, s.line.y0 * k, s.line.x1 * k, s.line.y1 * k};
  return s;
}

GradientPlane plane(const GradientShape& s) {
  GradientPlane p;
  p.style = uint32_t(s.style);
  p.reverse = s.reverse ? 1 : 0;
  p.sx = float(s.line.x0), p.sy = float(s.line.y0);
  const double ex = s.line.x1 - s.line.x0, ey = s.line.y1 - s.line.y0, l2 = ex * ex + ey * ey;
  if (l2 > 0 && std::isfinite(l2)) p.ax = float(ex / l2), p.ay = float(ey / l2), p.inv_len = float(1 / std::sqrt(l2));
  return p;
}

float gradient_t(const GradientPlane& g, int x, int y) {
  const float dx = (float(x) + 0.5f) - g.sx, dy = (float(y) + 0.5f) - g.sy;
  const float u = dx * g.ax + dy * g.ay;  // along the line: 1 at its end
  float t;
  switch (GradientStyle(g.style)) {
    case GradientStyle::Radial: t = std::sqrt(dx * dx + dy * dy) * g.inv_len; break;
    case GradientStyle::Angle: t = turns(u, dx * g.ay - dy * g.ax); break;
    case GradientStyle::Reflected: t = std::fabs(u); break;
    case GradientStyle::Diamond: t = std::fabs(u) + std::fabs(dx * g.ay - dy * g.ax); break;
    default: t = u;  // Linear
  }
  t = t > 0 ? (t < 1 ? t : 1) : 0;  // NaN: 0
  return g.reverse ? 1 - t : t;
}

// ---- Fill layers, Gradient Overlay -----------------------------------------------------------------------------------

std::optional<GradientFill> gradient_fill_from(const json& j) {
  if (!j.is_object()) return std::nullopt;
  GradientFill f;
  if (const auto g = j.find("gradient"); g != j.end()) {
    std::optional<Gradient> gr = gradient_from(*g);
    if (!gr) return std::nullopt;
    f.gradient = std::move(*gr);
  }
  if (const auto m = j.find("method"); m != j.end()) {
    const std::optional<GradientMethod> v = m->is_string() ? gradient_method(m->get<std::string>()) : std::nullopt;
    if (!v) return std::nullopt;
    f.method = *v;
  }
  if (const auto s = j.find("style"); s != j.end()) {
    const std::optional<GradientStyle> v = s->is_string() ? gradient_style(s->get<std::string>()) : std::nullopt;
    if (!v) return std::nullopt;
    f.style = *v;
  }
  if (!number(j, "angle", -1e6, 1e6, f.angle) || !number(j, "scale", 1, 1000, f.scale) ||
      !boolean(j, "reverse", f.reverse) || !boolean(j, "dither", f.dither) || !boolean(j, "align", f.align))
    return std::nullopt;
  if (const auto o = j.find("offset"); o != j.end()) {
    if (!o->is_array() || o->size() != 2) return std::nullopt;
    for (size_t k = 0; k < 2; k++) {
      const json& v = (*o)[k];
      if (!v.is_number() || !std::isfinite(v.get<double>())) return std::nullopt;
      f.offset[k] = v.get<double>();
    }
  }
  return f;
}

std::string to_json(const GradientFill& f) {
  json j = json::object();
  j["gradient"] = gradient_json(f.gradient);
  j["method"] = kGradientMethodNames[size_t(f.method)], j["style"] = kGradientStyleNames[size_t(f.style)];
  j["angle"] = f.angle, j["scale"] = f.scale, j["reverse"] = f.reverse, j["dither"] = f.dither, j["align"] = f.align;
  j["offset"] = json::array({f.offset[0], f.offset[1]});
  return dump(j);
}

GradientLine fill_line(const GradientFill& f, double x, double y, double w, double h) {
  const double a = f.angle * 3.14159265358979323846 / 180, c = std::cos(a), s = std::sin(a);
  const double dx = c, dy = -s;  // document y runs down, Photoshop's angles turn counterclockwise on screen
  const double cx = x + w / 2 + f.offset[0] / 100 * w, cy = y + h / 2 + f.offset[1] / 100 * h;
  const double half = fill_span(w, h, c, s) / 2 * f.scale / 100;
  if (f.style == GradientStyle::Linear) return {cx - half * dx, cy - half * dy, cx + half * dx, cy + half * dy};
  return {cx, cy, cx + half * dx, cy + half * dy};
}

// ---- Pixels ----------------------------------------------------------------------------------------------------------

std::array<float, 4> dither_noise(int x, int y, uint32_t seed) {
  const std::array<uint32_t, 4> h = hash4(uint32_t(x), uint32_t(y), seed, 0);
  return {hash_unit(h[0]) - 0.5f, hash_unit(h[1]) - 0.5f, hash_unit(h[2]) - 0.5f, hash_unit(h[3]) - 0.5f};
}

uint16_t to_level(float v, Depth d, float noise) {
  const float m = float(max_value(d)), amp = d == Depth::U16 ? kDitherLevels16 : kDitherLevels8;
  const float x = v * m, back = m - x;
  const float end = x < back ? x : back;  // levels to 0 or the maximum, whichever is nearer
  const float room = end + end + 1;       // the widest noise that cannot carry x past it
  const float w = room < amp ? room : amp;
  const float y = noise != 0 ? x + noise * w : x;
  return uint16_t(y > 0 ? (y < m ? y + 0.5f : m) : 0.f);
}

void gradient_pixels(const GradientRender& g, Format f, int x, int y, int w, int h, uint8_t* dst, size_t stride) {
  if (!g.lut) throw std::invalid_argument("pb: render_gradient() without a table");
  const GradientPlane p = plane(g.shape);
  const GradientLut& lut = *g.lut;
  // Which table channel and dither offset each sample takes: R, G, B, alpha; grey and alpha; grey.
  static constexpr int kFrom[3][4] = {{0, 0, 0, 0}, {0, 3, 0, 0}, {0, 1, 2, 3}};
  const int* from = kFrom[f.channels == 4 ? 2 : f.channels == 2 ? 1 : 0];
  for (int j = 0; j < h; j++) {
    uint8_t* row = dst + size_t(j) * stride;
    for (int i = 0; i < w; i++) {
      const int px = x + i, py = y + j;
      const std::array<float, 4> c = lut.at(gradient_t(p, px, py));
      const std::array<float, 4> n = g.dither ? dither_noise(px, py, g.seed) : std::array<float, 4>{};
      for (int k = 0; k < f.channels; k++) {
        const uint16_t v = to_level(c[size_t(from[k])], f.depth, n[size_t(from[k])]);
        if (f.depth == Depth::U16)
          std::memcpy(row + (size_t(i) * size_t(f.channels) + size_t(k)) * 2, &v, 2);
        else
          row[size_t(i) * size_t(f.channels) + size_t(k)] = uint8_t(v);
      }
    }
  }
}

void render_gradient(const GradientRender& g, TileGrid& grid, TileRect r) {
  PROFILE_ZONE("render_gradient");
  if (!g.lut) throw std::invalid_argument("pb: render_gradient() without a table");
  const int cols = r.x1 - r.x0, rows = r.y1 - r.y0;
  if (cols <= 0 || rows <= 0) return;
  const Format f = grid.format();
  const Pixel fill = grid.fill();
  std::vector<TilePtr> out(size_t(cols) * size_t(rows));
  parallel_for(cols * rows, [&](int begin, int end) {
    PROFILE_ZONE("render_gradient tiles");
    for (int i = begin; i < end; i++) {
      const int tx = r.x0 + i % cols, ty = r.y0 + i / cols;
      const std::shared_ptr<Tile> t = Tile::make(f);
      {
        const WritePin w = t->write();
        gradient_pixels(g, f, tx << kTileLog2, ty << kTileLog2, kTileSize, kTileSize, w.data(),
                        size_t(kTileSize) * size_t(f.bytes()));
      }
      out[size_t(i)] = compact(t, fill);
    }
  });
  for (int i = 0; i < cols * rows; i++) grid.set(r.x0 + i % cols, r.y0 + i / cols, std::move(out[size_t(i)]));
}

}  // namespace pb
