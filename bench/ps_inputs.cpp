// pb-bench ps-inputs <dir>: base.png and top.png, the two layers tools/ps-golden/blend.jsx composites in Photoshop in
// each P0 blend mode (tests/test_ps_golden.cpp composites them with B1 and compares). 256 x 384, RGBA, 16 bits, no
// colour chunk (sRGB values; an untagged file is one Photoshop never converts on opening, and the script assigns
// sRGB). Rows 0-255: the backdrop ramps across, the layer above down, both opaque, every pair of 256 levels in each
// channel; 256-319: patches of 8 colours across under 8 down; 320-383: the backdrop's alpha ramps across and the
// layer's down, on fixed colours.
#include <spng.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "bench.h"

namespace {

constexpr int W = 256, H = 384;
struct Rgba {
  double r, g, b, a;
};

// x in 0..1 as a 16-bit sample Photoshop reads back exactly: it keeps 16-bit samples as 0..32768 and rounds into
// them, so 2 v for level v (65535 for 32768) lands on v.
uint16_t level(double x) {
  const long v = std::lround(x * 32768);
  return uint16_t(v < 32768 ? 2 * v : 65535);
}

void write_png(const std::string& path, const std::vector<uint16_t>& px) {
  const std::unique_ptr<FILE, int (*)(FILE*)> f(std::fopen(path.c_str(), "wb"), std::fclose);
  if (!f) throw std::runtime_error("ps-inputs: cannot write " + path);
  const std::unique_ptr<spng_ctx, void (*)(spng_ctx*)> ctx(spng_ctx_new(SPNG_CTX_ENCODER), spng_ctx_free);
  const auto check = [&](int rc) {
    if (rc) throw std::runtime_error("ps-inputs: " + path + ": " + spng_strerror(rc));
  };
  check(spng_set_png_file(ctx.get(), f.get()));
  spng_ihdr ihdr{W, H, 16, SPNG_COLOR_TYPE_TRUECOLOR_ALPHA, 0, 0, 0};
  check(spng_set_ihdr(ctx.get(), &ihdr));
  check(spng_encode_image(ctx.get(), px.data(), px.size() * 2, SPNG_FMT_PNG, SPNG_ENCODE_FINALIZE));  // host-endian
}

}  // namespace

BENCH(ps_inputs) {
  if (args.size() != 1) return void(std::printf("ps_inputs: needs a directory: pb-bench ps-inputs <dir>\n"));
  const Rgba below[8] = {{0, 0, 0, 1},   {1, 1, 1, 1}, {.5, .5, .5, 1}, {1, 0, 0, 1},
                         {0, 1, 0, 1},   {0, 0, 1, 1}, {.87, .67, .55, 1}, {.1, .35, .4, 1}};
  const Rgba above[8] = {{1, 1, 1, 1}, {0, 0, 0, 1}, {.5, .5, .5, 1},    {0, 1, 1, 1},
                         {1, 0, 1, 1}, {1, 1, 0, 1}, {.75, .75, .75, 1}, {.4, .25, .1, 1}};
  std::vector<uint16_t> base(size_t(W) * H * 4), top(base.size());
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      const double u = x / 255., v = y / 255.;
      Rgba b, t;
      if (y < 256) {
        b = {u, 1 - u, u, 1}, t = {v, v, 1 - v, 1};
      } else if (y < 320) {
        b = below[x / 32], t = above[(y - 256) / 8];
      } else {  // B(Cb, Cs) differs from Cs in some channel in every mode
        b = {.8, .25, .55, u}, t = {.3, .6, .9, (y - 320) / 63.};
      }
      uint16_t* p = &base[(size_t(y) * W + size_t(x)) * 4];
      uint16_t* q = &top[(size_t(y) * W + size_t(x)) * 4];
      p[0] = level(b.r), p[1] = level(b.g), p[2] = level(b.b), p[3] = level(b.a);
      q[0] = level(t.r), q[1] = level(t.g), q[2] = level(t.b), q[3] = level(t.a);
    }
  write_png(args[0] + "/base.png", base);
  write_png(args[0] + "/top.png", top);
  std::printf("ps_inputs: %s/base.png, %s/top.png\n", args[0].c_str(), args[0].c_str());
}
