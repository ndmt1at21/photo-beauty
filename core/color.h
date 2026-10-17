#pragma once
// Colour spaces and ICC profiles: the profiles a document or an export embeds, and how the canvas (present.h) takes a
// document's samples to the screen's: its profile's curve to linear light (Source), a matrix through linear Rec.2020 to
// the screen's linear RGB and its curve (Display). Display, display_profile(), screen_profile() and rec2020_to() as
// snapone's core/color.* has them.
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace pb {

// RGB spaces, each with its ICC profile. Display P3 has sRGB's tone curve, Adobe RGB a gamma of 563/256, ProPhoto
// 1.8 as its profile stores it (461/256).
enum class ColorSpace { Srgb, DisplayP3, AdobeRgb, ProPhoto };
// The space's ICC profile, version 2: every reader takes it (Firefox ignores version 4 by default).
const std::vector<uint8_t>& icc_profile(ColorSpace);
// Linear Rec.2020 -> the space's linear RGB, rows. White stays white: ProPhoto's is D50, reached by Bradford
// adaptation like a conversion through ICC profiles.
std::array<float, 9> rec2020_to(ColorSpace);

// A screen's colours as its ICC profile describes them: the canvas shows documents in them, relative colorimetric with
// black point compensation, as snapone shows photos.
struct Display {
  static constexpr int kCurve = 1024;
  std::string name;                   // the profile's description
  std::array<float, 9> from_rec2020;  // linear Rec.2020 -> the screen's linear RGB, rows
  float curve[3][kCurve];             // R, G, B: the screen's value for linear (i / (kCurve - 1))²
};
// The screen an ICC profile describes; null when it shows colours as sRGB does (within a quarter level), so that frames
// for it stay sRGB's (srgb_display()), bit for bit. Matrix and curves profiles (what the OS and most calibrators make)
// exactly; LUT profiles through their primaries and the curves along them. Throws std::runtime_error for data that is
// not an RGB profile. Any thread.
std::shared_ptr<const Display> display_profile(const std::vector<uint8_t>& icc);
// sRGB's screen: what frames take where display_profile() gives null, and where the system has no screen profile.
const Display& srgb_display();
// The ICC profile Windows uses for a screen, by its GDI device name ("\\.\DISPLAY1", QScreen::name() on Windows); empty
// when there is none, and on other systems (sRGB until P1).
std::vector<uint8_t> screen_profile(const std::string& device);

// A document's samples in linear light as its ICC profile describes them, relative colorimetric with black point
// compensation as Display: what the canvas decodes them with.
struct Source {
  static constexpr int kLut = 4096;
  std::array<float, 9> to_rec2020;  // linear RGB -> linear Rec.2020, rows; a grey profile's grey is neutral (identity)
  float linear[3][kLut];            // R, G, B: the linear value of encoded value i / (kLut - 1); grey: its curve thrice
};
// Throws std::runtime_error for data that is not an RGB or grey ICC profile. Any thread.
std::shared_ptr<const Source> source_profile(const std::vector<uint8_t>& icc);

}  // namespace pb
