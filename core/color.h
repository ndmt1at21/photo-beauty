#pragma once
#include <cstdint>
#include <vector>

namespace pb {

// RGB spaces, each with its ICC profile. Display P3 has sRGB's tone curve, Adobe RGB a gamma of 563/256, ProPhoto
// 1.8 as its profile stores it (461/256).
enum class ColorSpace { Srgb, DisplayP3, AdobeRgb, ProPhoto };
// The space's ICC profile, version 2: every reader takes it (Firefox ignores version 4 by default).
const std::vector<uint8_t>& icc_profile(ColorSpace);

}  // namespace pb
