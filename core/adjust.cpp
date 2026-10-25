// Adjustments (adjust.h): the table of types, the JSON of their values, the cache, the test type, apply_adjustment()
// and preview_state(). The types themselves are core/adj_<type>.cpp's (D33-D39).
#include "adjust.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <mutex>
#include <set>
#include <stdexcept>

#include "lru.h"
#include "profile.h"

namespace pb {
namespace {

using nlohmann::json;

// ---- Hypotheses -----------------------------------------------------------------------------------------------------

// The adjustments Photoshop offers on a Grayscale document: those that need no colour, the rest greyed out in Image ›
// Adjustments and the Adjustments panel (Cần bạn chốt 13). Adobe lists none; Channel Mixer, Color Lookup and Photo
// Filter are the least sure. To check in Photoshop (D32, B33).
constexpr AdjType kGreyAdjustments[] = {
    AdjType::BrightnessContrast, AdjType::Levels,    AdjType::Curves,    AdjType::Exposure,
    AdjType::Invert,             AdjType::Posterize, AdjType::Threshold, AdjType::GradientMap,
    AdjType::TestMatrix,
};

// ---- The table of types ---------------------------------------------------------------------------------------------

using Maker = AdjustmentPtr (*)(const json&, const AdjustContext&);
AdjustmentPtr test_matrix(const json&, const AdjustContext&);  // below

// Each type's maker, by AdjType's value as kAdjNames: core/adj_<type>.cpp's, the test's last.
constexpr Maker kMakers[] = {
    adj::brightness_contrast, adj::levels,       adj::curves,       adj::exposure,       adj::vibrance,
    adj::hue_saturation,      adj::color_balance, adj::black_white,  adj::photo_filter,   adj::channel_mixer,
    adj::color_lookup,        adj::invert,       adj::posterize,    adj::threshold,      adj::gradient_map,
    adj::selective_color,     test_matrix,
};
static_assert(std::size(kMakers) == size_t(kAdjTypes), "a maker for every type");

// ---- JSON -----------------------------------------------------------------------------------------------------------

std::string dump(const json& j) { return j.dump(-1, ' ', false, json::error_handler_t::replace); }

// vals[key], null when it is not there or null (the UI writes null for some defaults).
const json* member(const json& vals, const char* key) {
  const auto it = vals.find(key);
  return it == vals.end() || it->is_null() ? nullptr : &*it;
}

// A number as the UI's JSON.stringify() writes it: whole ones without a fraction.
json number_json(double v) {
  if (v == std::floor(v) && std::fabs(v) < 9007199254740992.0) return json(int64_t(v));
  return json(v);
}

bool is_color(const std::string& s) {
  return s.size() == 7 && s[0] == '#' &&
         std::all_of(s.begin() + 1, s.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; });
}

// ---- The cache ------------------------------------------------------------------------------------------------------

// Adjustments by what made them, within a budget of what they hold (their tables mostly). An entry holds the
// document's profile (its context), so the profile's address in the key stays its own while the entry lives.
struct Cache {
  std::mutex m;
  Lru<std::string, AdjustmentPtr> lru{size_t(64) << 20};
};
Cache& cache() {
  static Cache& c = *new Cache;  // never destroyed: pool threads may still ask at exit
  return c;
}
std::string cache_key(AdjType type, const std::string& vals, const AdjustContext& c) {
  const std::vector<uint8_t>* icc = c.icc && !c.icc->empty() ? c.icc.get() : nullptr;
  std::string key(2 + sizeof icc, '\0');
  key[0] = char(type), key[1] = char(c.mode);
  std::memcpy(&key[2], &icc, sizeof icc);
  return key + vals;
}

// ---- The test type --------------------------------------------------------------------------------------------------

// For tests (D31's apply_adjustment(), D40's composite), and the worked example of a type: each channel out of a 3 x 4
// matrix of R, G, B and 1, then to a power of its own through a curve's table (what pow() makes is a table on the
// CPU). JSON {"m": [12 numbers, row by row: R's, G's, B's], "power": [r, g, b]}: the identity, 1, 1, 1 by default.
// Words: w[2..13] the matrix, w[14] 1 when the powers make a table: 4,098 entries, R, G, B in x, y, z.
// adjust.slang's adj_test_matrix() line by line.
constexpr double kIdentity[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
constexpr double kNoPower[3] = {1, 1, 1};

struct MatrixValues {
  double m[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
  double power[3] = {1, 1, 1};
  void fields(AdjFields& f) {
    f.numbers("m", m, 12, kIdentity, -10, 10);
    f.numbers("power", power, 3, kNoPower, 0.1, 10);
  }
};

class TestMatrix final : public Adjustment {
 public:
  TestMatrix(const MatrixValues& v, const AdjustContext& c, std::string vals)
      : Adjustment(AdjType::TestMatrix, c, std::move(vals)) {
    for (int i = 0; i < 12; i++) m_[i] = float(v.m[i]);
    curve_ = !std::equal(v.power, v.power + 3, kNoPower);
    identity_ = !curve_ && std::equal(v.m, v.m + 12, kIdentity);
    if (curve_) {
      tables_.resize(kCurveSteps + 2);
      for (int i = 0; i <= kCurveSteps; i++)
        for (int c = 0; c < 3; c++)
          tables_[size_t(i)][size_t(c)] = float(std::pow(double(i) / kCurveSteps, v.power[c]));
      tables_[kCurveSteps + 1] = tables_[kCurveSteps];
    }
  }
  bool identity() const override { return identity_; }

 private:
  void adjust_row(float* px, int n, int, int) const override {
    for (int i = 0; i < n; i++, px += 4) {
      const float r = px[0], g = px[1], b = px[2];
      float o[3];
      for (int c = 0; c < 3; c++) o[c] = m_[4 * c] * r + m_[4 * c + 1] * g + m_[4 * c + 2] * b + m_[4 * c + 3];
      if (curve_)
        for (int c = 0; c < 3; c++) o[c] = adjust_curve(tables_.data(), o[c])[size_t(c)];
      px[0] = o[0], px[1] = o[1], px[2] = o[2];
    }
  }
  void pack_words(AdjustParams& p) const override {
    for (int i = 0; i < 12; i++) p.w[size_t(2 + i)] = word(m_[i]);
    p.w[14] = curve_ ? 1 : 0;
  }

  float m_[12];
  bool curve_ = false, identity_ = true;
};

AdjustmentPtr test_matrix(const json& vals, const AdjustContext& c) {
  return make_adjustment<MatrixValues, TestMatrix>(vals, c);
}

std::atomic<bool> test_matrix_on{false};  // enable_test_matrix()'s: off, a file's "Test Matrix" is no type

inline float clamp01(float v) { return v > 0 ? (v < 1 ? v : 1) : 0; }  // NaN: 0

}  // namespace

// ---- Types ----------------------------------------------------------------------------------------------------------

std::optional<AdjType> adj_type(const std::string& name) {
  for (int i = 0; i < kAdjTypes; i++)
    if (name == kAdjNames[i] && (i != int(AdjType::TestMatrix) || test_matrix_on)) return AdjType(i);
  return std::nullopt;
}

void enable_test_matrix(bool on) { test_matrix_on = on; }

bool available(AdjType t, Mode mode) {
  if (mode != Mode::Gray) return true;
  return std::find(std::begin(kGreyAdjustments), std::end(kGreyAdjustments), t) != std::end(kGreyAdjustments);
}

Float4 adjust_curve(const Float4* table, float v) {
  v = v > 0 ? (v < 1 ? v : 1) : 0;
  const float x = v * kCurveSteps;  // exactly: a power of 2
  const int i = int(x);             // 0..kCurveSteps
  const float f = x - float(i);
  const Float4& a = table[i];
  const Float4& b = table[i + 1];
  return {a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f, a[2] + (b[2] - a[2]) * f, a[3] + (b[3] - a[3]) * f};
}

// ---- Adjustment -----------------------------------------------------------------------------------------------------

void Adjustment::apply(float* px, int n, int x, int y) const {
  adjust_row(px, n, x, y);
  for (int i = 0; i < n; i++, px += 4) px[0] = clamp01(px[0]), px[1] = clamp01(px[1]), px[2] = clamp01(px[2]);
}

std::array<float, 3> Adjustment::apply(const std::array<float, 3>& rgb, int x, int y) const {
  float px[4] = {rgb[0], rgb[1], rgb[2], 1};
  apply(px, 1, x, y);
  return {px[0], px[1], px[2]};
}

AdjustParams Adjustment::pack(uint32_t base) const {
  AdjustParams p;
  pack_words(p);
  p.w[0] = uint32_t(type_);
  p.w[1] = base;
  return p;
}

size_t Adjustment::bytes() const { return sizeof(*this) + tables_.size() * sizeof(Float4) + vals_.size(); }

AdjustmentPtr adjustment(AdjType type, const std::string& vals, const AdjustContext& context) {
  if (size_t(type) >= std::size(kMakers)) return nullptr;
  const std::string key = cache_key(type, vals, context);
  {
    std::lock_guard l(cache().m);
    if (AdjustmentPtr a = cache().lru.get(key)) return a;
  }
  PROFILE_ZONE("adjustment");
  const json j = json::parse(vals, nullptr, false);  // discarded, not an object, when malformed
  if (!j.is_object()) return nullptr;
  AdjustmentPtr a = kMakers[size_t(type)](j, context);  // outside the lock: tables can take milliseconds
  if (!a) return nullptr;
  std::lock_guard l(cache().m);
  if (AdjustmentPtr b = cache().lru.get(key)) return b;  // made meanwhile on another thread: one of them for all
  cache().lru.put(key, a, a->bytes() + key.size());
  return a;
}

AdjustmentPtr adjustment(const std::string& name, const std::string& vals, const AdjustContext& context) {
  const std::optional<AdjType> t = adj_type(name);
  return t ? adjustment(*t, vals, context) : nullptr;
}

AdjustmentPtr adjustment(const LayerProps& p, const AdjustContext& context) {
  return adjustment(p.adj, p.vals ? *p.vals : std::string("{}"), context);
}

// ---- AdjFields ------------------------------------------------------------------------------------------------------

AdjFields::AdjFields(const json* vals) : in_(vals) {
  if (!in_) out_ = std::make_unique<json>(json::object());
}
AdjFields::~AdjFields() = default;

void AdjFields::number(const char* key, double& v, double def, double lo, double hi) {
  if (in_) {
    v = def;
    if (const json* j = member(*in_, key)) {
      if (!j->is_number()) return fail();
      v = std::clamp(j->get<double>(), lo, hi);
    }
  } else if (v != def) {
    (*out_)[key] = number_json(v);
  }
}

void AdjFields::flag(const char* key, bool& v, bool def) {
  if (in_) {
    v = def;
    if (const json* j = member(*in_, key)) {
      if (!j->is_boolean()) return fail();
      v = j->get<bool>();
    }
  } else if (v != def) {
    (*out_)[key] = v;
  }
}

void AdjFields::index(const char* key, int& v, int def, int count) {
  if (in_) {
    v = def;
    if (const json* j = member(*in_, key)) {
      if (!j->is_number()) return fail();
      v = int(std::lround(std::clamp(j->get<double>(), 0., double(count - 1))));
    }
  } else if (v != def) {
    (*out_)[key] = v;
  }
}

void AdjFields::choice(const char* key, std::string& v, std::initializer_list<const char*> names) {
  const char* def = *names.begin();
  if (in_) {
    v = def;
    if (const json* j = member(*in_, key)) {
      if (!j->is_string()) return fail();
      const std::string& s = j->get_ref<const std::string&>();
      if (std::none_of(names.begin(), names.end(), [&](const char* n) { return s == n; })) return fail();
      v = s;
    }
  } else if (v != def) {
    (*out_)[key] = v;
  }
}

void AdjFields::text(const char* key, std::string& v, const char* def) {
  if (in_) {
    v = def;
    if (const json* j = member(*in_, key)) {
      if (!j->is_string()) return fail();
      v = j->get<std::string>();
    }
  } else if (v != def) {
    (*out_)[key] = v;
  }
}

void AdjFields::color(const char* key, std::string& v, const char* def) {
  if (in_) {
    v = def;
    if (const json* j = member(*in_, key)) {
      if (!j->is_string() || !is_color(j->get_ref<const std::string&>())) return fail();
      v = j->get<std::string>();
    }
  } else if (v != def) {
    (*out_)[key] = v;
  }
}

void AdjFields::numbers(const char* key, double* v, int n, const double* def, double lo, double hi) {
  if (in_) {
    std::copy(def, def + n, v);
    if (const json* j = member(*in_, key)) {
      if (!j->is_array() || j->size() != size_t(n)) return fail();
      for (int i = 0; i < n; i++) {
        const json& e = (*j)[size_t(i)];
        if (!e.is_number()) return fail();
        v[i] = std::clamp(e.get<double>(), lo, hi);
      }
    }
  } else if (!std::equal(v, v + n, def)) {
    json a = json::array();
    for (int i = 0; i < n; i++) a.push_back(number_json(v[i]));
    (*out_)[key] = std::move(a);
  }
}

void AdjFields::value(const char* key, json& v, const json& def, bool (*valid)(const json&)) {
  if (in_) {
    v = def;
    if (const json* j = member(*in_, key)) {
      if (valid && !valid(*j)) return fail();
      v = *j;
    }
  } else if (v != def) {
    (*out_)[key] = v;
  }
}

void AdjFields::object(const char* key, json& v) {
  value(key, v, nullptr, [](const json& j) { return j.is_object(); });
}

std::string AdjFields::written() const { return out_ ? dump(*out_) : std::string(); }

// ---- apply_adjustment() ---------------------------------------------------------------------------------------------

namespace {

constexpr int S = kTileSize;

// v / max_value() as composite.cpp's unit() rounds it: to_unit() without its division.
inline float unit(unsigned v, double k) { return float(double(v) * k); }

struct Work {
  const Adjustment& adj;
  Format f;
  Pixel in_fill, out_fill;  // what the grid's empty tiles hold, and the result's
  const TileGrid* sel;      // null: everywhere
  int width, height;        // the canvas
  bool clip;                // the selection stops at the canvas
};

// A tile to adjust: the grid's (null: an empty one of a mask, its fill), and what it becomes when it changes.
struct Task {
  int tx = 0, ty = 0;
  TilePtr in;
  TilePtr out;
  bool changed = false;
};

// The selection over tile (tx, ty) where it is one value there, 0..1; -1 where it varies. `t` its tile (null: none).
float uniform_weight(const Work& w, const Tile* t, int tx, int ty) {
  if (!w.sel) return 1;
  const double k = 1.0 / max_value(w.sel->format().depth);
  if (t && !t->is_solid()) return -1;
  const float v = unit(t ? t->value()[0] : w.sel->fill()[0], k);
  if (!w.clip) return v;
  const int64_t x0 = int64_t(tx) * S, y0 = int64_t(ty) * S;
  if (x0 + S <= 0 || y0 + S <= 0 || x0 >= w.width || y0 >= w.height) return 0;  // past the canvas
  const bool inside = x0 >= 0 && y0 >= 0 && x0 + S <= w.width && y0 + S <= w.height;
  return inside || v == 0 ? v : -1;
}

// The selection's weights along row y of tile (tx, ty), 0..1, 0 past the canvas: `t` its tile there, pinned as p
// (null: its fill).
void weights(const Work& w, const Tile* t, const Pin& p, int tx, int ty, int y, float* s) {
  const Format sf = w.sel->format();
  const double k = 1.0 / max_value(sf.depth);
  if (!t || !p.data()) {
    std::fill(s, s + S, unit(t ? t->value()[0] : w.sel->fill()[0], k));
  } else if (sf.depth == Depth::U16) {
    const uint16_t* row = reinterpret_cast<const uint16_t*>(p.data()) + size_t(y) * S;
    for (int i = 0; i < S; i++) s[i] = unit(row[i], k);
  } else {
    const uint8_t* row = p.data() + size_t(y) * S;
    for (int i = 0; i < S; i++) s[i] = unit(row[i], k);
  }
  if (!w.clip) return;
  const int64_t py = int64_t(ty) * S + y, x0 = int64_t(tx) * S;
  if (py < 0 || py >= w.height) return std::fill(s, s + S, 0.f);
  for (int i = 0; i < S; i++)
    if (x0 + i < 0 || x0 + i >= w.width) s[i] = 0;
}

// Task t done: samples of type T (uint8_t, uint16_t).
template <class T>
void adjust_tile(const Work& w, Task& t) {
  const Format f = w.f;
  const int ch = f.channels, colors = ch == 4 ? 3 : 1, a = f.alpha() ? ch - 1 : -1;
  const int g = ch == 4 ? 1 : 0, b = ch == 4 ? 2 : 0;
  const double k = 1.0 / max_value(f.depth);
  const Tile* st = w.sel ? w.sel->get(t.tx, t.ty) : nullptr;
  const float uni = uniform_weight(w, st, t.tx, t.ty);
  if (uni == 0) return;  // nothing selected here
  const Tile* in = t.in.get();
  const bool solid = !in || in->is_solid();
  const Pixel value = in ? in->value() : w.in_fill;  // a solid tile's
  if (solid && a >= 0 && value[size_t(a)] == 0) return;  // transparent: no colour
  const int x0 = t.tx * S, y0 = t.ty * S;

  // One value in, one weight, no dither: one value out.
  if (solid && uni > 0 && !w.adj.dithered()) {
    const std::array<float, 3> in3 = {unit(value[0], k), unit(value[size_t(g)], k), unit(value[size_t(b)], k)};
    const std::array<float, 3> out3 = w.adj.apply(in3, x0, y0);
    Pixel v = value;
    for (int c = 0; c < colors; c++)
      v[size_t(c)] = from_unit(uni < 1 ? in3[size_t(c)] * (1 - uni) + out3[size_t(c)] * uni : out3[size_t(c)], f.depth);
    if (v == value) return;
    t.out = compact(Tile::solid(f, v), w.out_fill), t.changed = true;
    return;
  }

  const Pin ip = solid ? Pin() : in->pin();
  const Pin sp = st && uni < 0 && !st->is_solid() ? st->pin() : Pin();
  const T* src = solid ? nullptr : reinterpret_cast<const T*>(ip.data());
  T one[4] = {};  // a solid tile's samples
  for (int c = 0; c < ch; c++) one[c] = T(value[size_t(c)]);
  const std::shared_ptr<Tile> out = Tile::make(f);
  bool changed = false;
  {
    const WritePin wp = out->write();
    T* dst = reinterpret_cast<T*>(wp.data());
    float px[4 * S], s[S];
    for (int y = 0; y < S; y++) {
      const T* srow = src ? src + size_t(y) * S * size_t(ch) : nullptr;
      T* drow = dst + size_t(y) * S * size_t(ch);
      if (uni < 0)
        weights(w, st, sp, t.tx, t.ty, y, s);
      else
        std::fill(s, s + S, uni);
      if (std::none_of(s, s + S, [](float v) { return v > 0; })) {  // a row of nothing selected: as it was
        for (int i = 0; i < S; i++) {
          const T* p = srow ? srow + size_t(i) * size_t(ch) : one;
          std::copy(p, p + ch, drow + size_t(i) * size_t(ch));
        }
        continue;
      }
      for (int i = 0; i < S; i++) {
        const T* p = srow ? srow + size_t(i) * size_t(ch) : one;
        px[4 * i] = unit(p[0], k), px[4 * i + 1] = unit(p[g], k), px[4 * i + 2] = unit(p[b], k), px[4 * i + 3] = 0;
      }
      w.adj.apply(px, S, x0, y0 + y);
      for (int i = 0; i < S; i++) {
        const T* p = srow ? srow + size_t(i) * size_t(ch) : one;
        T* q = drow + size_t(i) * size_t(ch);
        const float si = s[i];
        if (!(si > 0)) {
          std::copy(p, p + ch, q);
          continue;
        }
        for (int c = 0; c < colors; c++) {
          const float v = unit(p[c], k), r = px[4 * i + c];
          q[c] = T(from_unit(si < 1 ? v * (1 - si) + r * si : r, f.depth));
          changed |= q[c] != p[c];
        }
        if (a >= 0) q[a] = p[a];
      }
    }
  }
  if (!changed) return;
  t.out = compact(out, w.out_fill), t.changed = true;
}

// Where `id` sits: its group's id and its index there.
bool place(const Layer& group, LayerId id, LayerId& parent, int& index) {
  for (size_t i = 0; i < group.children.size(); i++) {
    const Layer& l = *group.children[i];
    if (l.id == id) {
      parent = group.id, index = int(i);
      return true;
    }
    if (l.kind == Kind::Group && place(l, id, parent, index)) return true;
  }
  return false;
}

}  // namespace

TileGrid apply_adjustment(const TileGrid& grid, const Adjustment& adj, const AdjustOptions& o) {
  PROFILE_ZONE("apply_adjustment");
  const Format f = grid.format();
  if (o.selection && o.selection->format().channels != 1)
    throw std::invalid_argument("pb: apply_adjustment: a selection is 1 channel");
  if (cancelled(o.cancel)) throw Cancelled();
  if (adj.identity()) return grid;
  const bool canvas = o.width > 0 && o.height > 0;
  Work w{adj, f, grid.fill(), grid.fill(), o.selection, o.width, o.height, o.selection && canvas};
  std::vector<Task> tasks;
  for (const auto& [xy, t] : grid.tiles()) tasks.push_back({xy.x, xy.y, t});

  // A mask's empty tiles hold its fill, which may change: the whole plane's (a new fill), or tiles of it.
  if (f.channels == 1) {
    const uint16_t fv = grid.fill()[0], top = uint16_t(max_value(f.depth));
    const float v = unit(fv, 1.0 / top);
    const uint16_t fa = from_unit(adj.apply({v, v, v})[0], f.depth);  // at (0, 0), where a dither differs
    if (adj.dithered() || fa != fv) {
      if (!o.selection && (!canvas || (!adj.dithered() && (fa == 0 || fa == top)))) {
        w.out_fill[0] = fa;
      } else {
        std::set<TileXY> more;  // empty tiles that change
        if (canvas && (!o.selection || o.selection->fill()[0] != 0))
          for (int ty = 0; ty < (o.height + S - 1) / S; ty++)
            for (int tx = 0; tx < (o.width + S - 1) / S; tx++) more.insert({tx, ty});
        if (o.selection)
          for (const auto& [xy, t] : o.selection->tiles()) more.insert(xy);
        for (const TileXY& xy : more)
          if (!grid.get(xy.x, xy.y)) tasks.push_back({xy.x, xy.y, nullptr});
      }
    }
  }

  const size_t n = tasks.size();
  constexpr size_t kBatch = 64;  // tiles between two looks at the token and the progress
  for (size_t at = 0; at < n; at += kBatch) {
    const size_t end = std::min(n, at + kBatch);
    parallel_for(int(end - at), [&](int begin, int stop) {
      PROFILE_ZONE("apply_adjustment tiles");
      for (int i = begin; i < stop; i++) {
        if (cancelled(o.cancel)) throw Cancelled();
        Task& t = tasks[at + size_t(i)];
        if (f.depth == Depth::U16)
          adjust_tile<uint16_t>(w, t);
        else
          adjust_tile<uint8_t>(w, t);
      }
    });
    if (cancelled(o.cancel)) throw Cancelled();
    if (o.progress) o.progress(double(end) / double(n));
  }
  if (n == 0 && o.progress) o.progress(1);

  if (w.out_fill == grid.fill()) {
    TileGrid out = grid;
    for (const Task& t : tasks)
      if (t.changed) out.set(t.tx, t.ty, t.out);
    return out;
  }
  TileGrid out(f, w.out_fill);  // the plane changed: every tile again, against the new fill
  for (const Task& t : tasks) out.set(t.tx, t.ty, t.changed ? t.out : compact(t.in, w.out_fill));
  return out;
}

// ---- preview_state() ------------------------------------------------------------------------------------------------

DocPtr preview_state(const DocState& doc, LayerId layer, const Adjustment& adj,
                     std::shared_ptr<const TileGrid> selection) {
  const Layer* l = doc.root ? find_layer(doc, layer) : nullptr;
  if (!l || l->kind != Kind::Pixel) throw std::invalid_argument("pb: preview_state: not a pixel layer of the document");
  if (!available(adj.type(), doc.mode))
    throw std::invalid_argument("pb: preview_state: not an adjustment of the document's mode");
  if (selection && selection->format() != mask_format(doc))
    throw std::invalid_argument("pb: preview_state: a selection not in the document's mask format");
  LayerId parent = 0;
  int index = 0;
  place(*doc.root, layer, parent, index);
  AddLayer add;
  add.parent = parent, add.index = index + 1, add.kind = Kind::Adj;
  add.props.name = add.props.adj = kAdjNames[size_t(adj.type())];
  add.props.vals = std::make_shared<const std::string>(adj.vals());
  add.props.clip = true;
  add.mask = std::move(selection);
  auto s = std::make_shared<DocState>(*run(doc, add));
  s->selected = doc.selected;
  return s;
}

}  // namespace pb
