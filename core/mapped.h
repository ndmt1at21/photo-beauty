#pragma once
// Files as a .pbd needs them (pbd.h): one mapped read-only for its tiles to point into; one written from start to end
// and flushed to the disk; one moved over another for good. POSIX (mmap, fsync, F_FULLFSYNC on macOS) and Windows
// (MapViewOfFile, FlushFileBuffers, MoveFileEx).
#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace pb {

// A file's bytes, mapped read-only, whole, as long as this lives. Linux and macOS keep them whatever happens to the
// file meanwhile; Windows keeps a mapped file from being replaced or deleted, not from being appended to. Throws
// std::system_error when the file cannot be opened or mapped. Any thread.
class MappedFile {
 public:
  explicit MappedFile(const std::filesystem::path&);
  ~MappedFile();
  MappedFile(const MappedFile&) = delete;
  MappedFile& operator=(const MappedFile&) = delete;
  const uint8_t* data() const { return data_; }  // null when empty
  size_t size() const { return size_; }

 private:
  const uint8_t* data_ = nullptr;
  size_t size_ = 0;
};

// A new file, or one emptied, written from start to end. Throws std::system_error when it cannot be made or written.
// One thread at a time.
class OutputFile {
 public:
  explicit OutputFile(const std::filesystem::path&);
  ~OutputFile();  // closes it, if close() did not
  OutputFile(const OutputFile&) = delete;
  OutputFile& operator=(const OutputFile&) = delete;
  void write(const void* data, size_t n);  // at its end
  // What was written reaches the disk before this returns: fsync, F_FULLFSYNC on macOS, FlushFileBuffers on Windows.
  void sync();
  void close();
  uint64_t size() const { return size_; }

 private:
  std::filesystem::path path_;  // for errors
  intptr_t handle_ = -1;        // a file descriptor, or a HANDLE on Windows
  uint64_t size_ = 0;
};

// `from` takes the place of `to`, replacing it, for good once this returns: rename(), then the directory synced, on
// Linux and macOS; MoveFileEx, write-through, on Windows. Throws std::system_error.
void replace_file(const std::filesystem::path& from, const std::filesystem::path& to);

}  // namespace pb
