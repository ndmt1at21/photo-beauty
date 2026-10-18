// copied from snapone ac04c50 (ui/rawframe.cpp: frames on the GPU: GpuWindow, acquire(), FrameNode, the two
// Core::useGpu() as useGpu() and useWindow(), RawFrame's texture pool), without its tiles, stand-ins and renders.
#include "canvas.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QPlatformSurfaceEvent>
#include <QPointer>
#include <QQuickGraphicsConfiguration>
#include <QQuickGraphicsDevice>
#include <QQuickWindow>
#include <QSGImageNode>
#include <QSGTransformNode>
#include <QScreen>
#include <QUrl>
#include <QVulkanFunctions>
#include <QVulkanInstance>
#include <QtQuick/qsgtexture_platform.h>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <vulkan/vulkan_win32.h>
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <utility>

#include "gpu.h"
#include "jobs.h"
#include "open.h"
#include "pattern.spv.h"
#include "pbd.h"
#include "profile.h"

Q_LOGGING_CATEGORY(lcGpu, "pb.gpu", QtWarningMsg)  // QT_LOGGING_RULES="pb.gpu=true": the frames' route, each frame

namespace {

// ---- frames on the GPU ----
// The window draws with Vulkan on the frames' GPU, its own device beside theirs: the GPU has one queue that both
// cannot share (snapone's docs/ui-plan.md, N2). Frames are drawn into textures whose memory the window's device imports
// (gpu.h, Shared); before a frame shows new pixels of one, the window takes it over from the frames' device with an
// acquire barrier.
#ifdef _WIN32
constexpr auto kShareType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
constexpr const char* kShareExtension = "VK_KHR_external_memory_win32";
#else
constexpr auto kShareType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
constexpr const char* kShareExtension = "VK_KHR_external_memory_fd";
#endif

struct GpuWindow : QObject {
  std::unique_ptr<pb::gpu::Device> dev;  // the frames'
  std::mutex lock;                       // one thread at a time on dev: the canvases' frame jobs
  pb::gpu::KernelPtr pattern;            // shaders/pattern.slang
  QVulkanInstance vk;
  pb::gpu::Shared ids;              // of the frames' GPU and driver
  QPointer<QQuickWindow> win;       // set up, on the frames' GPU: the first Qt Quick window, then the next one
  std::atomic<uint64_t> frames{0};  // synchronized so far
  std::atomic<bool> broken{false};  // an import failed: frames through the CPU from then on
  std::vector<VkImage> acquire;     // render thread: with new pixels, for the frame being made
  bool eventFilter(QObject* o, QEvent* e) override;
};
GpuWindow& gpuWindow() {
  static auto* g = new GpuWindow;  // never freed, as the windows it serves
  return *g;
}
// A texture off screen at the frame `frames` was then is free of the GPU from this frame on: two frames are in flight
// at most.
uint64_t offScreenFrom() { return gpuWindow().frames + 3; }

// Render thread, before the frame's render pass: the acquire barriers for the images with new pixels.
void acquire(QQuickWindow* w) {
  GpuWindow& g = gpuWindow();
  if (g.acquire.empty()) return;
  QSGRendererInterface* rif = w->rendererInterface();
  auto* cb = static_cast<VkCommandBuffer*>(rif->getResource(w, QSGRendererInterface::CommandListResource));
  auto* dev = static_cast<VkDevice*>(rif->getResource(w, QSGRendererInterface::DeviceResource));
  auto* family = static_cast<int*>(rif->getResource(w, QSGRendererInterface::GraphicsQueueFamilyIndexResource));
  std::vector<VkImageMemoryBarrier> bs;
  for (VkImage image : g.acquire) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    b.oldLayout = b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL;
    b.dstQueueFamilyIndex = uint32_t(*family);
    b.image = image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    bs.push_back(b);
  }
  g.acquire.clear();
  w->beginExternalCommands();
  w->vulkanInstance()->deviceFunctions(*dev)->vkCmdPipelineBarrier(
      *cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr,
      uint32_t(bs.size()), bs.data());
  w->endExternalCommands();
}

// Canvas's paint node. A transform node, not a plain one: the software renderer (offscreen, CI) places what changes
// under a node by that node's state, and keeps none for plain nodes. With frames on the GPU it keeps the frames'
// textures imported into the window's device, by serial.
class FrameNode : public QSGTransformNode {
 public:
  ~FrameNode() override {
    if (imports_.empty() && dying_.empty()) return;
    df_->vkDeviceWaitIdle(dev_);  // the render thread is the only one on the window's queue
    for (auto& [serial, im] : imports_) {
      delete im.tex;
      destroy(im);
    }
    for (Imported& im : dying_) destroy(im);
  }
  // Render thread, during sync. Null if the import fails.
  QSGTexture* texture(QQuickWindow* w, uint64_t serial, pb::gpu::Texture& t, bool fresh) {
    auto it = imports_.find(serial);
    if (it == imports_.end()) {
      Imported im;
      if (!import(w, t, im)) return nullptr;
      it = imports_.emplace(serial, im).first;
    }
    if (fresh) gpuWindow().acquire.push_back(it->second.image);
    return it->second.tex;
  }

  // The imports of textures that may show again stay; the rest go once the GPU is done with them.
  void keep(const std::vector<uint64_t>& serials) {
    const uint64_t now = gpuWindow().frames;
    for (auto it = imports_.begin(); it != imports_.end();) {
      if (std::find(serials.begin(), serials.end(), it->first) != serials.end()) {
        ++it;
        continue;
      }
      delete it->second.tex;  // Qt releases its view of the image after its frames in flight
      it->second.dropAt = now + 3;
      dying_.push_back(it->second);
      it = imports_.erase(it);
    }
    for (auto it = dying_.begin(); it != dying_.end();) {
      if (it->dropAt > now) {
        ++it;
        continue;
      }
      destroy(*it);
      it = dying_.erase(it);
    }
  }

 private:
  struct Imported {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    QSGTexture* tex = nullptr;
    uint64_t dropAt = 0;
  };
  // The image gpu.h's Shared describes, on the memory of t.
  bool import(QQuickWindow* w, pb::gpu::Texture& t, Imported& im) {
    QSGRendererInterface* rif = w->rendererInterface();
    if (rif->graphicsApi() != QSGRendererInterface::Vulkan) return false;
    dev_ = *static_cast<VkDevice*>(rif->getResource(w, QSGRendererInterface::DeviceResource));
    const auto pd = *static_cast<VkPhysicalDevice*>(rif->getResource(w, QSGRendererInterface::PhysicalDeviceResource));
    df_ = w->vulkanInstance()->deviceFunctions(dev_);
    pb::gpu::Shared s;
    try {
      s = pb::gpu::share(t);
    } catch (const std::exception& e) {
      qWarning("pb: %s", e.what());
      return false;
    }
    VkExternalMemoryImageCreateInfo ei{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
    ei.handleTypes = kShareType;
    VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.pNext = &ei;
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = VK_FORMAT_R8G8B8A8_UNORM;
    ii.extent = {uint32_t(s.width), uint32_t(s.height), 1};
    ii.mipLevels = ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    bool ok = df_->vkCreateImage(dev_, &ii, nullptr, &im.image) == VK_SUCCESS;
    if (ok) {
      VkMemoryRequirements req;
      df_->vkGetImageMemoryRequirements(dev_, im.image, &req);
      VkPhysicalDeviceMemoryProperties mp;
      w->vulkanInstance()->functions()->vkGetPhysicalDeviceMemoryProperties(pd, &mp);
      uint32_t type = 0;
      while (type < mp.memoryTypeCount && !((req.memoryTypeBits >> type & 1) &&
                                            (mp.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)))
        type++;
      VkMemoryDedicatedAllocateInfo dai{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
      dai.image = im.image;
#ifdef _WIN32
      VkImportMemoryWin32HandleInfoKHR mi{VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR};
      mi.handle = HANDLE(s.handle);  // stays ours: closed below
#else
      VkImportMemoryFdInfoKHR mi{VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR};
      mi.fd = int(s.handle);
#endif
      mi.pNext = &dai;
      mi.handleType = kShareType;
      VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
      ai.pNext = &mi;
      ai.allocationSize = s.bytes;
      ai.memoryTypeIndex = type;
      ok = type < mp.memoryTypeCount && df_->vkAllocateMemory(dev_, &ai, nullptr, &im.mem) == VK_SUCCESS;
#ifndef _WIN32
      if (ok) s.handle = -1;  // the import took the fd
#endif
      ok = ok && df_->vkBindImageMemory(dev_, im.image, im.mem, 0) == VK_SUCCESS;
    }
    pb::gpu::close_handle(s);
    if (ok)  // premultiplied, as the CPU route's QImage
      im.tex = QNativeInterface::QSGVulkanTexture::fromNative(im.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, w,
                                                              QSize(s.width, s.height), QQuickWindow::TextureHasAlphaChannel);
    if (!im.tex) {
      qWarning("pb: the window cannot import the canvas's frames; they come through the CPU");
      destroy(im);
      return false;
    }
    qCDebug(lcGpu, "imported a %d x %d frame", s.width, s.height);
    return true;
  }
  void destroy(Imported& im) {
    df_->vkDestroyImage(dev_, im.image, nullptr);
    df_->vkFreeMemory(dev_, im.mem, nullptr);
  }

  VkDevice dev_ = VK_NULL_HANDLE;
  QVulkanDeviceFunctions* df_ = nullptr;
  std::map<uint64_t, Imported> imports_;
  std::vector<Imported> dying_;
};

// The window on the frames' GPU (snapone's Core::useGpu(QQuickWindow*)): before its scene graph starts.
void useWindow(QQuickWindow* win) {
  GpuWindow& g = gpuWindow();
  win->setVulkanInstance(&g.vk);
  QVulkanFunctions* f = g.vk.functions();
  uint32_t n = 0;
  f->vkEnumeratePhysicalDevices(g.vk.vkInstance(), &n, nullptr);
  std::vector<VkPhysicalDevice> pds(n);
  f->vkEnumeratePhysicalDevices(g.vk.vkInstance(), &n, pds.data());
  for (VkPhysicalDevice pd : pds) {
    VkPhysicalDeviceIDProperties id{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
    VkPhysicalDeviceProperties2 props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    props.pNext = &id;
    f->vkGetPhysicalDeviceProperties2(pd, &props);
    if (!std::equal(g.ids.device_uuid.begin(), g.ids.device_uuid.end(), id.deviceUUID) ||
        !std::equal(g.ids.driver_uuid.begin(), g.ids.driver_uuid.end(), id.driverUUID))
      continue;
    win->setGraphicsDevice(QQuickGraphicsDevice::fromPhysicalDevice(pd));
    QQuickGraphicsConfiguration c = win->graphicsConfiguration();
    c.setDeviceExtensions({"VK_KHR_external_memory", kShareExtension});
    win->setGraphicsConfiguration(c);
    QObject::connect(win, &QQuickWindow::afterSynchronizing, win, [] { gpuWindow().frames++; }, Qt::DirectConnection);
    QObject::connect(win, &QQuickWindow::beforeRendering, win, [win] { acquire(win); }, Qt::DirectConnection);
    g.acquire.clear();
    g.win = win;
    qCInfo(lcGpu, "frames on the GPU: %s", g.dev->name().c_str());
    return;
  }
  qWarning("pb: Qt's Vulkan does not list the canvas's GPU; frames come through the CPU");
}

// The first Qt Quick window, and the next once it is gone, as its native window is made: before it shows.
bool GpuWindow::eventFilter(QObject* o, QEvent* e) {
  if (e->type() == QEvent::PlatformSurface && !win &&
      static_cast<QPlatformSurfaceEvent*>(e)->surfaceEventType() == QPlatformSurfaceEvent::SurfaceCreated)
    if (auto* w = qobject_cast<QQuickWindow*>(o)) useWindow(w);
  return false;
}

// Qt Quick on Vulkan, on the GPU frames are drawn on, when there will be a canvas (PB_CANVAS) and frames may go
// through the GPU: snapone's Core::useGpu(), from the QGuiApplication constructor, before any Qt Quick window.
void useGpu() {
  const QString platform = QGuiApplication::platformName();
  if (qEnvironmentVariableIsEmpty("PB_CANVAS") || qEnvironmentVariable("PB_GPU_FRAMES") == u"0" ||
      (platform != u"windows" && platform != u"xcb" && platform != u"wayland"))
    return;
  GpuWindow& g = gpuWindow();
  try {
    g.dev = pb::gpu::Device::create();
    const auto t = g.dev ? g.dev->texture(1, 1) : nullptr;
    if (!t) {  // no GPU, or one that cannot share memory
      g.dev.reset();
      return;
    }
    g.ids = pb::gpu::share(*t);
    pb::gpu::close_handle(g.ids);
    g.pattern = g.dev->kernel("pattern", pattern_spv, pattern_spv_sizeInBytes, 1, sizeof(pb::PatternConsts));
  } catch (const std::exception& e) {
    qWarning("pb: %s; frames come through the CPU", e.what());
    g.dev.reset();
    return;
  }
  g.vk.setApiVersion(QVersionNumber(1, 1));  // external memory in core
  g.vk.setExtensions(QQuickGraphicsConfiguration::preferredInstanceExtensions());
  if (!g.vk.create()) {
    g.dev.reset();
    return;
  }
  QQuickWindow::setGraphicsApi(QSGRendererInterface::Vulkan);
  qApp->installEventFilter(&g);
}
Q_COREAPP_STARTUP_FUNCTION(useGpu)

bool same(const pb::View& a, const pb::View& b) {
  return a.x == b.x && a.y == b.y && a.zoom == b.zoom && a.angle == b.angle && a.width == b.width &&
         a.height == b.height && a.dpr == b.dpr;
}

// A path, or a file: URL's.
std::filesystem::path local(const QString& s) {
  const QUrl url(s);
  return QFileInfo(url.isLocalFile() ? url.toLocalFile() : s).filesystemFilePath();
}

// A texture of the frames' device a frame went into.
struct Frame {
  std::shared_ptr<pb::gpu::Texture> tex;
  uint64_t serial = 0;  // its import's key in the paint node
  uint64_t freeAt = 0;  // the window's frame from which none in flight samples it
};

// The .pbd's preview where the frame shows the document, nearest, its 8-bit values as they are; the pasteboard past
// the canvas. ponytail: right for sRGB documents on sRGB screens; the Presenter's colours (Source, Display) if others
// show it long enough to tell.
QImage previewFrame(const pb::Preview& p, const pb::View& v, const pb::DocState& doc, const pb::Look& look) {
  const pb::View::Pixels px = v.pixels();
  QImage im(px.width, px.height, QImage::Format_RGBA8888_Premultiplied);
  const uint32_t board = look.pasteboard[0] | look.pasteboard[1] << 8 | look.pasteboard[2] << 16 | 0xff000000u;
  const int g = p.channels == 3 ? 1 : 0, b = p.channels == 3 ? 2 : 0;
  pb::parallel_for(px.height, [&](int begin, int end) {
    for (int j = begin; j < end; j++) {
      auto* row = reinterpret_cast<uint32_t*>(im.scanLine(j));
      for (int i = 0; i < px.width; i++) {
        const double x = px.origin[0] + i * px.across[0] + j * px.down[0];
        const double y = px.origin[1] + i * px.across[1] + j * px.down[1];
        if (!(x >= 0 && y >= 0 && x < doc.width && y < doc.height)) {
          row[i] = board;
          continue;
        }
        const size_t u = size_t(std::min(int(x) >> p.level, p.width - 1));
        const size_t w = size_t(std::min(int(y) >> p.level, p.height - 1));
        const uint8_t* q = &p.pixels[(w * size_t(p.width) + u) * size_t(p.channels)];
        row[i] = q[0] | uint32_t(q[g]) << 8 | uint32_t(q[b]) << 16 | 0xff000000u;
      }
    }
  });
  return im;
}

// Canvas.shown: what a frame of view v showed.
QVariantMap stats(const pb::Presenter::Frame& f, const pb::View& v, bool preview) {
  return {{"x", v.x},           {"y", v.y},           {"zoom", v.zoom},           {"angle", v.angle},
          {"tiles", f.tiles},   {"coarser", f.coarser}, {"missing", f.missing}, {"built", f.built},
          {"pending", f.pending}, {"preview", preview}};
}

}  // namespace

// What a canvas shares with its frame job (a Latest, Interactive: one frame at a time, the newest asked for next) and
// the scene graph (the render thread, during sync): the frame up and the textures off screen, under m. Jobs hold it,
// so it outlives the canvas until they are done.
struct CanvasState {
  std::mutex m;
  Canvas* canvas = nullptr;  // null once it goes; told under m
  Frame onScreen;            // the frame up through the GPU: its texture
  std::vector<Frame> pool;   // off screen, for the next frames of their size
  QImage image;              // the frame up through the CPU (a preview on either route)
  bool fresh = false;        // a frame the scene graph has not taken yet
  // The job's alone, after m: they go first, and their on_ready() takes m.
  uint64_t serials = 0;
  std::weak_ptr<const pb::DocState> previewed;  // the document whose preview went up
  std::unique_ptr<pb::Presenter> gpu, cpu;  // gpu: under GpuWindow::lock
  pb::Latest job{pb::Priority::Interactive};

  ~CanvasState() {
    const std::lock_guard l(gpuWindow().lock);
    gpu.reset();
  }

  // The presenters' on_ready(), on a pool thread: tiles a frame lacked came, so another frame.
  std::function<void()> ready() {
    return [this] {
      const std::lock_guard l(m);
      if (canvas) QMetaObject::invokeMethod(canvas, [c = canvas] { c->ready_++, c->polish(); }, Qt::QueuedConnection);
    };
  }
  // The GPU presenter before the first document comes: its kernels compile 1.7 s the first time on a machine (B3).
  void warm() {
    const std::lock_guard l(gpuWindow().lock);
    if (!gpu) gpu = std::make_unique<pb::Presenter>(gpuWindow().dev.get(), ready());
  }

  // The job: a frame of `a`, the .pbd's preview first while its document has shown none; through the GPU when asked
  // and it can, else the CPU.
  void draw(const Canvas::Ask& a, const pb::Look& look, const std::shared_ptr<const pb::Preview>& preview) {
    PROFILE_ZONE("canvas_frame");
    QElapsedTimer clock;
    clock.start();
    if (preview && a.doc && previewed.lock() != a.doc) {
      previewed = a.doc;
      up({}, previewFrame(*preview, a.view, *a.doc, look), stats({}, a.view, true), clock, false);
      clock.start();
    }
    pb::Presenter::Frame pf;
    Frame f;
    if (a.gpu && drawGpu(a, look, f, pf)) return up(f, {}, stats(pf, a.view, false), clock, true);
    const pb::View::Pixels px = a.view.pixels();
    QImage im(px.width, px.height, QImage::Format_RGBA8888_Premultiplied);
    if (a.doc) {
      if (!cpu) cpu = std::make_unique<pb::Presenter>(nullptr, ready());
      pb::Target t;
      t.pixels = reinterpret_cast<uint32_t*>(im.bits());  // rows packed: bytesPerLine() is width x 4
      pf = cpu->frame(pb::RenderInput{a.doc, {}}, a.view, t, look);
    } else {
      const pb::PatternConsts k = pb::pattern_consts(a.view);
      uchar* bits = im.bits();
      const qsizetype stride = im.bytesPerLine();
      pb::parallel_for(px.height, [&](int begin, int end) {
        for (int j = begin; j < end; j++) {
          auto* row = reinterpret_cast<uint32_t*>(bits + j * stride);
          for (int i = 0; i < px.width; i++) row[i] = pb::pattern_pixel(k, i, j);
        }
      });
    }
    up({}, std::move(im), stats(pf, a.view, false), clock, false);
  }

  // Into a texture the window imports, as RawFrame's renders (snapone's lease()): one free of the frames in flight.
  bool drawGpu(const Canvas::Ask& a, const pb::Look& look, Frame& f, pb::Presenter::Frame& pf) {
    GpuWindow& g = gpuWindow();
    const std::lock_guard l(g.lock);
    if (g.broken) return false;
    const pb::View::Pixels px = a.view.pixels();
    const std::array<int, 2> size{px.width, px.height};
    try {
      {
        // A texture no frame in flight samples (freeAt): the window's GPU may read one for two frames after it left
        // the screen, and writing it then shows a mix of two frames, a race no test catches.
        const std::lock_guard sl(m);
        const auto free = std::find_if(pool.begin(), pool.end(), [&](const Frame& p) {
          return pb::gpu::size(*p.tex) == size && p.freeAt <= g.frames;
        });
        if (free != pool.end()) f = *free, pool.erase(free);
      }
      if (!f.tex) {
        if (!(f.tex = g.dev->texture(px.width, px.height))) return false;
        f.serial = ++serials;
      }
      const pb::gpu::BufferPtr out = g.dev->buffer(size_t(px.width) * px.height * 4);
      if (a.doc) {
        if (!gpu) gpu = std::make_unique<pb::Presenter>(g.dev.get(), ready());
        pb::Target t;
        t.buffer = out.get();
        pf = gpu->frame(pb::RenderInput{a.doc, {}}, a.view, t, look);
      } else {
        const pb::PatternConsts k = pb::pattern_consts(a.view);
        g.dev->dispatch(*g.pattern, {out.get()}, &k, sizeof k, uint32_t(px.width + 7) / 8, uint32_t(px.height + 7) / 8);
      }
      // ponytail: copy buffer→texture ~1.2 ms at 1080p on Iris Xe; write the shared image as a storage image (HAL)
      // when the stroke latency budget needs it
      g.dev->copy(*out, *f.tex, 0, 0, px.width, px.height);
      g.dev->release(*f.tex);
      g.dev->flush();
    } catch (const std::exception& e) {  // out of device memory
      qWarning("pb: %s; frames come through the CPU", e.what());
      g.broken = true;
      return false;
    }
    return true;
  }

  // The frame up, a texture (f.tex) or an image, then the canvas told.
  void up(const Frame& f, QImage im, const QVariantMap& shown, const QElapsedTimer& clock, bool gpu) {
    const double ms = clock.nsecsElapsed() / 1e6;
    const std::lock_guard l(m);
    if (f.tex) {
      if (onScreen.tex) {
        onScreen.freeAt = offScreenFrom();
        pool.push_back(onScreen);
      }
      // ponytail: 4 textures off screen at most, oldest out; a byte budget if frames ever come in many sizes at once
      if (pool.size() > 4) pool.erase(pool.begin());
      onScreen = f;
      image = {};
    } else {
      image = std::move(im);
      onScreen = {};
      pool.clear();
    }
    fresh = true;
    if (canvas)
      QMetaObject::invokeMethod(canvas, [c = canvas, shown, ms, gpu] { c->up(shown, ms, gpu); }, Qt::QueuedConnection);
  }
};

bool Canvas::Ask::operator==(const Ask& o) const {
  return same(view, o.view) && doc == o.doc && look == o.look && ready == o.ready && gpu == o.gpu;
}

Canvas::Canvas(QQuickItem* parent) : QQuickItem(parent), s_(std::make_shared<CanvasState>()) {
  s_->canvas = this;
  setFlag(ItemHasContents);
}

Canvas::~Canvas() {
  const std::lock_guard l(s_->m);
  s_->canvas = nullptr;
}

void Canvas::setView(const QVariantMap& m) {
  if (m == viewMap_) return;
  viewMap_ = m;
  const auto number = [&](const char* key, double otherwise) {
    bool ok = false;
    const double d = m.value(QLatin1String(key)).toDouble(&ok);
    return ok && std::isfinite(d) ? d : otherwise;
  };
  view_.x = number("x", 0), view_.y = number("y", 0), view_.angle = number("angle", 0);
  view_.zoom = std::clamp(number("zoom", 1), 0.01, 128.);
  polish();
  emit viewChanged();
}

// ponytail: N1's Session replaces this way in (and the flag).
void Canvas::setSource(const QString& source) {
  if (source == source_) return;
  source_ = source;
  error_.clear();
  const uint64_t open = ++opens_;
  setDocument(nullptr);  // what is up stays while it opens
  emit sourceChanged();
  if (source.isEmpty()) return;
  const std::filesystem::path path = local(source);
  const bool pbd = QFileInfo(source).suffix().compare(u"pbd", Qt::CaseInsensitive) == 0;
  pb::submit(pb::Priority::Background, [s = s_, open, path, pbd] {
    PROFILE_ZONE("canvas_open");
    pb::DocPtr doc;
    std::shared_ptr<pb::Preview> preview;
    QString error;
    try {
      if (pbd) {
        pb::OpenedPbd o = pb::open_pbd(path);
        doc = std::move(o.doc);
        if (o.preview.width > 0) preview = std::make_shared<pb::Preview>(std::move(o.preview));
      } else {
        doc = pb::open_image(path);
      }
    } catch (const std::exception& e) {
      error = QString::fromUtf8(e.what());
    }
    const std::lock_guard l(s->m);
    if (s->canvas)
      QMetaObject::invokeMethod(s->canvas, [c = s->canvas, open, doc, preview, error] {
        if (open != c->opens_) return;  // another source since
        c->error_ = error;
        c->setDocument(doc, preview);
      }, Qt::QueuedConnection);
  });
}

void Canvas::setDocument(pb::DocPtr doc, std::shared_ptr<const pb::Preview> preview) {
  doc_ = std::move(doc);
  preview_ = std::move(preview);
  reference_.clear();
  polish();
  emit documentChanged();
}

QSize Canvas::docSize() const { return doc_ ? QSize(doc_->width, doc_->height) : QSize(0, 0); }

void Canvas::setPasteboard(const QColor& c) {
  if (c == pasteboard_) return;
  pasteboard_ = c;
  looks_++;
  polish();
  emit pasteboardChanged();
}

pb::View Canvas::current() const {
  pb::View v = view_;
  v.width = width(), v.height = height();
  v.dpr = window() ? window()->effectiveDevicePixelRatio() : 1;
  return v;
}

QPointF Canvas::toCanvas(QPointF doc) const {
  const auto p = current().to_canvas(doc.x(), doc.y());
  return {p[0], p[1]};
}

QPointF Canvas::toDoc(QPointF canvas) const {
  const auto p = current().to_doc(canvas.x(), canvas.y());
  return {p[0], p[1]};
}

QVariantList Canvas::composite(int level, int x, int y) const {
  pb::Pixel v{};
  if (doc_ && level >= 0 && level < 32 && x >= 0 && y >= 0) {
    const std::array<int, 3> at{level, x >> pb::kTileLog2, y >> pb::kTileLog2};
    auto it = reference_.find(at);
    if (it == reference_.end())
      it = reference_.emplace(at, pb::composite_tile(pb::RenderInput{doc_, {}}, level, at[1], at[2])).first;
    const pb::TilePtr& t = it->second;
    if (t && t->is_solid()) {
      v = t->value();
    } else if (t) {  // RGBA U16 rows
      const pb::Pin p = t->pin();
      const int mask = pb::kTileSize - 1;
      std::memcpy(v.data(), p.data() + ((size_t(y & mask) << pb::kTileLog2) + size_t(x & mask)) * 8, 8);
    }
  }
  return {int(v[0]), int(v[1]), int(v[2]), int(v[3])};
}

QString Canvas::save(const QString& path) const {
  if (!doc_) return QStringLiteral("no document");
  try {
    pb::save_pbd(*doc_, local(path));
  } catch (const std::exception& e) {
    return QString::fromUtf8(e.what());
  }
  return {};
}

void Canvas::geometryChange(const QRectF& now, const QRectF& old) {
  QQuickItem::geometryChange(now, old);
  polish();
  update();  // the frame's rect, until the next frame
}

void Canvas::itemChange(ItemChange c, const ItemChangeData& d) {
  QQuickItem::itemChange(c, d);
  if (c == ItemSceneChange) {
    asked_.reset();  // another window: its own textures
    disconnect(screen_);
    if (d.window) {
      screen_ = connect(d.window, &QWindow::screenChanged, this, &Canvas::readScreen);
      readScreen();
      if (gpuWindow().dev) pb::submit(pb::Priority::Background, [s = s_] { s->warm(); });
    }
  }
  if (c == ItemSceneChange || c == ItemVisibleHasChanged || c == ItemDevicePixelRatioHasChanged) polish();
}

// The screen's colours, from its profile (V2a's Look::screen): none (sRGB) where it has none, as on Linux and macOS
// until P1.
void Canvas::readScreen() {
  std::shared_ptr<const pb::Display> d;
  if (window() && window()->screen()) {
    try {
      const std::vector<uint8_t> icc = pb::screen_profile(window()->screen()->name().toStdString());
      if (!icc.empty()) d = pb::display_profile(icc);
    } catch (const std::exception& e) {
      qWarning("pb: the screen's profile: %s; frames in sRGB", e.what());
    }
  }
  if (!d && !look_.screen) return;
  look_.screen = std::move(d);
  looks_++;
  polish();
}

// A frame when what it shows changed since the last one asked for, drawn by the frame job; nothing otherwise.
void Canvas::updatePolish() {
  const pb::View v = current();
  if (!window() || !isVisible() || v.pixels().width == 0 || (!doc_ && !source_.isEmpty())) return;
  const GpuWindow& g = gpuWindow();
  const Ask a{v, doc_, looks_, ready_, g.win && window() == g.win && !g.broken};
  if (asked_ && *asked_ == a) return;
  asked_ = a;
  pb::Look look = look_;
  look.cell = std::max(1, int(std::lround(8 * v.dpr)));
  const QColor c = pasteboard_.toRgb();
  look.pasteboard = {uint8_t(c.red()), uint8_t(c.green()), uint8_t(c.blue())};
  s_->job.submit([s = s_, a, look, preview = preview_](const pb::CancelToken&) { s->draw(a, look, preview); });
}

// A frame up through the frame job: the scene graph takes it next.
void Canvas::up(const QVariantMap& shown, double ms, bool gpu) {
  frames_++;
  gpu_ = gpu;
  frameMs_ = ms;
  shown_ = shown;
  qCDebug(lcGpu, "frame %d through the %s, %.3f ms", frames_, gpu ? "GPU" : "CPU", ms);
  update();
  emit framed();
}

QSGNode* Canvas::updatePaintNode(QSGNode* old, UpdatePaintNodeData*) {
  auto* root = old ? static_cast<FrameNode*>(old) : new FrameNode;
  CanvasState& s = *s_;
  const std::lock_guard l(s.m);
  if (!old) s.fresh = s.onScreen.tex || !s.image.isNull();  // a new tree: the frame up shows again
  if (s.fresh) {
    s.fresh = false;
    while (QSGNode* n = root->firstChild()) {
      root->removeChildNode(n);
      delete n;
    }
    QSGTexture* tex = s.onScreen.tex ? root->texture(window(), s.onScreen.serial, *s.onScreen.tex, true)
                      : s.image.isNull() ? nullptr
                                         : window()->createTextureFromImage(s.image);
    if (tex) {
      QSGImageNode* n = window()->createImageNode();
      n->setTexture(tex);
      n->setOwnsTexture(!s.onScreen.tex);  // an import stays with the root, for the next time it shows
      n->setFiltering(QSGTexture::Nearest);  // a texel a device pixel
      n->setSourceRect(QRectF(QPointF(0, 0), tex->textureSize()));
      root->appendChildNode(n);
    } else if (s.onScreen.tex) {  // this frame again through the CPU: the import failed, or another window has it
      if (window() == gpuWindow().win) gpuWindow().broken = true;
      asked_.reset();
      QMetaObject::invokeMethod(this, &QQuickItem::polish, Qt::QueuedConnection);
    }
  }
  if (auto* n = static_cast<QSGImageNode*>(root->firstChild())) n->setRect(boundingRect());
  std::vector<uint64_t> keep{s.onScreen.serial};
  for (const Frame& f : s.pool) keep.push_back(f.serial);
  root->keep(keep);
  return root;
}
