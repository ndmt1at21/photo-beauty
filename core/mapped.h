#pragma once
// Files as a .pbd needs them (pbd.h): one mapped read-only for its tiles to point into; one written from start to end,
// or appended to, and flushed to the disk; one moved over another for good. POSIX (mmap, fsync, F_FULLFSYNC on macOS)
// and Windows (MapViewOfFile, FlushFileBuffers, MoveFileEx).
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace pb {

class OutputFile;

// A file's bytes from `from` to its end (whole, by default), mapped read-only, as long as this lives: data() is byte
// `from` of the file. Linux and macOS keep them whatever happens to the file meanwhile; Windows keeps a mapped file
// from being deleted or cut shorter, not from being replaced, renamed or appended to. Throws std::system_error when the
// file cannot be opened or mapped. Any thread.
class MappedFile {
 public:
  explicit MappedFile(const std::filesystem::path&, uint64_t from = 0);
  // The same of the file `out` writes, once its sync() wrote everything, through out's own handle: the file is not
  // opened again (Windows: a virus scanner may read all of a file just written before an open of it returns).
  MappedFile(const OutputFile& out, uint64_t from);
  ~MappedFile();
  MappedFile(const MappedFile&) = delete;
  MappedFile& operator=(const MappedFile&) = delete;
  const uint8_t* data() const { return data_; }  // null when nothing lies past `from`
  size_t size() const { return size_; }

 private:
  void map(intptr_t handle, const std::filesystem::path&, uint64_t from);  // of the open file
  const uint8_t* data_ = nullptr;
  size_t size_ = 0;
  size_t skip_ = 0;  // mapped before data(): views start at a page (Windows: at 64 KB)
};

// A new file, or one emptied, written from start to end; with `append`, an existing one written on at its end, size()
// starting at its size (Windows: shared as MappedFile shares, so that what maps it reads on). What write() takes goes
// out in blocks of 8 MB, each on its way to the drive as it fills (Windows: written through; Linux: its write-back
// started), so that the drive works while the caller goes on; sync() and close() write the rest. Throws
// std::system_error when it cannot be made, opened or written. One thread at a time.
class OutputFile {
 public:
  explicit OutputFile(const std::filesystem::path&, bool append = false);
  ~OutputFile();  // closes it, if close() did not, leaving out what waits in the block
  OutputFile(const OutputFile&) = delete;
  OutputFile& operator=(const OutputFile&) = delete;
  void write(const void* data, size_t n);  // at its end
  // What was written reaches the disk before this returns: fsync, F_FULLFSYNC on macOS, FlushFileBuffers on Windows.
  void sync();
  void close();
  uint64_t size() const { return size_; }  // what write() took counts

 private:
  friend class MappedFile;
  void flush();  // the block to the file
  std::filesystem::path path_;  // for errors
  intptr_t handle_ = -1;        // a file descriptor, or a HANDLE on Windows
  uint64_t size_ = 0;
  std::vector<uint8_t> block_;  // written, not yet in the file
};

// `from` takes the place of `to`, replacing it, for good once this returns: rename(), then the directory synced, on
// Linux and macOS; MoveFileEx, write-through, on Windows. Throws std::system_error.
void replace_file(const std::filesystem::path& from, const std::filesystem::path& to);

}  // namespace pb
