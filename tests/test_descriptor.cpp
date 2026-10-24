// Photoshop's descriptors (descriptor.h): each type in a descriptor made by hand, byte by byte from the Descriptor
// structure of Adobe's specification, reads to the JSON descriptor.h documents and writes back as the same bytes,
// through JSON text too; lookups by key name; cut short or damaged bytes refused at a position, never a crash; the
// descriptors of Photoshop's PSDs in tests/golden/psd/ (E33) and $PB_PSD_DIR read and written back byte for byte,
// skipped when there are none.
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "check.h"
#include "descriptor.h"

using namespace pb;
using json = nlohmann::json;

namespace {

// Bytes as the specification lays them out, big-endian.
struct Bytes {
  std::vector<uint8_t> b;
  Bytes& u8(uint8_t v) { return b.push_back(v), *this; }
  Bytes& u32(uint32_t v) {
    for (int i = 3; i >= 0; i--) b.push_back(uint8_t(v >> 8 * i));
    return *this;
  }
  Bytes& u64(uint64_t v) { return u32(uint32_t(v >> 32)).u32(uint32_t(v)); }
  Bytes& f64(double d) {
    uint64_t v;
    std::memcpy(&v, &d, 8);
    return u64(v);
  }
  Bytes& raw(const std::string& s) { return b.insert(b.end(), s.begin(), s.end()), *this; }
  Bytes& tag(const char* s) { return raw(std::string(s, 4)); }  // an OSType: a type, unit or reference form
  // A key or class: 4 bytes after a length of 0 (a char ID), or the length and the bytes (a string ID).
  Bytes& id(const std::string& s) { return (s.size() == 4 ? u32(0) : u32(uint32_t(s.size()))).raw(s); }
  Bytes& sid(const std::string& s) { return u32(uint32_t(s.size())).raw(s); }  // a string ID whatever its size
  // A Unicode string with its closing null counted, as Photoshop writes them; or without.
  Bytes& ustr(const std::u16string& s, bool null = true) {
    u32(uint32_t(s.size() + null));
    for (const char16_t c : s) u8(uint8_t(c >> 8)).u8(uint8_t(c));
    return null ? u8(0).u8(0) : *this;
  }
  // A descriptor's head: an empty name, its class, its item count.
  Bytes& head(const std::string& cls, uint32_t items) { return ustr(u"").id(cls).u32(items); }
};

struct Sample {
  const char* what;
  std::vector<uint8_t> bytes;
  const char* json;  // as read_descriptor() reads it
};

std::u16string utf16(std::initializer_list<uint16_t> units) {
  std::u16string s;
  for (const uint16_t u : units) s += char16_t(u);
  return s;
}

std::string pth_payload() {  // a 'Pth ' as Photoshop on Windows writes one: 'txtu', little-endian sizes, UTF-16LE
  const std::u16string path = u"C:\\a.psd";
  std::string s = "txtu";
  const uint32_t size = uint32_t(12 + 2 * path.size()), n = uint32_t(path.size());
  for (int i = 0; i < 4; i++) s += char(size >> 8 * i);
  for (int i = 0; i < 4; i++) s += char(n >> 8 * i);
  for (const char16_t c : path) s += char(c & 0xff), s += char(c >> 8);
  return s;
}

std::vector<Sample> samples() {
  std::vector<Sample> s;
  s.push_back({"vibA: long, char and string IDs",
               Bytes().head("null", 2).sid("vibrance").tag("long").u32(25).id("Strt").tag("long").u32(uint32_t(-8)).b,
               R"({"class":"null","items":[{"key":"vibrance","type":"long","value":25},
                   {"key":"Strt","type":"long","value":-8}]})"});
  s.push_back({"numbers: doub, UntF, comp, bool",
               Bytes()
                   .head("null", 8)
                   .id("Opct").tag("UntF").tag("#Prc").f64(75.5)
                   .id("Angl").tag("UntF").tag("#Ang").f64(-120)
                   .id("Intr").tag("doub").f64(4096)
                   .id("Ngtv").tag("doub").f64(-0.0)
                   .sid("layerID").tag("comp").u64(0xfedcba9876543210)
                   .id("Rvrs").tag("bool").u8(1)
                   .id("Dthr").tag("bool").u8(0)
                   .id("Odd ").tag("bool").u8(7)
                   .b,
               R"({"class":"null","items":[{"key":"Opct","type":"UntF","unit":"#Prc","value":75.5},
                   {"key":"Angl","type":"UntF","unit":"#Ang","value":-120.0},
                   {"key":"Intr","type":"doub","value":4096.0},{"key":"Ngtv","type":"doub","value":-0.0},
                   {"key":"layerID","type":"comp","value":-81985529216486896},
                   {"key":"Rvrs","type":"bool","value":true},{"key":"Dthr","type":"bool","value":false},
                   {"key":"Odd ","type":"bool","value":7}]})"});
  s.push_back({"TEXT: UTF-16, closing nulls, lone surrogates",
               Bytes()
                   .head("null", 7)
                   .id("Nm  ").tag("TEXT").ustr(u"Layer 1")
                   .id("Txt ").tag("TEXT").ustr(u"Xin ch\u00e0o \U0001F642\r")
                   .id("Empt").tag("TEXT").ustr(u"")
                   .id("Nul ").tag("TEXT").ustr(utf16({'a', 0, 'b'}))
                   .id("Open").tag("TEXT").ustr(u"abc", false)
                   .id("None").tag("TEXT").u32(0)
                   .id("Lone").tag("TEXT").ustr(utf16({0xd800, 'x'}))
                   .b,
               R"({"class":"null","items":[{"key":"Nm  ","type":"TEXT","value":"Layer 1"},
                   {"key":"Txt ","type":"TEXT","value":"Xin ch\u00e0o \ud83d\ude42\r"},
                   {"key":"Empt","type":"TEXT","value":""},{"key":"Nul ","type":"TEXT","value":"a\u0000b"},
                   {"key":"Open","type":"TEXT","value":[97,98,99]},{"key":"None","type":"TEXT","value":[]},
                   {"key":"Lone","type":"TEXT","value":[55296,120,0]}]})"});
  s.push_back({"enum, type, GlbC, a class name",
               Bytes()
                   .ustr(u"Effects").id("Lefx").u32(4)
                   .id("Md  ").tag("enum").id("BlnM").id("Nrml")
                   .id("Styl").tag("enum").sid("gradientType").sid("linear")
                   .id("Type").tag("type").ustr(u"").id("Lyr ")
                   .id("Glob").tag("GlbC").ustr(u"Layer").sid("layerClass")
                   .b,
               R"({"name":"Effects","class":"Lefx","items":[
                   {"key":"Md  ","type":"enum","enumType":"BlnM","value":"Nrml"},
                   {"key":"Styl","type":"enum","enumType":"gradientType","value":"linear"},
                   {"key":"Type","type":"type","class":"Lyr "},
                   {"key":"Glob","type":"GlbC","name":"Layer","class":"layerClass"}]})"});
  s.push_back({"Objc, GlbO, VlLs",
               Bytes()
                   .head("null", 3)
                   .id("Clr ").tag("Objc").head("RGBC", 3)
                   .id("Rd  ").tag("doub").f64(255).id("Grn ").tag("doub").f64(128.25).id("Bl  ").tag("doub").f64(0)
                   .id("Clrs").tag("VlLs").u32(4)
                   .tag("Objc").head("Clrt", 1).id("Lctn").tag("long").u32(0)
                   .tag("long").u32(4096)
                   .tag("VlLs").u32(0)
                   .tag("enum").id("Ordn").id("Trgt")
                   .id("Glob").tag("GlbO").head("null", 0)
                   .b,
               R"({"class":"null","items":[
                   {"key":"Clr ","type":"Objc","class":"RGBC","items":[{"key":"Rd  ","type":"doub","value":255.0},
                    {"key":"Grn ","type":"doub","value":128.25},{"key":"Bl  ","type":"doub","value":0.0}]},
                   {"key":"Clrs","type":"VlLs","value":[
                    {"type":"Objc","class":"Clrt","items":[{"key":"Lctn","type":"long","value":0}]},
                    {"type":"long","value":4096},{"type":"VlLs","value":[]},
                    {"type":"enum","enumType":"Ordn","value":"Trgt"}]},
                   {"key":"Glob","type":"GlbO","class":"null","items":[]}]})"});
  s.push_back({"obj : every reference form",
               Bytes()
                   .head("null", 1)
                   .id("null").tag("obj ").u32(7)
                   .tag("prop").ustr(u"").id("Prpr").id("Lefx")
                   .tag("Clss").ustr(u"").id("Lyr ")
                   .tag("Enmr").ustr(u"").id("Lyr ").id("Ordn").id("Trgt")
                   .tag("rele").ustr(u"").id("Lyr ").u32(uint32_t(-1))
                   .tag("Idnt").u32(42)
                   .tag("indx").u32(3)
                   .tag("name").ustr(u"Layer").id("Lyr ").ustr(u"Background")
                   .b,
               R"({"class":"null","items":[{"key":"null","type":"obj ","value":[
                   {"form":"prop","class":"Prpr","key":"Lefx"},{"form":"Clss","class":"Lyr "},
                   {"form":"Enmr","class":"Lyr ","enumType":"Ordn","value":"Trgt"},
                   {"form":"rele","class":"Lyr ","value":-1},{"form":"Idnt","value":42},{"form":"indx","value":3},
                   {"form":"name","name":"Layer","class":"Lyr ","value":"Background"}]}]})"});
  s.push_back({"alis, tdta, Pth ",
               Bytes()
                   .head("null", 4)
                   .sid("EngineData").tag("tdta").u32(10).raw("<< /a 1 >>")
                   .id("Empt").tag("tdta").u32(0)
                   .id("alis").tag("alis").u32(5).raw(std::string("\x01\x02\x00\xfe\xff", 5))
                   .id("null").tag("Pth ").u32(uint32_t(pth_payload().size())).raw(pth_payload())
                   .b,
               R"({"class":"null","items":[{"key":"EngineData","type":"tdta","value":"PDwgL2EgMSA+Pg=="},
                   {"key":"Empt","type":"tdta","value":""},{"key":"alis","type":"alis","value":"AQIA/v8="},
                   {"key":"null","type":"Pth ","value":"dHh0dRwAAAAIAAAAQwA6AFwAYQAuAHAAcwBkAA=="}]})"});
  s.push_back({"UnFl, ObAr",
               Bytes()
                   .head("null", 2)
                   .id("Hrzn").tag("UnFl").tag("#Pxl").u32(3).f64(0).f64(10.5).f64(-20)
                   .sid("meshPoints").tag("ObAr").u32(2).head("rationalPoint", 2)
                   .id("Hrzn").tag("UnFl").tag("#Pxl").u32(2).f64(1).f64(2)
                   .id("Vrtc").tag("UnFl").tag("#Pxl").u32(2).f64(3).f64(4)
                   .b,
               R"({"class":"null","items":[{"key":"Hrzn","type":"UnFl","unit":"#Pxl","value":[0.0,10.5,-20.0]},
                   {"key":"meshPoints","type":"ObAr","count":2,"class":"rationalPoint","items":[
                    {"key":"Hrzn","type":"UnFl","unit":"#Pxl","value":[1.0,2.0]},
                    {"key":"Vrtc","type":"UnFl","unit":"#Pxl","value":[3.0,4.0]}]}]})"});
  s.push_back({"IDs: 4 letters with a length, odd bytes, a backslash",
               Bytes()
                   .ustr(u"").sid("warp").u32(4)
                   .sid("warp").tag("long").u32(1)
                   .u32(0).raw(std::string(4, '\0')).tag("long").u32(2)
                   .sid("a\\b").tag("enum").sid("type\x01").id("\xff\xfe\x7f ")
                   .id("Unit").tag("UntF").raw("#\x80x\\").f64(1)
                   .b,
               R"({"class":"warp","stringIDs":["class"],"items":[
                   {"key":"warp","stringIDs":["key"],"type":"long","value":1},
                   {"key":"\\x00\\x00\\x00\\x00","type":"long","value":2},
                   {"key":"a\\x5cb","type":"enum","enumType":"type\\x01","value":"\\xff\\xfe\\x7f "},
                   {"key":"Unit","type":"UntF","unit":"#\\x80x\\x5c","value":1.0}]})"});
  s.push_back({"NaN, infinities",
               Bytes()
                   .head("null", 3)
                   .id("NaN ").tag("doub").u64(0x7ff8000000000001)
                   .id("Inf ").tag("UntF").tag("#Pxl").f64(-std::numeric_limits<double>::infinity())
                   .id("Nums").tag("UnFl").tag("#Nne").u32(2).u64(0x7ff0000000000000).f64(1.5)
                   .b,
               R"({"class":"null","items":[{"key":"NaN ","type":"doub","value":"7ff8000000000001"},
                   {"key":"Inf ","type":"UntF","unit":"#Pxl","value":"fff0000000000000"},
                   {"key":"Nums","type":"UnFl","unit":"#Nne","value":["7ff0000000000000",1.5]}]})"});
  s.push_back({"a key twice, a 4-letter string ID enum and list member",
               Bytes()
                   .head("null", 3)
                   .id("Strt").tag("long").u32(1)
                   .id("Strt").tag("long").u32(2)
                   .id("Ornt").tag("enum").sid("hold").sid("list")
                   .b,
               R"({"class":"null","items":[{"key":"Strt","type":"long","value":1},
                   {"key":"Strt","type":"long","value":2},
                   {"key":"Ornt","type":"enum","enumType":"hold","value":"list","stringIDs":["enumType","value"]}]})"});
  return s;
}

// What b reads to, checked to write back as b, from the JSON and from its text.
json round_trip(const std::vector<uint8_t>& b, const char* what) {
  size_t used = 0;
  const json j = read_descriptor(b.data(), b.size(), &used);
  std::vector<uint8_t> out, again;
  write_descriptor(j, out);
  write_descriptor(json::parse(j.dump()), again);
  if (used != b.size() || out != b || again != b) {
    std::fprintf(stderr, "%s: not written back as read (%zu of %zu bytes read): %s\n", what, used, b.size(),
                 j.dump().c_str());
    CHECK(false);
  }
  return j;
}

template <class F>
DescriptorError refused(F&& f) {
  try {
    f();
  } catch (const DescriptorError& e) {
    return e;
  }
  std::fprintf(stderr, "not refused\n");
  std::exit(1);
}
template <class F>
void invalid(F&& f) {
  try {
    f();
  } catch (const std::invalid_argument&) {
    return;
  }
  std::fprintf(stderr, "not refused as invalid\n");
  std::exit(1);
}

}  // namespace

TEST(test_descriptor_types) {
  for (const Sample& s : samples()) {
    const json j = round_trip(s.bytes, s.what);
    if (j != json::parse(s.json)) {
      std::fprintf(stderr, "%s: read as %s\n", s.what, j.dump().c_str());
      CHECK(false);
    }
  }
  // -0.0 stays negative; JSON text keeps every double bit for bit.
  const std::vector<Sample> s = samples();
  const json n = read_descriptor(s[1].bytes.data(), s[1].bytes.size());
  CHECK(std::signbit(n["items"][3]["value"].get<double>()));
  std::mt19937_64 r(39);
  for (int i = 0; i < 2000; i++) {
    const uint64_t bits = i < 1000 ? r() : r() >> (r() % 64);
    const std::vector<uint8_t> b = Bytes().head("null", 1).id("Nmbr").tag("doub").u64(bits).b;
    round_trip(b, "a double");
  }

  // What follows a descriptor is not read; a version of 16 before it is.
  std::vector<uint8_t> b = s[0].bytes;
  const size_t size = b.size();
  b.insert(b.end(), {0xde, 0xad});
  size_t used = 0;
  CHECK(read_descriptor(b.data(), b.size(), &used) == json::parse(s[0].json) && used == size);
  std::vector<uint8_t> v = Bytes().u32(16).b;
  v.insert(v.end(), b.begin(), b.end());
  CHECK(read_versioned_descriptor(v.data(), v.size(), &used) == json::parse(s[0].json) && used == size + 4);
  std::vector<uint8_t> out;
  write_versioned_descriptor(json::parse(s[0].json), out);
  CHECK(out == std::vector<uint8_t>(v.begin(), v.end() - 2));
  v[3] = 17;
  const DescriptorError e = refused([&] { read_versioned_descriptor(v.data(), v.size(), nullptr, 100); });
  CHECK(e.kind == DecodeError::Kind::Codec && e.codec == "descriptor_version" && e.offset == 100);
}

TEST(test_descriptor_lookup) {
  // A Gradient Fill ('GdFl') as Photoshop lays one out, read by key name.
  const std::vector<uint8_t> b = Bytes()
                                     .head("null", 4)
                                     .id("Angl").tag("UntF").tag("#Ang").f64(90)
                                     .id("Type").tag("enum").id("GrdT").id("Lnr ")
                                     .id("Rvrs").tag("bool").u8(1)
                                     .id("Grad").tag("Objc").ustr(u"Gradient").id("Grdn").u32(3)
                                     .id("Nm  ").tag("TEXT").ustr(u"Foreground to Background")
                                     .id("Intr").tag("doub").f64(4096)
                                     .id("Clrs").tag("VlLs").u32(2)
                                     .tag("Objc").head("Clrt", 2).id("Lctn").tag("long").u32(0)
                                     .id("Mdpn").tag("long").u32(50)
                                     .tag("Objc").head("Clrt", 2).id("Lctn").tag("long").u32(4096)
                                     .id("Mdpn").tag("long").u32(25)
                                     .b;
  const json d = round_trip(b, "GdFl");
  CHECK(desc::number(d, "Angl") == 90 && desc::at(d, "Angl")["unit"] == "#Ang");
  CHECK(desc::enumerated(d, "Type") == "Lnr " && desc::at(d, "Type")["enumType"] == "GrdT");
  CHECK(desc::boolean(d, "Rvrs"));
  const json& grad = desc::object(d, "Grad");
  CHECK(grad["class"] == "Grdn" && grad["name"] == "Gradient");
  CHECK(desc::text(grad, "Nm  ") == "Foreground to Background" && desc::number(grad, "Intr") == 4096);
  const json& stops = desc::list(grad, "Clrs");
  CHECK(stops.size() == 2 && desc::integer(desc::object(stops[1]), "Lctn") == 4096);
  CHECK(desc::integer(stops[1], "Mdpn") == 25 && desc::number(stops[0], "Mdpn") == 50);
  CHECK(!desc::find(d, "Nope") && desc::find(d, "Rvrs") == &d["items"][2]);

  // Missing keys, other types, no descriptor: DescriptorError naming the key, at no byte.
  const DescriptorError missing = refused([&] { desc::number(d, "Nope"); });
  CHECK(missing.offset == -1 && missing.kind == DecodeError::Kind::Corrupt &&
        std::string(missing.what()).find("'Nope'") != std::string::npos);
  const DescriptorError type = refused([&] { desc::text(d, "Angl"); });
  CHECK(std::string(type.what()) == "descriptor: 'Angl' is a UntF, not TEXT");
  refused([&] { desc::integer(d, "Angl"); });  // a unit float is no integer
  refused([&] { desc::object(d, "Clrs"); });
  refused([&] { desc::find(desc::at(d, "Angl"), "x"); });
  refused([&] { desc::list(stops[0]); });

  // Every getter on the type it reads, by value; text from code units; raw bytes.
  const std::vector<Sample> s = samples();
  const json all = round_trip(s[2].bytes, "TEXT");
  CHECK(desc::text(all, "Txt ") == "Xin ch\u00e0o \U0001F642\r" && desc::text(all, "Nul ") == std::string("a\0b", 3));
  CHECK(desc::text(all, "Open") == "abc" && desc::text(all, "None").empty() && desc::text(all, "Lone") == "\ufffdx");
  const json raw = round_trip(s[6].bytes, "raw");
  CHECK(desc::raw(raw, "EngineData") == std::vector<uint8_t>({'<', '<', ' ', '/', 'a', ' ', '1', ' ', '>', '>'}));
  CHECK(desc::raw(raw, "Empt").empty() && desc::raw(raw, "alis").size() == 5);
  const json numbers = round_trip(s[1].bytes, "numbers");
  CHECK(desc::integer(numbers, "layerID") == int64_t(0xfedcba9876543210) && desc::boolean(numbers, "Odd "));
  CHECK(!desc::boolean(numbers, "Dthr") && std::isnan(desc::number(round_trip(s[9].bytes, "NaN"), "NaN ")));

  // set() changes a value where it is, its key written as before, or adds one last.
  json w = round_trip(s[8].bytes, "IDs");
  desc::set(w, "warp", desc::make_double(2.5));
  desc::set(w, "Strt", desc::make_long(-3));
  std::vector<uint8_t> out;
  write_descriptor(w, out);
  Bytes expect = Bytes().ustr(u"").sid("warp").u32(5).sid("warp").tag("doub").f64(2.5);
  const size_t second = 6 + 8 + 4 + 16;  // the name, the class, the count, then the first item ("warp", a long)
  expect.b.insert(expect.b.end(), s[8].bytes.begin() + second, s[8].bytes.end());
  expect.id("Strt").tag("long").u32(uint32_t(-3));
  CHECK(out == expect.b);
  invalid([&] { json list = desc::make_list({}); desc::set(list, "x", desc::make_bool(true)); });
}

TEST(test_descriptor_make) {
  // Built with make_*(), a descriptor writes as Photoshop's bytes: vibA, then each type, the string IDs of 4 letters
  // with their lengths.
  std::vector<uint8_t> out;
  write_descriptor(desc::make_object("null", {{"vibrance", desc::make_long(25)}, {"Strt", desc::make_long(-8)}}), out);
  CHECK(out == samples()[0].bytes);
  const json d = desc::make_object(
      "warp", {{"warpStyle", desc::make_enum("warpStyle", "warpNone")},
               {"warpValue", desc::make_double(-0.5)},
               {"Angl", desc::make_unit("#Ang", 30)},
               {"Hrzn", desc::make_units("#Pxl", {1, std::nan("")})},
               {"Nm  ", desc::make_text("Gr\u00e2u")},
               {"layerID", desc::make_comp(-2)},
               {"Rvrs", desc::make_bool(false)},
               {"Type", desc::make_class("Lyr ")},
               {"EngineData", desc::make_raw({0, 255, 7})},
               {"Clrs", desc::make_list({desc::make_object("Clrt", {{"Lctn", desc::make_long(7)}})})},
               {"warp", desc::make_enum("time", "hold")}});
  out.clear();
  write_descriptor(d, out);
  const std::vector<uint8_t> expect = Bytes()
                                          .ustr(u"").sid("warp").u32(11)
                                          .sid("warpStyle").tag("enum").sid("warpStyle").sid("warpNone")
                                          .sid("warpValue").tag("doub").f64(-0.5)
                                          .id("Angl").tag("UntF").tag("#Ang").f64(30)
                                          .id("Hrzn").tag("UnFl").tag("#Pxl").u32(2).f64(1).f64(std::nan(""))
                                          .id("Nm  ").tag("TEXT").ustr(u"Gr\u00e2u")
                                          .sid("layerID").tag("comp").u64(uint64_t(-2))
                                          .id("Rvrs").tag("bool").u8(0)
                                          .id("Type").tag("type").ustr(u"").id("Lyr ")
                                          .sid("EngineData").tag("tdta").u32(3).raw(std::string("\0\xff\x07", 3))
                                          .id("Clrs").tag("VlLs").u32(1).tag("Objc").head("Clrt", 1)
                                          .id("Lctn").tag("long").u32(7)
                                          .sid("warp").tag("enum").sid("time").sid("hold")
                                          .b;
  CHECK(out == expect);
  round_trip(expect, "made");
}

TEST(test_descriptor_damaged) {
  const std::vector<Sample> s = samples();
  // Every cut refused, at a byte within it.
  for (const Sample& x : s)
    for (size_t n = 0; n < x.bytes.size(); n++) {
      const DescriptorError e = refused([&] { read_descriptor(x.bytes.data(), n, nullptr, 1000); });
      CHECK(e.kind == DecodeError::Kind::Corrupt && e.offset >= 1000 && e.offset <= int64_t(1000 + n));
    }
  // Bytes changed at random, cut or not: refused at a byte within them, or read and written back as the bytes read.
  std::mt19937 r(39);
  int read = 0, refusals = 0;
  for (int i = 0; i < 20000; i++) {
    std::vector<uint8_t> b = s[r() % s.size()].bytes;
    for (int k = 1 + int(r() % 4); k > 0; k--) {
      const size_t at = r() % b.size();
      b[at] = r() % 4 ? uint8_t(r()) : b[at] ^ uint8_t(1 << r() % 8);
    }
    if (r() % 4 == 0) b.resize(r() % b.size());
    size_t used = 0;
    try {
      const json j = read_descriptor(b.data(), b.size(), &used, 7);
      std::vector<uint8_t> out;
      write_descriptor(j, out);
      CHECK(out.size() == used && std::equal(out.begin(), out.end(), b.begin()));
      read++;
    } catch (const DescriptorError& e) {
      CHECK(e.offset >= 7 && e.offset <= int64_t(7 + b.size()));
      refusals++;
    }
  }
  std::printf("descriptors changed at random: %d read back, %d refused\n", read, refusals);
  CHECK(read > 1000 && refusals > 1000);

  // Counts and lengths past the bytes: refused at them, before allocating.
  const auto at = [](const std::vector<uint8_t>& b) {
    const DescriptorError e = refused([&] { read_descriptor(b.data(), b.size()); });
    CHECK(e.kind == DecodeError::Kind::Corrupt);
    return e.offset;
  };
  // (A descriptor's head is 18 bytes: the name 6, the class 8, the count 4; an item's key 8, its type 4.)
  CHECK(at(Bytes().head("null", 0xffffffff).b) == 14);
  CHECK(at(Bytes().head("null", 1).id("Txt ").tag("TEXT").u32(0x7fffffff).b) == 30);
  CHECK(at(Bytes().head("null", 1).id("Clrs").tag("VlLs").u32(0xffffffff).b) == 30);
  CHECK(at(Bytes().head("null", 1).id("Hrzn").tag("UnFl").tag("#Pxl").u32(0x20000000).b) == 34);
  CHECK(at(Bytes().head("null", 1).id("Data").tag("tdta").u32(0xfffffff0).raw("abc").b) == 30);
  CHECK(at(Bytes().head("null", 1).id("null").tag("obj ").u32(0x40000000).b) == 30);
  CHECK(at(Bytes().head("null", 1).u32(0x7fffffff).raw("key and more").b) == 18);
  CHECK(at(Bytes().head("null", 1).u32(0x7fffffff).raw("key").b) == 14);  // 7 bytes hold no item
  CHECK(at(Bytes().head("null", 1).id("What").tag("what").u32(0).b) == 26);
  CHECK(at(Bytes().head("null", 1).id("null").tag("obj ").u32(1).tag("what").u32(0).b) == 34);
  CHECK(at(Bytes().ustr(u"", false).u32(0x7fffffff).b) == 4);

  // Nesting: 64 descriptors in each other read; 65 do not.
  const auto nested = [](int levels) {
    Bytes b;
    for (int i = 0; i < levels; i++) (i ? b.id("Nest").tag("Objc") : b).head("null", i + 1 < levels ? 1 : 0);
    return b.b;
  };
  round_trip(nested(kMaxDescriptorDepth), "deepest");
  const std::vector<uint8_t> deep = nested(kMaxDescriptorDepth + 1);
  CHECK(refused([&] { read_descriptor(deep.data(), deep.size()); }).kind == DecodeError::Kind::Corrupt);
  json j = json::parse(R"({"class":"null","items":[]})");
  for (int i = 0; i < 70; i++) j = desc::make_object("null", {{"Nest", j}});
  std::vector<uint8_t> out;
  invalid([&] { write_descriptor(j, out); });
  // Past kMaxDescriptorValues values: TooBig, at the count, before reading them.
  Bytes many;
  many.head("null", 1).id("Many").tag("VlLs").u32(uint32_t(kMaxDescriptorValues));
  for (size_t i = 0; i < kMaxDescriptorValues; i++) many.tag("bool").u8(1);
  const DescriptorError big = refused([&] { read_descriptor(many.b.data(), many.b.size()); });
  CHECK(big.kind == DecodeError::Kind::TooBig && big.offset == 30);
}

TEST(test_descriptor_invalid_json) {
  // JSON not of descriptor.h's form: std::invalid_argument, whatever it holds.
  const auto write = [](const char* text) {
    std::vector<uint8_t> out;
    write_descriptor(json::parse(text), out);
  };
  write(R"({"class":"null","items":[]})");
  for (const char* bad : {
           R"([])",
           R"({"items":[]})",
           R"({"class":"null"})",
           R"({"class":"null","items":{}})",
           R"({"class":"","items":[]})",
           R"({"class":"\\x4","items":[]})",
           R"({"class":"null","name":7,"items":[]})",
           R"({"class":"null","name":[65536],"items":[]})",
           R"({"class":"null","items":[{"type":"long","value":1}]})",
           R"({"class":"null","items":[{"key":"Strt","type":"lng ","value":1}]})",
           R"({"class":"null","items":[{"key":"Strt","type":"long","value":2147483648}]})",
           R"({"class":"null","items":[{"key":"Strt","type":"long","value":1.5}]})",
           R"({"class":"null","items":[{"key":"Strt","type":"long"}]})",
           R"({"class":"null","items":[{"key":"Strt","type":"comp","value":"1"}]})",
           R"({"class":"null","items":[{"key":"Strt","type":"bool","value":256}]})",
           R"({"class":"null","items":[{"key":"Strt","type":"doub","value":"7ff8"}]})",
           R"({"class":"null","items":[{"key":"Strt","type":"UntF","unit":"#Ang5","value":1}]})",
           R"({"class":"null","items":[{"key":"Strt","type":"UnFl","unit":"#Ang","value":1}]})",
           R"({"class":"null","items":[{"key":"Strt","type":"TEXT","value":null}]})",
           R"({"class":"null","items":[{"key":"Strt","type":"enum","enumType":"BlnM"}]})",
           R"({"class":"null","items":[{"key":"Strt","type":"tdta","value":"abc"}]})",
           R"({"class":"null","items":[{"key":"Strt","type":"tdta","value":"a=bc"}]})",
           R"({"class":"null","items":[{"key":"Strt","type":"VlLs","value":[{"value":1}]}]})",
           R"({"class":"null","items":[{"key":"Strt","type":"obj ","value":[{"form":"Idnt"}]}]})",
           R"({"class":"null","items":[{"key":"Strt","type":"obj ","value":[{"form":"what","value":1}]}]})",
           R"({"class":"null","items":[{"key":"Strt","type":"ObAr","count":-1,"class":"null","items":[]}]})",
       })
    invalid([&] { write(bad); });
  invalid([] {
    std::vector<uint8_t> out;
    write_descriptor(desc::make_object("null", {{"Txt ", desc::make_text("\xff")}}), out);  // no UTF-8
  });
}

// ---- Photoshop's own descriptors ----

namespace {

// Where a PSD keeps descriptors: Photoshop's PSDs walked by their layout (header, image resources, layer records,
// tagged blocks, Lr16 and Lr32 layers, shmd metadata), each descriptor found as the offset of its version (16).
struct Psd {
  const std::vector<uint8_t>& f;
  bool psb = false;
  struct Found {
    std::string where, label;  // for messages, for counts by block
    size_t at, end;            // the version's offset, the block's end
    bool sure;                 // a block that holds one here, as opposed to one that looks like it
  };
  std::vector<Found> found;

  bool has(size_t at, size_t n) const { return at <= f.size() && n <= f.size() - at; }
  uint32_t be16(size_t at) const { return has(at, 2) ? uint32_t(f[at]) << 8 | f[at + 1] : 0; }
  uint32_t be32(size_t at) const { return be16(at) << 16 | be16(at + 2); }
  uint64_t be64(size_t at) const { return uint64_t(be32(at)) << 32 | be32(at + 4); }
  uint64_t length(size_t& at, bool wide) {
    const uint64_t n = wide ? be64(at) : be32(at);
    at += wide ? 8 : 4;
    return n;
  }
  void add(const std::string& where, const std::string& label, size_t at, size_t end, bool sure) {
    if (at < end && end <= f.size() && be32(at) == 16) found.push_back({where, label, at, end, sure});
  }

  void walk() {
    if (!has(0, 26) || std::memcmp(f.data(), "8BPS", 4)) return;
    psb = be16(4) == 2;
    size_t at = 26;
    at += 4 + be32(at);  // colour mode data
    const size_t resources = be32(at);
    image_resources(at + 4, std::min(f.size(), at + 4 + resources));
    at += 4 + resources;
    const uint64_t info = length(at, psb);
    const size_t end = size_t(std::min<uint64_t>(f.size(), at + info));
    const uint64_t layer_info = length(at, psb);
    layers(at, size_t(std::min<uint64_t>(end, at + layer_info)), "");
    at += size_t(layer_info);
    if (!has(at, 4)) return;
    at += 4 + be32(at);  // global layer mask
    blocks(at, end, "document");
  }

  void image_resources(size_t at, size_t end) {
    // Image resources Photoshop keeps a descriptor in (the specification's table), their data starting with 16.
    static const uint16_t kIds[] = {1065, 1074, 1075, 1076, 1078, 1080, 1082, 1083, 1088, 3000};
    while (at + 12 <= end && !std::memcmp(&f[at], "8BIM", 4)) {
      const uint32_t id = be16(at + 4);
      size_t p = at + 6 + ((f[at + 6] + 2) & ~size_t(1));  // a Pascal name, padded to even
      const size_t size = be32(p);
      p += 4;
      if (p + size > end) return;
      const std::string where = "resource " + std::to_string(id);
      if (std::find(std::begin(kIds), std::end(kIds), id) != std::end(kIds)) add(where, where, p, p + size, true);
      if (id == 1050 && be32(p) >= 7) add(where, where, p + 4, p + size, true);  // slices, from version 7
      at = p + size + (size & 1);
    }
  }

  void layers(size_t at, size_t end, const std::string& in) {
    if (!has(at, 2) || at >= end) return;
    const int count = std::abs(int16_t(be16(at)));
    at += 2;
    for (int i = 0; i < count && has(at, 18); i++) {
      at += 16;
      const size_t channels = be16(at);
      at += 2 + channels * (psb ? 10 : 6) + 12;
      const size_t extra = be32(at);
      at += 4;
      const size_t extra_end = std::min(end, at + extra);
      at += 4 + be32(at);  // mask
      at += 4 + be32(at);  // blending ranges
      if (!has(at, 1)) return;
      at += (f[at] + 4) & ~size_t(3);  // a Pascal name, padded to 4
      blocks(at, extra_end, in + "layer " + std::to_string(i));
      at = extra_end;
    }
  }

  void blocks(size_t at, size_t end, const std::string& in) {
    // Keys whose length is 8 bytes in a PSB (the specification's list).
    static const char* const kWide[] = {"LMsk", "Lr16", "Lr32", "Layr", "Mt16", "Mt32", "Mtrn",
                                        "Alph", "FMsk", "lnk2", "FEid", "FXid", "PxSD"};
    while (at + 12 <= end) {
      if (std::memcmp(&f[at], "8BIM", 4) && std::memcmp(&f[at], "8B64", 4)) {
        at++;  // padding
        continue;
      }
      const std::string key(&f[at + 4], &f[at + 8]);
      at += 8;
      const bool wide = psb && std::find_if(std::begin(kWide), std::end(kWide), [&](const char* k) {
                                 return key == k;
                               }) != std::end(kWide);
      const uint64_t n = length(at, wide);
      if (n > end - at) return;
      block(key, at, at + size_t(n), in + " '" + key + "'");
      at += size_t(n);
    }
  }

  void block(const std::string& key, size_t p, size_t end, const std::string& where) {
    if (key == "Lr16" || key == "Lr32" || key == "Layr") return layers(p, end, where + ": ");
    // Blocks Photoshop keeps one in, and where its version starts in them (seen in Photoshop's PSDs): after a version
    // of their own ('clrL' 2 bytes, 'lfx2', 'lfxs', 'lmfx', 'vogk', 'CAI ' 4), a key ('vscg'), or 'soLD' and a version.
    static const std::pair<const char*, size_t> kAt[] = {
        {"vibA", 0}, {"blwh", 0}, {"CgEd", 0}, {"SoCo", 0}, {"GdFl", 0}, {"PtFl", 0}, {"vstk", 0}, {"artb", 0},
        {"artd", 0}, {"cinf", 0}, {"extn", 0}, {"GenI", 0}, {"OCIO", 0}, {"extd", 0}, {"pths", 0}, {"clrL", 2},
        {"lfx2", 4}, {"lfxs", 4}, {"lmfx", 4}, {"vogk", 4}, {"vscg", 4}, {"CAI ", 4}, {"SoLd", 8}, {"SoLE", 8}};
    const std::string label = "'" + key + "'";
    for (const auto& [k, skip] : kAt)
      if (key == k) return add(where, label, p + skip, end, true);
    if (key == "TySh") {  // a version, a transform, a text version, the text, a warp version, the warp
      if (p + 56 > end) return;
      add(where + " text", label + " text", p + 52, end, true);
      size_t used = 0;
      try {
        read_versioned_descriptor(&f[p + 52], end - (p + 52), &used);
      } catch (const DescriptorError&) {
        return;
      }
      return add(where + " warp", label + " warp", p + 52 + used + 2, end, true);
    }
    if (key == "shmd") {  // metadata: a count, then items of a key, a flag, padding, a length and data
      size_t at = p + 4;
      for (uint32_t i = be32(p); i > 0 && at + 16 <= end; i--) {
        const std::string item(&f[at + 4], &f[at + 8]);
        const size_t n = be32(at + 12);
        if (n > end - (at + 16)) return;
        add(where + " '" + item + "'", label + " '" + item + "'", at + 16, at + 16 + n, false);
        at += 16 + n;
      }
      return;
    }
    // Blocks not known to hold one: tried after a version of 2 or 4 bytes, or none.
    for (const size_t skip : {0, 2, 4})
      if (p + skip + 4 <= end && be32(p + skip) == 16) return add(where, label, p + skip, end, false);
  }
};

// What in j takes descriptor.h's rarer forms: 4-byte IDs with a length, escaped bytes, code units, odd bools, NaNs.
void notes(const json& j, std::map<std::string, int>& seen) {
  if (j.is_object()) {
    if (const auto ids = j.find("stringIDs"); ids != j.end())
      for (const json& m : *ids) seen["string ID '" + j[m.get<std::string>()].get<std::string>() + "'"]++;
    for (const char* k : {"key", "class", "enumType", "unit"}) {
      const auto id = j.find(k);
      if (id != j.end() && id->is_string() && id->get<std::string>().find('\\') != std::string::npos)
        seen["an ID with escapes"]++;
    }
    const auto type = j.find("type"), value = j.find("value"), name = j.find("name");
    if (name != j.end() && name->is_array()) seen["a name as code units"]++;
    if (type != j.end()) {
      for (const char* t : {"UnFl", "ObAr", "Pth ", "alis", "obj ", "comp", "GlbO", "GlbC"})
        if (*type == t) seen["a value of type '" + std::string(t) + "'"]++;
      if (value != j.end() && *type == "TEXT" && value->is_array()) seen["a TEXT as code units"]++;
      if (value != j.end() && *type == "bool" && value->is_number()) seen["a bool of another byte"]++;
      if (value != j.end() && (*type == "doub" || *type == "UntF") && value->is_string()) seen["a NaN or infinity"]++;
    }
  }
  if (j.is_structured())
    for (const json& v : j) notes(v, seen);
}

}  // namespace

TEST(test_descriptor_psd) {
  std::vector<std::filesystem::path> dirs = {PB_SOURCE_DIR "/tests/golden/psd"};
  if (const char* more = std::getenv("PB_PSD_DIR")) dirs.emplace_back(more);
  int files = 0, descriptors = 0, unsure = 0;
  std::map<std::string, int> keys, rare;
  for (const auto& dir : dirs) {
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
      std::string ext = entry.path().extension().string();
      std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
      if (ext != ".psd" && ext != ".psb") continue;
      std::ifstream in(entry.path(), std::ios::binary);
      const std::vector<uint8_t> f((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      Psd psd{f};
      psd.walk();
      files++;
      for (const Psd::Found& d : psd.found) {
        json j;
        size_t used = 0;
        try {
          j = read_versioned_descriptor(&f[d.at], d.end - d.at, &used, d.at);
        } catch (const DescriptorError& e) {
          if (!d.sure) {  // not a descriptor after all
            unsure++;
            continue;
          }
          std::fprintf(stderr, "%s, %s: %s\n", entry.path().filename().string().c_str(), d.where.c_str(), e.what());
          CHECK(false);
        }
        std::vector<uint8_t> out;
        write_versioned_descriptor(j, out);
        if (out.size() != used || !std::equal(out.begin(), out.end(), f.begin() + std::ptrdiff_t(d.at))) {
          std::fprintf(stderr, "%s, %s: not written back as read\n", entry.path().filename().string().c_str(),
                       d.where.c_str());
          CHECK(false);
        }
        descriptors++;
        keys[d.label]++;
        notes(j, rare);
      }
    }
  }
  if (!files) {
    std::printf("skipping Photoshop's descriptors: no PSD in tests/golden/psd (E33) or $PB_PSD_DIR\n");
    return;
  }
  std::printf("Photoshop's descriptors: %d in %d files read and written back byte for byte (%d lookalikes not)\n",
              descriptors, files, unsure);
  std::string line;
  for (const auto& [k, n] : keys) line += ", " + k + " " + std::to_string(n);
  std::printf(" by block:%s\n", line.c_str() + 1);
  line.clear();
  for (const auto& [k, n] : rare) line += ", " + k + " " + std::to_string(n);
  std::printf(" rarer forms:%s\n", line.empty() ? " none" : line.c_str() + 1);
  CHECK(descriptors > 0);
}
