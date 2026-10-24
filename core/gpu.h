#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>

namespace pb::gpu {

// Minimal GPU HAL: storage buffers, compute kernels, in-order recording, submissions waited for.
// The API is backend-neutral; gpu_vulkan.cpp implements it. A Metal file with the same API
// replaces it on Apple platforms (P1), picked at build time, so no virtual interface.
struct Buffer;
struct Kernel;
struct Texture;
using BufferPtr = std::shared_ptr<Buffer>;
using KernelPtr = std::shared_ptr<Kernel>;
using TexturePtr = std::shared_ptr<Texture>;

// How another device (a Qt window's) imports a Texture: an identical image (RGBA8_UNORM, optimal
// tiling, one level, usage TRANSFER_SRC | TRANSFER_DST | SAMPLED, exclusive, external memory of the
// handle's type) bound to this memory on a dedicated allocation, after checking both UUIDs: only the
// same GPU and driver lay optimal tiling out alike. The image comes in SHADER_READ_ONLY_OPTIMAL
// layout, released to VK_QUEUE_FAMILY_EXTERNAL: the importer acquires it before sampling.
struct Shared {
  intptr_t handle = 0;  // an NT handle (OPAQUE_WIN32) on Windows, else an fd (OPAQUE_FD); close_handle() it
  uint64_t bytes = 0;   // allocation size
  int width = 0, height = 0;
  std::array<uint8_t, 16> device_uuid{}, driver_uuid{};
};
Shared share(Texture&);     // a new handle on every call
void close_handle(Shared&);  // once imported, or not needed
std::array<int, 2> size(const Texture&);

class Device {
 public:
  // nullptr when there is no usable device. Software rasterizers (lavapipe, SwiftShader) only
  // on request: the CPU pipeline is faster than them, they are for tests.
  static std::unique_ptr<Device> create(bool allow_software = false);
  ~Device();

  std::string name() const;
  std::string driver() const;  // the driver's name and version ("" when the device does not say)
  size_t max_buffer() const;   // largest buffer a kernel can bind
  size_t memory() const;       // largest device-local heap
  // Device-local. Once released it is kept for the next buffer of its size (trim()), unless `keep` is false: one whose
  // size is not asked for again (a store that grows) is freed once its submissions are waited for.
  BufferPtr buffer(size_t bytes, bool keep = true);
  // SPIR-V compute shader, entry "main", storage buffers at bindings 0..buffers-1. The name
  // labels its GPU zone in Tracy.
  KernelPtr kernel(const char* name, const uint32_t* spirv, size_t spirv_bytes, int buffers, size_t push_bytes);

  // Recorded in order, each after the previous one completes, across submissions too; nothing runs
  // until submit(), or an upload that submits. A submission keeps the buffers its commands use until it is waited for.
  // Into dst from byte `offset`, through staging; one that would take more than 32 MB of it since the last submit()
  // submits what was recorded first, so that the GPU starts on it.
  void upload(Buffer& dst, const void* src, size_t bytes, size_t offset = 0);
  // upload() of bytes the caller writes, from any thread, where this returns, before the next submit(): uploads filled
  // in parallel. It never submits: the caller keeps what it stages between submit()s to some 32 MB, as upload() does.
  // Uploads into a buffer one after the other at places apart go as one copy, without a barrier between them.
  void* stage(Buffer& dst, size_t bytes, size_t offset = 0);
  void fill(Buffer& dst, uint32_t value);
  void dispatch(Kernel&, std::initializer_list<Buffer*> buffers, const void* push, size_t push_bytes,
                uint32_t groups_x, uint32_t groups_y);
  // Into dst when the submission is waited for: `bytes` in one piece, or rows of `row` bytes that lie
  // `stride` apart in dst.
  void download(Buffer& src, void* dst, size_t bytes, size_t row = 0, size_t stride = 0);
  // Sends what was recorded to the GPU without waiting, so the next commands can be recorded while it
  // runs; returns its number. At most two submissions run at once: recording the third waits for the first.
  uint64_t submit();
  void wait(uint64_t submission);  // until it and every earlier one finished; their downloads are then in place
  void flush();  // submit and wait
  // The GPU's progress, for budgets of its time: mark() records a timestamp once the commands recorded before it are
  // done, which marked() gives, ms on the GPU's clock, once its submission was waited for: NaN before then, past the
  // 64th mark of a submission, some 1,000 marks later, or where the queue keeps no time.
  uint64_t mark();
  double marked(uint64_t mark) const;

  // An RGBA8 image another device can import (Shared), or null when this one cannot export memory.
  TexturePtr texture(int width, int height);
  // A texture of another device on the same GPU, from share(), to read with download().
  TexturePtr import(const Shared&);
  // Packed RGBA8 rows of src (w x h) into dst at x, y. The first copy after release() starts a
  // new picture: what dst held is dropped.
  void copy(Buffer& src, Texture& dst, int x, int y, int w, int h);
  // Hands dst to other devices once its submission is waited for (see Shared).
  void release(Texture& dst);
  void download(Texture& src, void* dst);  // an imported texture, as packed rows, like download() above
  // Released buffers are kept for the next buffer of the same size and kind, up to 256 MB (a quarter
  // of memory() where that is less) and for 10 s; trim() frees them now.
  void trim();
  // Memory the buffers of this device take, bytes: device-local, and host-visible for staging, alive or kept for
  // reuse; of those, `kept`: released, kept for reuse (trim()). `ram`: what K1's tile budget (tilecache.h) counts:
  // staging, and device-local memory where it is RAM (integrated GPUs and CPUs: the Iris Xe, lavapipe); and what was
  // freed of it, for 7 s more, as drivers keep it to reuse a while. Any thread.
  struct Held {
    size_t device = 0, staging = 0, kept = 0, ram = 0;
  };
  Held held() const;

 private:
  struct Impl;
  explicit Device(std::unique_ptr<Impl>);
  std::unique_ptr<Impl> impl_;
};

}  // namespace pb::gpu
