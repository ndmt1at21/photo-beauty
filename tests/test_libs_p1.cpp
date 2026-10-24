// P1's libraries (M30) link, built as vcpkg.json asks. What the formats do with them is their own tests' (M33-M37,
// M48).
#include <libraw/libraw.h>
#include <webp/decode.h>
#include <webp/demux.h>
#include <webp/encode.h>
#include <webp/mux.h>

#include <pugixml.hpp>

#include "check.h"

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <wincodec.h>
#endif

TEST(test_libs_p1) {
  // LibRaw 0.22, with lossy DNGs (JPEG: vcpkg's dng-lossy) and deflate ones (zlib).
  CHECK(libraw_versionNumber() >= LIBRAW_MAKE_VERSION(0, 22, 0));
  CHECK(libraw_capabilities() & LIBRAW_CAPS_JPEG);
  CHECK(libraw_capabilities() & LIBRAW_CAPS_ZLIB);

  // libwebp 1.6 with demux (ICC, EXIF, XMP, frames read) and mux (written).
  CHECK(WebPGetDecoderVersion() >= 0x010600 && WebPGetEncoderVersion() >= 0x010600);
  CHECK(WebPGetDemuxVersion() >= 0x010600 && WebPGetMuxVersion() >= 0x010600);

  // pugixml (XMP) has no version function: a packet's attribute instead.
  pugi::xml_document xmp;
  CHECK(xmp.load_string("<x:xmpmeta xmlns:x='adobe:ns:meta/' xmlns:xmp='http://ns.adobe.com/xap/1.0/' xmp:Rating='3'/>"));
  CHECK(xmp.child("x:xmpmeta").attribute("xmp:Rating").as_int() == 3);

#ifdef _WIN32
  // WIC (M36's HEIC) through COM: its factory, in every Windows since 7.
  CHECK(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)));
  IWICImagingFactory* wic = nullptr;
  CHECK(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))));
  wic->Release();
  CoUninitialize();
#endif
}
