#include "mapped.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <string>
#include <system_error>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "profile.h"

namespace pb {
namespace {

constexpr size_t kBlock = size_t(8) << 20;  // OutputFile's

// The last error of the system, said with what failed on which file.
[[noreturn]] void fail(const char* what, const std::filesystem::path& p) {
  const auto name = p.u8string();
  const std::string text = std::string("pb: ") + what + " " + std::string(name.begin(), name.end());
#ifdef _WIN32
  throw std::system_error(int(GetLastError()), std::system_category(), text);
#else
  throw std::system_error(errno, std::generic_category(), text);
#endif
}

}  // namespace

MappedFile::MappedFile(const OutputFile& out, uint64_t from) { map(out.handle_, out.path_, from); }

void OutputFile::write(const void* data, size_t n) {
  size_ += n;
  for (const auto* p = static_cast<const uint8_t*>(data); n;) {
    const size_t k = std::min(n, kBlock - block_.size());
    block_.insert(block_.end(), p, p + k);
    p += k, n -= k;
    if (block_.size() == kBlock) flush();
  }
}

#ifdef _WIN32

MappedFile::MappedFile(const std::filesystem::path& path, uint64_t from) {
  // Shared every way, so that a save can append to it (M3) and move another file over it meanwhile.
  const HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f == INVALID_HANDLE_VALUE) fail("cannot open", path);
  try {
    map(intptr_t(f), path, from);
  } catch (...) {
    CloseHandle(f);
    throw;
  }
  CloseHandle(f);  // the view keeps the file
}

void MappedFile::map(intptr_t handle, const std::filesystem::path& path, uint64_t from) {
  const HANDLE f = HANDLE(handle);
  SYSTEM_INFO si{};
  GetSystemInfo(&si);
  const uint64_t base = from - from % si.dwAllocationGranularity;  // where views may start
  LARGE_INTEGER n{};
  const bool any = GetFileSizeEx(f, &n) && uint64_t(n.QuadPart) > from;
  const HANDLE m = any ? CreateFileMappingW(f, nullptr, PAGE_READONLY, 0, 0, nullptr) : nullptr;
  const void* view = m ? MapViewOfFile(m, FILE_MAP_READ, DWORD(base >> 32), DWORD(base), SIZE_T(n.QuadPart - base))
                       : nullptr;  // the view keeps the mapping open
  const DWORD error = GetLastError();
  if (m) CloseHandle(m);
  if (any && !view) {
    SetLastError(error);
    fail("cannot map", path);
  }
  if (!view) return;
  skip_ = size_t(from - base);
  data_ = static_cast<const uint8_t*>(view) + skip_;
  size_ = size_t(uint64_t(n.QuadPart) - from);
}

MappedFile::~MappedFile() {
  if (data_) UnmapViewOfFile(data_ - skip_);
}

OutputFile::OutputFile(const std::filesystem::path& path, bool append) : path_(path) {
  block_.reserve(kBlock);
  // Written through: each block goes to the drive as it is written, not all at the last sync(). Read too, for a
  // MappedFile of it.
  const HANDLE h = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                               append ? FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE : FILE_SHARE_READ,
                               nullptr, append ? OPEN_EXISTING : CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
  if (h == INVALID_HANDLE_VALUE) fail(append ? "cannot open" : "cannot create", path);
  LARGE_INTEGER n{};
  if (append && !(GetFileSizeEx(h, &n) && SetFilePointerEx(h, LARGE_INTEGER{}, nullptr, FILE_END))) {
    const DWORD error = GetLastError();
    CloseHandle(h);
    SetLastError(error);
    fail("cannot open", path);
  }
  handle_ = intptr_t(h);
  size_ = uint64_t(n.QuadPart);
}

OutputFile::~OutputFile() {
  if (handle_ != -1) CloseHandle(HANDLE(handle_));
}

void OutputFile::flush() {
  PROFILE_ZONE("file_write");
  for (const uint8_t* p = block_.data(); p < block_.data() + block_.size();) {
    DWORD done = 0;
    if (!WriteFile(HANDLE(handle_), p, DWORD(block_.data() + block_.size() - p), &done, nullptr) || !done)
      fail("cannot write", path_);
    p += done;
  }
  block_.clear();
}

void OutputFile::sync() {
  flush();
  PROFILE_ZONE("file_sync");
  if (!FlushFileBuffers(HANDLE(handle_))) fail("cannot flush", path_);
}

void OutputFile::close() {
  flush();
  const HANDLE h = HANDLE(handle_);
  handle_ = -1;
  if (!CloseHandle(h)) fail("cannot close", path_);
}

void replace_file(const std::filesystem::path& from, const std::filesystem::path& to) {
  if (!MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    fail("cannot move a file over", to);
}

#else

MappedFile::MappedFile(const std::filesystem::path& path, uint64_t from) {
  const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) fail("cannot open", path);
  try {
    map(fd, path, from);
  } catch (...) {
    ::close(fd);
    throw;
  }
  ::close(fd);  // the mapping keeps the file
}

void MappedFile::map(intptr_t handle, const std::filesystem::path& path, uint64_t from) {
  const int fd = int(handle);
  struct stat st{};
  const uint64_t size = fstat(fd, &st) == 0 ? uint64_t(st.st_size) : 0;
  const uint64_t base = from - from % uint64_t(sysconf(_SC_PAGESIZE));  // where mappings may start
  void* view = size > from ? mmap(nullptr, size_t(size - base), PROT_READ, MAP_SHARED, fd, off_t(base)) : nullptr;
  if (view == MAP_FAILED) fail("cannot map", path);
  if (!view) return;
  skip_ = size_t(from - base);
  data_ = static_cast<const uint8_t*>(view) + skip_;
  size_ = size_t(size - from);
}

MappedFile::~MappedFile() {
  if (data_) munmap(const_cast<uint8_t*>(data_ - skip_), size_ + skip_);
}

OutputFile::OutputFile(const std::filesystem::path& path, bool append) : path_(path) {
  block_.reserve(kBlock);
  // Read too, for a MappedFile of it.
  handle_ = ::open(path.c_str(), append ? O_RDWR | O_APPEND | O_CLOEXEC : O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
  if (handle_ < 0) fail(append ? "cannot open" : "cannot create", path);
  struct stat st{};
  if (append && fstat(int(handle_), &st) != 0) {
    const int error = errno;
    ::close(int(handle_));
    errno = error;
    fail("cannot open", path);
  }
  size_ = append ? uint64_t(st.st_size) : 0;
}

OutputFile::~OutputFile() {
  if (handle_ >= 0) ::close(int(handle_));
}

void OutputFile::flush() {
  PROFILE_ZONE("file_write");
  const uint64_t at = size_ - block_.size();  // where the block goes
  for (const uint8_t* p = block_.data(); p < block_.data() + block_.size();) {
    const ssize_t done = ::write(int(handle_), p, size_t(block_.data() + block_.size() - p));
    if (done < 0 && errno == EINTR) continue;
    if (done <= 0) fail("cannot write", path_);
    p += done;
  }
#ifdef __linux__
  // On its way to the drive now, not all at sync(): a hint, so that failing changes nothing.
  sync_file_range(int(handle_), off_t(at), off_t(block_.size()), SYNC_FILE_RANGE_WRITE);
#else
  (void)at;
#endif
  block_.clear();
}

void OutputFile::sync() {
  flush();
  PROFILE_ZONE("file_sync");
#ifdef __APPLE__
  if (fcntl(int(handle_), F_FULLFSYNC) == 0) return;  // fsync() leaves it in the drive's cache; some volumes refuse
  if (fsync(int(handle_)) != 0) fail("cannot sync", path_);
#else
  if (fdatasync(int(handle_)) != 0) fail("cannot sync", path_);
#endif
}

void OutputFile::close() {
  flush();
  const int fd = int(handle_);
  handle_ = -1;
  if (::close(fd) != 0) fail("cannot close", path_);
}

void replace_file(const std::filesystem::path& from, const std::filesystem::path& to) {
  if (std::rename(from.c_str(), to.c_str()) != 0) fail("cannot move a file over", to);
  // The rename lasts once the directory holding it does. Some file systems cannot sync a directory: let it be.
  const std::filesystem::path dir = to.parent_path().empty() ? "." : to.parent_path();
  const int fd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (fd >= 0) {
    fsync(fd);
    ::close(fd);
  }
}

#endif

}  // namespace pb
