#pragma once
// Photoshop's action descriptors (M39): the typed, keyed trees a PSD keeps adjustments ('vibA', 'blwh', 'clrL',
// 'CgEd'), layer styles ('lfx2'), smart objects ('SoLd'), fill layers ('SoCo', 'GdFl'), type ('TySh') and more in,
// and an .atn its steps in; read into JSON and written back byte for byte. The layout is the Descriptor structure
// of Adobe's Photoshop File Formats Specification, integers big-endian; 'UnFl', 'ObAr', 'Pth ' and the Identifier
// and Index references, which it leaves out, as Photoshop writes them (build/orch/api/m39.md says what is unsure).
//
// The JSON (nlohmann::json; members of an object in any order, arrays in the file's order):
//
//   descriptor  {"class": ID, "name": USTR, "items": [item, ...]}: "name" (its "name from classID") left out when
//               empty, as Photoshop writes it. A descriptor is also the value of an "Objc" or "GlbO" item.
//   item        a value with "key": ID. Keys keep the file's order, and repeat if the file repeats them.
//   value       {"type": T, ...}, T the item's OSType, then by T:
//     "Objc", "GlbO"   a descriptor's members, "class", "name", "items"
//     "ObAr"           "count": the uint32 before them, then a descriptor's members (Photoshop: n objects of
//                      "class", each of its keys an "UnFl" holding the n objects' values)
//     "VlLs"           "value": [value, ...]  (values without "key", of any types)
//     "doub"           "value": NUM
//     "UntF"           "unit": ID4, "value": NUM  ("#Ang" degrees, "#Prc" percent, "#Pxl" pixels, "#Pnt", "#Mlm",
//                      "#Rsl", "#Rlt", "#Nne")
//     "UnFl"           "unit": ID4, "value": [NUM, ...]
//     "TEXT"           "value": USTR
//     "enum"           "enumType": ID, "value": ID  (as "BlnM", "Nrml")
//     "long"           "value": int32
//     "comp"           "value": int64
//     "bool"           "value": true | false  (a byte other than 0 and 1 as that number)
//     "type", "GlbC"   "class": ID, "name": USTR (left out when empty)
//     "obj "           "value": [reference, ...]
//     "alis", "tdta", "Pth "   "value": the bytes after the length, base64 (RFC 4648, with '=')
//   reference   {"form": F, ...} in an "obj ", by F: "Clss" "class", "name"; "prop" those and "key": ID; "Enmr"
//               those and "enumType": ID, "value": ID; "rele" those and "value": int32; "name" those and
//               "value": USTR; "Idnt", "indx" "value": int32 alone.
//   ID          a key, class, enum type or enum value: its bytes as a string, those outside printable ASCII
//               (0x20 to 0x7E) and the backslash as "\xHH" (lower-case hex digits). Written as 4 bytes after a
//               length of 0 when it has 4 bytes (Photoshop's char IDs, 'Strt'), else as its length and bytes
//               (string IDs, "vibrance"), unless its object's "stringIDs" names its member ("key", "class",
//               "enumType" or "value"): then as its length and bytes whatever its size, as Photoshop writes
//               "warp". make_*() below put those string IDs Photoshop has with 4 letters in "stringIDs".
//   ID4         a unit: always 4 bytes, no length; as an ID.
//   USTR        a Unicode string: its UTF-16 text in UTF-8, before the null that ends it (Photoshop counts that
//               null in its length); one not so ended, or not valid UTF-16 (a lone surrogate), as the array of its
//               UTF-16 code units, its last null among them if it has one.
//   NUM         a JSON number; a NaN or infinity as the 16 hex digits of its 8 bytes ("7ff8000000000000").
//
// For example, a Vibrance adjustment's ('vibA') descriptor:
//   {"class":"null","items":[{"key":"vibrance","type":"long","value":25},{"key":"Strt","type":"long","value":-8}]}
//
// Reading bytes nobody vouches for (a file's): every length and count is checked against the bytes left before
// anything is allocated for it, nesting stops at kMaxDescriptorDepth and the values at kMaxDescriptorValues, so a
// damaged or hostile descriptor throws DescriptorError, never crashes, hangs or eats memory. What it accepts writes
// back as the same bytes.
#include <cstddef>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "open.h"

namespace pb {

constexpr int kMaxDescriptorDepth = 64;            // descriptors and lists in each other (Photoshop's: about 8)
constexpr size_t kMaxDescriptorValues = 1u << 20;  // items, list values, references, UnFl's numbers, in one read

// Why a descriptor does not read: what() "descriptor: <reason>[ at byte <offset>]". Kind Corrupt for bytes cut short
// or damaged and for a lookup below that finds no such key or another type, TooBig past kMaxDescriptorValues, Codec
// ("descriptor_version") for a version other than 16.
struct DescriptorError : DecodeError {
  int64_t offset;  // of the field it failed at, counted from the read's `base`; -1 for a lookup in JSON
  DescriptorError(Kind kind, std::string codec, const std::string& reason, int64_t offset);
};

// The descriptor in data[0, size): its JSON, as above. `used` gets the bytes it took (a descriptor carries no
// length: what follows it is the caller's). `base` is data[0]'s offset in the file, for errors. Throws
// DescriptorError. Any thread.
nlohmann::json read_descriptor(const uint8_t* data, size_t size, size_t* used = nullptr, uint64_t base = 0);
// The same after its 4-byte version, which must be 16: how most PSD blocks hold one ('lfx2' after a version of its
// own, 'TySh' two of them among other fields).
nlohmann::json read_versioned_descriptor(const uint8_t* data, size_t size, size_t* used = nullptr, uint64_t base = 0);

// Appends d's bytes, a descriptor as above ("type" and "key" ignored, so an Objc value writes as one), to `out`:
// what read_descriptor() read, byte for byte. Throws std::invalid_argument for JSON not of the form above, out of
// range, or nesting past kMaxDescriptorDepth (`out` then holds part of it).
void write_descriptor(const nlohmann::json& d, std::vector<uint8_t>& out);
// The same after a version of 16.
void write_versioned_descriptor(const nlohmann::json& d, std::vector<uint8_t>& out);

namespace desc {

// d is a descriptor: the top level, or an "Objc", "GlbO" or "ObAr" value. The lookups throw DescriptorError (offset
// -1), naming the key, when d is no descriptor, has no such key, or holds it as another type.
const nlohmann::json* find(const nlohmann::json& d, std::string_view key);  // the item, the first if repeated; null
const nlohmann::json& at(const nlohmann::json& d, std::string_view key);    // the item

// A value's contents, of the types each lists.
double number(const nlohmann::json& v);                 // long, comp, doub, UntF (in its unit: v["unit"])
int64_t integer(const nlohmann::json& v);               // long, comp
bool boolean(const nlohmann::json& v);                  // bool (true for any byte but 0)
std::string text(const nlohmann::json& v);              // TEXT, in UTF-8 (U+FFFD for a lone surrogate)
std::string enumerated(const nlohmann::json& v);        // enum: its value (its type is v["enumType"])
const nlohmann::json& list(const nlohmann::json& v);    // VlLs: the array of its values
const nlohmann::json& object(const nlohmann::json& v);  // Objc, GlbO: v, a descriptor
std::vector<uint8_t> raw(const nlohmann::json& v);      // tdta, alis, Pth : the bytes

// The same of d's item `key`: desc::number(d, "Strt").
double number(const nlohmann::json& d, std::string_view key);
int64_t integer(const nlohmann::json& d, std::string_view key);
bool boolean(const nlohmann::json& d, std::string_view key);
std::string text(const nlohmann::json& d, std::string_view key);
std::string enumerated(const nlohmann::json& d, std::string_view key);
const nlohmann::json& list(const nlohmann::json& d, std::string_view key);
const nlohmann::json& object(const nlohmann::json& d, std::string_view key);
std::vector<uint8_t> raw(const nlohmann::json& d, std::string_view key);

// Gives d's item `key` the value v, where the item was (its key written as before), or as a new last item:
// changing a descriptor read from a file and leaving the rest as it was. Throws std::invalid_argument when d is no
// descriptor.
void set(nlohmann::json& d, std::string_view key, nlohmann::json v);

// Values to build descriptors with, for writing new ones. An ID among Photoshop's 4-letter string IDs ("warp",
// "time", "list", "Comp", "view", "json", "hold") goes in "stringIDs".
nlohmann::json make_object(std::string_view cls, std::vector<std::pair<std::string, nlohmann::json>> items);
nlohmann::json make_list(std::vector<nlohmann::json> values);
nlohmann::json make_long(int32_t v);
nlohmann::json make_comp(int64_t v);
nlohmann::json make_double(double v);
nlohmann::json make_unit(std::string_view unit, double v);
nlohmann::json make_units(std::string_view unit, const std::vector<double>& v);
nlohmann::json make_text(std::string_view utf8);
nlohmann::json make_enum(std::string_view type, std::string_view value);
nlohmann::json make_bool(bool v);
nlohmann::json make_class(std::string_view cls);             // a "type"
nlohmann::json make_raw(const std::vector<uint8_t>& bytes);  // a "tdta"

}  // namespace desc
}  // namespace pb
