#include "tilecache.h"

#include <zstd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "jobs.h"
#include "profile.h"
#include "tile.h"

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <stdlib.h>  // mkostemp
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif
#ifdef __APPLE__
#include <sys/sysctl.h>
#endif
#ifdef PB_TRACY
#include <tracy/Tracy.hpp>
#endif
#if defined(__SANITIZE_ADDRESS__)
#define PB_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define PB_ASAN 1
#endif
#endif
#ifdef PB_ASAN
#include <sanitizer/asan_interface.h>
#endif

namespace pb {
namespace {

constexpr size_t kPixels = size_t(kTileSize) * kTileSize;
constexpr int kLevel = 1;                       // zstd's; bench/tilecache.cpp weighs it against 3
constexpr size_t kMinBudget = size_t(1) << 20;  // two of the largest tiles

size_t raw_size(Format f) { return kPixels * size_t(f.bytes()); }

// Raw pixels and zstd copies lie in buffers of whole pages straight from the OS (mmap, VirtualAlloc), not from
// malloc: glibc's, its mmap threshold rising past a tile's size once one is freed, keeps them in its arenas, where
// freed ones stay resident between live ones (E1 saw RSS past the budget by half: D2, a 3 GB budget, 4.6 GB). A freed
// buffer waits in a pool for the next of its size, as faulting fresh pages in costs far more than filling them (WSL2:
// some 270 us a 256 KB tile against 16); past kPoolMax bytes free buffers go back to the OS. The budget counts a
// copy's whole pages. Under ASan buffers come from operator new, for its redzones, and wait poisoned. Any thread.
// ponytail: a mapping a buffer, the kernel merging neighbours (D2 under 3 GB: 6,168 at most); Linux caps a process at
// vm.max_map_count, 65,530 where distros keep the kernel's default, which a budget past some 30 GB might reach: carve
// buffers out of large slabs then.
constexpr size_t kPage = 4096, kPoolMax = size_t(32) << 20;
constexpr size_t kMostPages = kPixels * 8 / kPage;  // a raw RGBA U16 tile's
size_t paged(size_t n) { return (n + kPage - 1) / kPage * kPage; }
struct Pool {
  std::mutex m;
  uint8_t* free[kMostPages + 1] = {};  // by pages: each a list linked through its buffers' first bytes
  size_t bytes = 0;                    // theirs
};
Pool& pool() {
  static Pool* p = new Pool;  // never destroyed, as the store
  return *p;
}

uint8_t* take(size_t n) {  // whole pages for n bytes, at most kMostPages
  n = paged(n);
  Pool& p = pool();
  {
    const std::lock_guard<std::mutex> l(p.m);
    if (uint8_t*& head = p.free[n / kPage]) {
      uint8_t* r = head;
#ifdef PB_ASAN
      ASAN_UNPOISON_MEMORY_REGION(r, n);
#endif
      std::memcpy(&head, r, sizeof(uint8_t*));
      p.bytes -= n;
      return r;
    }
  }
#ifdef PB_ASAN
  return static_cast<uint8_t*>(::operator new(n, std::align_val_t(64)));
#elif defined(_WIN32)
  void* r = VirtualAlloc(nullptr, n, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
  if (!r) throw std::bad_alloc();
  return static_cast<uint8_t*>(r);
#else
  void* r = mmap(nullptr, n, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (r == MAP_FAILED) throw std::bad_alloc();
  return static_cast<uint8_t*>(r);
#endif
}

void give(uint8_t* r, size_t n) {  // a buffer of take(n)
  n = paged(n);
  Pool& p = pool();
  {
    const std::lock_guard<std::mutex> l(p.m);
    if (p.bytes + n <= kPoolMax) {
      uint8_t*& head = p.free[n / kPage];
      std::memcpy(r, &head, sizeof(uint8_t*));
      head = r;
      p.bytes += n;
#ifdef PB_ASAN
      ASAN_POISON_MEMORY_REGION(r + sizeof(uint8_t*), n - sizeof(uint8_t*));
#endif
      return;
    }
  }
#ifdef PB_ASAN
  ::operator delete(r, std::align_val_t(64));
#elif defined(_WIN32)
  VirtualFree(r, 0, MEM_RELEASE);
#else
  munmap(r, n);
#endif
}

// What a thread compresses and decompresses with.
struct Zstd {
  ZSTD_CCtx* c = ZSTD_createCCtx();
  ZSTD_DCtx* d = ZSTD_createDCtx();
  std::vector<uint8_t> frame;  // on its way in or out
  ~Zstd() {
    ZSTD_freeCCtx(c);
    ZSTD_freeDCtx(d);
  }
};
Zstd& zstd() {
  thread_local Zstd z;
  return z;
}

// n raw bytes as a zstd frame (take(size), `size` bytes) when that saves an eighth, else null.
uint8_t* squeeze(const uint8_t* raw, size_t n, size_t& size) noexcept {
  PROFILE_ZONE("tile_compress");
  try {
    Zstd& z = zstd();
    z.frame.resize(ZSTD_compressBound(n));
    const size_t c = z.c ? ZSTD_compressCCtx(z.c, z.frame.data(), z.frame.size(), raw, n, kLevel) : 0;
    if (!c || ZSTD_isError(c) || paged(c) > n - n / 8) return nullptr;
    uint8_t* out = take(c);
    std::memcpy(out, z.frame.data(), c);
    size = c;
    return out;
  } catch (...) {  // out of memory: scratch takes it raw
    return nullptr;
  }
}
bool unsqueeze(const uint8_t* frame, size_t size, uint8_t* raw, size_t n) {
  PROFILE_ZONE("tile_decompress");
  ZSTD_DCtx* d = zstd().d;
  return d && ZSTD_decompressDCtx(d, raw, n, frame, size) == n;
}

#ifdef _WIN32
using File = HANDLE;
const File kNoFile = INVALID_HANDLE_VALUE;
#else
using File = int;
constexpr File kNoFile = -1;
#endif

File open_scratch(const std::filesystem::path& dir) {
#ifdef _WIN32
  static std::atomic<unsigned> serial{0};
  for (int i = 0; i < 100; i++) {
    const std::filesystem::path name =
        dir / ("pb-scratch-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(serial++) + ".tmp");
    const HANDLE h = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                 FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (h != INVALID_HANDLE_VALUE || GetLastError() != ERROR_FILE_EXISTS) return h;
  }
  return kNoFile;
#else
  std::string name = (dir / "pb-scratch-XXXXXX").string();
  const int fd = mkostemp(name.data(), O_CLOEXEC);
  if (fd >= 0) unlink(name.c_str());
  return fd;
#endif
}
void close_file(File f) {
#ifdef _WIN32
  CloseHandle(f);
#else
  close(f);
#endif
}
// All n bytes, at offset `at`; any thread at once.
bool write_at(File f, uint64_t at, const uint8_t* p, size_t n) {
  PROFILE_ZONE("scratch_write");
  while (n) {
#ifdef _WIN32
    OVERLAPPED o{};
    o.Offset = DWORD(at), o.OffsetHigh = DWORD(at >> 32);
    DWORD done = 0;
    if (!WriteFile(f, p, DWORD(std::min<size_t>(n, size_t(1) << 30)), &done, &o) || !done) return false;
#else
    const ssize_t done = pwrite(f, p, n, off_t(at));
    if (done < 0 && errno == EINTR) continue;
    if (done <= 0) return false;
#endif
    p += done, n -= size_t(done), at += uint64_t(done);
  }
  return true;
}
bool read_at(File f, uint64_t at, uint8_t* p, size_t n) {
  PROFILE_ZONE("scratch_read");
  while (n) {
#ifdef _WIN32
    OVERLAPPED o{};
    o.Offset = DWORD(at), o.OffsetHigh = DWORD(at >> 32);
    DWORD done = 0;
    if (!ReadFile(f, p, DWORD(std::min<size_t>(n, size_t(1) << 30)), &done, &o) || !done) return false;
#else
    const ssize_t done = pread(f, p, n, off_t(at));
    if (done < 0 && errno == EINTR) continue;
    if (done <= 0) return false;
#endif
    p += done, n -= size_t(done), at += uint64_t(done);
  }
  return true;
}

// The scratch file: written at its end, read at offsets, under the store's lock but for the reads and writes, which
// take `file` under it first: the file stays open while any of it is in use.
struct Scratch {
  std::filesystem::path dir;
  File file = kNoFile;
  uint64_t end = 0;
  size_t live = 0;      // bytes in use, reserved ones included
  bool broken = false;  // it failed: no more spills until it empties or moves

  // Room for n bytes at its end, opening it first: false (said once) when it cannot be.
  bool reserve(size_t n, uint64_t& at) {
    if (broken) return false;
    if (file == kNoFile && (file = open_scratch(dir)) == kNoFile) return fail("cannot create a file in"), false;
    at = end, end += n, live += n;
    return true;
  }
  void release(size_t n) {
    live -= n;
    if (live || file == kNoFile) return;
    close_file(file);
    file = kNoFile, end = 0, broken = false;
  }
  void fail(const char* what) {
    if (!broken)
      std::fprintf(stderr, "pb: scratch: %s %s: tiles stay in RAM\n", what,
                   reinterpret_cast<const char*>(dir.u8string().c_str()));
    broken = true;
  }
};

// What to free once the store's lock is released: free() it after unlocking.
struct Garbage {
  uint8_t* raw = nullptr;
  size_t raw_size = 0;
  const uint8_t* zstd = nullptr;
  size_t zstd_size = 0;
  std::shared_ptr<const void> owner;
  void free() {
    if (raw) give(std::exchange(raw, nullptr), raw_size);
    if (zstd) give(const_cast<uint8_t*>(std::exchange(zstd, nullptr)), zstd_size);
    owner.reset();
  }
  ~Garbage() { free(); }
};

}  // namespace

// Where every data tile's pixels are, and the budget. Never destroyed: tiles may die after main() returns.
struct TileStore {
  // Tiles in one of two lists by a Link of theirs: hot_, unpinned with raw pixels; ram_, with a zstd copy in RAM.
  // Most recently used first. A busy tile is in neither.
  template <bool Ram>
  struct List {
    const Tile *head = nullptr, *tail = nullptr;
    static Tile::Link& link(const Tile* t) { return Ram ? t->ram_ : t->hot_; }
    void push_front(const Tile* t) {
      Tile::Link& l = link(t);
      if (l.in) return;
      l = {nullptr, head, true};
      (head ? link(head).prev : tail) = t;
      head = t;
    }
    void erase(const Tile* t) {
      Tile::Link& l = link(t);
      if (!l.in) return;
      (l.prev ? link(l.prev).next : head) = l.next;
      (l.next ? link(l.next).prev : tail) = l.prev;
      l = {};
    }
  };

  std::mutex m;
  std::condition_variable cv;  // a tile no longer busy
  size_t budget, raw = 0, pinned = 0, ram = 0, charged = 0;
  int evicting = 0;  // tiles being packed or spilled
  bool compress = true, background_queued = false;
  List<false> hot;
  List<true> rams;
  Scratch scratch;
  std::mutex users_m;  // users, and their release() while one runs
  std::vector<BudgetUser*> users;

  TileStore() {
    const size_t mem = physical_memory();
    budget = std::max(mem ? mem / 10 * 6 : size_t(4) << 30, kMinBudget);
    std::error_code e;
    scratch.dir = std::filesystem::temp_directory_path(e);
    if (e) scratch.dir = ".";
  }

  size_t held() const { return raw - pinned + ram + charged; }  // what the budget counts

  void plot() const {
#ifdef PB_TRACY
    TracyPlot("tiles raw MB", double(raw) / 1048576);
    TracyPlot("tiles compressed MB", double(ram) / 1048576);
    TracyPlot("tiles scratch MB", double(scratch.live) / 1048576);
#endif
  }

  // Back in the lists once a thread is done with it without the lock.
  void settle(const Tile* t) {
    t->busy_ = false;
    if (t->packed_ == Tile::Packed::Ram) rams.push_front(t);
    if (t->raw_ && !t->pins_) hot.push_front(t);
    cv.notify_all();
    plot();
  }

  // Its pixels change or it goes: the packed copy with them. Not busy.
  void drop_packed(const Tile* t, Garbage& g) {
    switch (t->packed_) {
      case Tile::Packed::None:
        return;
      case Tile::Packed::Ram:
        rams.erase(t);
        ram -= paged(t->size_);
        g.zstd = t->bytes_, g.zstd_size = t->size_;
        break;
      case Tile::Packed::Scratch:
        scratch.release(t->size_);
        break;
      case Tile::Packed::Elsewhere:
        g.owner = std::move(t->owner_);
        break;
    }
    t->packed_ = Tile::Packed::None;
    t->bytes_ = nullptr;
    t->size_ = 0;
    plot();
  }

  // Pushes one thing out, least recently used first, unlocking while it compresses or writes: zstd copies past their
  // cap (half the budget) to scratch; else the raw pixels of the coldest unpinned tile; else, when `deep`, zstd copies
  // all the same. Waits instead when all it could take is on its way out already. False when nothing goes.
  bool evict_one(std::unique_lock<std::mutex>& l, bool deep) {
    const bool spillable = rams.tail && !scratch.broken;
    if (spillable && (ram > budget / 2 || (deep && !hot.tail))) return spill(l, rams.tail);
    if (hot.tail) return push_out(l, hot.tail);
    if (!evicting) return false;
    cv.wait(l);
    return true;
  }

  // The raw pixels of t, unpinned: dropped when it has a packed copy, else packed first (zstd in RAM when that pays
  // and the tier is on, else scratch). False when that failed (the scratch file did) and t stays as it was.
  bool push_out(std::unique_lock<std::mutex>& l, const Tile* t) {
    PROFILE_ZONE("tile_push_out");
    const size_t n = raw_size(t->format_);
    Garbage g;
    hot.erase(t);
    if (t->packed_ == Tile::Packed::None) {
      t->busy_ = true;
      evicting++;
      const bool tier = compress;
      l.unlock();
      size_t size = 0;
      uint8_t* z = tier ? squeeze(t->raw_, n, size) : nullptr;
      l.lock();
      uint64_t at = 0;
      bool spilled = false;
      if (!z && scratch.reserve(n, at)) {
        const File f = scratch.file;
        l.unlock();
        spilled = write_at(f, at, t->raw_, n);
        l.lock();
        if (!spilled) scratch.release(n), scratch.fail("cannot write to a file in");
      }
      evicting--;
      if (!z && !spilled) {  // stays raw
        settle(t);
        return false;
      }
      if (!z) {
        t->packed_ = Tile::Packed::Scratch, t->zstd_ = false, t->size_ = uint32_t(n), t->offset_ = at;
      } else if (!t->pins_) {
        t->packed_ = Tile::Packed::Ram, t->zstd_ = true, t->size_ = uint32_t(size), t->bytes_ = z;
        ram += paged(size);
      } else {
        g.zstd = z, g.zstd_size = size;  // pinned meanwhile: kept raw, without the copy
      }
    }
    if (t->packed_ != Tile::Packed::None && !t->pins_) {
      g.raw = std::exchange(t->raw_, nullptr), g.raw_size = n;
      raw -= n;
    }
    settle(t);
    l.unlock();
    g.free();
    l.lock();
    return true;
  }

  // t's zstd copy from RAM to scratch. False when the scratch file failed.
  bool spill(std::unique_lock<std::mutex>& l, const Tile* t) {
    PROFILE_ZONE("tile_spill");
    const size_t size = t->size_;
    uint64_t at = 0;
    if (!scratch.reserve(size, at)) return false;
    const File f = scratch.file;
    rams.erase(t);
    hot.erase(t);
    t->busy_ = true;
    evicting++;
    l.unlock();
    const bool ok = write_at(f, at, t->bytes_, size);
    l.lock();
    evicting--;
    Garbage g;
    if (ok) {
      g.zstd = std::exchange(t->bytes_, nullptr), g.zstd_size = size;
      t->packed_ = Tile::Packed::Scratch, t->offset_ = at;
      ram -= paged(size);
    } else {
      scratch.release(size), scratch.fail("cannot write to a file in");
    }
    settle(t);
    l.unlock();
    g.free();
    l.lock();
    return ok;
  }

  // Lock held: queues the Background job once the budget is 90% used.
  void check_background() {
    if (background_queued || held() <= budget / 10 * 9) return;
    background_queued = true;
    try {
      submit(Priority::Background, [this] { background(); });
    } catch (...) {  // out of memory: the next unpin tries again
      background_queued = false;
    }
  }
  void background() {
    PROFILE_ZONE("tiles_background");
    std::unique_lock l(m);
    while (held() > budget / 10 * 8) {
      if (evict_one(l, true)) continue;
      const size_t want = held() - budget / 10 * 8;  // tiles alone cannot: ask the caches
      l.unlock();
      size_t freed = 0;
      {
        const std::lock_guard u(users_m);
        for (BudgetUser* user : users)
          if (user->release_ && freed < want) freed += user->release_(want - freed);
      }
      l.lock();
      if (!freed) break;
    }
    background_queued = false;
  }
};

namespace {
TileStore& store() {
  static TileStore* s = new TileStore;
  return *s;
}
}  // namespace

Tile::~Tile() {
  if (solid_) return;
  TileStore& s = store();
  Garbage g;  // freed once unlocked
  std::unique_lock l(s.m);
  while (busy_) s.cv.wait(l);  // an eviction holds no reference
  s.hot.erase(this);
  if (raw_) {
    s.raw -= raw_size(format_);
    g.raw = raw_, g.raw_size = raw_size(format_);
  }
  s.drop_packed(this, g);
}

Pin Tile::pin() const {
  if (solid_) return Pin();
  TileStore& s = store();
  const size_t n = raw_size(format_);
  std::unique_lock l(s.m);
  for (;;) {
    if (raw_) {
      if (pins_++ == 0) {
        s.hot.erase(this);
        s.pinned += n;
      }
      if (ram_.in) s.rams.erase(this), s.rams.push_front(this);  // just used
      return Pin(this, raw_);
    }
    if (!busy_) break;
    s.cv.wait(l);
  }
  PROFILE_ZONE("tile_read_back");
  pins_++;
  busy_ = true;
  s.rams.erase(this);
  const File f = s.scratch.file;
  l.unlock();
  uint8_t* raw = nullptr;
  bool ok = false;
  try {
    raw = take(n);
    switch (packed_) {
      case Packed::None:  // fresh
        fill(raw);
        ok = true;
        break;
      case Packed::Ram:
      case Packed::Elsewhere:
        ok = unsqueeze(bytes_, size_, raw, n);
        break;
      case Packed::Scratch: {
        if (!zstd_) {
          ok = read_at(f, offset_, raw, n);
          break;
        }
        std::vector<uint8_t>& frame = zstd().frame;
        frame.resize(size_);
        ok = read_at(f, offset_, frame.data(), size_) && unsqueeze(frame.data(), size_, raw, n);
        break;
      }
    }
  } catch (...) {
  }
  l.lock();
  if (ok) {
    raw_ = raw;
    s.raw += n;
    s.pinned += n;
  } else {
    pins_--;
  }
  s.settle(this);
  l.unlock();
  if (!ok) {
    if (raw) give(raw, n);
    throw std::runtime_error("pb: a tile's pixels could not be read back");
  }
  return Pin(this, raw);
}

void Tile::unpin() const {
  TileStore& s = store();
  const size_t n = raw_size(format_);
  std::unique_lock l(s.m);
  // The last pin: from now on the budget holds these n bytes. Past it, others go out first.
  while (pins_ == 1 && s.held() + n > s.budget && s.evict_one(l, true)) {
  }
  if (--pins_) return;
  s.pinned -= n;
  if (!busy_) s.hot.push_front(this);
  s.check_background();
}

Pin::~Pin() {
  if (tile_) tile_->unpin();
}

WritePin Tile::writable() {
  Pin p = pin();
  TileStore& s = store();
  Garbage g;
  {
    std::unique_lock l(s.m);
    while (busy_) s.cv.wait(l);  // spilling its copy
    s.drop_packed(this, g);
  }
  p.tile_ = nullptr;
  return WritePin(this, std::exchange(p.data_, nullptr));
}

WritePin Tile::write_copy(const uint8_t* from) {
  const size_t n = raw_size(format_);
  uint8_t* raw = take(n);
  std::memcpy(raw, from, n);
  TileStore& s = store();
  const std::lock_guard l(s.m);
  raw_ = raw;
  pins_ = 1;
  s.raw += n;
  s.pinned += n;
  return WritePin(this, raw);
}

void Tile::also_elsewhere(std::shared_ptr<const void> owner, const void* zstd, size_t size) const {
  if (solid_ || !zstd || !size || size > UINT32_MAX) throw std::invalid_argument("pb: Tile::also_elsewhere");
  TileStore& s = store();
  Garbage g;
  std::unique_lock l(s.m);
  while (busy_) s.cv.wait(l);
  s.drop_packed(this, g);
  packed_ = Packed::Elsewhere, zstd_ = true, size_ = uint32_t(size);
  bytes_ = static_cast<const uint8_t*>(zstd);
  owner_ = std::move(owner);
}

TileStats tile_stats() {
  TileStore& s = store();
  const std::lock_guard l(s.m);
  return {s.raw, s.ram, s.scratch.live, s.pinned, s.charged};
}

size_t physical_memory() {
#ifdef _WIN32
  MEMORYSTATUSEX m{};
  m.dwLength = sizeof m;
  return GlobalMemoryStatusEx(&m) ? size_t(m.ullTotalPhys) : 0;
#elif defined(__APPLE__)
  uint64_t n = 0;
  size_t len = sizeof n;
  return sysctlbyname("hw.memsize", &n, &len, nullptr, 0) == 0 ? size_t(n) : 0;
#else
  const long pages = sysconf(_SC_PHYS_PAGES), page = sysconf(_SC_PAGE_SIZE);
  return pages > 0 && page > 0 ? size_t(pages) * size_t(page) : 0;
#endif
}

size_t tile_budget() {
  TileStore& s = store();
  const std::lock_guard l(s.m);
  return s.budget;
}

void set_tile_budget(size_t bytes) {
  TileStore& s = store();
  const std::lock_guard l(s.m);
  s.budget = std::max(bytes, kMinBudget);
  s.check_background();
}

void set_tile_compression(bool on) {
  TileStore& s = store();
  const std::lock_guard l(s.m);
  s.compress = on;
}

void set_scratch_dir(const std::filesystem::path& dir) {
  TileStore& s = store();
  const std::lock_guard l(s.m);
  s.scratch.dir = dir;
  s.scratch.broken = false;
}

void trim_tiles() {
  TileStore& s = store();
  std::unique_lock l(s.m);
  while (s.evict_one(l, false)) {
  }
}

void make_room(size_t bytes) {
  TileStore& s = store();
  std::unique_lock l(s.m);
  while (s.held() + bytes > s.budget && s.evict_one(l, true)) {
  }
}

BudgetUser::BudgetUser(std::function<size_t(size_t)> release) : release_(std::move(release)) {
  TileStore& s = store();
  const std::lock_guard l(s.users_m);
  s.users.push_back(this);
}

BudgetUser::~BudgetUser() {
  TileStore& s = store();
  {
    const std::lock_guard l(s.users_m);  // after a release() running
    s.users.erase(std::find(s.users.begin(), s.users.end(), this));
  }
  charge(-charged_);
}

void BudgetUser::charge(ptrdiff_t bytes) {
  TileStore& s = store();
  const std::lock_guard l(s.m);
  charged_ += bytes;
  s.charged = size_t(ptrdiff_t(s.charged) + bytes);
  s.check_background();
}

}  // namespace pb
