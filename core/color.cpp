#include "color.h"

#include <lcms2.h>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace pb {
namespace {

// CIE xy of red, green, blue and white.
using Primaries = std::array<std::array<double, 2>, 4>;
constexpr Primaries kRec2020{{{0.708, 0.292}, {0.170, 0.797}, {0.131, 0.046}, {0.3127, 0.3290}}};
constexpr Primaries kSpaces[] = {  // in ColorSpace order
    {{{0.64, 0.33}, {0.30, 0.60}, {0.15, 0.06}, {0.3127, 0.3290}}},                  // sRGB
    {{{0.680, 0.320}, {0.265, 0.690}, {0.150, 0.060}, {0.3127, 0.3290}}},            // Display P3
    {{{0.64, 0.33}, {0.21, 0.71}, {0.15, 0.06}, {0.3127, 0.3290}}},                  // Adobe RGB (1998)
    {{{0.7347, 0.2653}, {0.1596, 0.8404}, {0.0366, 0.0001}, {0.345703, 0.358539}}},  // ProPhoto: ROMM, ICC's D50
};

using M3 = std::array<double, 9>;  // rows

M3 mul(const M3& a, const M3& b) {
  M3 c{};
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++)
      for (int k = 0; k < 3; k++) c[i * 3 + j] += a[i * 3 + k] * b[k * 3 + j];
  return c;
}

bool invert3(const double m[9], double out[9]) {  // false when singular
  const double a = m[0], b = m[1], c = m[2], d = m[3], e = m[4], f = m[5], g = m[6], h = m[7], i = m[8];
  const double det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
  if (std::abs(det) < 1e-12) return false;
  const double adj[9] = {e * i - f * h, c * h - b * i, b * f - c * e, f * g - d * i, a * i - c * g,
                         c * d - a * f, d * h - e * g, b * g - a * h, a * e - b * d};
  for (int k = 0; k < 9; k++) out[k] = adj[k] / det;
  return true;
}

M3 inverse(const M3& m) {
  M3 r{};
  invert3(m.data(), r.data());
  return r;
}

std::array<double, 3> xyz(std::array<double, 2> xy) { return {xy[0] / xy[1], 1, (1 - xy[0] - xy[1]) / xy[1]}; }

// RGB -> XYZ: the primaries' XYZ as columns, scaled so that RGB 1, 1, 1 is the white.
M3 to_xyz(const Primaries& p) {
  M3 m;
  for (int c = 0; c < 3; c++)
    for (int r = 0; r < 3; r++) m[r * 3 + c] = xyz(p[c])[r];
  const M3 inv = inverse(m);
  const auto w = xyz(p[3]);
  for (int c = 0; c < 3; c++)
    for (int r = 0; r < 3; r++) m[r * 3 + c] *= inv[c * 3] * w[0] + inv[c * 3 + 1] * w[1] + inv[c * 3 + 2] * w[2];
  return m;
}

// Bradford chromatic adaptation from white a to white b, both XYZ.
M3 bradford(std::array<double, 3> a, std::array<double, 3> b) {
  const M3 cone{0.8951, 0.2664, -0.1614, -0.7502, 1.7135, 0.0367, 0.0389, -0.0685, 1.0296};
  M3 d{};
  for (int i = 0; i < 3; i++)
    d[i * 4] = (cone[i * 3] * b[0] + cone[i * 3 + 1] * b[1] + cone[i * 3 + 2] * b[2]) /
               (cone[i * 3] * a[0] + cone[i * 3 + 1] * a[1] + cone[i * 3 + 2] * a[2]);
  return mul(inverse(cone), mul(d, cone));
}

std::array<double, 3> d50() {
  const cmsCIEXYZ* w = cmsD50_XYZ();
  return {w->X, w->Y, w->Z};
}

struct CloseProfile {
  void operator()(void* h) const { cmsCloseProfile(h); }
};
struct DeleteTransform {
  void operator()(void* t) const { cmsDeleteTransform(t); }
};
using Profile = std::unique_ptr<void, CloseProfile>;
using Transform = std::unique_ptr<void, DeleteTransform>;

Profile open(const std::vector<uint8_t>& icc) {
  Profile h(cmsOpenProfileFromMem(icc.data(), cmsUInt32Number(icc.size())));
  if (!h) throw std::runtime_error("not an ICC profile");
  return h;
}

// Into the PCS as a screen shows a document: relative colorimetric, black point compensation.
constexpr cmsUInt32Number kFlags = cmsFLAGS_BLACKPOINTCOMPENSATION | cmsFLAGS_NOOPTIMIZE;
constexpr int kSteps = 4096;  // values of a channel measured

// What an RGB profile's channels show: its primaries in the PCS (XYZ under D50), full red, green, blue as the columns
// of c, and each channel's response u, the amount of its primary that value k / (kSteps - 1) of the channel alone
// shows. Measured forward: lcms's own inverse is steep at black, and its rounding there turned black blue.
struct Response {
  M3 c, back;  // back: c's inverse
  std::vector<double> u[3];
};
Response respond(void* h, const char* what) {
  const Profile pcs(cmsCreateXYZProfile());
  const Transform in(cmsCreateTransform(h, TYPE_RGB_DBL, pcs.get(), TYPE_XYZ_DBL, INTENT_RELATIVE_COLORIMETRIC, kFlags));
  if (!in) throw std::runtime_error(what);  // lcms checks the space
  Response r;
  const double unit[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
  double prim[9];
  cmsDoTransform(in.get(), unit, prim, 3);
  for (int j = 0; j < 3; j++)
    for (int i = 0; i < 3; i++) r.c[i * 3 + j] = prim[j * 3 + i];
  if (!invert3(r.c.data(), r.back.data())) throw std::runtime_error("the ICC profile's primaries are degenerate");
  std::vector<double> dev(size_t(9) * kSteps), shows(dev.size());
  for (int ch = 0; ch < 3; ch++)
    for (int k = 0; k < kSteps; k++) dev[(size_t(ch) * kSteps + k) * 3 + ch] = double(k) / (kSteps - 1);
  cmsDoTransform(in.get(), dev.data(), shows.data(), cmsUInt32Number(3 * kSteps));
  for (int ch = 0; ch < 3; ch++) {
    r.u[ch].resize(kSteps);
    for (int k = 0; k < kSteps; k++) {
      const double* x = &shows[(size_t(ch) * kSteps + k) * 3];
      r.u[ch][size_t(k)] = r.back[ch * 3] * x[0] + r.back[ch * 3 + 1] * x[1] + r.back[ch * 3 + 2] * x[2];
    }
  }
  return r;
}

double srgb_encode(double v) { return v <= 0.0031308 ? 12.92 * v : 1.055 * std::pow(v, 1 / 2.4) - 0.055; }
float srgb_decode(float e) { return e <= 0.04045f ? e / 12.92f : std::pow((e + 0.055f) / 1.055f, 2.4f); }

// What a frame for d shows of linear value v on channel c: its table, interpolated as the canvas does (present.cpp).
float shown(const Display& d, int c, float v) {
  const float pos = std::sqrt(std::clamp(v, 0.f, 1.f)) * float(Display::kCurve - 1);
  const int i = std::min(int(pos), Display::kCurve - 2);
  return d.curve[c][i] + (pos - float(i)) * (d.curve[c][i + 1] - d.curve[c][i]);
}

// True when frames for d show every colour sRGB has within a quarter level of sRGB's route.
bool like_srgb(const Display& d) {
  const std::array<float, 9> to = rec2020_to(ColorSpace::Srgb);
  double from[9], t[9];
  std::copy(to.begin(), to.end(), t);
  invert3(t, from);
  for (int r = 0; r <= 8; r++)
    for (int g = 0; g <= 8; g++)
      for (int b = 0; b <= 8; b++) {
        const float e[3] = {r / 8.f, g / 8.f, b / 8.f};
        double lin[3], rec[3];
        for (int c = 0; c < 3; c++) lin[c] = srgb_decode(e[c]);
        for (int i = 0; i < 3; i++) rec[i] = from[i * 3] * lin[0] + from[i * 3 + 1] * lin[1] + from[i * 3 + 2] * lin[2];
        for (int c = 0; c < 3; c++) {
          const auto& m = d.from_rec2020;
          const double v = m[c * 3] * rec[0] + m[c * 3 + 1] * rec[1] + m[c * 3 + 2] * rec[2];
          if (std::abs(shown(d, c, float(v)) - e[c]) > 0.25f / 255) return false;
        }
      }
  return true;
}

}  // namespace

const std::vector<uint8_t>& icc_profile(ColorSpace space) {
  static const auto profiles = [] {
    // Adobe RGB (1998) is Adobe's name: "compatible" as darktable calls its own
    const char* names[] = {"sRGB IEC61966-2.1", "Display P3", "Adobe RGB (compatible)", "ProPhoto RGB"};
    const double srgb[5] = {2.4, 1 / 1.055, 0.055 / 1.055, 1 / 12.92, 0.04045};  // lcms's curve type 4
    std::array<std::vector<uint8_t>, 4> out;
    for (int i = 0; i < 4; i++) {
      const Primaries& p = kSpaces[i];
      const cmsCIExyY white{p[3][0], p[3][1], 1};
      const cmsCIExyYTRIPLE rgb{{p[0][0], p[0][1], 1}, {p[1][0], p[1][1], 1}, {p[2][0], p[2][1], 1}};
      cmsToneCurve* curve = ColorSpace(i) == ColorSpace::AdobeRgb   ? cmsBuildGamma(nullptr, 563 / 256.)
                            : ColorSpace(i) == ColorSpace::ProPhoto ? cmsBuildGamma(nullptr, 461 / 256.)
                                                                    : cmsBuildParametricToneCurve(nullptr, 4, srgb);
      cmsToneCurve* curves[3] = {curve, curve, curve};
      cmsHPROFILE h = curve ? cmsCreateRGBProfile(&white, &rgb, curves) : nullptr;
      cmsFreeToneCurve(curve);
      cmsMLU* desc = cmsMLUalloc(nullptr, 1);
      cmsUInt32Number n = 0;
      bool ok = h && desc && cmsMLUsetASCII(desc, "en", "US", names[i]) &&
                cmsWriteTag(h, cmsSigProfileDescriptionTag, desc);
      if (ok) cmsSetProfileVersion(h, 2.1);
      ok = ok && cmsSaveProfileToMem(h, nullptr, &n);
      out[i].resize(n);
      ok = ok && cmsSaveProfileToMem(h, out[i].data(), &n);
      if (desc) cmsMLUfree(desc);
      if (h) cmsCloseProfile(h);
      if (!ok) throw std::runtime_error(std::string("cannot make the ICC profile ") + names[i]);
    }
    return out;
  }();
  return profiles[int(space)];
}

std::array<float, 9> rec2020_to(ColorSpace space) {
  const Primaries& p = kSpaces[int(space)];
  const M3 m = mul(inverse(to_xyz(p)), mul(bradford(xyz(kRec2020[3]), xyz(p[3])), to_xyz(kRec2020)));
  std::array<float, 9> f;
  for (int i = 0; i < 9; i++) f[i] = float(m[i]);
  return f;
}

std::shared_ptr<const Display> display_profile(const std::vector<uint8_t>& icc) {
  const Profile h = open(icc);
  const Response r = respond(h.get(), "not an RGB profile of a screen");
  // Linear Rec.2020 into the PCS as its own profile would take it (Bradford from D65 to D50), then into the screen's
  // linear RGB.
  const M3 m = mul(r.back, mul(bradford(xyz(kRec2020[3]), d50()), to_xyz(kRec2020)));
  auto d = std::make_shared<Display>();
  for (int i = 0; i < 9; i++) d->from_rec2020[i] = float(m[i]);
  // Each channel's encoding: the inverse of its response.
  for (int ch = 0; ch < 3; ch++) {
    const std::vector<double>& u = r.u[ch];
    for (int i = 0, k = 0; i < Display::kCurve; i++) {
      const double want = std::pow(double(i) / (Display::kCurve - 1), 2);
      while (k < kSteps - 2 && u[size_t(k) + 1] < want) k++;  // u[k + 1] >= want unless at the top
      const double span = u[size_t(k) + 1] - u[size_t(k)];
      const double f = span > 0 ? std::clamp((want - u[size_t(k)]) / span, 0., 1.) : 0.;
      d->curve[ch][i] = float((k + f) / (kSteps - 1));
    }
  }
  if (like_srgb(*d)) return nullptr;
  char name[256] = "";
  cmsGetProfileInfoASCII(h.get(), cmsInfoDescription, "en", "US", name, sizeof name);
  d->name = name;
  return d;
}

const Display& srgb_display() {
  static const Display d = [] {
    Display s;
    s.name = "sRGB IEC61966-2.1";
    s.from_rec2020 = rec2020_to(ColorSpace::Srgb);
    for (int c = 0; c < 3; c++)
      for (int i = 0; i < Display::kCurve; i++)
        s.curve[c][i] = float(srgb_encode(std::pow(double(i) / (Display::kCurve - 1), 2)));
    return s;
  }();
  return d;
}

std::vector<uint8_t> screen_profile(const std::string& device) {
#ifdef _WIN32
  const std::wstring name = std::filesystem::path(std::u8string(device.begin(), device.end())).wstring();
  const HDC dc = CreateDCW(name.c_str(), nullptr, nullptr, nullptr);  // as Chromium asks for a monitor's
  if (!dc) return {};
  wchar_t file[MAX_PATH + 1];
  DWORD n = MAX_PATH;
  const BOOL ok = GetICMProfileW(dc, &n, file);
  DeleteDC(dc);
  if (!ok) return {};
  std::ifstream f(std::filesystem::path(file), std::ios::binary);
  return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
#else
  (void)device;  // ponytail: X11's _ICC_PROFILE and macOS's ColorSync come with those builds (P1)
  return {};
#endif
}

std::shared_ptr<const Source> source_profile(const std::vector<uint8_t>& icc) {
  static_assert(Source::kLut == kSteps, "the response is the table");
  const Profile h = open(icc);
  auto s = std::make_shared<Source>();
  if (cmsGetColorSpace(h.get()) == cmsSigGrayData) {  // its curve to the PCS's luminance, white 1
    const Profile pcs(cmsCreateXYZProfile());
    const Transform t(
        cmsCreateTransform(h.get(), TYPE_GRAY_DBL, pcs.get(), TYPE_XYZ_DBL, INTENT_RELATIVE_COLORIMETRIC, kFlags));
    if (!t) throw std::runtime_error("not a grey profile lcms takes");
    std::vector<double> g(kSteps), x(size_t(3) * kSteps);
    for (int k = 0; k < kSteps; k++) g[size_t(k)] = double(k) / (kSteps - 1);
    cmsDoTransform(t.get(), g.data(), x.data(), cmsUInt32Number(kSteps));
    for (int c = 0; c < 3; c++)
      for (int k = 0; k < kSteps; k++) s->linear[c][k] = float(x[size_t(k) * 3 + 1]);
    s->to_rec2020 = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    return s;
  }
  const Response r = respond(h.get(), "not an RGB or grey profile");
  // From the PCS (D50) to Rec.2020's white (D65) by Bradford, as display_profile() goes the other way.
  const M3 m = mul(inverse(to_xyz(kRec2020)), mul(bradford(d50(), xyz(kRec2020[3])), r.c));
  for (int i = 0; i < 9; i++) s->to_rec2020[size_t(i)] = float(m[size_t(i)]);
  for (int c = 0; c < 3; c++)
    for (int k = 0; k < kSteps; k++) s->linear[c][k] = float(r.u[c][size_t(k)]);
  return s;
}

}  // namespace pb
