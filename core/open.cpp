#include "open.h"

#include <spng.h>
#include <tiffio.h>

#include <algorithm>
#include <csetjmp>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "color.h"
#include "jobs.h"
#include "profile.h"

// libjpeg's API, which the TurboJPEG library holds too: rows as they decode, and a file cut
// short told by its code.
#include <jerror.h>
#include <jpeglib.h>

namespace pb {
namespace {

using Why = DecodeError::Kind;

// Lightroom's limits: pixels a side, pixels in all. Scans of a progressive JPEG (a hostile one's work
// capped); what a PNG's compressed chunks (ICC profile, text) may inflate to, and its chunks before the image in all;
// a row of TIFF blocks (strips, or tiles side by side) as decoded.
constexpr uint64_t kMaxSide = 65000, kMaxPixels = 512'000'000;
constexpr int kMaxScans = 500;
constexpr size_t kChunk = size_t(16) << 20, kChunks = size_t(64) << 20;
constexpr uint64_t kMaxBlocks = uint64_t(1) << 30;

// Why a file does not open, without its path (open_image() adds it); codec: an unsupported variant's name.
[[noreturn]] void fail(Why kind, const std::string& what, std::string codec = {}) {
  throw DecodeError(kind, std::move(codec), what);
}

void check_size(uint64_t w, uint64_t h) {
  if (w < 1 || h < 1) fail(Why::Corrupt, "no pixels");
  if (w > kMaxSide || h > kMaxSide || w * h > kMaxPixels)
    fail(Why::TooBig, std::to_string(w) + " x " + std::to_string(h) + " pixels: more than 65,000 a side or 512 MP");
}

// The layer being read. A decoder puts rows of the document's pixels (grey or RGB, then alpha, at the file's depth)
// into row(y) top to bottom and calls done(y) after each: every row of tiles they finish is cut on the pool and kept
// where neither empty nor solid. Rows live in a band of kTileSize, or all of them for a decoder that fills them out of
// order (Adam7), uninitialised: decoders write every sample.
class Bands {
 public:
  Bands(uint32_t w, uint32_t h, Format f, bool whole = false)
      : w_(w), h_(h), rows_(whole ? h : std::min<uint32_t>(h, kTileSize)), stride_(size_t(w) * size_t(f.bytes())),
        buf_(new uint8_t[stride_ * rows_]), grid_(f) {}

  uint8_t* row(uint32_t y) { return buf_.get() + size_t(y % rows_) * stride_; }

  void done(uint32_t y) {
    if ((y + 1) % kTileSize != 0 && y + 1 != h_) return;
    PROFILE_ZONE("open_tiles");
    const uint32_t y0 = y - y % kTileSize, n = y + 1 - y0;
    const Format f = grid_.format();
    const size_t bpp = size_t(f.bytes());
    const int across = int((w_ + kTileSize - 1) >> kTileLog2);
    std::vector<TilePtr> made(static_cast<size_t>(across));
    parallel_for(across, [&](int a, int b) {
      for (int tx = a; tx < b; tx++) {
        const uint32_t x0 = uint32_t(tx) << kTileLog2;
        const size_t bytes = std::min<uint32_t>(kTileSize, w_ - x0) * bpp;
        const std::shared_ptr<Tile> t = Tile::make(f);  // transparent past the image
        {
          const WritePin p = t->write();
          for (uint32_t r = 0; r < n; r++) std::memcpy(p.data() + r * kTileSize * bpp, row(y0 + r) + x0 * bpp, bytes);
        }
        made[size_t(tx)] = compact(t, {});
      }
    });
    for (int tx = 0; tx < across; tx++) grid_.set(tx, int(y0 >> kTileLog2), std::move(made[size_t(tx)]));
  }

  // Once every row is done.
  DocPtr document(std::vector<uint8_t> icc) {
    static const auto srgb = std::make_shared<const std::vector<uint8_t>>(icc_profile(ColorSpace::Srgb));
    const Format f = grid_.format();
    const DocPtr blank = new_document(int(w_), int(h_), f.channels == 2 ? Mode::Gray : Mode::Rgb, f.depth,
                                      icc.empty() ? srgb : std::make_shared<const std::vector<uint8_t>>(std::move(icc)));
    AddLayer add;
    add.props.name = "Background";
    add.pixels = std::make_shared<const TileGrid>(std::move(grid_));
    return run(*blank, add);
  }

 private:
  uint32_t w_, h_, rows_;
  size_t stride_;
  std::unique_ptr<uint8_t[]> buf_;
  TileGrid grid_;
};

// ---- JPEG (libjpeg's API; setjmp safe: every object with a destructor exists before the setjmp) ----

struct JpegError {
  jpeg_error_mgr pub;
  std::jmp_buf jump;
  bool cut;  // the data ended before the image did (libjpeg made up the rest)
  char why[JMSG_LENGTH_MAX];
};
void jpeg_exit(j_common_ptr j) {
  auto* e = reinterpret_cast<JpegError*>(j->err);
  (*j->err->format_message)(j, e->why);
  std::longjmp(e->jump, 1);
}
void jpeg_message(j_common_ptr j, int level) {  // warnings (recoverable damage, as in some cameras' files) pass
  if (level < 0 && j->err->msg_code == JWRN_JPEG_EOF) reinterpret_cast<JpegError*>(j->err)->cut = true;
}
void jpeg_scans(j_common_ptr j) {
  const auto* d = reinterpret_cast<j_decompress_ptr>(j);
  if (j->is_decompressor && d->progressive_mode && d->input_scan_number > kMaxScans) {
    auto* e = reinterpret_cast<JpegError*>(j->err);
    std::snprintf(e->why, sizeof e->why, "more than %d scans", kMaxScans);
    std::longjmp(e->jump, 1);
  }
}

DocPtr jpeg(const std::vector<uint8_t>& data) {
  jpeg_decompress_struct j{};
  struct Destroy {
    jpeg_decompress_struct& j;
    ~Destroy() { jpeg_destroy_decompress(&j); }  // nothing on a struct still zero
  } destroy{j};
  JpegError err{};
  jpeg_progress_mgr scans{};
  std::optional<Bands> bands;
  std::vector<uint8_t> icc, line;
  JOCTET* profile = nullptr;
  unsigned int profile_size = 0;
  j.err = jpeg_std_error(&err.pub);
  err.pub.error_exit = jpeg_exit, err.pub.emit_message = jpeg_message;
  scans.progress_monitor = jpeg_scans;
  if (setjmp(err.jump)) fail(Why::Corrupt, std::string("jpeg: ") + err.why);  // profile: freed before any libjpeg call
  jpeg_create_decompress(&j);
  j.progress = &scans;
  jpeg_mem_src(&j, data.data(), static_cast<unsigned long>(data.size()));
  jpeg_save_markers(&j, JPEG_APP0 + 2, 0xffff);  // ICC profile chunks
  jpeg_read_header(&j, TRUE);
  check_size(j.image_width, j.image_height);
  if (j.data_precision != 8)
    fail(Why::Codec, "jpeg: " + std::to_string(j.data_precision) + "-bit samples", "jpeg_" + std::to_string(j.data_precision) + "bit");
  if (j.jpeg_color_space == JCS_CMYK || j.jpeg_color_space == JCS_YCCK)
    fail(Why::Codec, j.jpeg_color_space == JCS_CMYK ? "jpeg: CMYK" : "jpeg: CMYK (YCCK)", j.jpeg_color_space == JCS_CMYK ? "jpeg_cmyk" : "jpeg_ycck");
  const bool grey = j.jpeg_color_space == JCS_GRAYSCALE;
  if (!grey && j.jpeg_color_space != JCS_YCbCr && j.jpeg_color_space != JCS_RGB)
    fail(Why::Codec, "jpeg: " + std::to_string(j.num_components) + " components", "jpeg_components");
  j.out_color_space = grey ? JCS_GRAYSCALE : JCS_EXT_RGBA;  // alpha 255
  if (jpeg_read_icc_profile(&j, &profile, &profile_size)) icc.assign(profile, profile + profile_size);
  std::free(profile);
  profile = nullptr;
  const uint32_t w = j.image_width, h = j.image_height;
  bands.emplace(w, h, Format{Depth::U8, grey ? 2 : 4});
  line.resize(grey ? w : 0);
  // Data ending before the image: libjpeg-turbo's jpeg8 build (vcpkg's) warns of it only sometimes; its memory source
  // then reads a fake EOI from outside the buffer. Checked from the first row on (a progressive file's scans all come
  // in jpeg_start_decompress()) and each band, so a short file claiming a huge image stops early.
  const auto cut = [&] {
    const JOCTET* at = j.src->next_input_byte;
    return err.cut || at < data.data() || at > data.data() + data.size();
  };
  jpeg_start_decompress(&j);
  for (uint32_t y = 0; y < h; y++) {
    if (y % kTileSize == 0 && cut()) fail(Why::Corrupt, "jpeg: cut short");
    uint8_t* out = bands->row(y);
    JSAMPROW p = grey ? line.data() : out;
    jpeg_read_scanlines(&j, &p, 1);
    if (grey)
      for (uint32_t x = 0; x < w; x++) out[2 * x] = line[x], out[2 * x + 1] = 255;
    bands->done(y);
  }
  jpeg_finish_decompress(&j);
  if (cut()) fail(Why::Corrupt, "jpeg: cut short");
  return bands->document(std::move(icc));
}

// ---- PNG (libspng, streamed from the file) ----

int png_read(spng_ctx*, void* file, void* to, size_t n) {
  auto& f = *static_cast<std::ifstream*>(file);
  f.read(static_cast<char*>(to), std::streamsize(n));
  return size_t(f.gcount()) == n ? 0 : SPNG_IO_EOF;
}

DocPtr png(std::ifstream& f) {
  const std::unique_ptr<spng_ctx, void (*)(spng_ctx*)> ctx(spng_ctx_new(0), spng_ctx_free);
  if (!ctx) throw std::bad_alloc();
  spng_ctx* s = ctx.get();
  const auto check = [](int rc) {
    if (rc) fail(rc == SPNG_EUSER_WIDTH || rc == SPNG_EUSER_HEIGHT ? Why::TooBig : Why::Corrupt, std::string("png: ") + spng_strerror(rc));
  };
  check(spng_set_image_limits(s, uint32_t(kMaxSide), uint32_t(kMaxSide)));
  check(spng_set_chunk_limits(s, kChunk, kChunks));
  check(spng_set_png_stream(s, png_read, &f));
  spng_ihdr ihdr{};
  check(spng_get_ihdr(s, &ihdr));
  check_size(ihdr.width, ihdr.height);
  std::vector<uint8_t> icc;
  spng_iccp iccp{};
  if (spng_get_iccp(s, &iccp) == 0) icc.assign(iccp.profile, iccp.profile + iccp.profile_len);
  const uint8_t type = ihdr.color_type;
  const bool grey = type == SPNG_COLOR_TYPE_GRAYSCALE || type == SPNG_COLOR_TYPE_GRAYSCALE_ALPHA, deep = ihdr.bit_depth == 16;
  // The document's layout straight from libspng: alpha from tRNS or opaque, 16 bits in the host's byte order, 1, 2
  // and 4 bits scaled to 8 (bits repeated), palettes looked up.
  const int fmt = type == SPNG_COLOR_TYPE_GRAYSCALE_ALPHA ? SPNG_FMT_PNG
                  : grey                                   ? (deep ? SPNG_FMT_GA16 : SPNG_FMT_GA8)
                                                           : (deep ? SPNG_FMT_RGBA16 : SPNG_FMT_RGBA8);
  const Format layout{deep ? Depth::U16 : Depth::U8, grey ? 2 : 4};
  const bool interlaced = ihdr.interlace_method != 0;  // Adam7: rows come pass by pass, so all of them are held
  Bands bands(ihdr.width, ihdr.height, layout, interlaced);
  const size_t row = size_t(ihdr.width) * size_t(layout.bytes());
  check(spng_decode_image(s, nullptr, 0, fmt, SPNG_DECODE_TRNS | SPNG_DECODE_PROGRESSIVE));
  for (int rc = 0; rc != SPNG_EOI;) {  // the last row says so
    spng_row_info info{};
    if ((rc = spng_get_row_info(s, &info)) == SPNG_EOI) break;
    check(rc);
    rc = spng_decode_row(s, bands.row(info.row_num), row);
    if (rc != SPNG_EOI) check(rc);
    if (!interlaced) bands.done(info.row_num);
  }
  if (interlaced)
    for (uint32_t y = 0; y < ihdr.height; y++) bands.done(y);
  return bands.document(std::move(icc));
}

// ---- TIFF (libtiff, through a stream of its own) ----

// A libtiff handle on the file, its first error kept.
struct Tiff {
  std::ifstream f;
  TIFF* t = nullptr;
  std::string error;
  bool cut = false;  // a tag's data past the end of the file, which libtiff leaves out with a warning
  Tiff(const Tiff&) = delete;
  explicit Tiff(const std::filesystem::path& path);
  ~Tiff() {
    if (t) TIFFClose(t);
  }
};
tmsize_t tiff_read(thandle_t h, void* to, tmsize_t n) {
  std::ifstream& f = static_cast<Tiff*>(h)->f;
  f.read(static_cast<char*>(to), std::streamsize(n));
  return tmsize_t(f.gcount());
}
tmsize_t tiff_write(thandle_t, void*, tmsize_t) { return 0; }
toff_t tiff_seek(thandle_t h, toff_t at, int whence) {
  std::ifstream& f = static_cast<Tiff*>(h)->f;
  f.clear();
  f.seekg(std::streamoff(at), whence == SEEK_SET ? std::ios::beg : whence == SEEK_CUR ? std::ios::cur : std::ios::end);
  return f ? toff_t(f.tellg()) : toff_t(-1);
}
int tiff_close(thandle_t) { return 0; }
toff_t tiff_size(thandle_t h) {
  std::ifstream& f = static_cast<Tiff*>(h)->f;
  f.clear();
  const std::streampos at = f.tellg();
  f.seekg(0, std::ios::end);
  const std::streampos end = f.tellg();
  f.seekg(at);
  return toff_t(std::max<std::streamoff>(end, 0));
}
int tiff_map(thandle_t, void**, toff_t*) { return 0; }
void tiff_unmap(thandle_t, void*, toff_t) {}
int tiff_error(TIFF*, void* user, const char*, const char* format, va_list args) {
  std::string& e = static_cast<Tiff*>(user)->error;
  if (e.empty()) {
    char why[512];
    std::vsnprintf(why, sizeof why, format, args);
    e = why;
  }
  return 1;
}
// As JPEG's, recoverable; but a tag left out for data past the end (libtiff writes tag data last, an ICC profile
// often last of all) means a file cut short.
int tiff_warning(TIFF*, void* user, const char*, const char* format, va_list) {
  if (std::strncmp(format, "IO error", 8) == 0) static_cast<Tiff*>(user)->cut = true;
  return 1;
}

Tiff::Tiff(const std::filesystem::path& path) : f(path, std::ios::binary) {
  TIFFOpenOptions* o = TIFFOpenOptionsAlloc();
  TIFFOpenOptionsSetErrorHandlerExtR(o, tiff_error, this);
  TIFFOpenOptionsSetWarningHandlerExtR(o, tiff_warning, this);
  TIFFOpenOptionsSetMaxSingleMemAlloc(o, tmsize_t(kMaxBlocks));
  t = TIFFClientOpenExt("tiff", "r", this, tiff_read, tiff_write, tiff_seek, tiff_close, tiff_size, tiff_map, tiff_unmap, o);
  TIFFOpenOptionsFree(o);
  if (!t) fail(Why::Corrupt, "tiff: " + error);
}

// Row y of a row of blocks (strips, or tiles `bw` wide side by side; each `planes` planes of `plane` bytes, `per`
// samples a pixel) as the document's: the colours (premultiplied made straight, then MinIsWhite turned over), then
// alpha, opaque where the file has none.
template <class T>
void tiff_row(const uint8_t* blocks, size_t plane, int planes, uint32_t bw, uint32_t y, int per, int colours, int alpha,
              bool white, uint32_t w, T* out) {
  constexpr uint64_t top = T(~T(0));
  for (uint32_t x0 = 0; x0 < w; x0 += bw, blocks += size_t(planes) * plane) {
    const T* s[4];  // this row of the block in each plane (planes together: all in the first)
    for (int p = 0; p < 4; p++)
      s[p] = reinterpret_cast<const T*>(blocks + size_t(std::min(p, planes - 1)) * plane) + size_t(y) * bw * size_t(per);
    for (uint32_t x = 0, n = std::min(bw, w - x0); x < n; x++, out += colours + 1) {
      const auto sample = [&](int c) -> uint64_t { return planes > 1 ? s[c][x] : s[0][size_t(x) * size_t(per) + size_t(c)]; };
      const uint64_t a = alpha ? sample(colours) : top;
      for (int c = 0; c < colours; c++) {
        uint64_t v = sample(c);
        if (alpha == EXTRASAMPLE_ASSOCALPHA) v = a ? std::min((v * top + a / 2) / a, top) : 0;
        out[c] = T(white ? top - v : v);
      }
      out[colours] = T(a);
    }
  }
}

// libtiff's YCbCr JPEG data as RGB.
void tiff_rgb(TIFF* t) {
  uint16_t photo = 0, compression = 0;
  if (TIFFGetField(t, TIFFTAG_PHOTOMETRIC, &photo) && photo == PHOTOMETRIC_YCBCR &&
      TIFFGetFieldDefaulted(t, TIFFTAG_COMPRESSION, &compression) && compression == COMPRESSION_JPEG)
    TIFFSetField(t, TIFFTAG_JPEGCOLORMODE, JPEGCOLORMODE_RGB);
}

// ponytail: blocks decode on this thread, a row of them at a time; a libtiff handle a thread when big
// TIFFs open slowly.
DocPtr tiff(const std::filesystem::path& path) {
  Tiff file(path);
  TIFF* t = file.t;
  if (file.cut) fail(Why::Corrupt, "tiff: cut short");
  uint32_t w = 0, h = 0;
  uint16_t bits = 1, spp = 1, photo = 0, planar = PLANARCONFIG_CONTIG, format = SAMPLEFORMAT_UINT, compression = 0;
  TIFFGetField(t, TIFFTAG_IMAGEWIDTH, &w), TIFFGetField(t, TIFFTAG_IMAGELENGTH, &h);
  TIFFGetFieldDefaulted(t, TIFFTAG_BITSPERSAMPLE, &bits), TIFFGetFieldDefaulted(t, TIFFTAG_SAMPLESPERPIXEL, &spp);
  TIFFGetFieldDefaulted(t, TIFFTAG_PLANARCONFIG, &planar), TIFFGetFieldDefaulted(t, TIFFTAG_SAMPLEFORMAT, &format);
  TIFFGetFieldDefaulted(t, TIFFTAG_COMPRESSION, &compression);
  if (!TIFFGetField(t, TIFFTAG_PHOTOMETRIC, &photo)) fail(Why::Corrupt, "tiff: no PhotometricInterpretation");
  check_size(w, h);
  if (photo == PHOTOMETRIC_SEPARATED) fail(Why::Codec, "tiff: CMYK", "tiff_cmyk");
  if (photo == PHOTOMETRIC_CIELAB || photo == PHOTOMETRIC_ICCLAB || photo == PHOTOMETRIC_ITULAB) fail(Why::Codec, "tiff: Lab", "tiff_lab");
  if (format == SAMPLEFORMAT_IEEEFP) fail(Why::Codec, "tiff: floating point samples", "tiff_float");
  if (format != SAMPLEFORMAT_UINT || (bits != 8 && bits != 16))
    fail(Why::Codec, "tiff: " + std::to_string(bits) + "-bit samples", "tiff_" + std::to_string(bits) + "bit");
  const bool grey = photo == PHOTOMETRIC_MINISBLACK || photo == PHOTOMETRIC_MINISWHITE, deep = bits == 16;
  if (!grey && !(photo == PHOTOMETRIC_RGB || (photo == PHOTOMETRIC_YCBCR && compression == COMPRESSION_JPEG)))
    fail(Why::Codec, "tiff: PhotometricInterpretation " + std::to_string(photo), "tiff_photometric_" + std::to_string(photo));
  tiff_rgb(t);
  const int colours = grey ? 1 : 3;
  if (spp < colours) fail(Why::Corrupt, "tiff: " + std::to_string(spp) + " samples a pixel");
  uint16_t extras = 0, *kinds = nullptr;  // the first extra sample when it is alpha
  const int alpha = spp > colours && TIFFGetField(t, TIFFTAG_EXTRASAMPLES, &extras, &kinds) && extras > 0 &&
                            (kinds[0] == EXTRASAMPLE_ASSOCALPHA || kinds[0] == EXTRASAMPLE_UNASSALPHA)
                        ? kinds[0]
                        : 0;
  uint32_t icc_size = 0;
  void* icc_data = nullptr;
  std::vector<uint8_t> icc;
  if (TIFFGetField(t, TIFFTAG_ICCPROFILE, &icc_size, &icc_data) && icc_data)
    icc.assign(static_cast<uint8_t*>(icc_data), static_cast<uint8_t*>(icc_data) + icc_size);
  // Blocks: strips of whole rows, or tiles; a plane each when planes lie apart.
  const bool tiled = TIFFIsTiled(t);
  uint32_t bw = w, bh = 0;
  if (tiled)
    TIFFGetField(t, TIFFTAG_TILEWIDTH, &bw), TIFFGetField(t, TIFFTAG_TILELENGTH, &bh);
  else
    TIFFGetFieldDefaulted(t, TIFFTAG_ROWSPERSTRIP, &bh), bh = std::min(bh, h);
  const bool apart = planar == PLANARCONFIG_SEPARATE;
  const int per = apart ? 1 : spp, planes = apart ? colours + (alpha ? 1 : 0) : 1;  // planes read: colours, alpha
  const size_t bytes = deep ? 2 : 1;
  const uint64_t plane = uint64_t(bw) * bh * uint64_t(per) * bytes;  // a block's plane as decoded
  // Tiles past the image by less than a tile's step of 16, or up to 1024 a side for a small image, as libtiff sizes
  // them: a block's allocation stays near the image's size.
  if (bw < 1 || bh < 1 || (tiled && (bw > std::max(w + 15, 1024u) || bh > std::max(h + 15, 1024u))) ||
      plane != uint64_t(tiled ? TIFFTileSize64(t) : TIFFStripSize64(t)))
    fail(Why::Corrupt, "tiff: blocks of " + std::to_string(bw) + " x " + std::to_string(bh));
  const uint32_t across = (w + bw - 1) / bw;
  const uint64_t row_bytes = plane * uint64_t(planes) * across;
  // ponytail: a row of blocks held whole, as libtiff decodes a block, at most kMaxBlocks: a huge single strip is
  // refused; read it in row bands (TIFFReadScanline) if real files need that.
  if (row_bytes > kMaxBlocks) fail(Why::TooBig, "tiff: blocks of " + std::to_string(row_bytes >> 20) + " MB a row");
  Bands bands(w, h, {deep ? Depth::U16 : Depth::U8, colours + 1});
  const std::unique_ptr<uint8_t[]> blocks(new uint8_t[size_t(row_bytes)]);  // uninitialised: read before use
  for (uint32_t y0 = 0; y0 < h; y0 += bh) {
    const uint32_t rows = std::min(bh, h - y0);
    for (uint32_t b = 0; b < across; b++)
      for (int p = 0; p < planes; p++) {
        uint8_t* to = blocks.get() + (size_t(b) * size_t(planes) + size_t(p)) * plane;
        const tmsize_t want = tmsize_t(tiled ? plane : uint64_t(rows) * w * uint64_t(per) * bytes);
        const tmsize_t got = tiled ? TIFFReadEncodedTile(t, TIFFComputeTile(t, b * bw, y0, 0, uint16_t(p)), to, tmsize_t(plane))
                                   : TIFFReadEncodedStrip(t, TIFFComputeStrip(t, y0, uint16_t(p)), to, tmsize_t(plane));
        if (got < want) fail(Why::Corrupt, "tiff: " + (file.error.empty() ? std::string("a block cut short") : file.error));
      }
    for (uint32_t y = 0; y < rows; y++) {
      uint8_t* out = bands.row(y0 + y);
      const bool white = photo == PHOTOMETRIC_MINISWHITE;
      if (deep)
        tiff_row(blocks.get(), size_t(plane), planes, bw, y, per, colours, alpha, white, w, reinterpret_cast<uint16_t*>(out));
      else
        tiff_row(blocks.get(), size_t(plane), planes, bw, y, per, colours, alpha, white, w, out);
      bands.done(y0 + y);
    }
  }
  return bands.document(std::move(icc));
}

}  // namespace

DocPtr open_image(const std::filesystem::path& path) {
  PROFILE_ZONE("open_image");
  try {
    std::ifstream f(path, std::ios::binary);
    if (!f) fail(Why::Corrupt, "cannot open");
    uint8_t head[8] = {};
    f.read(reinterpret_cast<char*>(head), 8);
    f.clear();
    f.seekg(0);
    if (head[0] == 0xff && head[1] == 0xd8 && head[2] == 0xff) {
      f.seekg(0, std::ios::end);
      std::vector<uint8_t> data(size_t(std::max<std::streamoff>(f.tellg(), 0)));
      f.seekg(0);
      if (!f.read(reinterpret_cast<char*>(data.data()), std::streamsize(data.size()))) fail(Why::Corrupt, "cannot read");
      return jpeg(data);
    }
    if (std::memcmp(head, "\x89PNG\r\n\x1a\n", 8) == 0) return png(f);
    if (!std::memcmp(head, "II*\0", 4) || !std::memcmp(head, "MM\0*", 4) || !std::memcmp(head, "II+\0", 4) || !std::memcmp(head, "MM\0+", 4))
      return tiff(path);  // classic and BigTIFF
    fail(Why::Corrupt, "not a JPEG, PNG or TIFF file");
  } catch (const DecodeError& e) {
    const auto name = path.u8string();
    throw DecodeError(e.kind, e.codec, std::string(name.begin(), name.end()) + ": " + e.what());
  }
}

}  // namespace pb
