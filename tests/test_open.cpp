// Opening JPEG, PNG and TIFF (open.h), from files written here by TurboJPEG, libspng and libtiff:
// every sample as stored, at sizes of part tiles, in each layout the decoders take;
// ICC profiles byte for byte; empty and solid tiles; what it refuses; sizes past the limits from the header; files cut
// short or with a byte flipped: an error or (flips) some valid document, never a crash (ASan's run).
#include <lcms2.h>
#include <spng.h>
#include <tiffio.h>
#include <turbojpeg.h>
#include <zlib.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <random>
#include <string>
#include <system_error>
#include <vector>

#include "check.h"
#include "color.h"
#include "open.h"

using namespace pb;
namespace fs = std::filesystem;
using Why = DecodeError::Kind;

namespace {

struct TempDir {
  fs::path path = fs::temp_directory_path() / ("pb-test-open-" + std::to_string(std::random_device{}()));
  TempDir() { fs::create_directories(path); }
  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
  fs::path operator/(const char* name) const { return path / name; }
};

void put(const fs::path& p, const std::vector<uint8_t>& bytes) {
  std::ofstream(p, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
}
std::vector<uint8_t> get(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
}

// n samples of a seed, each 0..top.
template <class T>
std::vector<T> noise(size_t n, unsigned seed, T top = T(~T(0))) {
  std::mt19937 rng(seed);
  std::vector<T> v(n);
  for (T& s : v) s = T(rng() % (uint32_t(top) + 1));
  return v;
}
// Samples `channels` a pixel (1 to 4) as a document holds them: grey or RGB, then alpha, `top` where there is none.
template <class T>
std::vector<T> with_alpha(const std::vector<T>& px, int channels, T top = T(~T(0))) {
  if (channels == 2 || channels == 4) return px;
  std::vector<T> out;
  for (size_t i = 0; i < px.size(); i += size_t(channels)) {
    out.insert(out.end(), px.begin() + std::ptrdiff_t(i), px.begin() + std::ptrdiff_t(i) + channels);
    out.push_back(top);
  }
  return out;
}

// The Background layer's bytes over the canvas, and its samples (T as its depth).
std::vector<uint8_t> raw(const DocState& d) {
  const size_t row = size_t(d.width) * size_t(pixel_format(d).bytes());
  std::vector<uint8_t> out(row * size_t(d.height));
  find_layer(d, d.selected.at(0))->pixels->read_rect(0, 0, d.width, d.height, out.data(), row);
  return out;
}
template <class T>
std::vector<T> pixels(const DocState& d) {
  CHECK((sizeof(T) == 2) == (d.depth == Depth::U16));
  const std::vector<uint8_t> b = raw(d);
  std::vector<T> out(b.size() / sizeof(T));
  std::memcpy(out.data(), b.data(), b.size());
  return out;
}

// What every opened document is: its size, mode, depth and profile; one visible Normal pixel layer "Background" at
// full opacity, selected; a state validate() takes.
void check_doc(const DocPtr& d, int w, int h, Mode mode, Depth depth, const std::vector<uint8_t>& icc) {
  CHECK(d && d->width == w && d->height == h && d->mode == mode && d->depth == depth);
  CHECK(d->icc && *d->icc == icc);
  CHECK(d->root->children.size() == 1 && d->selected.size() == 1);
  const Layer& l = *d->root->children[0];
  CHECK(l.kind == Kind::Pixel && l.props.name == "Background" && d->selected[0] == l.id && l.props.visible);
  CHECK(l.props.opacity == 1 && l.props.fill == 1 && l.props.blend == Blend::Normal);
  validate(*d);
}

// Whether opening throws a DecodeError of this kind naming the file, its text having `what` and its codec `codec`.
bool fails(const fs::path& p, Why kind, const char* what, const char* codec = "") {
  try {
    open_image(p);
  } catch (const DecodeError& e) {
    if (e.kind == kind && std::strstr(e.what(), what) && e.codec == codec &&
        std::strstr(e.what(), p.filename().string().c_str()))
      return true;
    std::printf("unexpected error: %s (kind %d, codec %s)\n", e.what(), int(e.kind), e.codec.c_str());
  }
  return false;
}

// ---- Writers ----

// A JPEG of pixels `px` (TurboJPEG pixel format pf), quality 95, with an ICC profile when given; progressive when
// asked; 12-bit samples (shorts) at precision 12; colorspace TurboJPEG's choice unless given (TJCS_CMYK).
std::vector<uint8_t> jpeg_of(int w, int h, int pf, const void* px, int samp, const std::vector<uint8_t>& icc = {},
                             bool progressive = false, int colorspace = -1, int precision = 8) {
  tjhandle t = tj3Init(TJINIT_COMPRESS);
  CHECK(t && !tj3Set(t, TJPARAM_QUALITY, 95) && !tj3Set(t, TJPARAM_SUBSAMP, samp));
  CHECK(!tj3Set(t, TJPARAM_PROGRESSIVE, progressive));
  if (colorspace >= 0) CHECK(!tj3Set(t, TJPARAM_COLORSPACE, colorspace));
  if (!icc.empty()) CHECK(!tj3SetICCProfile(t, const_cast<uint8_t*>(icc.data()), icc.size()));
  unsigned char* buf = nullptr;
  size_t size = 0;
  const int rc = precision == 12 ? tj3Compress12(t, static_cast<const short*>(px), w, 0, h, pf, &buf, &size)
                                 : tj3Compress8(t, static_cast<const unsigned char*>(px), w, 0, h, pf, &buf, &size);
  CHECK(rc == 0);
  std::vector<uint8_t> out(buf, buf + size);
  tj3Free(buf);
  tj3Destroy(t);
  return out;
}

// What TurboJPEG decodes of a JPEG in pixel format pf: the samples a decode must give.
std::vector<uint8_t> jpeg_levels(const std::vector<uint8_t>& jpeg, int pf) {
  tjhandle t = tj3Init(TJINIT_DECOMPRESS);
  CHECK(t && !tj3DecompressHeader(t, jpeg.data(), jpeg.size()));
  std::vector<uint8_t> px(size_t(tj3Get(t, TJPARAM_JPEGWIDTH)) * size_t(tj3Get(t, TJPARAM_JPEGHEIGHT)) * size_t(tjPixelSize[pf]));
  CHECK(!tj3Decompress8(t, jpeg.data(), jpeg.size(), px.data(), 0, pf));
  tj3Destroy(t);
  return px;
}

// A PNG of samples `px` as libspng takes them (16 bits in the host's byte order), with an ICC profile, a palette and
// tRNS when given; Adam7 when interlaced.
std::vector<uint8_t> png_of(uint32_t w, uint32_t h, uint8_t type, uint8_t bits, const void* px, size_t bytes,
                            const std::vector<uint8_t>& icc = {}, bool interlaced = false,
                            const std::vector<spng_plte_entry>& palette = {}, const spng_trns* trns = nullptr) {
  const std::unique_ptr<spng_ctx, void (*)(spng_ctx*)> ctx(spng_ctx_new(SPNG_CTX_ENCODER), spng_ctx_free);
  spng_ctx* s = ctx.get();
  CHECK(s && !spng_set_option(s, SPNG_ENCODE_TO_BUFFER, 1));
  spng_ihdr ihdr{w, h, bits, type, 0, 0, uint8_t(interlaced ? 1 : 0)};
  CHECK(!spng_set_ihdr(s, &ihdr));
  std::vector<char> profile(icc.begin(), icc.end());
  spng_iccp iccp{"ICC profile", profile.size(), profile.data()};
  if (!icc.empty()) CHECK(!spng_set_iccp(s, &iccp));
  if (!palette.empty()) {
    spng_plte plte{uint32_t(palette.size()), {}};
    std::copy(palette.begin(), palette.end(), plte.entries);
    CHECK(!spng_set_plte(s, &plte));
  }
  if (trns) {
    spng_trns copy = *trns;
    CHECK(!spng_set_trns(s, &copy));
  }
  CHECK(!spng_encode_image(s, px, bytes, SPNG_FMT_PNG, SPNG_ENCODE_FINALIZE));
  size_t n = 0;
  int err = 0;
  const std::unique_ptr<void, void (*)(void*)> buf(spng_get_png_buffer(s, &n, &err), std::free);
  CHECK(buf && !err);
  return std::vector<uint8_t>(static_cast<uint8_t*>(buf.get()), static_cast<uint8_t*>(buf.get()) + n);
}

// How tiff_of() lays a TIFF out.
struct TiffSpec {
  uint16_t spp = 3, bits = 8, photo = PHOTOMETRIC_RGB, compression = COMPRESSION_NONE;
  uint32_t tile = 0;  // a tile's side (a multiple of 16); 0: strips
  uint32_t rows = 7;  // a strip's
  bool apart = false;  // planes
  int extra = -1;  // the last sample's ExtraSamples kind, when spp has one past the colours
  uint16_t format = SAMPLEFORMAT_UINT;
  std::vector<uint8_t> icc;
};

// A TIFF written by libtiff of samples `px` (contiguous, spp a pixel, host byte order past 8 bits) as `s` says.
void tiff_of(const fs::path& path, uint32_t w, uint32_t h, const TiffSpec& s, const void* px) {
  TIFF* t = TIFFOpen(path.string().c_str(), "w");
  CHECK(t);
  TIFFSetField(t, TIFFTAG_IMAGEWIDTH, w), TIFFSetField(t, TIFFTAG_IMAGELENGTH, h);
  TIFFSetField(t, TIFFTAG_SAMPLESPERPIXEL, s.spp), TIFFSetField(t, TIFFTAG_BITSPERSAMPLE, s.bits);
  TIFFSetField(t, TIFFTAG_PHOTOMETRIC, s.photo), TIFFSetField(t, TIFFTAG_COMPRESSION, s.compression);
  TIFFSetField(t, TIFFTAG_SAMPLEFORMAT, s.format);
  TIFFSetField(t, TIFFTAG_PLANARCONFIG, s.apart ? PLANARCONFIG_SEPARATE : PLANARCONFIG_CONTIG);
  const uint16_t kind = uint16_t(s.extra);
  if (s.extra >= 0) TIFFSetField(t, TIFFTAG_EXTRASAMPLES, 1, &kind);
  if (!s.icc.empty()) TIFFSetField(t, TIFFTAG_ICCPROFILE, uint32_t(s.icc.size()), s.icc.data());
  if (s.compression == COMPRESSION_JPEG) TIFFSetField(t, TIFFTAG_JPEGQUALITY, 95), TIFFSetField(t, TIFFTAG_JPEGCOLORMODE, JPEGCOLORMODE_RGB);
  std::vector<uint16_t> map;  // a palette's: grey ramps
  if (s.photo == PHOTOMETRIC_PALETTE) {
    map.resize(size_t(3) << s.bits);
    for (size_t i = 0; i < map.size(); i++) map[i] = uint16_t(i % (size_t(1) << s.bits) * 257);
    TIFFSetField(t, TIFFTAG_COLORMAP, map.data(), map.data() + (1 << s.bits), map.data() + (2 << s.bits));
  }
  const size_t bytes = s.bits / 8;
  const uint32_t bw = s.tile ? s.tile : w, bh = s.tile ? s.tile : s.rows;
  if (s.tile)
    TIFFSetField(t, TIFFTAG_TILEWIDTH, bw), TIFFSetField(t, TIFFTAG_TILELENGTH, bh);
  else
    TIFFSetField(t, TIFFTAG_ROWSPERSTRIP, bh);
  const uint16_t planes = s.apart ? s.spp : 1, per = s.apart ? 1 : s.spp;
  std::vector<uint8_t> block(size_t(bw) * bh * per * bytes);
  for (uint16_t p = 0; p < planes; p++)
    for (uint32_t y0 = 0; y0 < h; y0 += bh)
      for (uint32_t x0 = 0; x0 < w; x0 += bw) {
        std::fill(block.begin(), block.end(), 0);
        for (uint32_t y = y0; y < std::min(h, y0 + bh); y++)
          for (uint32_t x = x0; x < std::min(w, x0 + bw); x++)
            for (uint16_t c = 0; c < per; c++)
              std::memcpy(&block[((size_t(y - y0) * bw + (x - x0)) * per + c) * bytes],
                          static_cast<const uint8_t*>(px) + ((size_t(y) * w + x) * s.spp + (s.apart ? p : c)) * bytes, bytes);
        const tmsize_t n = s.tile ? tmsize_t(block.size()) : tmsize_t(size_t(std::min(bh, h - y0)) * w * per * bytes);
        CHECK((s.tile ? TIFFWriteEncodedTile(t, TIFFComputeTile(t, x0, y0, 0, p), block.data(), n)
                      : TIFFWriteEncodedStrip(t, TIFFComputeStrip(t, y0, p), block.data(), n)) >= 0);
      }
  TIFFClose(t);
}

std::vector<uint8_t> saved(cmsHPROFILE p) {
  cmsUInt32Number n = 0;
  CHECK(cmsSaveProfileToMem(p, nullptr, &n));
  std::vector<uint8_t> out(n);
  CHECK(cmsSaveProfileToMem(p, out.data(), &n));
  cmsCloseProfile(p);
  return out;
}
// A grey profile, gamma 2.2.
std::vector<uint8_t> grey_profile() {
  cmsToneCurve* g = cmsBuildGamma(nullptr, 2.2);
  std::vector<uint8_t> out = saved(cmsCreateGrayProfile(cmsD50_xyY(), g));
  cmsFreeToneCurve(g);
  return out;
}
// Adobe RGB with curves of 30,000 points (about 180 KB): a JPEG carries it in three APP2 chunks.
std::vector<uint8_t> big_profile() {
  const std::vector<uint8_t>& adobe = icc_profile(ColorSpace::AdobeRgb);
  cmsHPROFILE h = cmsOpenProfileFromMem(adobe.data(), cmsUInt32Number(adobe.size()));
  std::vector<cmsUInt16Number> v(30000);
  for (size_t i = 0; i < v.size(); i++) v[i] = cmsUInt16Number(std::lround(std::pow(double(i) / 29999, 563 / 256.) * 65535));
  cmsToneCurve* c = cmsBuildTabulatedToneCurve16(nullptr, cmsUInt32Number(v.size()), v.data());
  for (cmsTagSignature s : {cmsSigRedTRCTag, cmsSigGreenTRCTag, cmsSigBlueTRCTag}) CHECK(cmsWriteTag(h, s, c));
  cmsFreeToneCurve(c);
  return saved(h);
}

}  // namespace

// PNG: 16-bit RGBA plain and Adam7 under a Display P3 profile; 16-bit RGB; grey at 8 and 16 bits, with alpha, with
// tRNS, at 2 bits; palettes with and without tRNS. 300 x 270: two tiles across and down, the last ones in part.
TEST(test_open_png) {
  TempDir dir;
  const uint32_t w = 300, h = 270;
  const size_t n = size_t(w) * h;
  const std::vector<uint8_t>& p3 = icc_profile(ColorSpace::DisplayP3), &srgb = icc_profile(ColorSpace::Srgb);
  const auto open = [&](const std::vector<uint8_t>& png) {
    put(dir / "a.png", png);
    return open_image(dir / "a.png");
  };
  const std::vector<uint16_t> rgba = noise<uint16_t>(n * 4, 1);
  for (bool interlaced : {false, true}) {
    const DocPtr d = open(png_of(w, h, SPNG_COLOR_TYPE_TRUECOLOR_ALPHA, 16, rgba.data(), rgba.size() * 2, p3, interlaced));
    check_doc(d, int(w), int(h), Mode::Rgb, Depth::U16, p3);
    CHECK(pixels<uint16_t>(*d) == rgba);
  }
  const std::vector<uint16_t> rgb16 = noise<uint16_t>(n * 3, 2);
  DocPtr d = open(png_of(w, h, SPNG_COLOR_TYPE_TRUECOLOR, 16, rgb16.data(), rgb16.size() * 2));
  check_doc(d, int(w), int(h), Mode::Rgb, Depth::U16, srgb);
  CHECK(pixels<uint16_t>(*d) == with_alpha(rgb16, 3));

  const std::vector<uint8_t> g8 = noise<uint8_t>(n, 3), ga8 = noise<uint8_t>(n * 2, 4);
  d = open(png_of(w, h, SPNG_COLOR_TYPE_GRAYSCALE, 8, g8.data(), g8.size()));
  check_doc(d, int(w), int(h), Mode::Gray, Depth::U8, srgb);
  CHECK(pixels<uint8_t>(*d) == with_alpha(g8, 1));
  d = open(png_of(w, h, SPNG_COLOR_TYPE_GRAYSCALE_ALPHA, 8, ga8.data(), ga8.size()));
  check_doc(d, int(w), int(h), Mode::Gray, Depth::U8, srgb);
  CHECK(pixels<uint8_t>(*d) == ga8);
  const std::vector<uint16_t> ga16 = noise<uint16_t>(n * 2, 5);
  d = open(png_of(w, h, SPNG_COLOR_TYPE_GRAYSCALE_ALPHA, 16, ga16.data(), ga16.size() * 2));
  check_doc(d, int(w), int(h), Mode::Gray, Depth::U16, srgb);
  CHECK(pixels<uint16_t>(*d) == ga16);
  std::vector<uint16_t> g16 = noise<uint16_t>(n, 6), want(n * 2);
  spng_trns key{};
  key.gray = 1234;  // transparent where the sample is it
  for (size_t i = 0; i < n; i += 7) g16[i] = key.gray;
  for (size_t i = 0; i < n; i++) want[i * 2] = g16[i], want[i * 2 + 1] = g16[i] == key.gray ? 0 : 65535;
  d = open(png_of(w, h, SPNG_COLOR_TYPE_GRAYSCALE, 16, g16.data(), g16.size() * 2, {}, false, {}, &key));
  check_doc(d, int(w), int(h), Mode::Gray, Depth::U16, srgb);
  CHECK(pixels<uint16_t>(*d) == want);
  // 2 bits, four pixels a byte from the left (rows padded to whole bytes): levels times 85.
  const uint32_t row = (w * 2 + 7) / 8;
  const std::vector<uint8_t> packed = noise<uint8_t>(size_t(row) * h, 7);
  std::vector<uint8_t> scaled(n * 2);
  for (size_t y = 0; y < h; y++)
    for (size_t x = 0; x < w; x++)
      scaled[(y * w + x) * 2] = uint8_t((packed[y * row + x / 4] >> (6 - 2 * (x % 4)) & 3) * 85), scaled[(y * w + x) * 2 + 1] = 255;
  d = open(png_of(w, h, SPNG_COLOR_TYPE_GRAYSCALE, 2, packed.data(), packed.size()));
  check_doc(d, int(w), int(h), Mode::Gray, Depth::U8, srgb);
  CHECK(pixels<uint8_t>(*d) == scaled);

  // Palettes: RGB at 8 bits, alpha from tRNS (entries past it opaque) or opaque.
  const std::vector<spng_plte_entry> palette = {{255, 0, 0, 0}, {0, 128, 255, 0}, {20, 200, 90, 0}};
  spng_trns alphas{};
  alphas.n_type3_entries = 2, alphas.type3_alpha[0] = 255, alphas.type3_alpha[1] = 100;
  const std::vector<uint8_t> index = noise<uint8_t>(n, 8, 2);
  for (const spng_trns* t : std::initializer_list<const spng_trns*>{&alphas, nullptr}) {
    d = open(png_of(w, h, SPNG_COLOR_TYPE_INDEXED, 8, index.data(), index.size(), p3, false, palette, t));
    check_doc(d, int(w), int(h), Mode::Rgb, Depth::U8, p3);
    std::vector<uint8_t> looked(n * 4);
    for (size_t i = 0; i < n; i++) {
      const spng_plte_entry& e = palette[index[i]];
      looked[i * 4] = e.red, looked[i * 4 + 1] = e.green, looked[i * 4 + 2] = e.blue;
      looked[i * 4 + 3] = t && index[i] < t->n_type3_entries ? t->type3_alpha[index[i]] : 255;
    }
    CHECK(pixels<uint8_t>(*d) == looked);
  }
}

// Rows into tiles: a tile of alpha 0 (whatever its colours) is left empty, one of a single colour is solid, the rest
// data, transparent past the canvas; layers cover the canvas only.
TEST(test_open_tiles) {
  TempDir dir;
  const uint32_t w = 600, h = 300;
  std::vector<uint8_t> px = noise<uint8_t>(size_t(w) * h * 4, 9);
  for (uint32_t y = 0; y < h; y++)
    for (uint32_t x = 0; x < w; x++) {
      uint8_t* p = &px[(size_t(y) * w + x) * 4];
      if (x < 256)
        p[3] = 0;
      else if (x < 512 && y < 256)
        p[0] = 10, p[1] = 20, p[2] = 30, p[3] = 255;
      else
        p[3] = uint8_t(std::max(1, int(p[3])));
    }
  put(dir / "t.png", png_of(w, h, SPNG_COLOR_TYPE_TRUECOLOR_ALPHA, 8, px.data(), px.size()));
  const DocPtr d = open_image(dir / "t.png");
  const TileGrid& g = *find_layer(*d, d->selected[0])->pixels;
  CHECK(g.tiles().size() == 4 && !g.get(0, 0) && !g.get(0, 1));
  CHECK(g.get(1, 0)->is_solid() && g.get(1, 0)->value() == (Pixel{10, 20, 30, 255}));
  CHECK(!g.get(1, 1)->is_solid() && !g.get(2, 0)->is_solid() && !g.get(2, 1)->is_solid());
  for (size_t i = 0; i < size_t(w) * h; i++)
    if (px[i * 4 + 3] == 0) std::fill(&px[i * 4], &px[i * 4] + 4, 0);  // colour under alpha 0 goes with the tile
  CHECK(pixels<uint8_t>(*d) == px);
  uint8_t past[8] = {1, 1, 1, 1, 1, 1, 1, 1};
  g.read_rect(int(w), int(h) - 1, 1, 2, past, 4);
  CHECK(std::all_of(past, past + 8, [](uint8_t v) { return v == 0; }));
}

// TIFF: 16-bit RGBA with planes apart in strips of 7 rows (strips across bands of tile rows), 16 x 16 tiles and
// 512 x 512 ones (past the image, taller than a band), uncompressed, LZW and Deflate, an Adobe RGB profile; planes
// together; RGB without alpha; grey (MinIsWhite turned over), grey and alpha; premultiplied alpha made straight; an
// extra sample that is not alpha; JPEG in TIFF; what it refuses.
TEST(test_open_tiff) {
  TempDir dir;
  const uint32_t w = 300, h = 270;
  const size_t n = size_t(w) * h;
  const fs::path f = dir / "t.tif";
  const std::vector<uint8_t>& adobe = icc_profile(ColorSpace::AdobeRgb), &srgb = icc_profile(ColorSpace::Srgb);
  const std::vector<uint16_t> rgba = noise<uint16_t>(n * 4, 10);
  TiffSpec s;
  s.spp = 4, s.bits = 16, s.extra = EXTRASAMPLE_UNASSALPHA, s.apart = true, s.icc = adobe;
  for (uint16_t compression : {COMPRESSION_NONE, COMPRESSION_LZW, COMPRESSION_ADOBE_DEFLATE})
    for (uint32_t tile : {0u, 16u, 512u}) {
      s.compression = compression, s.tile = tile;
      tiff_of(f, w, h, s, rgba.data());
      const DocPtr d = open_image(f);
      check_doc(d, int(w), int(h), Mode::Rgb, Depth::U16, adobe);
      CHECK(pixels<uint16_t>(*d) == rgba);
    }
  s.apart = false, s.tile = 0, s.icc.clear();
  tiff_of(f, w, h, s, rgba.data());
  DocPtr d = open_image(f);
  check_doc(d, int(w), int(h), Mode::Rgb, Depth::U16, srgb);
  CHECK(pixels<uint16_t>(*d) == rgba);

  const std::vector<uint8_t> rgb = noise<uint8_t>(n * 3, 11);
  TiffSpec r;
  r.compression = COMPRESSION_LZW, r.tile = 16;
  tiff_of(f, w, h, r, rgb.data());
  d = open_image(f);
  check_doc(d, int(w), int(h), Mode::Rgb, Depth::U8, srgb);
  CHECK(pixels<uint8_t>(*d) == with_alpha(rgb, 3));

  const std::vector<uint8_t> grey = noise<uint8_t>(n, 12);
  TiffSpec g;
  g.spp = 1, g.compression = COMPRESSION_PACKBITS;
  for (uint16_t photo : {PHOTOMETRIC_MINISBLACK, PHOTOMETRIC_MINISWHITE}) {
    g.photo = photo;
    tiff_of(f, w, h, g, grey.data());
    d = open_image(f);
    check_doc(d, int(w), int(h), Mode::Gray, Depth::U8, srgb);
    std::vector<uint8_t> want = with_alpha(grey, 1);
    if (photo == PHOTOMETRIC_MINISWHITE)
      for (size_t i = 0; i < n; i++) want[i * 2] = uint8_t(255 - want[i * 2]);
    CHECK(pixels<uint8_t>(*d) == want);
  }
  const std::vector<uint16_t> ga = noise<uint16_t>(n * 2, 13);
  g.photo = PHOTOMETRIC_MINISBLACK, g.spp = 2, g.bits = 16, g.extra = EXTRASAMPLE_UNASSALPHA, g.apart = true, g.tile = 32;
  tiff_of(f, w, h, g, ga.data());
  d = open_image(f);
  check_doc(d, int(w), int(h), Mode::Gray, Depth::U16, srgb);
  CHECK(pixels<uint16_t>(*d) == ga);

  // Premultiplied (associated) alpha: colours v <= a become v * 255 / a, rounded; alpha 0 gives 0.
  std::vector<uint8_t> pre = noise<uint8_t>(n * 4, 14), straight(n * 4);
  for (size_t i = 0; i < n; i++) {
    const uint8_t a = pre[i * 4 + 3];
    for (int c = 0; c < 3; c++) {
      pre[i * 4 + c] = uint8_t(a ? pre[i * 4 + c] % (a + 1) : 0);
      straight[i * 4 + c] = uint8_t(a ? (pre[i * 4 + c] * 255 + a / 2) / a : 0);
    }
    straight[i * 4 + 3] = a;
  }
  TiffSpec p;
  p.spp = 4, p.extra = EXTRASAMPLE_ASSOCALPHA, p.compression = COMPRESSION_ADOBE_DEFLATE;
  tiff_of(f, w, h, p, pre.data());
  d = open_image(f);
  check_doc(d, int(w), int(h), Mode::Rgb, Depth::U8, srgb);
  CHECK(pixels<uint8_t>(*d) == straight);
  // An extra sample of no kind (a saved selection, say): left out, opaque.
  p.extra = EXTRASAMPLE_UNSPECIFIED;
  tiff_of(f, w, h, p, pre.data());
  d = open_image(f);
  std::vector<uint8_t> colours = pre;
  for (size_t i = 0; i < n; i++) colours[i * 4 + 3] = 255;
  CHECK(pixels<uint8_t>(*d) == colours);

  // JPEG in TIFF (YCbCr as Photoshop writes it), strips of 16 rows: RGB within compression's error.
  TiffSpec j;
  j.photo = PHOTOMETRIC_YCBCR, j.compression = COMPRESSION_JPEG, j.rows = 16;
  std::vector<uint8_t> smooth(n * 3);
  for (size_t i = 0; i < smooth.size(); i++) smooth[i] = uint8_t(i / 3 % w * 200 / w + i % 3 * 20);
  tiff_of(f, w, h, j, smooth.data());
  d = open_image(f);
  check_doc(d, int(w), int(h), Mode::Rgb, Depth::U8, srgb);
  const std::vector<uint8_t> got = pixels<uint8_t>(*d), want = with_alpha(smooth, 3);
  double err = 0;
  for (size_t i = 0; i < got.size(); i++) err += std::abs(int(got[i]) - int(want[i])) / double(got.size());
  std::printf("open: JPEG in TIFF, %.2f levels off on average\n", err);
  CHECK(err < 3);

  // Refused.
  std::vector<uint8_t> cmyk(n * 4, 50);
  std::vector<float> floats(n * 3, 0.5f);
  TiffSpec x;
  x.spp = 4, x.photo = PHOTOMETRIC_SEPARATED;
  tiff_of(dir / "cmyk.tif", w, h, x, cmyk.data());
  x.spp = 3, x.photo = PHOTOMETRIC_CIELAB;
  tiff_of(dir / "lab.tif", w, h, x, cmyk.data());
  x.photo = PHOTOMETRIC_RGB, x.bits = 32, x.format = SAMPLEFORMAT_IEEEFP;
  tiff_of(dir / "float.tif", w, h, x, floats.data());
  x.format = SAMPLEFORMAT_UINT;
  tiff_of(dir / "32.tif", w, h, x, floats.data());
  x.spp = 1, x.bits = 8, x.photo = PHOTOMETRIC_PALETTE;
  tiff_of(dir / "palette.tif", w, h, x, cmyk.data());
  CHECK(fails(dir / "cmyk.tif", Why::Codec, "CMYK", "tiff_cmyk") && fails(dir / "lab.tif", Why::Codec, "Lab", "tiff_lab"));
  CHECK(fails(dir / "float.tif", Why::Codec, "floating point", "tiff_float"));
  CHECK(fails(dir / "32.tif", Why::Codec, "32-bit", "tiff_32bit"));
  CHECK(fails(dir / "palette.tif", Why::Codec, "PhotometricInterpretation 3", "tiff_photometric_3"));
}

// JPEG: grey under a grey profile; RGB 4:2:0, baseline and progressive, under Display P3 and under a profile in three
// APP2 chunks: the samples TurboJPEG decodes. 517 x 263: three tiles across, two down. Refused: CMYK, YCCK, 12 bits.
TEST(test_open_jpeg) {
  TempDir dir;
  const int w = 517, h = 263;
  const size_t n = size_t(w) * h;
  std::vector<uint8_t> rgb(n * 3);
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      uint8_t* p = &rgb[(size_t(y) * w + x) * 3];
      p[0] = uint8_t(x * 255 / (w - 1)), p[1] = uint8_t(y * 255 / (h - 1)), p[2] = uint8_t((x * 7 + y * 3) % 256);
    }
  const std::vector<uint8_t> grey_icc = grey_profile(), big = big_profile(), &p3 = icc_profile(ColorSpace::DisplayP3);
  CHECK(big.size() > 2 * 65519);  // 65,519 bytes an APP2 chunk
  std::vector<uint8_t> grey(n);
  for (size_t i = 0; i < n; i++) grey[i] = rgb[i * 3 + 1];
  std::vector<uint8_t> jpeg = jpeg_of(w, h, TJPF_GRAY, grey.data(), TJSAMP_GRAY, grey_icc);
  put(dir / "g.jpg", jpeg);
  DocPtr d = open_image(dir / "g.jpg");
  check_doc(d, w, h, Mode::Gray, Depth::U8, grey_icc);
  CHECK(pixels<uint8_t>(*d) == with_alpha(jpeg_levels(jpeg, TJPF_GRAY), 1));
  for (bool progressive : {false, true})
    for (const std::vector<uint8_t>* icc : {&p3, &big}) {
      jpeg = jpeg_of(w, h, TJPF_RGB, rgb.data(), TJSAMP_420, *icc, progressive);
      put(dir / "c.jpg", jpeg);
      d = open_image(dir / "c.jpg");
      check_doc(d, w, h, Mode::Rgb, Depth::U8, *icc);
      CHECK(pixels<uint8_t>(*d) == jpeg_levels(jpeg, TJPF_RGBA));
    }
  std::vector<uint8_t> cmyk(n * 4, 100);
  put(dir / "cmyk.jpg", jpeg_of(w, h, TJPF_CMYK, cmyk.data(), TJSAMP_444, {}, false, TJCS_CMYK));
  put(dir / "ycck.jpg", jpeg_of(w, h, TJPF_CMYK, cmyk.data(), TJSAMP_444));
  CHECK(fails(dir / "cmyk.jpg", Why::Codec, "CMYK", "jpeg_cmyk") && fails(dir / "ycck.jpg", Why::Codec, "YCCK", "jpeg_ycck"));
  std::vector<short> deep(n * 3, 2000);
  put(dir / "12.jpg", jpeg_of(w, h, TJPF_RGB, deep.data(), TJSAMP_444, {}, false, -1, 12));
  CHECK(fails(dir / "12.jpg", Why::Codec, "12-bit", "jpeg_12bit"));
}

// Not an image, none there; sizes past the limits in a header, refused from it before anything is allocated for them
// (30,000 x 30,000 of 16-bit RGBA would be 7.2 GB): JPEG's SOF, PNG's IHDR (its CRC made right), TIFF's IFD, and
// there, within both limits, one strip of 1 GB; a file's kind from its signature, whatever its name.
TEST(test_open_refused) {
  TempDir dir;
  put(dir / "x.jpg", {'n', 'o', 't', ' ', 'a', 'n', ' ', 'i', 'm', 'a', 'g', 'e'});
  put(dir / "empty.png", {});
  CHECK(fails(dir / "x.jpg", Why::Corrupt, "not a JPEG, PNG or TIFF") && fails(dir / "empty.png", Why::Corrupt, "not a JPEG"));
  CHECK(fails(dir / "missing.tif", Why::Corrupt, "cannot open"));

  const int w = 61, h = 37;
  const std::vector<uint8_t> rgb = noise<uint8_t>(size_t(w) * h * 3, 15);
  const std::vector<uint8_t> jpeg = jpeg_of(w, h, TJPF_RGB, rgb.data(), TJSAMP_444);
  size_t sof = 2;
  while (!(jpeg[sof] == 0xff && jpeg[sof + 1] == 0xc0)) sof += 2 + size_t(jpeg[sof + 2] << 8 | jpeg[sof + 3]);
  const auto jpeg_sized = [&](int sw, int sh) {
    std::vector<uint8_t> j = jpeg;
    j[sof + 5] = uint8_t(sh >> 8), j[sof + 6] = uint8_t(sh), j[sof + 7] = uint8_t(sw >> 8), j[sof + 8] = uint8_t(sw);
    put(dir / "huge.jpg", j);
    return fails(dir / "huge.jpg", Why::TooBig, "512 MP");
  };
  CHECK(jpeg_sized(65001, 10) && jpeg_sized(30000, 30000));

  const std::vector<uint8_t> png = png_of(w, h, SPNG_COLOR_TYPE_TRUECOLOR, 8, rgb.data(), rgb.size());
  const auto png_sized = [&](uint32_t sw, uint32_t sh) {
    std::vector<uint8_t> p = png;
    for (int i = 0; i < 4; i++) p[16 + i] = uint8_t(sw >> (24 - 8 * i)), p[20 + i] = uint8_t(sh >> (24 - 8 * i));
    const uLong crc = crc32(0, p.data() + 12, 17);
    for (int i = 0; i < 4; i++) p[29 + i] = uint8_t(crc >> (24 - 8 * i));
    put(dir / "huge.png", p);
    return fails(dir / "huge.png", Why::TooBig, sw > 65000 ? "png:" : "512 MP");
  };
  CHECK(png_sized(65001, 10) && png_sized(30000, 30000));

  // The signature decides, not the name: a PNG named .jpg, a JPEG named .tif.
  put(dir / "png.jpg", png);
  put(dir / "jpeg.tif", jpeg);
  CHECK(open_image(dir / "png.jpg")->width == w && open_image(dir / "jpeg.tif")->height == h);

  TiffSpec s;
  s.rows = h, s.compression = COMPRESSION_LZW;  // one strip (libtiff cuts an uncompressed one into rows)
  tiff_of(dir / "t.tif", w, h, s, rgb.data());
  const std::vector<uint8_t> tiff = get(dir / "t.tif");
  const auto tiff_sized = [&](uint32_t sw, uint32_t sh, const char* what) {
    std::vector<uint8_t> p = tiff;
    const uint32_t ifd = p[4] | p[5] << 8 | p[6] << 16 | uint32_t(p[7]) << 24;
    for (uint32_t i = 0, k = p[ifd] | p[ifd + 1] << 8; i < k; i++) {
      uint8_t* e = &p[ifd + 2 + 12 * i];
      const int tag = e[0] | e[1] << 8;
      if (tag == 256 || tag == 257 || tag == 278) {  // width, height, rows a strip, as a LONG
        const uint32_t v = tag == 256 ? sw : sh;
        e[2] = 4, e[3] = 0, e[4] = 1, e[5] = e[6] = e[7] = 0;
        for (int b = 0; b < 4; b++) e[8 + b] = uint8_t(v >> 8 * b);
      }
    }
    put(dir / "huge.tif", p);
    return fails(dir / "huge.tif", Why::TooBig, what);
  };
  CHECK(tiff_sized(65001, 11, "512 MP") && tiff_sized(30000, 30000, "512 MP") && tiff_sized(60000, 6000, "MB a row"));
}

// Damaged files: every file cut short (each length over its first 600 bytes, every 7th past them) is an error, but a
// PNG missing only what follows its image (IEND), which opens whole; 300 single-byte flips each are an error or a
// valid document (its pixels read through, for ASan), never a crash or an error of another kind.
TEST(test_open_damaged) {
  TempDir dir;
  const uint32_t w = 37, h = 29;
  const size_t n = size_t(w) * h;
  std::vector<uint8_t> rgb(n * 3);
  for (size_t i = 0; i < rgb.size(); i++) rgb[i] = uint8_t(i / 3 % w * 7 + i % 3 * 40 + i / 3 / w * 3);
  const std::vector<uint16_t> rgba16 = noise<uint16_t>(n * 4, 16);
  struct File {
    const char* name;
    std::vector<uint8_t> bytes;
  };
  std::vector<File> files = {
      {"base.jpg", jpeg_of(int(w), int(h), TJPF_RGB, rgb.data(), TJSAMP_420)},
      {"prog.jpg", jpeg_of(int(w), int(h), TJPF_RGB, rgb.data(), TJSAMP_420, {}, true)},
      {"ga.png", png_of(w, h, SPNG_COLOR_TYPE_GRAYSCALE_ALPHA, 8, rgb.data(), n * 2)},
      {"adam7.png", png_of(w, h, SPNG_COLOR_TYPE_TRUECOLOR_ALPHA, 16, rgba16.data(), rgba16.size() * 2, {}, true)},
  };
  TiffSpec strips, tiles;
  strips.compression = COMPRESSION_LZW, strips.icc = icc_profile(ColorSpace::DisplayP3);
  tiles.spp = 4, tiles.bits = 16, tiles.extra = EXTRASAMPLE_UNASSALPHA, tiles.apart = true, tiles.tile = 16;
  tiles.compression = COMPRESSION_ADOBE_DEFLATE;
  tiff_of(dir / "s.tif", w, h, strips, rgb.data());
  tiff_of(dir / "t.tif", w, h, tiles, rgba16.data());
  files.push_back({"strips.tif", get(dir / "s.tif")});
  files.push_back({"tiles.tif", get(dir / "t.tif")});
  std::mt19937 rng(17);
  int cut = 0, flipped = 0, opened = 0;
  for (const File& f : files) {
    const fs::path p = dir / f.name;
    put(p, f.bytes);
    const DocPtr whole = open_image(p);
    const std::vector<uint8_t> want = raw(*whole);
    const bool is_png = fs::path(f.name).extension() == ".png";
    for (size_t k = 0; k < f.bytes.size(); k += k < 600 ? 1 : 7, cut++) {
      put(p, std::vector<uint8_t>(f.bytes.begin(), f.bytes.begin() + std::ptrdiff_t(k)));
      try {
        const DocPtr d = open_image(p);
        if (!(is_png && d->depth == whole->depth && raw(*d) == want)) std::printf("%s cut at %zu of %zu opened\n", f.name, k, f.bytes.size());
        CHECK(is_png && d->depth == whole->depth && raw(*d) == want);
      } catch (const DecodeError& e) {
        if (e.kind != Why::Corrupt) std::printf("%s cut at %zu: %s\n", f.name, k, e.what());
        CHECK(e.kind == Why::Corrupt);
      }
    }
    for (int i = 0; i < 300; i++, flipped++) {
      std::vector<uint8_t> b = f.bytes;
      b[rng() % b.size()] ^= uint8_t(1 + rng() % 255);
      put(p, b);
      try {
        const DocPtr d = open_image(p);
        validate(*d);
        CHECK(raw(*d).size() == size_t(d->width) * size_t(d->height) * size_t(pixel_format(*d).bytes()));
        opened++;
      } catch (const DecodeError&) {
      }
    }
  }
  std::printf("open: %d cut files refused, %d flipped bytes: %d opened, the rest refused\n", cut, flipped, opened);
}
