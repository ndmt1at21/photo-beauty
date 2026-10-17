// The tile store (tilecache.h): pixels pushed out of RAM (zstd in RAM, scratch, a frame elsewhere) come back exact;
// pinned tiles stay; the budget holds while 8 threads write and read back 1 GB of tiles through 64 MB; caches are
// asked to release; the scratch file goes with its process.
#include <zstd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "check.h"
#include "tile.h"
#include "tilecache.h"

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

using namespace pb;
namespace fs = std::filesystem;

namespace {

constexpr Format kFormats[] = {{Depth::U8, 1},  {Depth::U8, 2},  {Depth::U8, 4},
                               {Depth::U16, 1}, {Depth::U16, 2}, {Depth::U16, 4}};
constexpr Format kRgba8{Depth::U8, 4};
size_t tile_bytes(Format f) { return size_t(kTileSize) * kTileSize * f.bytes(); }

uint64_t mix(uint64_t x) {  // splitmix64
  x += 0x9e3779b97f4a7c15ull;
  x = (x ^ x >> 30) * 0xbf58476d1ce4e5b9ull;
  x = (x ^ x >> 27) * 0x94d049bb133111ebull;
  return x ^ x >> 31;
}
// Contents by kind: noise (incompressible), runs of 4 equal words (zstd to 25%), runs of 64 (to 2%), noise with
// one byte in 8 zero (to 94%: not worth keeping in RAM).
enum Kind { kNoise, kRuns, kFlat, kSparse };
uint64_t word(uint64_t seed, size_t i, int kind) {
  const uint64_t w = mix(seed * 0x100000001b3ull + (kind == kRuns ? i / 4 : kind == kFlat ? i / 64 : i));
  return kind == kSparse ? w & ~uint64_t(0xff) : w;
}
void put(uint8_t* p, Format f, uint64_t seed, int kind) {
  for (size_t i = 0; i < tile_bytes(f) / 8; i++) {
    const uint64_t w = word(seed, i, kind);
    std::memcpy(p + 8 * i, &w, 8);
  }
}
bool same(const uint8_t* p, Format f, uint64_t seed, int kind) {
  for (size_t i = 0; i < tile_bytes(f) / 8; i++) {
    uint64_t w;
    std::memcpy(&w, p + 8 * i, 8);
    if (w != word(seed, i, kind)) return false;
  }
  return true;
}
TilePtr made(Format f, uint64_t seed, int kind) {
  const std::shared_ptr<Tile> t = Tile::make(f);
  put(t->write().data(), f, seed, kind);
  return t;
}
bool holds(const TilePtr& t, uint64_t seed, int kind) { return same(t->pin().data(), t->format(), seed, kind); }

// A zstd frame of a tile's content, kept alive by the shared_ptr, as a .pbd's mapping would be.
std::shared_ptr<const std::vector<uint8_t>> frame(Format f, uint64_t seed, int kind) {
  std::vector<uint8_t> raw(tile_bytes(f));
  put(raw.data(), f, seed, kind);
  auto z = std::make_shared<std::vector<uint8_t>>(ZSTD_compressBound(raw.size()));
  z->resize(ZSTD_compress(z->data(), z->size(), raw.data(), raw.size(), 1));
  return z;
}

// Raw pixels go out on trim_tiles(): zstd in RAM when that pays, else scratch; an elsewhere() frame costs nothing to
// drop. Ids stay; pins bring the pixels back with the copies kept, so they go out again for free; a write drops the
// copy (and the frame's owner), a tile's death everything.
void check_tiers() {
  trim_tiles();
  const TileStats s0 = tile_stats();
  const size_t n = tile_bytes(kRgba8);
  {
    const TilePtr noise = made(kRgba8, 1, kNoise), runs = made(kRgba8, 2, kRuns), sparse = made(kRgba8, 8, kSparse);
    auto f = frame(kRgba8, 3, kRuns);
    const std::weak_ptr<const void> owner = f;
    const TilePtr away = Tile::elsewhere(kRgba8, f, f->data(), f->size());
    f.reset();
    const uint64_t ids[] = {noise->id(), runs->id(), away->id()};
    TileStats s = tile_stats();
    CHECK(s.raw == s0.raw + 3 * n && s.compressed == s0.compressed && s.scratch == s0.scratch && !owner.expired());
    {
      const Pin p = noise->pin(), again = noise->pin();
      CHECK(tile_stats().pinned == s0.pinned + n && again.data() == p.data());
    }
    CHECK(tile_stats().pinned == s0.pinned);

    trim_tiles();
    s = tile_stats();
    const size_t zstd = s.compressed - s0.compressed;
    CHECK(s.raw == s0.raw && zstd > 0 && zstd < n / 2 && s.scratch == s0.scratch + 2 * n && s.pinned == s0.pinned);
    CHECK(noise->id() == ids[0] && runs->id() == ids[1] && away->id() == ids[2]);
    {
      const Pin p = noise->pin(), q = runs->pin(), r = away->pin();
      CHECK(same(p.data(), kRgba8, 1, kNoise) && same(q.data(), kRgba8, 2, kRuns) && same(r.data(), kRgba8, 3, kRuns));
      s = tile_stats();
      CHECK(s.raw == s0.raw + 3 * n && s.pinned == s0.pinned + 3 * n);
      CHECK(s.compressed == s0.compressed + zstd && s.scratch == s0.scratch + 2 * n);
      trim_tiles();  // pinned: they stay
      CHECK(tile_stats().raw == s0.raw + 3 * n && same(p.data(), kRgba8, 1, kNoise));
    }
    CHECK(holds(sparse, 8, kSparse));
    trim_tiles();  // dropped, nothing packed again
    s = tile_stats();
    CHECK(s.raw == s0.raw && s.compressed == s0.compressed + zstd && s.scratch == s0.scratch + 2 * n);

    // A write drops the copy: here the scratch one, then the frame's owner.
    const std::shared_ptr<Tile> mine = std::const_pointer_cast<Tile>(noise);  // only this test holds it
    {
      const WritePin w = mine->write();
      CHECK(same(w.data(), kRgba8, 1, kNoise));
      w.data()[5] ^= 1;
    }
    CHECK(noise->id() != ids[0] && tile_stats().scratch == s0.scratch + n);
    trim_tiles();  // out again: the new pixels
    {
      const Pin p = noise->pin();
      std::vector<uint8_t> want(n);
      put(want.data(), kRgba8, 1, kNoise);
      want[5] ^= 1;
      CHECK(std::memcmp(p.data(), want.data(), n) == 0);
    }
    std::const_pointer_cast<Tile>(away)->write();
    CHECK(owner.expired() && holds(away, 3, kRuns));
  }
  CHECK(tile_stats().raw == s0.raw && tile_stats().compressed == s0.compressed && tile_stats().scratch == s0.scratch);

  // Without the zstd tier everything goes to scratch.
  set_tile_compression(false);
  {
    const TilePtr runs = made(kRgba8, 4, kRuns);
    trim_tiles();
    CHECK(tile_stats().compressed == s0.compressed && tile_stats().scratch == s0.scratch + n && holds(runs, 4, kRuns));
  }
  set_tile_compression(true);
  CHECK(tile_stats().scratch == s0.scratch);
}

// also_elsewhere(): the store's own copy goes, the frame stands in; a corrupt frame throws on every pin, harmlessly.
void check_elsewhere() {
  trim_tiles();
  const TileStats s0 = tile_stats();
  const Format f{Depth::U16, 4};
  {
    const TilePtr t = made(f, 5, kNoise);
    trim_tiles();
    CHECK(tile_stats().scratch == s0.scratch + tile_bytes(f));
    auto z = frame(f, 5, kNoise);
    const std::weak_ptr<const void> owner = z;
    t->also_elsewhere(z, z->data(), z->size());
    z.reset();
    CHECK(tile_stats().scratch == s0.scratch && !owner.expired() && holds(t, 5, kNoise));
    trim_tiles();
    CHECK(tile_stats().scratch == s0.scratch && tile_stats().raw == s0.raw && holds(t, 5, kNoise));
  }
  CHECK(tile_stats().raw == s0.raw);
  auto junk = std::make_shared<std::vector<uint8_t>>(frame(f, 6, kRuns)->size());
  for (size_t i = 0; i < junk->size(); i++) (*junk)[i] = uint8_t(mix(i));
  const TilePtr bad = Tile::elsewhere(f, junk, junk->data(), junk->size());
  for (int i = 0; i < 2; i++) {
    bool threw = false;
    try {
      bad->pin();
    } catch (const std::runtime_error&) {
      threw = true;
    }
    CHECK(threw && tile_stats().raw == s0.raw && tile_stats().pinned == s0.pinned);
  }
}

// Least recently used goes first. Each tile's frame elsewhere here holds other pixels than its own, so a tile whose
// raw pixels went (for free, the frame standing in) reads back the frame's: which ones went shows.
void check_lru() {
  const size_t old = tile_budget();
  const Format f{Depth::U16, 4};
  const size_t n = tile_bytes(f);
  set_tile_budget(0);  // the least: what other tests left goes, but for zstd copies within half of it
  trim_tiles();
  set_tile_budget(old);
  const auto fake = frame(f, 99, kRuns);
  std::vector<TilePtr> t;
  for (int i = 0; i < 4; i++) {
    t.push_back(made(f, 300 + i, kNoise));
    t.back()->also_elsewhere(fake, fake->data(), fake->size());
  }
  holds(t[0], 300, kNoise);  // used last now: t[1] is the least recently used
  auto held = [] {
    const TileStats s = tile_stats();
    return s.raw - s.pinned + s.compressed + s.charged;
  };
  // A budget whose 80% (the background job's aim) is met by pushing out one tile.
  const size_t target = held() - n, budget = (target / 8 + 1) * 10;
  set_tile_budget(budget);
  const auto t0 = std::chrono::steady_clock::now();
  while (held() > budget / 10 * 8 && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(10))
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  CHECK(held() == target);
  CHECK(holds(t[0], 300, kNoise) && holds(t[2], 302, kNoise) && holds(t[3], 303, kNoise) && holds(t[1], 99, kRuns));
  set_tile_budget(old);
}

// A pinned tile keeps its pixels where they are while the store pushes every other tile out around it.
void check_pinned() {
  const size_t old = tile_budget();
  set_tile_budget(4 << 20);
  trim_tiles();
  {
    const TilePtr kept = made(kRgba8, 7, kNoise);
    const Pin p = kept->pin();
    std::vector<TilePtr> others;
    auto kind = [](int i) { return i % 8 ? kRuns : kNoise; };
    for (int i = 0; i < 48; i++) others.push_back(made(kRgba8, 100 + i, kind(i)));  // 12 MB through 4
    const TileStats s = tile_stats();
    CHECK(s.pinned >= tile_bytes(kRgba8) && s.raw + s.compressed <= (4 << 20) + s.pinned);
    CHECK(kept->pin().data() == p.data() && same(p.data(), kRgba8, 7, kNoise));
    for (int i = 0; i < 48; i++) CHECK(holds(others[size_t(i)], 100 + i, kind(i)));
    trim_tiles();  // zstd copies past half the budget go to scratch: these come to 2.7 MB
    CHECK(tile_stats().compressed <= (2 << 20) && tile_stats().raw == tile_bytes(kRgba8));
  }
  set_tile_budget(old);
}

// Evictions racing pins and writes of the same tiles: 8 threads each rewrite a tile of their own, read one of 4 shared
// ones, then read back their own, through a budget of two tiles, so the others keep pushing it out from under them;
// pinned bytes stay within raw ones meanwhile.
void check_contention() {
  const size_t old = tile_budget();
  const Format f{Depth::U16, 4};
  set_tile_budget(2 * tile_bytes(f));
  trim_tiles();
  std::vector<TilePtr> shared;
  for (int i = 0; i < 4; i++) shared.push_back(made(f, 500 + i, kRuns));
  std::atomic<bool> done{false};
  std::atomic<size_t> wrong{0};
  std::thread monitor([&] {
    while (!done) {
      const TileStats s = tile_stats();
      wrong += s.pinned > s.raw;
    }
  });
  std::vector<std::thread> threads;
  for (int t = 0; t < 8; t++)
    threads.emplace_back([&, t] {
      const std::shared_ptr<Tile> mine = Tile::make(f);
      for (uint64_t k = 0; k < 60; k++) {
        const uint64_t seed = uint64_t(t) << 32 | k;
        const int kind = int(k % 3);
        put(mine->write().data(), f, seed, kind);
        wrong += !holds(shared[k % 4], 500 + k % 4, kRuns);
        wrong += !holds(mine, seed, kind);
      }
    });
  for (std::thread& t : threads) t.join();
  done = true;
  monitor.join();
  CHECK(!wrong);
  set_tile_budget(old);
}

// Over budget with no tiles left to push out, the background job asks the caches.
void check_users() {
  const size_t old = tile_budget(), base = tile_stats().charged;
  trim_tiles();
  set_tile_budget(64 << 20);
  std::atomic<int> calls{0};
  BudgetUser* self = nullptr;
  BudgetUser user([&](size_t) {
    calls++;
    self->charge(-(16 << 20));
    return size_t(16) << 20;
  });
  self = &user;
  user.charge(96 << 20);  // past 90%: the job wakes; tiles out of RAM already, it asks
  const auto t0 = std::chrono::steady_clock::now();
  while (tile_stats().charged > base + (48 << 20) && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(30))
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  CHECK(calls >= 1 && tile_stats().charged <= base + (48 << 20));
  set_tile_budget(old);
  const size_t left = tile_stats().charged - base;
  CHECK(left > 0);
  {
    BudgetUser other;
    other.charge(1000);
    CHECK(tile_stats().charged == base + left + 1000);
  }
  CHECK(tile_stats().charged == base + left);
}

// 8 threads write about 1 GB of tiles of every format and kind through a 64 MB budget, then read them all back in a
// random order, some at once by several threads, every byte checked; meanwhile the raw and compressed bytes never
// pass the budget plus what is pinned. Everything goes when the tiles do.
void check_gigabyte() {
  const size_t old = tile_budget(), budget = 64 << 20;
  set_tile_budget(budget);
  trim_tiles();
  const TileStats s0 = tile_stats();
  struct Item {
    Format f;
    int kind;
    TilePtr tile;
  };
  std::vector<Item> items;
  std::mt19937 rng(9);
  for (size_t total = 0; total < (size_t(1) << 30);) {
    items.push_back({kFormats[rng() % 6], int(rng() % 3), nullptr});
    total += tile_bytes(items.back().f);
  }
  std::atomic<bool> done{false};
  std::atomic<size_t> samples{0}, most_scratch{0}, most_zstd{0};
  std::thread monitor([&] {
    while (!done) {
      const TileStats s = tile_stats();
      CHECK(s.raw + s.compressed <= budget + s.pinned);
      most_scratch = std::max<size_t>(most_scratch, s.scratch);
      most_zstd = std::max<size_t>(most_zstd, s.compressed);
      samples++;
    }
  });
  constexpr int kThreads = 8;
  auto on_threads = [](const auto& fn) {
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; t++) threads.emplace_back(fn, t);
    for (auto& t : threads) t.join();
  };
  on_threads([&](int t) {
    for (size_t i = size_t(t); i < items.size(); i += kThreads) items[i].tile = made(items[i].f, i, items[i].kind);
  });
  std::vector<size_t> order(items.size());
  std::iota(order.begin(), order.end(), 0);
  std::shuffle(order.begin(), order.end(), rng);
  std::atomic<size_t> wrong{0};
  on_threads([&](int t) {
    std::mt19937 r{unsigned(t)};
    for (size_t j = size_t(t); j < order.size(); j += kThreads) {
      const size_t i = order[j], k = j % 16 == 0 ? r() % items.size() : i;  // now and then one another reads too
      for (const size_t at : {i, k})
        if (!holds(items[at].tile, at, items[at].kind)) wrong++;
    }
  });
  done = true;
  monitor.join();
  std::printf("tilecache: 1 GB through 64 MB: at most %zu MB in scratch, %zu MB zstd; %zu samples\n",
              most_scratch.load() >> 20, most_zstd.load() >> 20, samples.load());
  CHECK(!wrong && samples > 0 && most_scratch > s0.scratch + (size_t(256) << 20) && most_zstd > s0.compressed);
  items.clear();
  const TileStats s1 = tile_stats();
  CHECK(s1.raw == s0.raw && s1.pinned == s0.pinned && s1.compressed + s1.scratch == s0.compressed + s0.scratch);
  set_tile_budget(old);
}

// Scratch files of process pid: Linux keeps them nameless but open, Windows named in `dir`.
size_t scratch_files(const fs::path& dir, [[maybe_unused]] long pid) {
  size_t n = 0;
#ifdef __linux__
  const std::string prefix = fs::canonical(dir).string() + "/pb-scratch-";
  for (const auto& e : fs::directory_iterator("/proc/" + std::to_string(pid) + "/fd")) {
    std::error_code ec;
    if (fs::read_symlink(e.path(), ec).string().find(prefix) == 0) n++;
  }
#else
  for ([[maybe_unused]] const auto& e : fs::directory_iterator(dir)) n++;
#endif
  return n;
}

// A process filling scratch, killed: nothing of it stays in the scratch directory.
void check_scratch_dies() {
#if defined(_WIN32) || defined(__linux__)
#ifdef _WIN32
  const long self = long(GetCurrentProcessId());
#else
  const long self = long(getpid());
#endif
  const fs::path dir = fs::temp_directory_path() / ("pb-scratch-test-" + std::to_string(self));
  fs::remove_all(dir);
  fs::create_directories(dir);
  std::string got;
#ifdef _WIN32
  SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
  HANDLE r = nullptr, w = nullptr;
  CHECK(CreatePipe(&r, &w, &sa, 0) && SetHandleInformation(r, HANDLE_FLAG_INHERIT, 0));
  wchar_t exe[MAX_PATH];
  CHECK(GetModuleFileNameW(nullptr, exe, MAX_PATH) > 0);
  std::wstring cmd = L"\"" + std::wstring(exe) + L"\" test_tilecache_child";
  SetEnvironmentVariableW(L"PB_SCRATCH_CHILD", dir.wstring().c_str());
  STARTUPINFOW si{};
  si.cb = sizeof si;
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE), si.hStdOutput = w, si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
  PROCESS_INFORMATION pi{};
  const bool spawned = CreateProcessW(exe, cmd.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi);
  SetEnvironmentVariableW(L"PB_SCRATCH_CHILD", nullptr);
  CloseHandle(w);
  CHECK(spawned);
  char buf[256];
  for (DWORD n = 0; got.find("ready") == std::string::npos && ReadFile(r, buf, sizeof buf, &n, nullptr) && n;)
    got.append(buf, n);
  const long child = long(pi.dwProcessId);
#else
  int fds[2];
  CHECK(pipe(fds) == 0);
  std::vector<std::string> env;
  for (char** e = environ; *e; e++) env.emplace_back(*e);
  env.push_back("PB_SCRATCH_CHILD=" + dir.string());
  std::vector<char*> envp;
  for (std::string& e : env) envp.push_back(e.data());
  envp.push_back(nullptr);
  char arg0[] = "pb-tests", arg1[] = "test_tilecache_child";
  char* argv[] = {arg0, arg1, nullptr};
  const pid_t pid = fork();
  if (pid == 0) {
    dup2(fds[1], 1);
    close(fds[0]), close(fds[1]);
    execve("/proc/self/exe", argv, envp.data());
    _exit(127);
  }
  close(fds[1]);
  CHECK(pid > 0);
  char buf[256];
  for (ssize_t n; got.find("ready") == std::string::npos && (n = read(fds[0], buf, sizeof buf)) > 0;)
    got.append(buf, size_t(n));
  const long child = long(pid);
#endif
  // Killed before any CHECK: a failing one would leave it running.
  const bool ready = got.find("ready") != std::string::npos;  // else the child failed: its CHECK is above
  const size_t files = ready ? scratch_files(dir, child) : 0;
#ifdef _WIN32
  TerminateProcess(pi.hProcess, 1);
  WaitForSingleObject(pi.hProcess, INFINITE);
  CloseHandle(pi.hProcess), CloseHandle(pi.hThread), CloseHandle(r);
#else
  const bool nameless = fs::is_empty(dir);
  kill(pid, SIGKILL);
  int status = 0;
  waitpid(pid, &status, 0);
  close(fds[0]);
#endif
  CHECK(ready && files == 1);
#ifndef _WIN32
  CHECK(nameless);
#endif
  const auto t0 = std::chrono::steady_clock::now();
  while (!fs::is_empty(dir) && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(10))
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  CHECK(fs::is_empty(dir));
  fs::remove_all(dir);
#else
  std::printf("skipping the scratch kill check: Linux and Windows only\n");
#endif
}

}  // namespace

TEST(test_tilecache) {
  CHECK(physical_memory() > (size_t(1) << 30));
  CHECK(tile_budget() >= std::min(physical_memory() / 2, size_t(1) << 30));
  check_tiers();
  check_elsewhere();
  check_lru();
  check_pinned();
  check_contention();
  check_users();
  check_gigabyte();
  check_scratch_dies();
  trim_tiles();  // leaves no eviction running for the tests after
}

// What check_scratch_dies() runs in a process of its own (PB_SCRATCH_CHILD: the scratch directory); else nothing.
// Tiles out in scratch, then gone, close the file; tiles out again, it says "ready" and waits to be killed.
TEST(test_tilecache_child) {
  const char* dir = std::getenv("PB_SCRATCH_CHILD");
  if (!dir) return;
  set_scratch_dir(dir);
#ifdef _WIN32
  const long self = long(GetCurrentProcessId());
#else
  const long self = long(getpid());
#endif
  auto spill = [] {
    std::vector<TilePtr> tiles;
    for (int i = 0; i < 4; i++) tiles.push_back(made(kRgba8, 200 + i, kNoise));
    trim_tiles();
    CHECK(tile_stats().scratch >= 4 * tile_bytes(kRgba8));
    return tiles;
  };
  spill();
  const auto t0 = std::chrono::steady_clock::now();
  while (scratch_files(dir, self) && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(2))
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  CHECK(tile_stats().scratch == 0 && scratch_files(dir, self) == 0);
  const std::vector<TilePtr> kept = spill();
  CHECK(scratch_files(dir, self) == 1);
  std::printf("ready\n");
  std::fflush(stdout);
  for (;;) {  // until killed; should the test die first, writing to its pipe fails and ends this one too
    std::this_thread::sleep_for(std::chrono::seconds(1));
    if (std::fputc('.', stdout) == EOF || std::fflush(stdout) == EOF) std::exit(1);
  }
}
