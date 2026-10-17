#include "strokelog.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <utility>

#include "jobs.h"
#include "profile.h"

namespace pb {
namespace {

constexpr int kVersion = 1;  // of the format: bump when a key changes meaning

uint64_t mix(uint64_t h, uint64_t v) {
  h = (h ^ v) * 0x9e3779b97f4a7c15ull;
  return h ^ h >> 29;
}

// g's pixels over the tiles `where` has (StrokeHash).
uint64_t hash_tiles(const TileGrid& g, const TileGrid& where) {
  const size_t stride = size_t(kTileSize) * size_t(g.format().bytes());
  std::vector<uint64_t> px(stride * kTileSize / 8);  // a tile is whole words: 2^16 pixels
  uint64_t h = where.tiles().size();
  for (const auto& [xy, t] : where.tiles()) {
    auto* bytes = reinterpret_cast<uint8_t*>(px.data());
    g.read_rect(xy.x * kTileSize, xy.y * kTileSize, kTileSize, kTileSize, bytes, stride);
    h = mix(h, uint64_t(uint32_t(xy.x)) << 32 | uint32_t(xy.y));
    for (const uint64_t w : px) h = mix(h, w);
  }
  return h;
}

std::string hex(uint64_t v) {
  char s[17];
  std::snprintf(s, sizeof s, "%016llx", static_cast<unsigned long long>(v));
  return s;
}

}  // namespace

StrokeHash stroke_hash(const StrokeState& s) {
  PROFILE_ZONE("stroke_hash");
  if (!s.pixels || !s.buffer) throw std::invalid_argument("pb: stroke_hash: the stroke has not merged");
  return {hash_tiles(*s.buffer, *s.buffer), hash_tiles(*s.pixels, *s.buffer)};
}

std::string to_jsonl(const StrokeRecord& r) {
  using nlohmann::ordered_json;
  const BrushParams& b = r.brush;
  const ordered_json head = {
      {"strokeLog", kVersion}, {"width", r.width}, {"height", r.height},
      {"mode", r.mode == Mode::Gray ? "gray" : "rgb"}, {"depth", r.depth == Depth::U16 ? 16 : 8},
      {"layer", r.layer}, {"zoom", r.zoom}, {"doc", r.doc},
      {"brush",
       {{"size", b.size}, {"hardness", b.hardness}, {"opacity", b.opacity}, {"flow", b.flow}, {"spacing", b.spacing},
        {"pressureSize", b.pressure_size}, {"pressureOpacity", b.pressure_opacity}, {"pressureFlow", b.pressure_flow},
        {"color", b.color}, {"erase", b.erase}}}};
  std::string s = head.dump(-1, ' ', false, ordered_json::error_handler_t::replace) + '\n';
  for (const PenEvent& e : r.events)
    s += ordered_json{{"t", e.t}, {"x", e.x}, {"y", e.y}, {"pressure", e.pressure}, {"tiltX", e.tilt_x},
                      {"tiltY", e.tilt_y}, {"rotation", e.rotation}}.dump() + '\n';
  return s + ordered_json{{"buffer", hex(r.hash.buffer)}, {"pixels", hex(r.hash.pixels)}}.dump() + '\n';
}

StrokeRecord read_stroke_log(const std::string& path) {
  using nlohmann::json;
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error(path + ": cannot open");
  StrokeRecord r;
  std::string line;
  int n = 0;
  try {
    while (std::getline(in, line)) {
      const json j = json::parse(line);  // '\r' too is whitespace
      if (++n == 1) {
        if (j.at("strokeLog").get<int>() != kVersion) throw std::runtime_error("not a version 1 log");
        r.width = j.at("width").get<int>(), r.height = j.at("height").get<int>();
        const std::string mode = j.at("mode").get<std::string>();
        const int depth = j.at("depth").get<int>();
        if ((mode != "rgb" && mode != "gray") || (depth != 8 && depth != 16))
          throw std::runtime_error("mode or depth unknown");
        r.mode = mode == "gray" ? Mode::Gray : Mode::Rgb, r.depth = depth == 16 ? Depth::U16 : Depth::U8;
        r.layer = j.at("layer").get<LayerId>(), r.zoom = j.value("zoom", 1.0), r.doc = j.value("doc", "");
        const json& b = j.at("brush");
        BrushParams& p = r.brush;
        p.size = b.at("size").get<float>(), p.hardness = b.at("hardness").get<float>();
        p.opacity = b.at("opacity").get<float>(), p.flow = b.at("flow").get<float>();
        p.spacing = b.at("spacing").get<float>();
        p.pressure_size = b.at("pressureSize").get<bool>(), p.pressure_opacity = b.at("pressureOpacity").get<bool>();
        p.pressure_flow = b.at("pressureFlow").get<bool>();
        p.color = b.at("color").get<std::array<float, 3>>(), p.erase = b.at("erase").get<bool>();
      } else if (j.contains("buffer")) {
        r.hash = {std::stoull(j.at("buffer").get<std::string>(), nullptr, 16),
                  std::stoull(j.at("pixels").get<std::string>(), nullptr, 16)};
        return r;
      } else {
        r.events.push_back({j.at("t").get<double>(), j.at("x").get<float>(), j.at("y").get<float>(),
                            j.value("pressure", 1.f), j.value("tiltX", 0.f), j.value("tiltY", 0.f),
                            j.value("rotation", 0.f)});
      }
    }
  } catch (const std::exception& e) {
    throw std::runtime_error(path + ":" + std::to_string(n) + ": " + e.what());
  }
  throw std::runtime_error(path + ": no hash line: cut short");
}

DocPtr replay_doc(const StrokeRecord& r) {
  const DocPtr d = new_document(r.width, r.height, r.mode, r.depth, nullptr);
  const Format f = pixel_format(*d);
  const auto m = uint16_t(max_value(r.depth));
  const Pixel v = f.channels == 4 ? Pixel{uint16_t(m / 5), uint16_t(m / 2), uint16_t(m / 5 * 4), uint16_t(m / 2)}
                                  : Pixel{uint16_t(m / 3), uint16_t(m / 2)};
  auto g = std::make_shared<TileGrid>(f);
  for (int ty = 0; ty * kTileSize < r.height; ty++)
    for (int tx = 0; tx * kTileSize < r.width; tx++) g->set(tx, ty, Tile::solid(f, v));
  return run(*d, AddLayer{0, 0, Kind::Pixel, {}, std::move(g)});
}

std::unique_ptr<StrokeLog> StrokeLog::record(const DocState& doc, LayerId layer, const BrushParams& b, double zoom,
                                             std::string name, std::string dir) {
  if (const char* env = std::getenv("PB_RECORD_STROKES"); dir.empty() && env) dir = env;
  if (dir.empty()) return nullptr;
  std::unique_ptr<StrokeLog> log(new StrokeLog);
  log->dir_ = std::move(dir);
  log->rec_ = {doc.width, doc.height, doc.mode, doc.depth, layer, b, zoom, std::move(name), {}, {}};
  log->rec_.events.reserve(1024);  // a few seconds of a pen
  return log;
}

void StrokeLog::add(const PenEvent& e) { rec_.events.push_back(e); }

std::future<std::string> StrokeLog::finish(std::shared_ptr<const StrokeState> merged) {
  if (!merged || !merged->pixels) throw std::invalid_argument("pb: StrokeLog::finish: the stroke has not merged");
  static std::atomic<unsigned> serial{0};
  char name[64];
  std::snprintf(name, sizeof name, "stroke-%lld-%u.jsonl", static_cast<long long>(std::time(nullptr)), serial++);
  const std::filesystem::path dir = dir_, path = dir / name;
  auto done = std::make_shared<std::promise<std::string>>();
  submit(Priority::Background, [r = std::move(rec_), merged = std::move(merged), dir, path, done]() mutable {
    try {
      r.hash = stroke_hash(*merged);
      std::filesystem::create_directories(dir);
      std::ofstream f(path, std::ios::binary);
      f << to_jsonl(r);
      f.close();
      if (!f) throw std::runtime_error(path.string() + ": cannot write");
      done->set_value(path.string());
    } catch (...) {
      done->set_exception(std::current_exception());
    }
  });
  return done->get_future();
}

}  // namespace pb
