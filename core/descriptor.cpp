// Photoshop's action descriptors (descriptor.h): bytes <-> JSON, and lookups in the JSON.
#include "descriptor.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <stdexcept>

#include "profile.h"

namespace pb {

using nlohmann::json;

DescriptorError::DescriptorError(Kind kind, std::string codec, const std::string& reason, int64_t offset)
    : DecodeError(kind, std::move(codec),
                  "descriptor: " + reason + (offset >= 0 ? " at byte " + std::to_string(offset) : std::string())),
      offset(offset) {}

namespace {

constexpr uint32_t tag(const char (&s)[5]) {
  return uint32_t(uint8_t(s[0])) << 24 | uint32_t(uint8_t(s[1])) << 16 | uint32_t(uint8_t(s[2])) << 8 |
         uint32_t(uint8_t(s[3]));
}

// The value types, and the reference forms, by OSType.
constexpr const char* kTypes[] = {"obj ", "Objc", "VlLs", "doub", "UntF", "UnFl", "TEXT", "enum", "long",
                                  "comp", "bool", "GlbO", "type", "GlbC", "alis", "tdta", "ObAr", "Pth "};
constexpr const char* kForms[] = {"prop", "Clss", "Enmr", "rele", "Idnt", "indx", "name"};
// Least bytes of an item (a key of 1 byte after its length, a type, a bool), of a list's value, of a reference.
constexpr size_t kMinItem = 10, kMinValue = 5, kMinReference = 8;
// Photoshop's string IDs of 4 letters, written with their length: seen in its PSDs ('warp' of type and smart objects,
// 'time' of the timeline, 'list' and 'Comp' of layer comps, 'view' of OCIO, 'json' of generator settings), and
// 'hold' of ag-psd's list (build/orch/api/m39.md).
constexpr std::string_view kStringIds4[] = {"warp", "time", "list", "Comp", "view", "json", "hold"};

uint32_t be32(const uint8_t* p) {
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | uint32_t(p[3]);
}
uint64_t be64(const uint8_t* p) { return uint64_t(be32(p)) << 32 | be32(p + 4); }

const char kHex[] = "0123456789abcdef";

// An ID's bytes as its JSON string: printable ASCII as is, the rest and '\' as \xHH.
std::string id_text(const uint8_t* p, size_t n) {
  std::string s;
  s.reserve(n);
  for (size_t i = 0; i < n; i++) {
    const uint8_t c = p[i];
    if (c >= 0x20 && c <= 0x7e && c != '\\') {
      s += char(c);
    } else {
      s += "\\x";
      s += kHex[c >> 4];
      s += kHex[c & 15];
    }
  }
  return s;
}
int hex_digit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}
// The bytes an ID's JSON string stands for; false for a broken \x escape.
bool id_bytes(std::string_view s, std::string& out) {
  out.clear();
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] != '\\') {
      out += s[i];
      continue;
    }
    if (s.size() - i < 4 || s[i + 1] != 'x' || hex_digit(s[i + 2]) < 0 || hex_digit(s[i + 3]) < 0) return false;
    out += char(hex_digit(s[i + 2]) << 4 | hex_digit(s[i + 3]));
    i += 3;
  }
  return true;
}

// UTF-16 code units, big-endian, as UTF-8; false where a surrogate is unpaired.
bool utf8_of(const uint8_t* p, size_t units, std::string& out) {
  out.clear();
  out.reserve(units);
  for (size_t i = 0; i < units; i++) {
    uint32_t c = uint32_t(p[2 * i]) << 8 | p[2 * i + 1];
    if (c >= 0xdc00 && c <= 0xdfff) return false;
    if (c >= 0xd800 && c <= 0xdbff) {
      const uint32_t d = i + 1 < units ? uint32_t(p[2 * i + 2]) << 8 | p[2 * i + 3] : 0;
      if (d < 0xdc00 || d > 0xdfff) return false;
      c = 0x10000 + ((c - 0xd800) << 10) + (d - 0xdc00);
      i++;
    }
    if (c < 0x80) {
      out += char(c);
    } else if (c < 0x800) {
      out += char(0xc0 | c >> 6), out += char(0x80 | (c & 0x3f));
    } else if (c < 0x10000) {
      out += char(0xe0 | c >> 12), out += char(0x80 | (c >> 6 & 0x3f)), out += char(0x80 | (c & 0x3f));
    } else {
      out += char(0xf0 | c >> 18), out += char(0x80 | (c >> 12 & 0x3f)), out += char(0x80 | (c >> 6 & 0x3f)),
          out += char(0x80 | (c & 0x3f));
    }
  }
  return true;
}
// UTF-8 as UTF-16 code units; false for bytes that are not UTF-8 (overlong, a surrogate, past U+10FFFF, cut short).
bool utf16_of(std::string_view s, std::vector<uint16_t>& out) {
  out.clear();
  for (size_t i = 0; i < s.size();) {
    const uint8_t b = uint8_t(s[i]);
    const int n = b < 0x80 ? 1 : (b & 0xe0) == 0xc0 ? 2 : (b & 0xf0) == 0xe0 ? 3 : (b & 0xf8) == 0xf0 ? 4 : 0;
    if (!n || s.size() - i < size_t(n)) return false;
    uint32_t c = n == 1 ? b : n == 2 ? b & 0x1f : n == 3 ? b & 0x0f : b & 0x07;
    for (int k = 1; k < n; k++) {
      const uint8_t t = uint8_t(s[i + size_t(k)]);
      if ((t & 0xc0) != 0x80) return false;
      c = c << 6 | (t & 0x3f);
    }
    constexpr uint32_t kLeast[] = {0, 0, 0x80, 0x800, 0x10000};
    if (c < kLeast[n] || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) return false;
    if (c < 0x10000) {
      out.push_back(uint16_t(c));
    } else {
      out.push_back(uint16_t(0xd800 + ((c - 0x10000) >> 10)));
      out.push_back(uint16_t(0xdc00 + ((c - 0x10000) & 0x3ff)));
    }
    i += size_t(n);
  }
  return true;
}

const char kBase64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64(const uint8_t* p, size_t n) {
  std::string s;
  s.reserve((n + 2) / 3 * 4);
  for (size_t i = 0; i < n; i += 3) {
    const uint32_t v = uint32_t(p[i]) << 16 | (i + 1 < n ? uint32_t(p[i + 1]) << 8 : 0) | (i + 2 < n ? p[i + 2] : 0);
    s += kBase64[v >> 18];
    s += kBase64[v >> 12 & 63];
    s += i + 1 < n ? kBase64[v >> 6 & 63] : '=';
    s += i + 2 < n ? kBase64[v & 63] : '=';
  }
  return s;
}
// false for text that is not base64 with its '=' padding
bool unbase64(std::string_view s, std::vector<uint8_t>& out) {
  out.clear();
  if (s.size() % 4) return false;
  out.reserve(s.size() / 4 * 3);
  for (size_t i = 0; i < s.size(); i += 4) {
    uint32_t v = 0;
    int pad = 0;
    for (size_t k = 0; k < 4; k++) {
      const char c = s[i + k];
      const char* at = c ? std::strchr(kBase64, c) : nullptr;
      if (c == '=' && i + 4 == s.size() && k >= 2) {
        pad++;
      } else if (!at || pad) {
        return false;
      }
      v = v << 6 | (at ? uint32_t(at - kBase64) : 0);
    }
    out.push_back(uint8_t(v >> 16));
    if (pad < 2) out.push_back(uint8_t(v >> 8));
    if (pad < 1) out.push_back(uint8_t(v));
  }
  return true;
}

// A double's JSON: the number, or for a NaN or an infinity (no JSON number) the hex digits of its bytes.
json number_json(uint64_t bits) {
  double d;
  std::memcpy(&d, &bits, 8);
  if (std::isfinite(d)) return d;
  std::string s(16, '0');
  for (int i = 0; i < 16; i++) s[size_t(i)] = kHex[bits >> (60 - 4 * i) & 15];
  return s;
}
json number_json(double d) {
  uint64_t bits;
  std::memcpy(&bits, &d, 8);
  return number_json(bits);
}
// A number's bytes back from number_json(), or any JSON number; false for another value.
bool number_bits(const json& j, uint64_t& bits) {
  if (j.is_number()) {
    const double d = j.get<double>();
    std::memcpy(&bits, &d, 8);
    return true;
  }
  if (!j.is_string() || j.get_ref<const std::string&>().size() != 16) return false;
  bits = 0;
  for (const char c : j.get_ref<const std::string&>()) {
    const int h = hex_digit(c);
    if (h < 0) return false;
    bits = bits << 4 | uint64_t(h);
  }
  return true;
}

const json* member(const json& o, const char* key) {
  if (!o.is_object()) return nullptr;
  const auto it = o.find(key);
  return it == o.end() ? nullptr : &*it;
}
// Whether o's "stringIDs" names `key`: its ID written with its length whatever its size.
bool listed(const json& o, const char* key) {
  const json* s = member(o, "stringIDs");
  return s && s->is_array() && std::find(s->begin(), s->end(), key) != s->end();
}
bool string_id4(std::string_view id) {
  return std::find(std::begin(kStringIds4), std::end(kStringIds4), id) != std::end(kStringIds4);
}

// ---- Reading ----

struct Reader {
  const uint8_t* data;
  size_t size;
  uint64_t base;
  size_t at = 0;
  size_t values = 0;

  [[noreturn]] void fail(size_t where, const std::string& why) const {
    throw DescriptorError(DecodeError::Kind::Corrupt, {}, why, int64_t(base + where));
  }
  size_t left() const { return size - at; }
  // n bytes, refused at `where` (the length that gave n) when past the end
  const uint8_t* take(size_t n, const char* what, size_t where) {
    if (n > left())
      fail(where, std::string(what) + " of " + std::to_string(n) + " bytes, past the end (" + std::to_string(left()) +
                      " left)");
    const uint8_t* p = data + at;
    at += n;
    return p;
  }
  const uint8_t* take(size_t n, const char* what) { return take(n, what, at); }
  uint32_t u32(const char* what) { return be32(take(4, what)); }
  // n more values, against kMaxDescriptorValues
  void count(size_t n, size_t where) {
    if (n > kMaxDescriptorValues - values)
      throw DescriptorError(DecodeError::Kind::TooBig, {},
                            "more than " + std::to_string(kMaxDescriptorValues) + " values", int64_t(base + where));
    values += n;
  }
  // A count of n values of at least `least` bytes each, that the bytes left must hold and the values left allow
  // (each counted as it is read).
  uint32_t many(size_t least, const char* what) {
    const size_t where = at;
    const uint32_t n = u32(what);
    if (n > left() / least)
      fail(where,
           std::string(what) + " of " + std::to_string(n) + ", more than " + std::to_string(left()) + " bytes hold");
    if (n > kMaxDescriptorValues - values)
      throw DescriptorError(DecodeError::Kind::TooBig, {},
                            "more than " + std::to_string(kMaxDescriptorValues) + " values", int64_t(base + where));
    return n;
  }

  // A length and that many bytes, or a length of 0 and 4 bytes: o[key], listed in `ids` when 4 bytes long anyway.
  void id(json& o, const char* key, json& ids, const char* what) {
    const size_t where = at;
    const uint32_t n = u32(what);
    o[key] = id_text(take(n ? n : 4, what, where), n ? n : 4);
    if (n == 4) ids.push_back(key);
  }
  json ustr(const char* what) {
    const size_t where = at;
    const uint32_t n = u32(what);
    if (n > left() / 2)
      fail(where, std::string(what) + " of " + std::to_string(n) + " characters, past the end (" +
                      std::to_string(left()) + " bytes left)");
    const size_t bytes = size_t(n) * 2;
    const uint8_t* p = take(bytes, what);
    std::string s;
    if (n && !p[bytes - 2] && !p[bytes - 1] && utf8_of(p, n - 1, s)) return s;
    count(n, where);
    json units = json::array();
    for (size_t i = 0; i < bytes; i += 2) units.push_back(uint32_t(p[i]) << 8 | p[i + 1]);
    return units;
  }
  // A class structure: a Unicode name, then a class ID.
  void class_of(json& o, json& ids) {
    json name = ustr("a class name");
    if (!name.is_string() || !name.get_ref<const std::string&>().empty()) o["name"] = std::move(name);
    id(o, "class", ids, "a class ID");
  }

  void body(json& d, json& ids, int depth) {
    if (depth > kMaxDescriptorDepth) fail(at, "descriptors nested deeper than " + std::to_string(kMaxDescriptorDepth));
    class_of(d, ids);
    const uint32_t n = many(kMinItem, "an item count");
    json items = json::array();
    for (uint32_t i = 0; i < n; i++) {
      json item = json::object(), item_ids = json::array();
      id(item, "key", item_ids, "a key");
      value(item, item_ids, depth);
      if (!item_ids.empty()) item["stringIDs"] = std::move(item_ids);
      items.push_back(std::move(item));
    }
    d["items"] = std::move(items);
  }

  void references(json& v) {
    const uint32_t n = many(kMinReference, "a reference's count");
    json refs = json::array();
    for (uint32_t i = 0; i < n; i++) {
      const size_t where = at;
      count(1, where);
      const uint32_t form = u32("a reference form");
      json r = json::object(), ids = json::array();
      switch (form) {
        case tag("prop"): class_of(r, ids), id(r, "key", ids, "a property's key"); break;
        case tag("Clss"): class_of(r, ids); break;
        case tag("Enmr"):
          class_of(r, ids), id(r, "enumType", ids, "an enum type"), id(r, "value", ids, "an enum value");
          break;
        case tag("rele"): class_of(r, ids), r["value"] = int32_t(u32("an offset")); break;
        case tag("Idnt"): r["value"] = int32_t(u32("an identifier")); break;
        case tag("indx"): r["value"] = int32_t(u32("an index")); break;
        case tag("name"): class_of(r, ids), r["value"] = ustr("a name"); break;
        default: fail(where, "a reference of no known form ('" + id_text(data + where, 4) + "')");
      }
      r["form"] = id_text(data + where, 4);
      if (!ids.empty()) r["stringIDs"] = std::move(ids);
      refs.push_back(std::move(r));
    }
    v["value"] = std::move(refs);
  }

  // A type, then a value of it, into v.
  void value(json& v, json& ids, int depth) {
    const size_t where = at;
    count(1, where);
    const uint32_t type = u32("a type");
    switch (type) {
      case tag("Objc"):
      case tag("GlbO"): body(v, ids, depth + 1); break;
      case tag("ObAr"): v["count"] = u32("an object array's count"), body(v, ids, depth + 1); break;
      case tag("VlLs"): {
        if (depth + 1 > kMaxDescriptorDepth)
          fail(where, "descriptors nested deeper than " + std::to_string(kMaxDescriptorDepth));
        const uint32_t n = many(kMinValue, "a list's count");
        json list = json::array();
        for (uint32_t i = 0; i < n; i++) {
          json e = json::object(), e_ids = json::array();
          value(e, e_ids, depth + 1);
          if (!e_ids.empty()) e["stringIDs"] = std::move(e_ids);
          list.push_back(std::move(e));
        }
        v["value"] = std::move(list);
        break;
      }
      case tag("doub"): v["value"] = number_json(be64(take(8, "a double"))); break;
      case tag("UntF"):
        v["unit"] = id_text(take(4, "a unit"), 4);
        v["value"] = number_json(be64(take(8, "a unit float")));
        break;
      case tag("UnFl"): {
        v["unit"] = id_text(take(4, "a unit"), 4);
        const size_t from = at;
        const uint32_t n = many(8, "a count of unit floats");
        count(n, from);
        const uint8_t* p = take(size_t(n) * 8, "unit floats");
        json numbers = json::array();
        for (uint32_t i = 0; i < n; i++) numbers.push_back(number_json(be64(p + 8 * size_t(i))));
        v["value"] = std::move(numbers);
        break;
      }
      case tag("TEXT"): v["value"] = ustr("a TEXT"); break;
      case tag("enum"): id(v, "enumType", ids, "an enum type"), id(v, "value", ids, "an enum value"); break;
      case tag("long"): v["value"] = int32_t(u32("a long")); break;
      case tag("comp"): v["value"] = int64_t(be64(take(8, "a comp"))); break;
      case tag("bool"): {
        const uint8_t b = *take(1, "a bool");
        v["value"] = b > 1 ? json(b) : json(b == 1);
        break;
      }
      case tag("type"):
      case tag("GlbC"): class_of(v, ids); break;
      case tag("obj "): references(v); break;
      case tag("alis"):
      case tag("tdta"):
      case tag("Pth "): {
        const size_t from = at;
        const uint32_t n = u32("a length");
        v["value"] = base64(take(n, "raw data", from), n);
        break;
      }
      default: fail(where, "an item of no known type ('" + id_text(data + where, 4) + "')");
    }
    v["type"] = id_text(data + where, 4);
  }
};

// ---- Writing ----

[[noreturn]] void bad(const std::string& why) { throw std::invalid_argument("pb: descriptor: " + why); }

// v[what] as one of `names`' OSTypes.
uint32_t type_tag(const json& v, const char* const* names, size_t count, const char* what) {
  const json* t = member(v, what);
  if (t && t->is_string())
    for (size_t i = 0; i < count; i++)
      if (*t == names[i]) return be32(reinterpret_cast<const uint8_t*>(names[i]));
  bad(std::string("a value whose \"") + what + "\" is " + (t ? t->dump() : "missing"));
}

// j as an integer from lo to hi: a JSON integer, or a float with no fraction.
bool int_in(const json& j, int64_t lo, int64_t hi, int64_t& v) {
  if (j.is_number_unsigned()) {
    if (j.get<uint64_t>() > uint64_t(INT64_MAX)) return false;
    v = int64_t(j.get<uint64_t>());
  } else if (j.is_number_integer()) {
    v = j.get<int64_t>();
  } else if (j.is_number_float()) {
    const double d = j.get<double>();
    if (!(d == std::floor(d) && d >= -9223372036854775808.0 && d < 9223372036854775808.0)) return false;
    v = int64_t(d);
  } else {
    return false;
  }
  return v >= lo && v <= hi;
}

struct Writer {
  std::vector<uint8_t>& out;

  void u32(uint32_t v) {
    for (int i = 3; i >= 0; i--) out.push_back(uint8_t(v >> 8 * i));
  }
  void u64(uint64_t v) { u32(uint32_t(v >> 32)), u32(uint32_t(v)); }
  void bytes(const std::string& b) { out.insert(out.end(), b.begin(), b.end()); }
  void length(size_t n) {
    if (n > UINT32_MAX) bad("a length past 4 GB");
    u32(uint32_t(n));
  }

  const json& need(const json& o, const char* key) {
    const json* m = member(o, key);
    if (!m) bad(std::string("a value without its \"") + key + '"');
    return *m;
  }
  int64_t integer(const json& o, const char* key, int64_t lo, int64_t hi) {
    int64_t v = 0;
    if (!int_in(need(o, key), lo, hi, v))
      bad(std::string("\"") + key + "\" is " + need(o, key).dump() + ", not an integer from " + std::to_string(lo) +
          " to " + std::to_string(hi));
    return v;
  }
  void number(const json& j) {
    uint64_t bits = 0;
    if (!number_bits(j, bits)) bad(j.dump() + " is not a number");
    u64(bits);
  }

  void id(const json& o, const char* key) {
    const json& s = need(o, key);
    std::string b;
    if (!s.is_string() || !id_bytes(s.get_ref<const std::string&>(), b)) bad(std::string("an ID ") + s.dump());
    if (b.size() == 4 && !listed(o, key)) {
      u32(0);
    } else {
      if (b.empty()) bad("an empty ID");
      length(b.size());
    }
    bytes(b);
  }
  void unit(const json& o) {
    const json& s = need(o, "unit");
    std::string b;
    if (!s.is_string() || !id_bytes(s.get_ref<const std::string&>(), b) || b.size() != 4)
      bad("a unit " + s.dump() + ", not 4 bytes");
    bytes(b);
  }
  // A Unicode string: text with its closing null, or code units; none, an empty one.
  void ustr(const json* s) {
    std::vector<uint16_t> units;
    if (!s) {
      units.push_back(0);
    } else if (s->is_string()) {
      if (!utf16_of(s->get_ref<const std::string&>(), units)) bad("a string that is not UTF-8");
      units.push_back(0);
    } else if (s->is_array()) {
      for (const json& u : *s) {
        int64_t v = 0;
        if (!int_in(u, 0, 0xffff, v)) bad("a UTF-16 code unit " + u.dump());
        units.push_back(uint16_t(v));
      }
    } else {
      bad("a Unicode string " + s->dump());
    }
    length(units.size());
    for (const uint16_t u : units) out.push_back(uint8_t(u >> 8)), out.push_back(uint8_t(u));
  }
  void class_of(const json& o) { ustr(member(o, "name")), id(o, "class"); }

  void body(const json& d, int depth) {
    if (depth > kMaxDescriptorDepth) bad("descriptors nested deeper than " + std::to_string(kMaxDescriptorDepth));
    class_of(d);
    const json& items = need(d, "items");
    if (!items.is_array()) bad("\"items\" is no array");
    length(items.size());
    for (const json& item : items) {
      id(item, "key");
      value(item, depth);
    }
  }

  void value(const json& v, int depth) {
    const uint32_t type = type_tag(v, kTypes, std::size(kTypes), "type");
    u32(type);
    switch (type) {
      case tag("Objc"):
      case tag("GlbO"): body(v, depth + 1); break;
      case tag("ObAr"): u32(uint32_t(integer(v, "count", 0, UINT32_MAX))), body(v, depth + 1); break;
      case tag("VlLs"): {
        if (depth + 1 > kMaxDescriptorDepth)
          bad("descriptors nested deeper than " + std::to_string(kMaxDescriptorDepth));
        const json& list = need(v, "value");
        if (!list.is_array()) bad("a VlLs whose value is no array");
        length(list.size());
        for (const json& e : list) value(e, depth + 1);
        break;
      }
      case tag("doub"): number(need(v, "value")); break;
      case tag("UntF"): unit(v), number(need(v, "value")); break;
      case tag("UnFl"): {
        unit(v);
        const json& numbers = need(v, "value");
        if (!numbers.is_array()) bad("an UnFl whose value is no array");
        length(numbers.size());
        for (const json& n : numbers) number(n);
        break;
      }
      case tag("TEXT"): ustr(&need(v, "value")); break;
      case tag("enum"): id(v, "enumType"), id(v, "value"); break;
      case tag("long"): u32(uint32_t(integer(v, "value", INT32_MIN, INT32_MAX))); break;
      case tag("comp"): u64(uint64_t(integer(v, "value", INT64_MIN, INT64_MAX))); break;
      case tag("bool"): {
        const json& b = need(v, "value");
        out.push_back(b.is_boolean() ? uint8_t(b.get<bool>()) : uint8_t(integer(v, "value", 0, 255)));
        break;
      }
      case tag("type"):
      case tag("GlbC"): class_of(v); break;
      case tag("obj "): {
        const json& refs = need(v, "value");
        if (!refs.is_array()) bad("an obj whose value is no array");
        length(refs.size());
        for (const json& r : refs) reference(r);
        break;
      }
      default: {  // alis, tdta, Pth
        const json& s = need(v, "value");
        std::vector<uint8_t> raw;
        if (!s.is_string() || !unbase64(s.get_ref<const std::string&>(), raw)) bad("raw data not in base64");
        length(raw.size());
        out.insert(out.end(), raw.begin(), raw.end());
      }
    }
  }

  void reference(const json& r) {
    const uint32_t form = type_tag(r, kForms, std::size(kForms), "form");
    u32(form);
    switch (form) {
      case tag("prop"): class_of(r), id(r, "key"); break;
      case tag("Clss"): class_of(r); break;
      case tag("Enmr"): class_of(r), id(r, "enumType"), id(r, "value"); break;
      case tag("rele"): class_of(r), u32(uint32_t(integer(r, "value", INT32_MIN, INT32_MAX))); break;
      case tag("name"): class_of(r), ustr(&need(r, "value")); break;
      default: u32(uint32_t(integer(r, "value", INT32_MIN, INT32_MAX)));  // Idnt, indx
    }
  }
};

}  // namespace

json read_descriptor(const uint8_t* data, size_t size, size_t* used, uint64_t base) {
  PROFILE_ZONE("read_descriptor");
  Reader r{data, size, base};
  json d = json::object(), ids = json::array();
  r.body(d, ids, 1);
  if (!ids.empty()) d["stringIDs"] = std::move(ids);
  if (used) *used = r.at;
  return d;
}

json read_versioned_descriptor(const uint8_t* data, size_t size, size_t* used, uint64_t base) {
  if (size < 4)
    throw DescriptorError(DecodeError::Kind::Corrupt, {}, "a version past the end (" + std::to_string(size) + " left)",
                          int64_t(base));
  if (const uint32_t v = be32(data); v != 16)
    throw DescriptorError(DecodeError::Kind::Codec, "descriptor_version", "version " + std::to_string(v) + ", not 16",
                          int64_t(base));
  json d = read_descriptor(data + 4, size - 4, used, base + 4);
  if (used) *used += 4;
  return d;
}

void write_descriptor(const json& d, std::vector<uint8_t>& out) {
  PROFILE_ZONE("write_descriptor");
  if (!d.is_object()) bad("a descriptor that is no object");
  Writer{out}.body(d, 1);
}

void write_versioned_descriptor(const json& d, std::vector<uint8_t>& out) {
  Writer{out}.u32(16);
  write_descriptor(d, out);
}

namespace desc {
namespace {

[[noreturn]] void wrong(const std::string& why) { throw DescriptorError(DecodeError::Kind::Corrupt, {}, why, -1); }
std::string type_of(const json& v) {
  const json* t = member(v, "type");
  return t && t->is_string() ? t->get<std::string>() : "value of no type";
}
// v["type"] is one of `types`, v["value"] there; else throws, naming v as `what`.
const json& checked(const json& v, std::initializer_list<const char*> types, std::string_view what,
                    const char* member_name = "value") {
  const json* t = member(v, "type");
  const json* m = member(v, member_name);
  if (m && t && std::any_of(types.begin(), types.end(), [&](const char* n) { return *t == n; })) return *m;
  std::string names;
  for (const char* n : types) names += std::string(names.empty() ? "" : " or ") + n;
  wrong(std::string(what) + " is a " + type_of(v) + ", not " + names);
}
const json& items_of(const json& d) {
  const json* items = member(d, "items");
  if (!items || !items->is_array()) wrong("a " + type_of(d) + " is no descriptor");
  return *items;
}
std::string in_quotes(std::string_view key) { return "'" + std::string(key) + "'"; }

double number_of(const json& v, std::string_view what) {
  const json& n = checked(v, {"long", "comp", "doub", "UntF"}, what);
  uint64_t bits = 0;
  if (!number_bits(n, bits)) wrong(std::string(what) + " holds no number");
  if (n.is_number_integer()) return n.get<double>();
  double d;
  std::memcpy(&d, &bits, 8);
  return d;
}
int64_t integer_of(const json& v, std::string_view what) {
  const json& n = checked(v, {"long", "comp"}, what);
  int64_t i = 0;
  if (!int_in(n, INT64_MIN, INT64_MAX, i)) wrong(std::string(what) + " holds no integer");
  return i;
}
bool boolean_of(const json& v, std::string_view what) {
  const json& b = checked(v, {"bool"}, what);
  if (b.is_boolean()) return b.get<bool>();
  if (!b.is_number()) wrong(std::string(what) + " holds no bool");
  return b.get<double>() != 0;
}
std::string text_of(const json& v, std::string_view what) {
  const json& s = checked(v, {"TEXT"}, what);
  if (s.is_string()) return s.get<std::string>();
  if (!s.is_array()) wrong(std::string(what) + " holds no text");
  // Code units: lone surrogates as U+FFFD, a closing null dropped.
  std::vector<uint8_t> be;
  for (const json& u : s) {
    int64_t c = 0;
    if (!int_in(u, 0, 0xffff, c)) wrong(std::string(what) + " holds no text");
    be.push_back(uint8_t(c >> 8)), be.push_back(uint8_t(c));
  }
  size_t n = be.size() / 2;
  if (n && !be[2 * n - 2] && !be[2 * n - 1]) n--;
  std::string out;
  for (size_t i = 0; i < n; i++) {
    // one unit, or a surrogate pair, at a time
    const bool pair = i + 1 < n && (be[2 * i] & 0xfc) == 0xd8 && (be[2 * i + 2] & 0xfc) == 0xdc;
    std::string part;
    if (utf8_of(&be[2 * i], pair ? 2 : 1, part))
      out += part;
    else
      out += "\xef\xbf\xbd";
    i += pair;
  }
  return out;
}
std::string enumerated_of(const json& v, std::string_view what) {
  const json& e = checked(v, {"enum"}, what);
  if (!e.is_string()) wrong(std::string(what) + " holds no enum value");
  return e.get<std::string>();
}
const json& list_of(const json& v, std::string_view what) {
  const json& l = checked(v, {"VlLs"}, what);
  if (!l.is_array()) wrong(std::string(what) + " holds no list");
  return l;
}
const json& object_of(const json& v, std::string_view what) {
  checked(v, {"Objc", "GlbO"}, what, "items");
  items_of(v);
  return v;
}
std::vector<uint8_t> raw_of(const json& v, std::string_view what) {
  const json& s = checked(v, {"tdta", "alis", "Pth "}, what);
  std::vector<uint8_t> bytes;
  if (!s.is_string() || !unbase64(s.get_ref<const std::string&>(), bytes))
    wrong(std::string(what) + " holds no base64");
  return bytes;
}

// v keyed `key`: its key ID listed in "stringIDs" when written with its length although it has 4 letters.
void key(json& v, std::string_view k, bool string_form) {
  if (!v.is_object()) bad("an item that is no object");
  v["key"] = std::string(k);
  json ids = json::array();
  if (const json* old = member(v, "stringIDs"); old && old->is_array())
    for (const json& m : *old)
      if (m != "key") ids.push_back(m);
  if (string_form) ids.push_back("key");
  if (ids.empty())
    v.erase("stringIDs");
  else
    v["stringIDs"] = std::move(ids);
}
// {"type": t} with members, IDs that are 4-letter string IDs listed in "stringIDs"
json with_ids(json v, std::initializer_list<const char*> id_members) {
  json ids = json::array();
  for (const char* m : id_members)
    if (string_id4(v[m].get_ref<const std::string&>())) ids.push_back(m);
  if (!ids.empty()) v["stringIDs"] = std::move(ids);
  return v;
}

}  // namespace

const json* find(const json& d, std::string_view k) {
  for (const json& item : items_of(d)) {
    const json* m = member(item, "key");
    if (m && m->is_string() && m->get_ref<const std::string&>() == k) return &item;
  }
  return nullptr;
}
const json& at(const json& d, std::string_view k) {
  if (const json* item = find(d, k)) return *item;
  const json* cls = member(d, "class");
  wrong("no " + in_quotes(k) + " in a " + (cls && cls->is_string() ? in_quotes(cls->get<std::string>()) : "descriptor"));
}

double number(const json& v) { return number_of(v, "a value"); }
int64_t integer(const json& v) { return integer_of(v, "a value"); }
bool boolean(const json& v) { return boolean_of(v, "a value"); }
std::string text(const json& v) { return text_of(v, "a value"); }
std::string enumerated(const json& v) { return enumerated_of(v, "a value"); }
const json& list(const json& v) { return list_of(v, "a value"); }
const json& object(const json& v) { return object_of(v, "a value"); }
std::vector<uint8_t> raw(const json& v) { return raw_of(v, "a value"); }

double number(const json& d, std::string_view k) { return number_of(at(d, k), in_quotes(k)); }
int64_t integer(const json& d, std::string_view k) { return integer_of(at(d, k), in_quotes(k)); }
bool boolean(const json& d, std::string_view k) { return boolean_of(at(d, k), in_quotes(k)); }
std::string text(const json& d, std::string_view k) { return text_of(at(d, k), in_quotes(k)); }
std::string enumerated(const json& d, std::string_view k) { return enumerated_of(at(d, k), in_quotes(k)); }
const json& list(const json& d, std::string_view k) { return list_of(at(d, k), in_quotes(k)); }
const json& object(const json& d, std::string_view k) { return object_of(at(d, k), in_quotes(k)); }
std::vector<uint8_t> raw(const json& d, std::string_view k) { return raw_of(at(d, k), in_quotes(k)); }

void set(json& d, std::string_view k, json v) {
  json* items = d.is_object() && d.contains("items") && d["items"].is_array() ? &d["items"] : nullptr;
  if (!items) bad("set() on no descriptor");
  for (json& item : *items) {
    const json* m = member(item, "key");
    if (m && m->is_string() && m->get_ref<const std::string&>() == k) {
      key(v, k, listed(item, "key"));
      item = std::move(v);
      return;
    }
  }
  key(v, k, string_id4(k));
  items->push_back(std::move(v));
}

json make_object(std::string_view cls, std::vector<std::pair<std::string, json>> items) {
  json d = with_ids({{"type", "Objc"}, {"class", std::string(cls)}}, {"class"});
  d["items"] = json::array();
  for (auto& [k, v] : items) set(d, k, std::move(v));
  return d;
}
json make_list(std::vector<json> values) { return {{"type", "VlLs"}, {"value", std::move(values)}}; }
json make_long(int32_t v) { return {{"type", "long"}, {"value", v}}; }
json make_comp(int64_t v) { return {{"type", "comp"}, {"value", v}}; }
json make_double(double v) { return {{"type", "doub"}, {"value", number_json(v)}}; }
json make_unit(std::string_view unit, double v) {
  return {{"type", "UntF"}, {"unit", std::string(unit)}, {"value", number_json(v)}};
}
json make_units(std::string_view unit, const std::vector<double>& v) {
  json numbers = json::array();
  for (const double x : v) numbers.push_back(number_json(x));
  return {{"type", "UnFl"}, {"unit", std::string(unit)}, {"value", std::move(numbers)}};
}
json make_text(std::string_view utf8) { return {{"type", "TEXT"}, {"value", std::string(utf8)}}; }
json make_enum(std::string_view type, std::string_view value) {
  return with_ids({{"type", "enum"}, {"enumType", std::string(type)}, {"value", std::string(value)}},
                  {"enumType", "value"});
}
json make_bool(bool v) { return {{"type", "bool"}, {"value", v}}; }
json make_class(std::string_view cls) { return with_ids({{"type", "type"}, {"class", std::string(cls)}}, {"class"}); }
json make_raw(const std::vector<uint8_t>& bytes) {
  return {{"type", "tdta"}, {"value", base64(bytes.data(), bytes.size())}};
}

}  // namespace desc
}  // namespace pb
