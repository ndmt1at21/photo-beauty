// pb-bench open: a 45 MP JPEG (8256 x 5504, a D850's size: smooth colours with grain, quality 92, 4:2:0, written by
// TurboJPEG to a temporary file first) opened as a document (open_image()), the median of 5 runs; and TurboJPEG alone
// decoding it from memory into one buffer, the floor.
#include <turbojpeg.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <system_error>
#include <vector>

#include "bench.h"
#include "jobs.h"  // snapone's parallel_for()
#include "open.h"

BENCH(open) {
  const int w = 8256, h = 5504;
  std::vector<uint8_t> rgb(size_t(w) * h * 4);  // RGB to compress, RGBX the floor's decode
  snapone::parallel_for(h, [&](int y0, int y1) {
    std::minstd_rand rng(uint32_t(y0) + 1);
    for (int y = y0; y < y1; y++)
      for (int x = 0; x < w; x++) {
        uint8_t* p = &rgb[(size_t(y) * w + x) * 3];
        const double v[3] = {128 + 90 * std::sin(x / 300. + y / 470.), 128 + 90 * std::sin(x / 210. - y / 330. + 1),
                             128 + 90 * std::cos(x / 150. + y / 520.)};
        for (int c = 0; c < 3; c++) p[c] = uint8_t(std::clamp(v[c] + double(rng() % 17) - 8, 0., 255.));
      }
  });
  tjhandle t = tj3Init(TJINIT_COMPRESS);
  unsigned char* jpeg = nullptr;
  size_t size = 0;
  if (!t || tj3Set(t, TJPARAM_QUALITY, 92) || tj3Set(t, TJPARAM_SUBSAMP, TJSAMP_420) ||
      tj3Compress8(t, rgb.data(), w, 0, h, TJPF_RGB, &jpeg, &size)) {
    std::fprintf(stderr, "open: TurboJPEG: %s\n", tj3GetErrorStr(t));
    return;
  }
  const std::filesystem::path path =
      std::filesystem::temp_directory_path() / ("pb-bench-open-" + std::to_string(std::random_device{}()) + ".jpg");
  std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(jpeg), std::streamsize(size));
  tj3Destroy(t);
  std::vector<double> ms, floor;
  for (int i = 0; i < 5; i++) {
    const auto t0 = pb::bench::Clock::now();
    const pb::DocPtr d = pb::open_image(path);
    ms.push_back(pb::bench::ms_since(t0));
  }
  // The floor: TurboJPEG decoding the same data from memory into one RGBA buffer, no tiles.
  t = tj3Init(TJINIT_DECOMPRESS);
  for (int i = 0; i < 5; i++) {
    const auto t0 = pb::bench::Clock::now();
    if (tj3DecompressHeader(t, jpeg, size) || tj3Decompress8(t, jpeg, size, rgb.data(), 0, TJPF_RGBX)) break;
    floor.push_back(pb::bench::ms_since(t0));
  }
  tj3Destroy(t);
  tj3Free(jpeg);
  std::error_code ec;
  std::filesystem::remove(path, ec);
  std::sort(ms.begin(), ms.end());
  std::sort(floor.begin(), floor.end());
  pb::bench::report("open_jpeg_45mp", ms[2], "ms");
  if (floor.size() == 5) pb::bench::report("open_jpeg_45mp_turbojpeg_only", floor[2], "ms");
  pb::bench::report("open_jpeg_45mp_file", double(size) / 1e6, "MB");
}
