// tests/golden.h (B30): its PNG reading and writing, CIEDE2000 against Sharma, Wu and Dalal's pairs, comparing over
// white and black, tiles, sets and index.json; P1's inputs as pb-bench ps-inputs wrote them into tests/golden/ps/p1/in/;
// and Photoshop's sets once B33 brought them (until then said to be skipped).
#include "golden.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

#include "check.h"

using namespace pb;
namespace fs = std::filesystem;

namespace {

// A folder of its own, removed with it.
struct TempDir {
  fs::path path = fs::temp_directory_path() / ("pb-golden-" + std::to_string(std::random_device{}()));
  TempDir() { fs::create_directories(path); }
  ~TempDir() {
    std::error_code e;
    fs::remove_all(path, e);
  }
};

void set_env(const char* name, const char* value) {  // null: unset
#ifdef _WIN32
  _putenv_s(name, value ? value : "");
#else
  if (value)
    setenv(name, value, 1);
  else
    unsetenv(name);
#endif
}

std::string bytes(const fs::path& path) {
  std::ifstream f(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

golden::Image read_bytes(const unsigned char* data, size_t n, const fs::path& path) {
  std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(data), std::streamsize(n));
  return golden::read_png(path);
}

golden::Image random_image(int w, int h, std::mt19937& rng) {
  golden::Image im(w, h);
  for (uint16_t& v : im.px) v = uint16_t(rng());
  return im;
}

bool same(const uint16_t* p, uint16_t r, uint16_t g, uint16_t b, uint16_t a) {
  return p[0] == r && p[1] == g && p[2] == b && p[3] == a;
}

// PNGs made by Python's zlib, not spng: 3 x 2 grey and alpha at 8 bits (g, a: (0, 255) (128, 64) (255, 0), (1, 2)
// (254, 253) (77, 200)); 2 x 2 RGB at 8 bits ((255, 0, 0) (0, 255, 0), (0, 0, 255) (12, 34, 56)); 2 x 1 grey at 16
// bits (0x0102, 0xfeff); 2 x 1 palette with tRNS (red, blue at alpha 128).
const unsigned char kGreyAlpha8[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x03,
    0x00, 0x00, 0x00, 0x02, 0x08, 0x04, 0x00, 0x00, 0x00, 0x37, 0x7d, 0xae, 0x91, 0x00, 0x00, 0x00, 0x16, 0x49, 0x44, 0x41,
    0x54, 0x78, 0xda, 0x63, 0x60, 0xf8, 0xdf, 0xe0, 0xf0, 0x9f, 0x81, 0x81, 0x91, 0xe9, 0xdf, 0x5f, 0xdf, 0x13, 0x00, 0x25,
    0x5a, 0x05, 0xd2, 0x3e, 0xde, 0xef, 0xcd, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
const unsigned char kRgb8[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x02,
    0x00, 0x00, 0x00, 0x02, 0x08, 0x02, 0x00, 0x00, 0x00, 0xfd, 0xd4, 0x9a, 0x73, 0x00, 0x00, 0x00, 0x13, 0x49, 0x44, 0x41,
    0x54, 0x78, 0xda, 0x63, 0xf8, 0xcf, 0xc0, 0xc0, 0x00, 0xc2, 0x0c, 0xff, 0x79, 0x94, 0x2c, 0x00, 0x1a, 0x94, 0x03, 0x64,
    0xda, 0x2a, 0xa5, 0xa5, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
const unsigned char kGrey16[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x02,
    0x00, 0x00, 0x00, 0x01, 0x10, 0x00, 0x00, 0x00, 0x00, 0x81, 0xd9, 0xfc, 0x15, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x44, 0x41,
    0x54, 0x78, 0xda, 0x63, 0x60, 0x64, 0xfa, 0xf7, 0x1f, 0x00, 0x03, 0x0a, 0x02, 0x01, 0x6a, 0x34, 0xdc, 0xc8, 0x00, 0x00,
    0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
const unsigned char kPalette8[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x02,
    0x00, 0x00, 0x00, 0x01, 0x08, 0x03, 0x00, 0x00, 0x00, 0xc3, 0xfc, 0x8f, 0xb8, 0x00, 0x00, 0x00, 0x06, 0x50, 0x4c, 0x54,
    0x45, 0xff, 0x00, 0x00, 0x00, 0x00, 0xff, 0x6c, 0xa1, 0xfd, 0x8e, 0x00, 0x00, 0x00, 0x02, 0x74, 0x52, 0x4e, 0x53, 0xff,
    0x80, 0x08, 0x0f, 0xb3, 0x6a, 0x00, 0x00, 0x00, 0x0b, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0x60, 0x60, 0x04, 0x00,
    0x00, 0x04, 0x00, 0x02, 0x2c, 0xde, 0x48, 0xad, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};

const fs::path kIn = golden::kRoot / "p1" / "in";
const fs::path kPhoto = fs::exists(PB_SOURCE_DIR "/ui/mock/photos/monan.jpg") ? fs::path(PB_SOURCE_DIR "/ui/mock/photos/monan.jpg")
                                                                              : fs::path("ui/mock/photos/monan.jpg");

// Whether tests/golden/ps and the photo are here: in the source tree always (p1/in/ missing there fails the tests),
// beside a Windows .exe only when copied.
bool have_tree(const char* test) {
  if (fs::exists(golden::kRoot) && fs::exists(kPhoto)) return true;
  std::printf("%s: skipped, no %s or %s here (a Windows .exe: copy tests/golden/ps and ui/mock/photos/monan.jpg "
              "beside it)\n", test, golden::kRoot.generic_string().c_str(), kPhoto.generic_string().c_str());
  return false;
}

}  // namespace

// The 34 pairs of Sharma, Wu and Dalal's "The CIEDE2000 color-difference formula: implementation notes, supplementary
// test data, and mathematical observations" (Color Research & Application, 2005), Table 1: to 1e-4 either way round.
// And sRGB through Lab D65: white L 100, black 0, the primaries as Lindbloom's calculator has them.
TEST(test_golden_delta_e) {
  struct Pair {
    double l1, a1, b1, l2, a2, b2, de;
  };
  const Pair pairs[] = {
      {50, 2.6772, -79.7751, 50, 0, -82.7485, 2.0425},          {50, 3.1571, -77.2803, 50, 0, -82.7485, 2.8615},
      {50, 2.8361, -74.0200, 50, 0, -82.7485, 3.4412},          {50, -1.3802, -84.2814, 50, 0, -82.7485, 1.0000},
      {50, -1.1848, -84.8006, 50, 0, -82.7485, 1.0000},         {50, -0.9009, -85.5211, 50, 0, -82.7485, 1.0000},
      {50, 0, 0, 50, -1, 2, 2.3669},                            {50, -1, 2, 50, 0, 0, 2.3669},
      {50, 2.49, -0.001, 50, -2.49, 0.0009, 7.1792},            {50, 2.49, -0.001, 50, -2.49, 0.001, 7.1792},
      {50, 2.49, -0.001, 50, -2.49, 0.0011, 7.2195},            {50, 2.49, -0.001, 50, -2.49, 0.0012, 7.2195},
      {50, -0.001, 2.49, 50, 0.0009, -2.49, 4.8045},            {50, -0.001, 2.49, 50, 0.001, -2.49, 4.8045},
      {50, -0.001, 2.49, 50, 0.0011, -2.49, 4.7461},            {50, 2.5, 0, 50, 0, -2.5, 4.3065},
      {50, 2.5, 0, 73, 25, -18, 27.1492},                       {50, 2.5, 0, 61, -5, 29, 22.8977},
      {50, 2.5, 0, 56, -27, -3, 31.9030},                       {50, 2.5, 0, 58, 24, 15, 19.4535},
      {50, 2.5, 0, 50, 3.1736, 0.5854, 1.0000},                 {50, 2.5, 0, 50, 3.2972, 0, 1.0000},
      {50, 2.5, 0, 50, 1.8634, 0.5757, 1.0000},                 {50, 2.5, 0, 50, 3.2592, 0.3350, 1.0000},
      {60.2574, -34.0099, 36.2677, 60.4626, -34.1751, 39.4387, 1.2644},
      {63.0109, -31.0961, -5.8663, 62.8187, -29.7946, -4.0864, 1.2630},
      {61.2901, 3.7196, -5.3901, 61.4292, 2.2480, -4.9620, 1.8731},
      {35.0831, -44.1164, 3.7933, 35.0232, -40.0716, 1.5901, 1.8645},
      {22.7233, 20.0904, -46.6940, 23.0331, 14.9730, -42.5619, 2.0373},
      {36.4612, 47.8580, 18.3852, 36.2715, 50.5065, 21.2231, 1.4146},
      {90.8027, -2.0831, 1.4410, 91.1528, -1.6435, 0.0447, 1.4441},
      {90.9257, -0.5406, -0.9208, 88.6381, -0.8985, -0.7239, 1.5381},
      {6.7747, -0.2908, -2.4247, 5.8714, -0.0985, -2.2286, 0.6377},
      {2.0776, 0.0795, -1.1350, 0.9033, -0.0636, -0.5514, 0.9082}};
  static_assert(std::size(pairs) == 34);
  int i = 0;
  for (const Pair& p : pairs) {
    i++;
    const double de = golden::delta_e2000({p.l1, p.a1, p.b1}, {p.l2, p.a2, p.b2});
    const double back = golden::delta_e2000({p.l2, p.a2, p.b2}, {p.l1, p.a1, p.b1});
    if (!(std::abs(de - p.de) < 1e-4 && std::abs(back - p.de) < 1e-4))
      std::printf("CIEDE2000 pair %d: %.6f and %.6f, Sharma %.4f\n", i, de, back, p.de);
    CHECK(std::abs(de - p.de) < 1e-4 && std::abs(back - p.de) < 1e-4);
  }
  const auto near = [](const golden::Lab& l, double L, double a, double b) {
    return std::abs(l.L - L) < 1e-3 && std::abs(l.a - a) < 1e-3 && std::abs(l.b - b) < 1e-3;
  };
  CHECK(near(golden::lab(1, 1, 1), 100, 0, 0) && near(golden::lab(0, 0, 0), 0, 0, 0));
  CHECK(near(golden::lab(1, 0, 0), 53.2408, 80.0925, 67.2032));
  CHECK(near(golden::lab(0, 0, 1), 32.2970, 79.1875, -107.8602));
  CHECK(golden::delta_e2000(golden::lab(.3, .5, .7), golden::lab(.3, .5, .7)) == 0);
}

// PNGs in: 8 and 16 bits, grey, grey and alpha, RGB, RGBA, palette, as RGBA at 16 bits (8 as v * 257), from files
// spng did not write; B4's base.png (spng's, its levels known); each kind written and read back; no file, and a broken
// one.
TEST(test_golden_png) {
  TempDir tmp;
  const golden::Image ga = read_bytes(kGreyAlpha8, sizeof kGreyAlpha8, tmp.path / "ga8.png");
  CHECK(ga.w == 3 && ga.h == 2 && ga.bits == 8 && ga.channels == 2);
  CHECK(same(ga.at(0, 0), 0, 0, 0, 65535) && same(ga.at(1, 0), 128 * 257, 128 * 257, 128 * 257, 64 * 257) &&
        same(ga.at(2, 0), 65535, 65535, 65535, 0) && same(ga.at(0, 1), 257, 257, 257, 2 * 257) &&
        same(ga.at(1, 1), 254 * 257, 254 * 257, 254 * 257, 253 * 257) && same(ga.at(2, 1), 77 * 257, 77 * 257, 77 * 257, 200 * 257));
  const golden::Image rgb = read_bytes(kRgb8, sizeof kRgb8, tmp.path / "rgb8.png");
  CHECK(rgb.w == 2 && rgb.h == 2 && rgb.bits == 8 && rgb.channels == 3);
  CHECK(same(rgb.at(0, 0), 65535, 0, 0, 65535) && same(rgb.at(1, 0), 0, 65535, 0, 65535) &&
        same(rgb.at(0, 1), 0, 0, 65535, 65535) && same(rgb.at(1, 1), 12 * 257, 34 * 257, 56 * 257, 65535));
  const golden::Image g16 = read_bytes(kGrey16, sizeof kGrey16, tmp.path / "g16.png");
  CHECK(g16.w == 2 && g16.h == 1 && g16.bits == 16 && g16.channels == 1);
  CHECK(same(g16.at(0, 0), 0x0102, 0x0102, 0x0102, 65535) && same(g16.at(1, 0), 0xfeff, 0xfeff, 0xfeff, 65535));
  const golden::Image pal = read_bytes(kPalette8, sizeof kPalette8, tmp.path / "pal8.png");
  CHECK(pal.w == 2 && pal.h == 1 && pal.bits == 8 && pal.channels == 4);
  CHECK(same(pal.at(0, 0), 65535, 0, 0, 65535) && same(pal.at(1, 0), 0, 0, 65535, 128 * 257));

  golden::Image base, top;
  golden::inputs::base_top(base, top);
  if (have_tree("test_golden_png")) {
    const golden::Image b4 = golden::read_png(golden::kRoot / "base.png");
    CHECK(b4.w == 256 && b4.h == 384 && b4.bits == 16 && b4.channels == 4 && b4.px == base.px);
    CHECK(same(b4.at(1, 0), 258, 65278, 258, 65535));  // level(1 / 255) = 2 * 129, as B4 wrote it
  }

  std::mt19937 rng(7);
  for (int channels = 1; channels <= 4; channels++)
    for (const int bits : {8, 16}) {
      golden::Image im = random_image(5, 3, rng);
      im.channels = channels, im.bits = bits;
      const fs::path path = tmp.path / ("c" + std::to_string(channels) + "b" + std::to_string(bits) + ".png");
      golden::write_png(path, im);
      const golden::Image back = golden::read_png(path);
      CHECK(back.w == 5 && back.h == 3 && back.bits == bits && back.channels == channels);
      for (int y = 0; y < 3; y++)
        for (int x = 0; x < 5; x++) {
          uint16_t want[4];
          const uint16_t* p = im.at(x, y);
          const auto level = [&](uint16_t v) { return bits == 16 ? v : uint16_t((v + 128) / 257 * 257); };
          for (int c = 0; c < 3; c++) want[c] = level(p[channels >= 3 ? c : 0]);
          want[3] = channels == 2 || channels == 4 ? level(p[3]) : 65535;
          CHECK(same(back.at(x, y), want[0], want[1], want[2], want[3]));
        }
    }
  CHECK(!golden::read_png(tmp.path / "none.png") && golden::read_png(tmp.path / "none.png").w == 0);
  std::ofstream(tmp.path / "broken.png", std::ios::binary) << "\x89PNG\r\n\x1a\nnot a png at all";
  bool threw = false;
  try {
    golden::read_png(tmp.path / "broken.png");
  } catch (const std::runtime_error&) {
    threw = true;
  }
  CHECK(threw);
  // Turns: clockwise, as Photoshop's Image Rotation.
  const golden::Image t90 = golden::rotate(top, 90), t270 = golden::rotate(top, 270), t180 = golden::rotate(top, 180);
  CHECK(t90.w == 384 && t90.h == 256 && t270.w == 384 && t180.w == 256);
  CHECK(std::equal(t90.at(10, 20), t90.at(10, 20) + 4, top.at(20, 383 - 10)));
  CHECK(std::equal(t270.at(10, 20), t270.at(10, 20) + 4, top.at(255 - 20, 10)));
  CHECK(std::equal(t180.at(10, 20), t180.at(10, 20) + 4, top.at(255 - 10, 383 - 20)));
  CHECK(golden::rotate(golden::rotate(t90, 180), 90).px == top.px);
}

// compare(): nothing between an image and itself; a sample a few levels off, its ΔE2000 and levels where it is;
// alpha put over black and white, the larger counting (alpha alone, black and white shown on one only); an area; the
// mean and p99 of known differences; sizes that differ; the diff image PB_GOLDEN_DIFF asks for.
TEST(test_golden_compare) {
  std::mt19937 rng(11);
  golden::Image a = random_image(8, 8, rng);
  for (size_t i = 3; i < a.px.size(); i += 4) a.px[i] = 65535;
  golden::Diff d = golden::compare(a, a);
  CHECK(d.n == 64 && d.mean == 0 && d.p99 == 0 && d.max == 0 && d.levels == 0 && d.over1 == 0 && !d.alpha);

  a.at(5, 6)[0] = a.at(5, 6)[1] = a.at(5, 6)[2] = 30000;
  golden::Image b = a;
  b.at(5, 6)[1] += 3 * 257;  // green 3 levels of 8 bits up
  const auto lab = [](const uint16_t* p) { return golden::lab(p[0] / 65535., p[1] / 65535., p[2] / 65535.); };
  const double one = golden::delta_e2000(lab(a.at(5, 6)), lab(b.at(5, 6)));
  d = golden::compare(a, b);
  CHECK(one > .5 && std::abs(d.max - one) < 1e-12 && d.x == 5 && d.y == 6 && std::abs(d.mean - one / 64) < 1e-12);
  CHECK(std::abs(d.levels - 3) < 1e-9 && d.lx == 5 && d.ly == 6 && d.lc == 1 && d.over1 == 1. / 64 && d.p99 == 0);
  CHECK(golden::compare(a, b, "", {0, 0, 4, 4}).max == 0);
  d = golden::compare(a, b, "", {4, 4, 4, 4});
  CHECK(d.n == 16 && d.x == 5 && d.y == 6 && std::abs(d.max - one) < 1e-12);

  // Alpha: over black and over white, from the composites in doubles.
  const auto over = [](double c, double al, double bg) { return c * al + bg * (1 - al); };
  const auto pair = [&](const uint16_t* p, const uint16_t* q, double bg) {
    const double pa = p[3] / 65535., qa = q[3] / 65535.;
    return golden::delta_e2000(golden::lab(over(p[0] / 65535., pa, bg), over(p[1] / 65535., pa, bg), over(p[2] / 65535., pa, bg)),
                               golden::lab(over(q[0] / 65535., qa, bg), over(q[1] / 65535., qa, bg), over(q[2] / 65535., qa, bg)));
  };
  struct Case {
    uint16_t p[4], q[4];
    int shows;  // 0 over black only, 1 over white only, 2 both
  };
  const Case cases[] = {{{65535, 0, 0, 32768}, {0, 0, 65535, 32768}, 2},
                        {{0, 0, 0, 65535}, {0, 0, 0, 32768}, 1},
                        {{65535, 65535, 65535, 65535}, {65535, 65535, 65535, 32768}, 0}};
  for (const Case& c : cases) {
    golden::Image p(1, 1), q(1, 1);
    std::copy_n(c.p, 4, p.at(0, 0)), std::copy_n(c.q, 4, q.at(0, 0));
    d = golden::compare(p, q);
    const double black = pair(c.p, c.q, 0), white = pair(c.p, c.q, 1);
    CHECK(d.alpha && (c.shows == 0 ? white == 0 && black > 1 : c.shows == 1 ? black == 0 && white > 1 : black > 1 && white > 1));
    CHECK(std::abs(d.max - std::max(black, white)) < 2e-3);
    double levels = 0;  // of a sample over either
    for (int ch = 0; ch < 3; ch++)
      for (const double bg : {0., 1.})
        levels = std::max(levels, std::abs(over(c.p[ch] / 65535., c.p[3] / 65535., bg) - over(c.q[ch] / 65535., c.q[3] / 65535., bg)) * 255);
    CHECK(std::abs(d.levels - levels) < 1e-9);
  }

  // 100 pixels, pixel i off by 97 i in blue: the mean and the 99th of their ΔE2000.
  golden::Image u(10, 10), v(10, 10);
  std::vector<double> want;
  for (int i = 0; i < 100; i++) {
    uint16_t* p = u.at(i % 10, i / 10);
    uint16_t* q = v.at(i % 10, i / 10);
    p[0] = q[0] = uint16_t(500 * i), p[1] = q[1] = uint16_t(30000), p[2] = uint16_t(2000), q[2] = uint16_t(2000 + 97 * i);
    p[3] = q[3] = 65535;
    want.push_back(golden::delta_e2000(lab(p), lab(q)));
  }
  d = golden::compare(u, v);
  double sum = 0;
  for (const double e : want) sum += e;
  const int top = int(std::max_element(want.begin(), want.end()) - want.begin());
  std::sort(want.begin(), want.end());
  CHECK(std::abs(d.mean - sum / 100) < 1e-12 && d.p99 == want[98] && d.max == want[99] && d.x == top % 10 && d.y == top / 10);

  bool threw = false;
  try {
    golden::compare(u, golden::Image(10, 11));
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  CHECK(threw);

  TempDir tmp;
  set_env("PB_GOLDEN_DIFF", tmp.path.string().c_str());
  golden::compare(a, b, "sub/one");
  set_env("PB_GOLDEN_DIFF", nullptr);
  const golden::Image diff = golden::read_png(tmp.path / "sub" / "one.png");
  CHECK(diff.w == 24 && diff.h == 8 && diff.bits == 8 && diff.channels == 3);
  CHECK(same(diff.at(16, 0), 0, 0, 0, 65535) && !same(diff.at(16 + 5, 6), 0, 0, 0, 65535));  // ΔE in its band
  CHECK(std::equal(diff.at(5, 6), diff.at(5, 6) + 3, std::vector<uint16_t>{uint16_t((a.at(5, 6)[0] + 128) / 257 * 257),
                                                                           uint16_t((a.at(5, 6)[1] + 128) / 257 * 257),
                                                                           uint16_t((a.at(5, 6)[2] + 128) / 257 * 257)}.begin()));
  golden::compare(a, b, "sub/two");  // unset: nothing written
  CHECK(!fs::exists(tmp.path / "sub" / "two.png"));
}

// Images to tiles and back: RGBA at 16 and 8 bits, grey and alpha, one channel from alpha (a mask) or from R.
TEST(test_golden_grid) {
  std::mt19937 rng(3);
  const golden::Image im = random_image(300, 20, rng);  // across two tiles
  golden::Image back = golden::image(*golden::grid(im, {Depth::U16, 4}), 300, 20);
  CHECK(back.channels == 4 && back.bits == 16 && back.px == im.px);
  back = golden::image(*golden::grid(im, {Depth::U8, 4}), 300, 20);
  CHECK(back.bits == 8);
  for (size_t i = 0; i < im.px.size(); i++) CHECK(back.px[i] == (im.px[i] + 128) / 257 * 257);
  back = golden::image(*golden::grid(im, {Depth::U16, 2}), 300, 20);
  for (int x = 0; x < 300; x += 7) CHECK(same(back.at(x, 5), im.at(x, 5)[0], im.at(x, 5)[0], im.at(x, 5)[0], im.at(x, 5)[3]));
  back = golden::image(*golden::grid(im, {Depth::U16, 1}), 300, 20);  // a mask: the alpha
  for (int x = 0; x < 300; x += 7) CHECK(same(back.at(x, 5), im.at(x, 5)[3], im.at(x, 5)[3], im.at(x, 5)[3], 65535));
  golden::Image opaque = im;
  opaque.channels = 3;
  back = golden::image(*golden::grid(opaque, {Depth::U16, 1}), 300, 20);  // no alpha: R
  for (int x = 0; x < 300; x += 7) CHECK(back.at(x, 5)[0] == im.at(x, 5)[0]);
}

// A set: index.json as blend.jsx writes it (lines of its own, taken from what jscheck printed of its cases, inputs
// pointed at p1/in/), its entries, scenes (kinds, props photo-beauty reads with A30's keys, inputs turned, masks,
// groups), images there and not, problems; a broken index's problems; a folder with no index.
TEST(test_golden_index) {
  if (!have_tree("test_golden_index")) return;
  TempDir tmp;
  const std::string head = "{\"script\": \"blend.jsx\", \"photoshop\": \"26.0.0\", \"complete\": true, \"inputs\": " +
                           nlohmann::json(kIn.generic_string()).dump() + ", \"failed\": [],\n\"images\": [\n";
  const char* const lines =
      R"({"name": "normal-50", "canvas": [256, 384], "bits": 8, "layers": [{"kind": "pixel", "src": "base.png", "props": {"version": 1, "name": "base"}}, {"kind": "pixel", "src": "top.png", "props": {"version": 1, "name": "top", "blend": "normal", "opacity": 0.5}}]},
{"name": "alpha-hue", "canvas": [256, 256], "bits": 8, "layers": [{"kind": "pixel", "src": "alpha.png", "props": {"version": 1, "name": "under"}}, {"kind": "pixel", "src": "alpha.png", "rotate": 90, "props": {"version": 1, "name": "over", "blend": "hue"}}]},
{"name": "group-passthrough-50-mask", "canvas": [256, 256], "bits": 8, "layers": [{"kind": "pixel", "src": "ramp.png", "props": {"version": 1, "name": "ramp"}}, {"kind": "group", "mask": "shapes.png", "props": {"version": 1, "name": "group", "blend": "passThrough", "opacity": 0.5}, "children": [{"kind": "pixel", "src": "shapes.png", "props": {"version": 1, "name": "shapes", "blend": "overlay"}}, {"kind": "pixel", "src": "alpha.png", "props": {"version": 1, "name": "alpha", "blend": "multiply"}}]}]},
{"name": "mask-density50", "canvas": [256, 256], "bits": 8, "layers": [{"kind": "pixel", "src": "ramp.png", "props": {"version": 1, "name": "ramp"}}, {"kind": "pixel", "src": "alpha.png", "rotate": 90, "mask": "shapes.png", "props": {"version": 1, "name": "masked", "maskDensity": 0.5}}]},
{"name": "clip-multiply-mask", "canvas": [256, 256], "bits": 8, "layers": [{"kind": "pixel", "src": "ramp.png", "props": {"version": 1, "name": "ramp"}}, {"kind": "pixel", "src": "shapes.png", "props": {"version": 1, "name": "base"}}, {"kind": "pixel", "src": "alpha.png", "rotate": 90, "mask": "alpha.png", "props": {"version": 1, "name": "clipped", "blend": "multiply", "clip": true}}]},
{"name": "blendif-mixed", "canvas": [256, 384], "bits": 8, "layers": [{"kind": "pixel", "src": "base.png", "props": {"version": 1, "name": "base"}}, {"kind": "pixel", "src": "top.png", "props": {"version": 1, "name": "top", "blendIf": {"gray": {"layer": [0, 0, 128, 255], "under": [32, 96, 255, 255]}, "red": {"layer": [0, 0, 255, 255], "under": [0, 0, 200, 200]}}}}]},
{"name": "channels-gb-off-multiply", "canvas": [256, 384], "bits": 8, "layers": [{"kind": "pixel", "src": "base.png", "props": {"version": 1, "name": "base"}}, {"kind": "pixel", "src": "top.png", "props": {"version": 1, "name": "top", "blend": "multiply", "channels": {"g": false, "b": false}}}]},
{"name": "invert-clip", "canvas": [256, 256], "bits": 8, "layers": [{"kind": "pixel", "src": "ramp.png", "props": {"version": 1, "name": "ramp"}}, {"kind": "pixel", "src": "shapes.png", "props": {"version": 1, "name": "shapes"}}, {"kind": "adj", "props": {"version": 1, "name": "invert", "clip": true, "adj": "Invert"}}]},
{"name": "in-cube", "canvas": [190, 190], "bits": 16, "layers": [{"kind": "pixel", "src": "cube.png", "props": {"version": 1, "name": "cube"}}]}
)";
  std::ofstream(tmp.path / "index.json") << head << lines << "]}\n";
  golden::Image base, top;
  golden::inputs::base_top(base, top);
  golden::write_png(tmp.path / "normal-50.png", base);  // one image there, any

  golden::Set set(tmp.path);
  CHECK(set.has_index() && set.ready("test_golden_index") && set.entries().size() == 9);
  CHECK(set.index().value("photoshop", "") == "26.0.0" && set.find("alpha-hue") && !set.find("nope"));
  const auto props = [](const golden::SceneLayer& l) { return *props_from_json(l.props); };

  auto layers = set.layers(*set.find("normal-50"));
  CHECK(layers.size() == 2 && layers[0].kind == "pixel" && layers[1].kind == "pixel");
  CHECK(props(layers[0]).name == "base" && props(layers[1]).blend == Blend::Normal && props(layers[1]).opacity == .5f);
  CHECK(layers[0].pixels && layers[0].pixels->px == base.px && layers[1].pixels->px == top.px && !layers[1].mask);

  const golden::Image alpha = golden::read_png(kIn / "alpha.png");
  layers = set.layers(*set.find("alpha-hue"));
  CHECK(layers[1].pixels->px == golden::rotate(alpha, 90).px && props(layers[1]).blend == Blend::Hue);
  CHECK(layers[1].pixels->at(10, 20)[3] == golden::ps_level(20 / 255.));  // turned: alpha runs down

  layers = set.layers(*set.find("group-passthrough-50-mask"));
  CHECK(layers.size() == 2 && layers[1].kind == "group" && layers[1].children.size() == 2 && !layers[1].pixels);
  CHECK(props(layers[1]).blend == Blend::PassThrough && props(layers[1]).opacity == .5f);
  CHECK(layers[1].mask && layers[1].mask->px == golden::read_png(kIn / "shapes.png").px);
  CHECK(props(layers[1].children[0]).blend == Blend::Overlay && props(layers[1].children[1]).blend == Blend::Multiply);

  layers = set.layers(*set.find("mask-density50"));
  CHECK(layers[1].mask && layers[1].json->at("props").at("maskDensity") == .5);

  layers = set.layers(*set.find("clip-multiply-mask"));
  CHECK(layers.size() == 3 && props(layers[2]).blend == Blend::Multiply && layers[2].mask->px == alpha.px);
  CHECK(layers[2].json->at("props").at("clip") == true);  // A30's key: photo-beauty reads it from then on

  layers = set.layers(*set.find("blendif-mixed"));  // A30's keys: kBlendIfNames, both sliders of a channel
  const nlohmann::json& blend_if = layers[1].json->at("props").at("blendIf");
  CHECK(blend_if.at("gray").at("layer") == nlohmann::json({0, 0, 128, 255}) && blend_if.size() == 2);
  CHECK(blend_if.at("red").at("layer") == nlohmann::json({0, 0, 255, 255}) &&
        blend_if.at("red").at("under") == nlohmann::json({0, 0, 200, 200}));

  layers = set.layers(*set.find("channels-gb-off-multiply"));  // the boxes off only
  CHECK(layers[1].json->at("props").at("channels") == nlohmann::json({{"g", false}, {"b", false}}));

  layers = set.layers(*set.find("invert-clip"));
  CHECK(layers.size() == 3 && layers[2].kind == "adj" && !layers[2].pixels && !layers[2].mask);
  CHECK(layers[2].json->at("props").at("adj") == "Invert");

  layers = set.layers(*set.find("in-cube"));
  CHECK(layers.size() == 1 && layers[0].pixels->w == 190 && layers[0].pixels->h == 190);

  CHECK(set.image(*set.find("normal-50")) && !set.image(*set.find("alpha-hue")));
  set.skipped("test_golden_index");  // "skipped 1 of 9"
  auto problems = set.problems();
  CHECK(problems.size() == 8);  // every image but normal-50.png missing, the scenes all read

  // A broken index: a kind photo-beauty has not, an input not there, props it does not read, a group without children.
  const fs::path bad = tmp.path / "bad";
  fs::create_directories(bad);
  std::ofstream(bad / "index.json") << "{\"inputs\": " << nlohmann::json(kIn.generic_string()).dump() << ", \"images\": ["
      R"({"name": "kind", "layers": [{"kind": "wat", "src": "base.png"}]},
         {"name": "input", "layers": [{"src": "nope.png"}]},
         {"name": "props", "layers": [{"src": "base.png", "props": {"opacity": "half"}}]},
         {"name": "group", "layers": [{"kind": "group"}]},
         {"name": "src", "layers": [{"kind": "pixel"}]}]})";
  golden::Set broken(bad);
  problems = broken.problems();
  for (const auto& p : problems) std::printf("  %s\n", p.c_str());
  CHECK(problems.size() == 10);  // 5 images missing, 5 scenes

  golden::Set none(tmp.path / "none");
  CHECK(!none.has_index() && !none.ready("test_golden_index") && none.entries().empty());
  bool threw = false;
  std::ofstream(tmp.path / "index.json") << "{not json";
  try {
    golden::Set garbled(tmp.path);
  } catch (const std::runtime_error&) {
    threw = true;
  }
  CHECK(threw);
}

// pb-bench ps-inputs wrote every input of golden::inputs into tests/golden/ps/p1/in/: there, at 16 bits, the same
// samples, every one a level Photoshop reads back exactly; written and read again the same; B4's base.png and top.png
// the same bytes as tests/golden/ps/'s; and they hold what golden.h says they do.
TEST(test_golden_inputs) {
  if (!have_tree("test_golden_inputs")) return;
  const auto files = golden::inputs::all(kPhoto);
  CHECK(files.size() == 8);
  TempDir tmp;
  for (const auto& f : files) {
    const golden::Image got = golden::read_png(kIn / f.name);
    if (!got || got.px != f.image.px)
      std::printf("tests/golden/ps/p1/in/%s %s: run pb-bench ps-inputs tests/golden/ps\n", f.name.c_str(),
                  got ? "is not as golden::inputs makes it" : "is not there");
    CHECK(got && got.bits == 16 && got.channels == f.image.channels && got.w == f.image.w && got.h == f.image.h);
    CHECK(got.px == f.image.px);
    for (const uint16_t s : got.px) CHECK(golden::is_ps_level(s));
    golden::write_png(tmp.path / f.name, f.image);
    CHECK(golden::read_png(tmp.path / f.name).px == f.image.px);
  }
  CHECK(bytes(golden::kRoot / "base.png") == bytes(kIn / "base.png") && bytes(golden::kRoot / "top.png") == bytes(kIn / "top.png"));

  const auto file = [&](const char* name) -> const golden::Image& {
    for (const auto& f : files)
      if (f.name == name) return f.image;
    CHECK(false);
    return files[0].image;
  };
  using golden::ps_level;
  namespace in = golden::inputs;
  const golden::Image& cube = file("cube.png");
  const int i = in::cube_index(32, 0, 16);
  CHECK(same(cube.at(i % in::kCubeSide, i / in::kCubeSide), 65535, 0, ps_level(.5), 65535));
  CHECK(same(cube.at(in::kCubeSide - 1, in::kCubeSide - 1), 0, 0, 0, 65535));  // past the lattice
  const golden::Image& ramp = file("ramp.png");
  CHECK(same(ramp.at(255, 0), 65535, 65535, 65535, 65535) && same(ramp.at(255, 2 * in::kRampRows), 0, 65535, 0, 65535));
  CHECK(same(ramp.at(0, 4 * in::kRampRows), 65535, 0, 0, 65535) && same(ramp.at(0, 5 * in::kRampRows), 32768, 0, 0, 65535));
  const golden::Image& impulse = file("impulse.png");
  int white = 0;
  for (size_t p = 0; p < impulse.px.size(); p += 4) white += impulse.px[p] == 65535;
  CHECK(white == 64 && impulse.at(16, 16)[0] == 65535 && impulse.at(48, 240)[0] == 65535 && impulse.at(0, 200)[0] == 32768);
  const golden::Image& shapes = file("shapes.png");
  CHECK(shapes.at(0, 0)[3] == 0 && shapes.at(64, 64)[3] == 65535 && shapes.at(150, 136)[3] == 65535);
  CHECK(shapes.at(64, 192)[3] == 0 && shapes.at(64 + 36, 192)[3] == 65535);  // the ring's hole and its band
  const uint16_t edge = shapes.at(97, 97)[3], soft = shapes.at(192 + 12, 216)[3];  // the disc's edge at 45 degrees
  CHECK(edge > 0 && edge < 65535 && soft > 30000 && soft < 45000);  // part covered; a Gaussian at 1 sigma
  const golden::Image& alpha = file("alpha.png");
  CHECK(alpha.at(0, 9)[3] == 0 && alpha.at(255, 9)[3] == 65535 && alpha.at(100, 9)[3] == ps_level(100 / 255.));
  const golden::Image& photo = file("photo.png");
  CHECK(photo.w == 256 && photo.channels == 3);
}

// Photoshop's sets, once B33 has brought them: each p1 set whole (every image index.json lists, every scene readable);
// each input as Photoshop read it (blend.jsx's in-*) holding the file's levels; tests/golden/ps/p1/ within 30 MB.
TEST(test_golden_ps) {
  if (!have_tree("test_golden_ps")) return;
  bool any = false;
  for (const char* name : {"blend", "adjust", "gradient", "filters", "styles"}) {
    golden::Set set(std::string("p1/") + name);
    if (!set.has_index()) continue;
    any = true;
    const auto problems = set.problems();
    for (const auto& p : problems) std::printf("tests/golden/ps/p1/%s: %s\n", name, p.c_str());
    CHECK(problems.empty());
  }
  golden::Set blend("p1/blend");
  if (blend.ready("test_golden_ps")) {
    for (const auto& f : golden::inputs::all(kPhoto)) {
      const std::string stem = f.name.substr(0, f.name.size() - 4);
      const golden::Entry* e = blend.find("in-" + stem);
      const golden::Image ps = e ? blend.image(*e) : golden::Image();
      if (!ps) continue;
      CHECK(ps.w == f.image.w && ps.h == f.image.h && ps.bits == 16);
      size_t off = 0;
      for (size_t p = 0; p < ps.px.size(); p += 4) {
        const uint16_t *a = &f.image.px[p], *b = &ps.px[p];
        bool ok = golden::ps15(a[3]) == golden::ps15(b[3]);
        for (int c = 0; c < 3 && ok && a[3]; c++)  // opaque: the same level; else as shown, within one
          ok = a[3] == 65535 ? golden::ps15(a[c]) == golden::ps15(b[c])
                             : std::abs(double(golden::ps15(a[c])) * a[3] - double(golden::ps15(b[c])) * b[3]) <= 65535.;
        off += !ok;
      }
      std::printf("ps golden in-%s: %zu pixels not as in the file\n", stem.c_str(), off);
      CHECK(off == 0);
    }
    blend.skipped("test_golden_ps");
  }
  uintmax_t size = 0;
  const fs::path p1 = golden::kRoot / "p1";
  for (const auto& f : fs::recursive_directory_iterator(p1))
    if (f.is_regular_file()) size += f.file_size();
  std::printf("tests/golden/ps/p1: %.1f MB%s\n", double(size) / (1 << 20), any ? "" : " (inputs only: no sets yet)");
  CHECK(size <= 30u << 20);
}
