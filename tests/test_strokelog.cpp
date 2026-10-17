// Stroke logs (strokelog.h): numbers read back to the bit, bad files refused; the logs in tests/data/strokes/ (made on
// Linux) painted again to the hashes they end with, on the pool's threads and on one, all at once and an event at a
// time, the hashes telling a sample apart; StrokeLog records what C3 paints as the log it came from; a process exiting
// while a dropped stroke's job still makes masks does not crash.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "check.h"
#include "strokelog.h"

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define popen _popen
#define pclose _pclose
#endif

using namespace pb;
namespace fs = std::filesystem;

namespace {

fs::path strokes_dir() {  // in the source tree, or tests/data/strokes/ copied beside a Windows .exe
  const fs::path in_source = PB_SOURCE_DIR "/tests/data/strokes";
  return fs::exists(in_source) ? in_source : fs::path("tests/data/strokes");
}

void write(const fs::path& p, const std::string& text) { std::ofstream(p, std::ios::binary) << text; }

bool refused(const fs::path& p) {
  try {
    read_stroke_log(p.string());
  } catch (const std::runtime_error&) {
    return true;
  }
  return false;
}

// g with the first byte of pixel (x, y) changed.
std::shared_ptr<const TileGrid> nudged(const TileGrid& g, int x, int y) {
  TileGrid n(g);
  uint8_t px[8];
  n.read_rect(x, y, 1, 1, px, sizeof px);
  px[0] ^= 1;
  n.write_rect(x, y, 1, 1, px, sizeof px);
  return std::make_shared<const TileGrid>(std::move(n));
}

// Any finite double and float, -0 and subnormals among them, reads back to the bit; CRLF line ends too. A file cut
// short, of a later version, or no log at all is refused.
void check_format(const fs::path& dir) {
  std::mt19937_64 rng(7);
  const auto any_float = [&] {
    for (float f;;) {
      const auto bits = uint32_t(rng());
      std::memcpy(&f, &bits, sizeof f);
      if (std::isfinite(f)) return f;
    }
  };
  const auto any_double = [&] {
    for (double d;;) {
      const uint64_t bits = rng();
      std::memcpy(&d, &bits, sizeof d);
      if (std::isfinite(d)) return d;
    }
  };
  StrokeRecord r{701, 333, Mode::Gray, Depth::U16, 42, {}, any_double(), "a \"name\"\n"};
  BrushParams& b = r.brush;
  for (float* f : {&b.size, &b.hardness, &b.opacity, &b.flow, &b.spacing, &b.color[0], &b.color[1], &b.color[2]})
    *f = any_float();
  b.pressure_size = false, b.pressure_flow = b.erase = true;
  for (int i = 0; i < 3000; i++)
    r.events.push_back({any_double(), any_float(), any_float(), any_float(), any_float(), any_float(), any_float()});
  r.events.push_back({-0.0, -0.f, 0, 1e-45f, -1e-45f, 3.4e38f, 1});
  r.hash = {rng(), rng()};
  const std::string text = to_jsonl(r);
  const fs::path p = dir / "format.jsonl";
  write(p, text);
  const StrokeRecord back = read_stroke_log(p.string());
  CHECK(back.events.size() == r.events.size());
  CHECK(std::memcmp(back.events.data(), r.events.data(), r.events.size() * sizeof(PenEvent)) == 0);
  const auto same = [](const auto& a, const auto& c) { return std::memcmp(&a, &c, sizeof a) == 0; };
  const BrushParams& q = back.brush;
  CHECK(same(q.size, b.size) && same(q.hardness, b.hardness) && same(q.opacity, b.opacity) && same(q.flow, b.flow) &&
        same(q.spacing, b.spacing) && same(q.color, b.color) && q.pressure_size == b.pressure_size &&
        q.pressure_opacity == b.pressure_opacity && q.pressure_flow == b.pressure_flow && q.erase == b.erase);
  CHECK(back.width == r.width && back.height == r.height && back.mode == r.mode && back.depth == r.depth &&
        back.layer == r.layer && same(back.zoom, r.zoom) && back.doc == r.doc && back.hash == r.hash);
  std::string crlf;
  for (const char c : text) crlf += c == '\n' ? std::string("\r\n") : std::string(1, c);
  write(p, crlf);
  CHECK(to_jsonl(read_stroke_log(p.string())) == text);

  const size_t last = text.rfind('{');  // the hash line
  for (const std::string& bad : {text.substr(0, last), text.substr(0, last - 30), std::string("nothing\n"),
                                 "{\"strokeLog\":2" + text.substr(text.find(',')), std::string()}) {
    write(p, bad);
    CHECK(refused(p));
  }
  fs::remove(p);
  CHECK(refused(p));  // no file
}

// The logs in tests/data/strokes/ (pb-bench make-strokes <dir> test, on Linux) painted again here on replay_doc():
// every event at once on the pool's threads, then one at a time on one thread, each to the hashes the log ends with,
// which differ from log to log and see one sample of the layer or of the buffer changed.
std::vector<StrokeRecord> check_logs() {
  std::vector<fs::path> files;
  for (const auto& e : fs::directory_iterator(strokes_dir()))
    if (e.path().extension() == ".jsonl") files.push_back(e.path());
  std::sort(files.begin(), files.end());
  CHECK(files.size() >= 4);
  std::vector<StrokeRecord> logs;
  std::set<uint64_t> buffers, pixels;
  for (const fs::path& f : files) {
    StrokeRecord r = read_stroke_log(f.string());
    CHECK(r.doc == "plain" && r.events.size() >= 50);
    const DocPtr d = replay_doc(r);
    std::shared_ptr<const StrokeState> st;
    for (const int threads : {0, 1}) {
      set_stroke_threads(threads);
      Stroke s(d, d->selected[0], r.brush);
      for (const PenEvent& e : r.events) {
        s.add(e);
        if (threads == 1) s.wait();
      }
      s.finish();
      st = s.wait();
      const StrokeHash h = stroke_hash(*st);
      if (h != r.hash)
        std::fprintf(stderr, "test_strokelog: %s: buffer %016llx pixels %016llx\n", f.filename().string().c_str(),
                     static_cast<unsigned long long>(h.buffer), static_cast<unsigned long long>(h.pixels));
      CHECK(h == r.hash);
    }
    StrokeState changed = *st;
    const TileXY at = st->buffer->tiles().begin()->first;  // a tile the stroke reached
    changed.pixels = nudged(*st->pixels, at.x * kTileSize, at.y * kTileSize);
    CHECK(stroke_hash(changed).buffer == r.hash.buffer && stroke_hash(changed).pixels != r.hash.pixels);
    changed.buffer = nudged(*st->buffer, at.x * kTileSize, at.y * kTileSize);
    CHECK(stroke_hash(changed).buffer != r.hash.buffer);
    buffers.insert(r.hash.buffer), pixels.insert(r.hash.pixels);
    logs.push_back(std::move(r));
  }
  set_stroke_threads(0);
  CHECK(buffers.size() == logs.size() && pixels.size() == logs.size());
  return logs;
}

// As C3 records: StrokeLog::record() at pen down, add() beside each Stroke::add() while the stroke paints, finish()
// with the merged state; the files it writes are the logs the strokes came from, header, events and hashes alike.
void check_record(const std::vector<StrokeRecord>& logs, const fs::path& dir) {
  const DocPtr d0 = replay_doc(logs[0]);
  if (!std::getenv("PB_RECORD_STROKES")) CHECK(!StrokeLog::record(*d0, 1, {}, 1));
  int refused = 0;
  try {
    StrokeLog::record(*d0, 1, {}, 1, "", dir.string())->finish(Stroke(d0, 1, {}).state());  // not merged
  } catch (const std::invalid_argument&) {
    refused++;
  }
  CHECK(refused == 1);
  std::vector<std::future<std::string>> written;
  for (const StrokeRecord& r : logs) {
    const DocPtr d = replay_doc(r);
    const auto log = StrokeLog::record(*d, d->selected[0], r.brush, r.zoom, r.doc, dir.string());
    CHECK(log);
    Stroke s(d, d->selected[0], r.brush);
    for (const PenEvent& e : r.events) s.add(e), log->add(e);
    s.finish();
    written.push_back(log->finish(s.wait()));
  }
  std::set<std::string> paths;
  for (size_t i = 0; i < logs.size(); i++) {
    const std::string path = written[i].get();
    CHECK(to_jsonl(read_stroke_log(path)) == to_jsonl(logs[i]));
    paths.insert(path);
  }
  CHECK(paths.size() == logs.size());
}

// A process whose last stroke was dropped while its job made masks exits cleanly (test_strokelog_exit below).
void check_exit() {
#if defined(__linux__) || defined(_WIN32)
#ifdef _WIN32
  char exe[MAX_PATH];
  CHECK(GetModuleFileNameA(nullptr, exe, MAX_PATH) > 0);
  _putenv_s("PB_STROKE_EXIT", "1");
  const std::string cmd = "\"" + std::string(exe) + "\" test_strokelog_exit";
#else
  const std::string cmd = "PB_STROKE_EXIT=1 '" + fs::read_symlink("/proc/self/exe").string() + "' test_strokelog_exit";
#endif
  FILE* p = popen(cmd.c_str(), "r");
  CHECK(p);
  std::string out;
  char buf[256];
  while (std::fgets(buf, sizeof buf, p)) out += buf;
  const int status = pclose(p);
#ifdef _WIN32
  _putenv_s("PB_STROKE_EXIT", "");
#endif
  if (status != 0) std::fprintf(stderr, "test_strokelog: the exiting process ended with status %d\n", status);
  CHECK(status == 0 && out.find("stroke dropped") != std::string::npos);
#else
  std::printf("skipping the exit check: Linux and Windows only\n");
#endif
}

}  // namespace

TEST(test_strokelog) {
  const fs::path dir = fs::temp_directory_path() /
                       ("pb-strokelog-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  fs::create_directories(dir);
  check_format(dir);
  check_record(check_logs(), dir);
  fs::remove_all(dir);
  check_exit();
}

// What check_exit() runs in a process of its own (PB_STROKE_EXIT set), else nothing: a 300 px stroke at 1% spacing,
// a new mask nearly every dab, on one thread, dropped while its job makes masks; the part of its dabs under way goes on
// for some 70 ms more, through exit's destruction of statics.
TEST(test_strokelog_exit) {
  if (!std::getenv("PB_STROKE_EXIT")) return;
  set_stroke_threads(1);
  const DocPtr d = run(*new_document(3000, 3000, Mode::Rgb, Depth::U8, nullptr), AddLayer{});
  BrushParams b;
  b.size = 300, b.spacing = 0.01f;
  {
    Stroke s(d, d->selected[0], b);
    for (int i = 0; i < 2000; i++) {
      const double p = std::sin(i / 40.);
      s.add({i / 200., float(500 + i), float(1500 + 300 * std::sin(i / 90.)), float(0.4 + 0.6 * p * p)});
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  std::printf("stroke dropped\n");
  std::fflush(stdout);
}
