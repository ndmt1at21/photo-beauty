#include "color.h"

#include <lcms2.h>

#include <array>
#include <stdexcept>
#include <string>

namespace pb {
namespace {

// CIE xy of red, green, blue and white.
using Primaries = std::array<std::array<double, 2>, 4>;
constexpr Primaries kSpaces[] = {  // in ColorSpace order
    {{{0.64, 0.33}, {0.30, 0.60}, {0.15, 0.06}, {0.3127, 0.3290}}},                  // sRGB
    {{{0.680, 0.320}, {0.265, 0.690}, {0.150, 0.060}, {0.3127, 0.3290}}},            // Display P3
    {{{0.64, 0.33}, {0.21, 0.71}, {0.15, 0.06}, {0.3127, 0.3290}}},                  // Adobe RGB (1998)
    {{{0.7347, 0.2653}, {0.1596, 0.8404}, {0.0366, 0.0001}, {0.345703, 0.358539}}},  // ProPhoto: ROMM, ICC's D50
};

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

}  // namespace pb
