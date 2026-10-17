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

#ifdef _WIN32

MappedFile::MappedFile(const std::filesystem::path& path) {
  // Shared every way, so that a save can append to it (M3) and move another file meanwhile.
  const HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f == INVALID_HANDLE_VALUE) fail("cannot open", path);
  LARGE_INTEGER n{};
  const HANDLE m =
      GetFileSizeEx(f, &n) && n.QuadPart > 0 ? CreateFileMappingW(f, nullptr, PAGE_READONLY, 0, 0, nullptr) : nullptr;
  const void* view = m ? MapViewOfFile(m, FILE_MAP_READ, 0, 0, 0) : nullptr;  // the view keeps the mapping open
  const DWORD error = GetLastError();
  if (m) CloseHandle(m);
  CloseHandle(f);
  if (n.QuadPart > 0 && !view) {
    SetLastError(error);
    fail("cannot map", path);
  }
  data_ = static_cast<const uint8_t*>(view);
  size_ = view ? size_t(n.QuadPart) : 0;
}

MappedFile::~MappedFile() {
  if (data_) UnmapViewOfFile(data_);
}

OutputFile::OutputFile(const std::filesystem::path& path) : path_(path) {
  const HANDLE h =
      CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) fail("cannot create", path);
  handle_ = intptr_t(h);
}

OutputFile::~OutputFile() {
  if (handle_ != -1) CloseHandle(HANDLE(handle_));
}

void OutputFile::write(const void* data, size_t n) {
  PROFILE_ZONE("file_write");
  for (const uint8_t* p = static_cast<const uint8_t*>(data); n;) {
    DWORD done = 0;
    if (!WriteFile(HANDLE(handle_), p, DWORD(std::min<size_t>(n, size_t(1) << 30)), &done, nullptr) || !done)
      fail("cannot write", path_);
    p += done, n -= done, size_ += done;
  }
}

void OutputFile::sync() {
  PROFILE_ZONE("file_sync");
  if (!FlushFileBuffers(HANDLE(handle_))) fail("cannot flush", path_);
}

void OutputFile::close() {
  const HANDLE h = HANDLE(handle_);
  handle_ = -1;
  if (!CloseHandle(h)) fail("cannot close", path_);
}

void replace_file(const std::filesystem::path& from, const std::filesystem::path& to) {
  if (!MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    fail("cannot move a file over", to);
}

#else

MappedFile::MappedFile(const std::filesystem::path& path) {
  const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) fail("cannot open", path);
  struct stat st{};
  void* view = nullptr;
  if (fstat(fd, &st) == 0 && st.st_size > 0) view = mmap(nullptr, size_t(st.st_size), PROT_READ, MAP_SHARED, fd, 0);
  const int error = errno;
  ::close(fd);  // the mapping keeps the file
  if (view == MAP_FAILED || (!view && st.st_size != 0)) {
    errno = error;
    fail("cannot map", path);
  }
  data_ = static_cast<const uint8_t*>(view);
  size_ = view ? size_t(st.st_size) : 0;
}

MappedFile::~MappedFile() {
  if (data_) munmap(const_cast<uint8_t*>(data_), size_);
}

OutputFile::OutputFile(const std::filesystem::path& path) : path_(path) {
  handle_ = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
  if (handle_ < 0) fail("cannot create", path);
}

OutputFile::~OutputFile() {
  if (handle_ >= 0) ::close(int(handle_));
}

void OutputFile::write(const void* data, size_t n) {
  PROFILE_ZONE("file_write");
  for (const uint8_t* p = static_cast<const uint8_t*>(data); n;) {
    const ssize_t done = ::write(int(handle_), p, n);
    if (done < 0 && errno == EINTR) continue;
    if (done <= 0) fail("cannot write", path_);
    p += done, n -= size_t(done), size_ += uint64_t(done);
  }
}

void OutputFile::sync() {
  PROFILE_ZONE("file_sync");
#ifdef __APPLE__
  if (fcntl(int(handle_), F_FULLFSYNC) == 0) return;  // fsync() leaves it in the drive's cache; some volumes refuse
  if (fsync(int(handle_)) != 0) fail("cannot sync", path_);
#else
  if (fdatasync(int(handle_)) != 0) fail("cannot sync", path_);
#endif
}

void OutputFile::close() {
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
