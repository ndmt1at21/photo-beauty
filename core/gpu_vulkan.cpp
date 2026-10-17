#include <volk.h>
#ifdef PB_TRACY
#include <tracy/TracyVulkan.hpp>
#endif
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <vulkan/vulkan_win32.h>
#else
#include <unistd.h>
#endif

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <iterator>
#include <list>
#include <mutex>
#include <stdexcept>
#include <vector>

#include "gpu.h"
#include "jobs.h"
#include "profile.h"

namespace pb::gpu {
namespace {
void check(VkResult r, const char* what) {
  if (r != VK_SUCCESS) throw std::runtime_error(std::string("Vulkan: ") + what + " failed (" + std::to_string(r) + ")");
}
bool has(const std::vector<VkExtensionProperties>& list, const char* name) {
  for (const auto& e : list)
    if (!std::strcmp(e.extensionName, name)) return true;
  return false;
}

// Textures other devices import (Shared): the extension and handle type of this platform.
#ifdef _WIN32
constexpr const char* kShareExtension = "VK_KHR_external_memory_win32";
constexpr const char* kShareExport = "vkGetMemoryWin32HandleKHR";
using ShareExport = PFN_vkGetMemoryWin32HandleKHR;
constexpr auto kShareType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
#else
constexpr const char* kShareExtension = "VK_KHR_external_memory_fd";
constexpr const char* kShareExport = "vkGetMemoryFdKHR";
using ShareExport = PFN_vkGetMemoryFdKHR;
constexpr auto kShareType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
#endif
constexpr VkImageUsageFlags kTextureUsage =
    VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

bool exportable(VkPhysicalDevice pd) {
  VkPhysicalDeviceExternalImageFormatInfo ei{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO};
  ei.handleType = kShareType;
  VkPhysicalDeviceImageFormatInfo2 fi{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2};
  fi.pNext = &ei;
  fi.format = VK_FORMAT_R8G8B8A8_UNORM;
  fi.type = VK_IMAGE_TYPE_2D;
  fi.tiling = VK_IMAGE_TILING_OPTIMAL;
  fi.usage = kTextureUsage;
  VkExternalImageFormatProperties ep{VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES};
  VkImageFormatProperties2 fp{VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2};
  fp.pNext = &ep;
  const VkExternalMemoryFeatureFlags both =
      VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT | VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT;
  return vkGetPhysicalDeviceImageFormatProperties2(pd, &fi, &fp) == VK_SUCCESS &&
         (ep.externalMemoryProperties.externalMemoryFeatures & both) == both;
}
}  // namespace

struct Texture {
  VkDevice dev = VK_NULL_HANDLE;
  VkImage image = VK_NULL_HANDLE;
  VkDeviceMemory mem = VK_NULL_HANDLE;
  Shared info;           // the handle exported once, of exported textures: the Iris Xe's Windows driver
                         // gives an NT handle on the first vkGetMemoryWin32HandleKHR only, 0 after
  bool writing = false;  // TRANSFER_DST, from the first copy() to release()
  ~Texture() {
    close_handle(info);
    vkDestroyImage(dev, image, nullptr);
    vkFreeMemory(dev, mem, nullptr);
  }
};

Shared share(Texture& t) {
  Shared s = t.info;
#ifdef _WIN32
  HANDLE h = nullptr;
  if (!DuplicateHandle(GetCurrentProcess(), HANDLE(t.info.handle), GetCurrentProcess(), &h, 0, FALSE,
                       DUPLICATE_SAME_ACCESS))
    throw std::runtime_error("share: DuplicateHandle failed");
  s.handle = intptr_t(h);
#else
  s.handle = dup(int(t.info.handle));
  if (s.handle < 0) throw std::runtime_error("share: dup failed");
#endif
  return s;
}

struct Buffer : std::enable_shared_from_this<Buffer> {  // the submissions using it keep it
  VkDevice dev = VK_NULL_HANDLE;
  VkBuffer buf = VK_NULL_HANDLE;
  VkDeviceMemory mem = VK_NULL_HANDLE;
  void* mapped = nullptr;  // host-visible staging only
  size_t size = 0;
  uint64_t kind = 0;  // usage and memory properties, for reuse
  std::chrono::steady_clock::time_point released;
  ~Buffer() {
    vkDestroyBuffer(dev, buf, nullptr);
    vkFreeMemory(dev, mem, nullptr);
  }
};

// Released buffers, newest first, for the next request of the same size and kind: on an Intel Iris
// Xe, allocating, first touching and freeing the ~2 GB a full-size 45 MP render goes through costs
// ~0.7 s, more than its kernels. Past the budget, oldest first, or after 10 s unused, a pool job frees
// them: freeing the few hundred MB of a 1:1 render cost the next render 70-90 ms on the Iris Xe.
// ponytail: exact sizes and a fixed share of device memory; VK_EXT_memory_budget when caches multiply.
struct Spare : std::enable_shared_from_this<Spare> {
  using Clock = std::chrono::steady_clock;
  std::mutex m;
  std::condition_variable freed;
  std::list<std::unique_ptr<Buffer>> list, expired;  // expired: for the pool job
  size_t bytes = 0, budget = 0;
  int freeing = 0;      // pool jobs freeing what they took
  bool closed = false;  // the device is going away: free on release

  // Under m: moves the buffers past the budget or unused for too long to the pool job.
  void expire() {
    const bool queued = !expired.empty();
    while (!list.empty() && (bytes > budget || Clock::now() - list.back()->released > std::chrono::seconds(10))) {
      bytes -= list.back()->size;
      expired.splice(expired.begin(), list, std::prev(list.end()));
    }
    if (queued || expired.empty()) return;
    submit(Priority::Background, [s = shared_from_this()] {
      std::list<std::unique_ptr<Buffer>> drop;
      {
        std::lock_guard l(s->m);
        drop.swap(s->expired);
        s->freeing++;
      }
      {
        PROFILE_ZONE("vk_free");
        drop.clear();  // vkFreeMemory takes a while
      }
      std::lock_guard l(s->m);
      s->freeing--;
      s->freed.notify_all();
    });
  }
  void put(Buffer* b) {
    std::unique_ptr<Buffer> own(b);  // freed after the lock once the device is going away
    std::lock_guard l(m);
    if (closed) return;
    own->released = Clock::now();
    bytes += own->size;
    list.push_front(std::move(own));
    expire();
  }
  std::unique_ptr<Buffer> take(size_t size, uint64_t kind) {
    std::lock_guard l(m);
    expire();
    for (auto it = list.begin(); it != list.end(); ++it)
      if ((*it)->size == size && (*it)->kind == kind) {
        std::unique_ptr<Buffer> b = std::move(*it);
        list.erase(it);
        bytes -= size;
        return b;
      }
    return nullptr;
  }
  // Frees them all now, after the lock; `close`: once the pool's frees are done too, as the device goes.
  void clear(bool close = false) {
    std::list<std::unique_ptr<Buffer>> drop;
    std::unique_lock l(m);
    drop.swap(list);
    drop.splice(drop.end(), expired);
    bytes = 0;
    closed |= close;
    if (close) freed.wait(l, [&] { return freeing == 0; });
  }
};

struct Kernel {
  VkDevice dev = VK_NULL_HANDLE;
  VkShaderModule module = VK_NULL_HANDLE;
  VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
  VkPipelineLayout layout = VK_NULL_HANDLE;
  VkPipeline pipeline = VK_NULL_HANDLE;
  int buffers = 0;
  std::string name;
  ~Kernel() {
    vkDestroyPipeline(dev, pipeline, nullptr);
    vkDestroyPipelineLayout(dev, layout, nullptr);
    vkDestroyDescriptorSetLayout(dev, set_layout, nullptr);
    vkDestroyShaderModule(dev, module, nullptr);
  }
};

struct Device::Impl {
  VkInstance instance = VK_NULL_HANDLE;
  VkPhysicalDevice phys = VK_NULL_HANDLE;
  VkPhysicalDeviceMemoryProperties mem{};
  std::string name, driver;
  size_t max_buffer = 0, memory = 0;
  VkDevice dev = VK_NULL_HANDLE;
  uint32_t family = 0;
  VkQueue queue = VK_NULL_HANDLE;
  PFN_vkVoidFunction get_handle = nullptr;  // textures can be shared (kShareExport)
  std::array<uint8_t, 16> device_uuid{}, driver_uuid{};
  VkCommandPool pool = VK_NULL_HANDLE;
  struct Readback {
    BufferPtr staging;
    void* dst;
    size_t bytes, row, stride;
  };
  // A submission: its commands, and what they need until it is waited for. Two take turns, so the
  // GPU runs one while the next one records (a full-size render's bands, back to back). Its descriptor
  // sets come from pools of kPoolSets sets and kPoolBuffers buffers; a submission that needs more takes
  // another pool (a run of 46 small cached bands, 6 buffers a finish, needed 276 of 256).
  static constexpr uint32_t kPoolSets = 64, kPoolBuffers = 256;
  struct Batch {
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    std::vector<VkDescriptorPool> pools;
    size_t pool = 0;                  // the one dispatches take sets from
    uint32_t sets = 0, buffers = 0;   // taken from it
    uint64_t serial = 0;  // submitted and not waited for; else 0
    std::vector<BufferPtr> used;  // by its commands, staging included
    std::vector<Readback> readbacks;
  };
  std::array<Batch, 2> batches;
  Batch* batch = &batches[0];  // the one recording, or to record next
  VkCommandBuffer cmd = VK_NULL_HANDLE;  // batch's
  bool recording = false;
  uint64_t submitted = 0;
  std::shared_ptr<Spare> spare = std::make_shared<Spare>();  // outlives the buffers it hands out
#ifdef PB_TRACY
  tracy::VkCtx* tracy = nullptr;  // GPU zones, when the queue has timestamps
#endif

  ~Impl() {
    if (dev) {
      vkDeviceWaitIdle(dev);
      for (Batch& b : batches) {
        b.used.clear();
        b.readbacks.clear();
      }
      spare->clear(true);
#ifdef PB_TRACY
      if (tracy) TracyVkDestroy(tracy);
#endif
      for (Batch& b : batches) {
        for (VkDescriptorPool p : b.pools) vkDestroyDescriptorPool(dev, p, nullptr);
        vkDestroyFence(dev, b.fence, nullptr);
      }
      vkDestroyCommandPool(dev, pool, nullptr);
      vkDestroyDevice(dev, nullptr);
    }
    if (instance) vkDestroyInstance(instance, nullptr);
  }

  uint32_t memory_type(uint32_t bits, VkMemoryPropertyFlags need, VkMemoryPropertyFlags prefer) const {
    for (VkMemoryPropertyFlags want : {need | prefer, need})
      for (uint32_t i = 0; i < mem.memoryTypeCount; i++)
        if ((bits >> i & 1) && (mem.memoryTypes[i].propertyFlags & want) == want) return i;
    throw std::runtime_error("Vulkan: no suitable memory type");
  }

  // ponytail: one allocation per buffer; buffers are few and large. VMA if that changes.
  BufferPtr make_buffer(size_t bytes, VkBufferUsageFlags usage, VkMemoryPropertyFlags need,
                        VkMemoryPropertyFlags prefer = 0) {
    bytes = std::max<size_t>(bytes, 4);
    const uint64_t kind = uint64_t(usage) << 32 | need | prefer;
    std::unique_ptr<Buffer> b = spare->take(bytes, kind);
    if (!b) {
      PROFILE_ZONE("vk_alloc");
      b = std::make_unique<Buffer>();
      b->dev = dev, b->size = bytes, b->kind = kind;
      VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
      bi.size = bytes;
      bi.usage = usage;
      bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
      check(vkCreateBuffer(dev, &bi, nullptr, &b->buf), "vkCreateBuffer");
      VkMemoryRequirements req;
      vkGetBufferMemoryRequirements(dev, b->buf, &req);
      VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
      ai.allocationSize = req.size;
      ai.memoryTypeIndex = memory_type(req.memoryTypeBits, need, prefer);
      if (vkAllocateMemory(dev, &ai, nullptr, &b->mem) != VK_SUCCESS) {
        spare->clear();  // what the spares hold may be what is missing
        check(vkAllocateMemory(dev, &ai, nullptr, &b->mem), "vkAllocateMemory");
      }
      check(vkBindBufferMemory(dev, b->buf, b->mem, 0), "vkBindBufferMemory");
      if (need & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
        check(vkMapMemory(dev, b->mem, 0, VK_WHOLE_SIZE, 0, &b->mapped), "vkMapMemory");
    }
    return BufferPtr(b.release(), [s = spare](Buffer* x) { s->put(x); });
  }

  // Waits for a submitted batch, then copies its downloads into place and lets go of what it held.
  void finish(Batch& b) {
    {
      PROFILE_ZONE("gpu_wait");
      check(vkWaitForFences(dev, 1, &b.fence, VK_TRUE, UINT64_MAX), "vkWaitForFences");
    }
    vkResetFences(dev, 1, &b.fence);
    for (VkDescriptorPool p : b.pools) vkResetDescriptorPool(dev, p, 0);
    b.pool = 0, b.sets = b.buffers = 0;
    PROFILE_ZONE("readback");
    for (const Readback& r : b.readbacks) {
      const auto* from = static_cast<const char*>(r.staging->mapped);
      auto* to = static_cast<char*>(r.dst);
      if (!r.row) std::memcpy(to, from, r.bytes);
      for (size_t o = 0; r.row && o < r.bytes; o += r.row, to += r.stride) std::memcpy(to, from + o, r.row);
    }
    b.readbacks.clear();
    b.used.clear();
    b.serial = 0;
  }

  // A descriptor set of `layout` for `n` buffers, from the recording batch's pools.
  VkDescriptorSet descriptor_set(VkDescriptorSetLayout layout, uint32_t n) {
    Batch& b = *batch;
    if (b.sets + 1 > kPoolSets || b.buffers + n > kPoolBuffers) b.pool++, b.sets = b.buffers = 0;
    if (b.pool == b.pools.size()) {
      const VkDescriptorPoolSize sizes{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kPoolBuffers};
      VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
      dpi.maxSets = kPoolSets;
      dpi.poolSizeCount = 1;
      dpi.pPoolSizes = &sizes;
      b.pools.push_back(VK_NULL_HANDLE);
      check(vkCreateDescriptorPool(dev, &dpi, nullptr, &b.pools.back()), "vkCreateDescriptorPool");
    }
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = b.pools[b.pool];
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &layout;
    VkDescriptorSet set;
    check(vkAllocateDescriptorSets(dev, &ai, &set), "vkAllocateDescriptorSets");
    b.sets++, b.buffers += n;
    return set;
  }

  // Begins a batch (once the one submitted before last finished), or orders the next command after
  // everything recorded so far. `buffers` stay with the batch until it is waited for.
  void record(std::initializer_list<Buffer*> buffers = {}) {
    if (!recording) {
      if (batch->serial) finish(*batch);
      cmd = batch->cmd;
      VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
      bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
      check(vkBeginCommandBuffer(cmd, &bi), "vkBeginCommandBuffer");
      recording = true;
#ifdef PB_TRACY
      if (tracy) TracyVkCollect(tracy, cmd);
#endif
    }
    for (Buffer* b : buffers) batch->used.push_back(b->shared_from_this());
    // After everything submitted before too: a barrier's first scope spans the queue's earlier
    // submissions. ponytail: full barrier between commands; precise per-buffer barriers if kernels
    // ever need to overlap.
    VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT |
                       VK_ACCESS_TRANSFER_WRITE_BIT;
    const VkPipelineStageFlags stages = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
    vkCmdPipelineBarrier(cmd, stages, stages, 0, 1, &mb, 0, nullptr, 0, nullptr);
  }

  // The image Shared describes on a dedicated allocation: exported, or imported from `from`.
  TexturePtr make_texture(int w, int h, const Shared* from) {
    auto t = std::make_shared<Texture>();
    t->dev = dev;
#ifndef _WIN32
    t->info.handle = -1;  // until exported
#endif
    t->info.width = w, t->info.height = h, t->info.device_uuid = device_uuid, t->info.driver_uuid = driver_uuid;
    VkExternalMemoryImageCreateInfo ei{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
    ei.handleTypes = kShareType;
    VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.pNext = &ei;
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = VK_FORMAT_R8G8B8A8_UNORM;
    ii.extent = {uint32_t(w), uint32_t(h), 1};
    ii.mipLevels = ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = kTextureUsage;
    ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    check(vkCreateImage(dev, &ii, nullptr, &t->image), "vkCreateImage");
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(dev, t->image, &req);
    VkMemoryDedicatedAllocateInfo dai{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
    dai.image = t->image;
    VkExportMemoryAllocateInfo xi{VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO};
    xi.pNext = &dai;
    xi.handleTypes = kShareType;
#ifdef _WIN32
    VkImportMemoryWin32HandleInfoKHR mi{VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR};
    if (from) mi.handle = HANDLE(from->handle);
#else
    VkImportMemoryFdInfoKHR mi{VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR};
    if (from) mi.fd = dup(int(from->handle));  // a successful import takes this one; from's stays the caller's
#endif
    mi.pNext = &dai;
    mi.handleType = kShareType;
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.pNext = from ? static_cast<const void*>(&mi) : &xi;
    ai.allocationSize = from ? from->bytes : req.size;
    ai.memoryTypeIndex = memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
    const VkResult r = vkAllocateMemory(dev, &ai, nullptr, &t->mem);
#ifndef _WIN32
    if (from && r != VK_SUCCESS) close(mi.fd);
#endif
    check(r, "vkAllocateMemory");
    check(vkBindImageMemory(dev, t->image, t->mem, 0), "vkBindImageMemory");
    t->info.bytes = ai.allocationSize;
    if (from) return t;
#ifdef _WIN32
    VkMemoryGetWin32HandleInfoKHR gi{VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR};
    HANDLE handle = nullptr;
#else
    VkMemoryGetFdInfoKHR gi{VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR};
    int handle = -1;
#endif
    gi.memory = t->mem;
    gi.handleType = kShareType;
    check(reinterpret_cast<ShareExport>(get_handle)(dev, &gi, &handle), kShareExport);
    t->info.handle = intptr_t(handle);
    return t;
  }

  void barrier(Texture& t, VkImageLayout from, VkImageLayout to, VkAccessFlags src_access, VkAccessFlags dst_access,
               VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage, uint32_t src_family = VK_QUEUE_FAMILY_IGNORED,
               uint32_t dst_family = VK_QUEUE_FAMILY_IGNORED) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcAccessMask = src_access;
    b.dstAccessMask = dst_access;
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = src_family;
    b.dstQueueFamilyIndex = dst_family;
    b.image = t.image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &b);
  }
};

std::unique_ptr<Device> Device::create(bool allow_software) {
  if (volkInitialize() != VK_SUCCESS) return nullptr;  // no Vulkan loader on this machine
  auto d = std::make_unique<Impl>();
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.pApplicationName = "photo-beauty";
  app.apiVersion = VK_API_VERSION_1_1;
  VkInstanceCreateInfo ii{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  ii.pApplicationInfo = &app;
  // MoltenVK behind the Vulkan loader (macOS) is only listed with portability enumeration.
  // ponytail: not yet run on a Mac; the Metal backend (P1) replaces this path there.
  uint32_t count = 0;
  vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
  std::vector<VkExtensionProperties> exts(count);
  vkEnumerateInstanceExtensionProperties(nullptr, &count, exts.data());
  const char* portability = "VK_KHR_portability_enumeration";
  if (has(exts, portability)) {
    ii.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    ii.enabledExtensionCount = 1;
    ii.ppEnabledExtensionNames = &portability;
  }
  if (vkCreateInstance(&ii, nullptr, &d->instance) != VK_SUCCESS) return nullptr;
  // Device functions go through loader trampolines (no volkLoadDevice), so several devices
  // can coexist in one process.
  volkLoadInstance(d->instance);

  uint32_t n = 0;
  vkEnumeratePhysicalDevices(d->instance, &n, nullptr);
  std::vector<VkPhysicalDevice> devices(n);
  vkEnumeratePhysicalDevices(d->instance, &n, devices.data());
  int best = 0;
  uint32_t family = 0;
  [[maybe_unused]] bool timestamps = false;  // for Tracy GPU zones
  for (VkPhysicalDevice pd : devices) {
    VkPhysicalDeviceProperties p;
    vkGetPhysicalDeviceProperties(pd, &p);
    if (p.apiVersion < VK_API_VERSION_1_1) continue;  // shaders are SPIR-V 1.3
    const int rank = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU     ? 4
                     : p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 3
                     : p.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU    ? 2
                     : p.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU && allow_software ? 1
                                                                                     : 0;
    if (rank <= best) continue;
    uint32_t nq = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &nq, nullptr);
    std::vector<VkQueueFamilyProperties> queues(nq);
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &nq, queues.data());
    for (uint32_t i = 0; i < nq; i++)
      if (queues[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
        best = rank;
        family = i;
        timestamps = queues[i].timestampValidBits > 0;
        d->phys = pd;
        d->name = p.deviceName;
        d->max_buffer = p.limits.maxStorageBufferRange;
        break;
      }
  }
  if (!d->phys) return nullptr;
  vkGetPhysicalDeviceMemoryProperties(d->phys, &d->mem);
  for (uint32_t i = 0; i < d->mem.memoryHeapCount; i++)
    if (d->mem.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
      d->memory = std::max<size_t>(d->memory, d->mem.memoryHeaps[i].size);
  d->spare->budget = d->memory / 4;

  const float priority = 1;
  VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  qi.queueFamilyIndex = family;
  qi.queueCount = 1;
  qi.pQueuePriorities = &priority;
  VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  di.queueCreateInfoCount = 1;
  di.pQueueCreateInfos = &qi;
  vkEnumerateDeviceExtensionProperties(d->phys, nullptr, &count, nullptr);
  exts.resize(count);
  vkEnumerateDeviceExtensionProperties(d->phys, nullptr, &count, exts.data());
  std::vector<const char*> enable;
  const char* subset = "VK_KHR_portability_subset";  // must be enabled when offered (MoltenVK)
  if (has(exts, subset)) enable.push_back(subset);
  const bool share = has(exts, kShareExtension) && exportable(d->phys);
  if (share) enable.push_back(kShareExtension);
  di.enabledExtensionCount = uint32_t(enable.size());
  di.ppEnabledExtensionNames = enable.data();
  if (vkCreateDevice(d->phys, &di, nullptr, &d->dev) != VK_SUCCESS) return nullptr;
  d->family = family;
  vkGetDeviceQueue(d->dev, family, 0, &d->queue);
  if (share) d->get_handle = vkGetDeviceProcAddr(d->dev, kShareExport);
  VkPhysicalDeviceIDProperties ids{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
  VkPhysicalDeviceDriverPropertiesKHR drv{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES_KHR};
  VkPhysicalDeviceProperties2 props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
  props.pNext = &ids;
  if (has(exts, VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME)) ids.pNext = &drv;
  vkGetPhysicalDeviceProperties2(d->phys, &props);
  std::copy_n(ids.deviceUUID, VK_UUID_SIZE, d->device_uuid.begin());
  std::copy_n(ids.driverUUID, VK_UUID_SIZE, d->driver_uuid.begin());
  if (ids.pNext) d->driver = std::string(drv.driverName) + " " + drv.driverInfo;

  VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  pi.queueFamilyIndex = family;
  check(vkCreateCommandPool(d->dev, &pi, nullptr, &d->pool), "vkCreateCommandPool");
  for (Device::Impl::Batch& b : d->batches) {
    VkCommandBufferAllocateInfo ci{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ci.commandPool = d->pool;
    ci.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ci.commandBufferCount = 1;
    check(vkAllocateCommandBuffers(d->dev, &ci, &b.cmd), "vkAllocateCommandBuffers");
    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    check(vkCreateFence(d->dev, &fi, nullptr, &b.fence), "vkCreateFence");
  }
#ifdef PB_TRACY
  if (timestamps) d->tracy = TracyVkContext(d->phys, d->dev, d->queue, d->batches[0].cmd);
#endif
  return std::unique_ptr<Device>(new Device(std::move(d)));
}

Device::Device(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Device::~Device() = default;
std::string Device::name() const { return impl_->name; }
std::string Device::driver() const { return impl_->driver; }
void Device::trim() { impl_->spare->clear(); }
size_t Device::max_buffer() const { return impl_->max_buffer; }
size_t Device::memory() const { return impl_->memory; }

BufferPtr Device::buffer(size_t bytes) {
  return impl_->make_buffer(
      bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
}

KernelPtr Device::kernel(const char* name, const uint32_t* spirv, size_t spirv_bytes, int buffers,
                         size_t push_bytes) {
  const VkDevice dev = impl_->dev;
  auto k = std::make_shared<Kernel>();
  k->dev = dev;
  k->buffers = buffers;
  k->name = name;
  VkShaderModuleCreateInfo si{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  si.codeSize = spirv_bytes;
  si.pCode = spirv;
  check(vkCreateShaderModule(dev, &si, nullptr, &k->module), "vkCreateShaderModule");
  std::vector<VkDescriptorSetLayoutBinding> bindings(buffers);
  for (int i = 0; i < buffers; i++)
    bindings[i] = {uint32_t(i), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
  VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  li.bindingCount = uint32_t(buffers);
  li.pBindings = bindings.data();
  check(vkCreateDescriptorSetLayout(dev, &li, nullptr, &k->set_layout), "vkCreateDescriptorSetLayout");
  const VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0, uint32_t(push_bytes)};
  VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  pli.setLayoutCount = 1;
  pli.pSetLayouts = &k->set_layout;
  pli.pushConstantRangeCount = push_bytes ? 1 : 0;
  pli.pPushConstantRanges = &range;
  check(vkCreatePipelineLayout(dev, &pli, nullptr, &k->layout), "vkCreatePipelineLayout");
  VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  ci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  ci.stage.module = k->module;
  ci.stage.pName = "main";
  ci.layout = k->layout;
  // ponytail: no VkPipelineCache yet; add one persisted to disk when startup time matters.
  check(vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &ci, nullptr, &k->pipeline), "vkCreateComputePipelines");
  return k;
}

void Device::upload(Buffer& dst, const void* src, size_t bytes, size_t offset) {
  if (!bytes) return;
  Impl& d = *impl_;
  // ponytail: always through staging, even on unified memory (zero-copy is a P2 item).
  BufferPtr st = d.make_buffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  std::memcpy(st->mapped, src, bytes);
  d.record({st.get(), &dst});
  const VkBufferCopy region{0, offset, bytes};
  vkCmdCopyBuffer(d.cmd, st->buf, dst.buf, 1, &region);
}

void Device::fill(Buffer& dst, uint32_t value) {
  impl_->record({&dst});
  vkCmdFillBuffer(impl_->cmd, dst.buf, 0, VK_WHOLE_SIZE, value);
}

void Device::dispatch(Kernel& k, std::initializer_list<Buffer*> buffers, const void* push, size_t push_bytes,
                      uint32_t groups_x, uint32_t groups_y) {
  Impl& d = *impl_;
  if (int(buffers.size()) != k.buffers) throw std::logic_error("dispatch: wrong number of buffers");
  d.record(buffers);  // first: the batch the descriptor set comes from
  const VkDescriptorSet set = d.descriptor_set(k.set_layout, uint32_t(buffers.size()));
  std::vector<VkDescriptorBufferInfo> infos;
  for (Buffer* b : buffers) infos.push_back({b->buf, 0, VK_WHOLE_SIZE});
  std::vector<VkWriteDescriptorSet> writes(infos.size(), {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET});
  for (size_t i = 0; i < infos.size(); i++) {
    writes[i].dstSet = set;
    writes[i].dstBinding = uint32_t(i);
    writes[i].descriptorCount = 1;
    writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[i].pBufferInfo = &infos[i];
  }
  vkUpdateDescriptorSets(d.dev, uint32_t(writes.size()), writes.data(), 0, nullptr);
  vkCmdBindPipeline(d.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, k.pipeline);
  vkCmdBindDescriptorSets(d.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, k.layout, 0, 1, &set, 0, nullptr);
  if (push_bytes) vkCmdPushConstants(d.cmd, k.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, uint32_t(push_bytes), push);
#ifdef PB_TRACY
  TracyVkZoneTransient(d.tracy, zone, d.cmd, k.name.c_str(), d.tracy != nullptr);
#endif
  vkCmdDispatch(d.cmd, groups_x, groups_y, 1);
}

void Device::download(Buffer& src, void* dst, size_t bytes, size_t row, size_t stride) {
  if (!bytes) return;
  Impl& d = *impl_;
  BufferPtr st = d.make_buffer(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                               VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
  d.record({&src, st.get()});
  const VkBufferCopy region{0, 0, bytes};
  vkCmdCopyBuffer(d.cmd, src.buf, st->buf, 1, &region);
  d.batch->readbacks.push_back({std::move(st), dst, bytes, row, stride});
}

uint64_t Device::submit() {
  Impl& d = *impl_;
  if (!d.recording) return d.submitted;  // the last one, if any
  Impl::Batch& b = *d.batch;
  if (!b.readbacks.empty()) {
    VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(d.cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, nullptr, 0,
                         nullptr);
  }
  d.recording = false;
  check(vkEndCommandBuffer(d.cmd), "vkEndCommandBuffer");
  VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  si.commandBufferCount = 1;
  si.pCommandBuffers = &d.cmd;
  check(vkQueueSubmit(d.queue, 1, &si, b.fence), "vkQueueSubmit");
  b.serial = ++d.submitted;
  d.batch = &d.batches[(&b - d.batches.data() + 1) % d.batches.size()];
  return b.serial;
}

void Device::wait(uint64_t submission) {
  Impl& d = *impl_;
  for (Impl::Batch* b : {d.batch, &d.batches[(d.batch - d.batches.data() + 1) % d.batches.size()]})  // oldest first
    if (b->serial && b->serial <= submission) d.finish(*b);
}

void Device::flush() { wait(submit()); }

TexturePtr Device::texture(int width, int height) {
  if (!impl_->get_handle || width <= 0 || height <= 0) return nullptr;
  return impl_->make_texture(width, height, nullptr);
}

TexturePtr Device::import(const Shared& s) {
  Impl& d = *impl_;
  if (!d.get_handle || s.device_uuid != d.device_uuid || s.driver_uuid != d.driver_uuid) return nullptr;
  return d.make_texture(s.width, s.height, &s);
}

std::array<int, 2> size(const Texture& t) { return {t.info.width, t.info.height}; }

void close_handle(Shared& s) {
#ifdef _WIN32
  if (s.handle) CloseHandle(HANDLE(s.handle));
  s.handle = 0;
#else
  if (s.handle >= 0) close(int(s.handle));
  s.handle = -1;
#endif
}

void Device::copy(Buffer& src, Texture& dst, int x, int y, int w, int h) {
  Impl& d = *impl_;
  d.record({&src});
  if (!dst.writing) {  // a new picture
    d.barrier(dst, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
              VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    dst.writing = true;
  }
  VkBufferImageCopy r{};
  r.bufferRowLength = uint32_t(w);
  r.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  r.imageOffset = {x, y, 0};
  r.imageExtent = {uint32_t(w), uint32_t(h), 1};
  vkCmdCopyBufferToImage(d.cmd, src.buf, dst.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);
}

void Device::release(Texture& t) {
  Impl& d = *impl_;
  if (!t.writing) return;
  d.record();
  d.barrier(t, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            d.family, VK_QUEUE_FAMILY_EXTERNAL);
  t.writing = false;
}

void Device::download(Texture& src, void* dst) {
  Impl& d = *impl_;
  const int w = src.info.width, h = src.info.height;
  const size_t bytes = size_t(w) * h * 4;
  BufferPtr st = d.make_buffer(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                               VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
  d.record({st.get()});
  d.barrier(src, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 0,
            VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_QUEUE_FAMILY_EXTERNAL, d.family);  // acquire what the exporter released
  VkBufferImageCopy r{};
  r.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  r.imageExtent = {uint32_t(w), uint32_t(h), 1};
  vkCmdCopyImageToBuffer(d.cmd, src.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, st->buf, 1, &r);
  d.batch->readbacks.push_back({std::move(st), dst, bytes, 0, 0});
}

}  // namespace pb::gpu
