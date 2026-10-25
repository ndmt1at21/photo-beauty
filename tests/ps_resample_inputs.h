#pragma once
// The inputs of tools/ps-golden/resample.jsx (T38: Image Size, Free Transform, Warp, Image Rotation and Canvas Size in
// Photoshop), as pb-bench ps-resample writes them into tests/golden/ps/resample/in and test_ps_resample_inputs checks
// the files there: what each holds is below. Every sample a level Photoshop reads back exactly (golden::ps_level), 16
// bits, no colour chunk (sRGB values: the script assigns sRGB, nothing converts). Header only, for pb-bench and
// pb-tests, as golden.h.
#include <algorithm>
#include <filesystem>
#include <vector>

#include "golden.h"

namespace pb::golden::resample {

// checker.png, ramp.png, photo.png are kW x kH: not multiples of 8, so that 37% of either is no simple ratio (37 x 26
// pixels; 3/8 of a multiple of 8 rounds the same).
inline constexpr int kW = 100, kH = 70;
// The *-pad.png: kPadW x kPadH, RGBA, a kW x kH image at (kPadX, kPadY) on transparent black, room around it for what
// a transform or a warp makes of it. That rectangle is the content's bounds: Photoshop's transform and warp box.
inline constexpr int kPadW = 160, kPadH = 128, kPadX = 30, kPadY = 29;

// checker.png, RGB: kSquare px squares, kLight at the top left, then kDark; the squares from x = kFineX on are checks
// of 1 px (in the rows of squares 0 and 1), 2 px (2 and 3) and 5 px (4 to 6). The squares: a kernel's step response
// at every scale; the fine checks: what reducing makes of detail (prefiltered grey or aliased). The two colours differ
// by another amount in each channel and keep away from black and white, so that ringing shows unclipped.
inline constexpr int kSquare = 10, kFineX = 70;
inline constexpr double kLight[3] = {.80, .72, .60}, kDark[3] = {.20, .28, .42};
inline int check_size(int x, int y) {
  if (x < kFineX) return kSquare;
  const int row = y / kSquare;
  return row < 2 ? 1 : row < 4 ? 2 : 5;
}
inline bool light(int x, int y) {  // of checker.png's pixel (x, y)
  const int s = check_size(x, y);
  return (x / s + y / s) % 2 == 0;
}
inline Image checker() {
  Image im(kW, kH, 3);
  for (int y = 0; y < kH; y++)
    for (int x = 0; x < kW; x++) {
      const double* c = light(x, y) ? kLight : kDark;
      uint16_t* p = im.at(x, y);
      for (int k = 0; k < 3; k++) p[k] = ps_level(c[k]);
      p[3] = 65535;
    }
  return im;
}

// ramp.png, RGB: R (x + 0.5) / kW and G (y + 0.5) / kH, where the pixel's centre is: what a pixel resampled,
// transformed or warped holds of them says where in the input it was taken (interpolation keeps a plane exactly, away
// from the edges); B (R² + G²) / 2, a curve: how far an interpolation is from exact at second order (Keys' cubic is
// exact there only at a = -0.5). Without the curve (ramp-pad.png's), B is 0.5: a warped curve costs three times the
// bytes and says nothing of the map.
inline Image ramp(bool curve = true) {
  Image im(kW, kH, 3);
  for (int y = 0; y < kH; y++)
    for (int x = 0; x < kW; x++) {
      const double u = (x + .5) / kW, v = (y + .5) / kH;
      uint16_t* p = im.at(x, y);
      p[0] = ps_level(u), p[1] = ps_level(v), p[2] = ps_level(curve ? (u * u + v * v) / 2 : .5), p[3] = 65535;
    }
  return im;
}

// photo.png, RGB: kW x kH of B30's photo.png (golden::inputs::photo) from (kPhotoX, kPhotoY): meat, a red chilli, onion
// rings, leaves; the thin rings and the chilli's edges show what resampling does to real detail.
inline constexpr int kPhotoX = 96, kPhotoY = 120;
inline Image photo(const std::filesystem::path& jpeg) {
  const Image whole = inputs::photo(jpeg);
  Image im(kW, kH, 3);
  for (int y = 0; y < kH; y++)
    for (int x = 0; x < kW; x++) std::copy_n(whole.at(kPhotoX + x, kPhotoY + y), 4, im.at(x, y));
  return im;
}

// im, kW x kH, at (kPadX, kPadY) of a kPadW x kPadH canvas of transparent black: RGBA.
inline Image pad(const Image& im) {
  Image out(kPadW, kPadH, 4);
  for (int y = 0; y < im.h; y++)
    for (int x = 0; x < im.w; x++) std::copy_n(im.at(x, y), 4, out.at(kPadX + x, kPadY + y));
  return out;
}

// Every file of resample/in/, by name: checker.png, ramp.png, photo.png, then the three padded.
inline std::vector<inputs::File> all(const std::filesystem::path& jpeg) {
  const Image c = checker(), p = photo(jpeg);
  return {{"checker.png", c},        {"ramp.png", ramp()},               {"photo.png", p},
          {"checker-pad.png", pad(c)}, {"ramp-pad.png", pad(ramp(false))}, {"photo-pad.png", pad(p)}};
}

}  // namespace pb::golden::resample
