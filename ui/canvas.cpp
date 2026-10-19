// copied from snapone ac04c50 (ui/rawframe.cpp: frames on the GPU: GpuWindow, acquire(), FrameNode, the two
// Core::useGpu() as useGpu() and useWindow(), RawFrame's texture pool), without its tiles, stand-ins and renders.
#include "canvas.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QMouseEvent>
#include <QPlatformSurfaceEvent>
#include <QPointer>
#include <QPointingDevice>
#include <QQuickGraphicsConfiguration>
#include <QQuickGraphicsDevice>
#include <QQuickWindow>
#include <QSGImageNode>
#include <QSGTransformNode>
#include <QScreen>
#include <QTimer>
#include <QTabletEvent>
#include <QUrl>
#include <QVulkanFunctions>
#include <QVulkanInstance>
#include <QtQuick/qsgtexture_platform.h>
// QPA, for pen() and play(): input handed over as a platform does. Its headers are QtGui's private ones (no
// Qt6::GuiPrivate here), this one public-only inside. ponytail: the path names Qt's version; bump it with Qt.
#include <QtGui/6.11.3/QtGui/qpa/qwindowsysteminterface.h>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <vulkan/vulkan_win32.h>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <mutex>
#include <thread>
#include <utility>

#include "frame.h"
#include "gpu.h"
#include "history.h"
#include "jobs.h"
#include "open.h"
#include "pattern.spv.h"
#include "pbd.h"
#include "profile.h"
#include "strokelog.h"

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
      imported++;
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

 public:
  int imported = 0;  // imports so far
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
bool same(const pb::View::Pixels& a, const pb::View::Pixels& b) {
  return a.width == b.width && a.height == b.height && a.origin == b.origin && a.across == b.across && a.down == b.down;
}

double now() {  // seconds on std::chrono::steady_clock, as PenEvent::t
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

QVariantList list(const std::vector<double>& v) { return QVariantList(v.begin(), v.end()); }

ulong msNow() {  // pen() and play()'s timestamps
  return ulong(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
                   .count());
}

double percentile(std::vector<double> v, double q) {  // as pb-bench's; NaN for none
  if (v.empty()) return std::numeric_limits<double>::quiet_NaN();
  std::sort(v.begin(), v.end());
  return v[std::min(v.size() - 1, size_t(q * double(v.size())))];
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
QVariantMap shownOf(const pb::Presenter::Frame& f, const pb::View& v, bool preview) {
  return {{"x", v.x},           {"y", v.y},           {"zoom", v.zoom},           {"angle", v.angle},
          {"tiles", f.tiles},   {"coarser", f.coarser}, {"missing", f.missing}, {"built", f.built},
          {"pending", f.pending}, {"preview", preview}};
}

// ---- input handed over as a platform does: pen(), play() ----
// The pen they use: a stylus Qt knows. UI thread first.
const QPointingDevice* stylus() {
  static const QPointingDevice* d = [] {
    auto* p = new QPointingDevice(QStringLiteral("pb test pen"), 0x7062, QInputDevice::DeviceType::Stylus,
                                  QPointingDevice::PointerType::Pen,
                                  QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 1);
    QWindowSystemInterface::registerInputDevice(p);
    return p;
  }();
  return d;
}
// A window's logical point as its platform has it. ponytail: logical x dpr, as on Windows, X11 and offscreen, where
// the platform's own ratio is 1; macOS's and Wayland's would take QHighDpiScaling's factor.
QPointF native(const QWindow* w, QPointF p) { return p * w->devicePixelRatio(); }

// Until t: asleep while more than 2 ms off, then yielding (pb-bench frame's). Windows sleeps 15.6 ms at a time: there
// it yields all along.
void waitUntil(std::chrono::steady_clock::time_point t) {
#ifndef _WIN32
  while (t - std::chrono::steady_clock::now() > std::chrono::milliseconds(2))
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif
  while (std::chrono::steady_clock::now() < t) std::this_thread::yield();
}

}  // namespace

// What a canvas shares with its frame jobs and the scene graph (the render thread, during sync): the frame up and the
// textures off screen, under m. A document's frames come from the canvas's Painting (frame.h), whose hooks are here:
// target(), shown() (the canvas ends the Painting before this can go); V1's test card's and a .pbd's preview from
// jobs that hold it, so it outlives the canvas until they are done.
struct CanvasState {
  std::mutex m;
  Canvas* canvas = nullptr;  // null once it goes; told under m
  Frame onScreen;            // the frame up through the GPU: its texture
  std::vector<Frame> pool;   // off screen, for the next frames of their size
  uint64_t drawing = 0;      // the serial of the pool's texture a frame draws into: its import stays too
  QImage image;              // the frame up through the CPU (a preview on either route)
  bool fresh = false;        // a frame the scene graph has not taken yet
  std::vector<pb::View> asked;   // the Painting's frames' views asked for, oldest first: target() finds its own by pixels
  double upAt = 0, took = 0;     // when the frame up, the one the window took last, was submitted (s); 0: none
  std::vector<double> toScreen;  // ms from a document frame's submit to the window's swap with it, this stroke on
  int imports = 0;               // the window's imports of its textures (stats())
  // The jobs' alone, after m: they go first, and their on_ready() takes m.
  std::atomic<uint64_t> serials{0};  // textures made (stats() reads it)
  std::unique_ptr<pb::Presenter> gpu, cpu;  // gpu: under GpuWindow::lock
  std::atomic<bool> warmed{false};          // warm() is through: gpu made, or broken
  pb::Latest job{pb::Priority::Interactive};
  struct Drawing {  // the Painting's frame from its target() to its shown(): one at a time
    Frame f;
    pb::gpu::BufferPtr out;
    QImage image;
    pb::View view;
    QElapsedTimer clock;
    bool open = false;  // target() gave it and shown() did not come: it threw in between
  } cur;

  ~CanvasState() {
    const std::lock_guard l(gpuWindow().lock);
    gpu.reset();
  }

  // f(canvas) on the UI thread while the canvas lives. Not under m.
  void post(std::function<void(Canvas*)> f) {
    const std::lock_guard l(m);
    if (canvas) QMetaObject::invokeMethod(canvas, [c = canvas, f = std::move(f)] { f(c); }, Qt::QueuedConnection);
  }
  // The presenters' on_ready(), on a pool thread: tiles a frame lacked came, so another frame.
  std::function<void()> ready() {
    return [this] { post([](Canvas* c) { c->ready_++, c->polish(); }); };
  }
  // The GPU presenter before the first document comes: its kernels compile 1.7 s the first time on a machine (B3).
  void warm() {
    {
      const std::lock_guard l(gpuWindow().lock);
      try {
        if (!gpu) gpu = std::make_unique<pb::Presenter>(gpuWindow().dev.get(), ready());
      } catch (const std::exception& e) {  // out of device memory
        broke(e.what());
      }
    }
    warmed = true;
    post([](Canvas* c) { c->polish(); });
  }
  void broke(const char* why) {  // frames through the CPU from now on
    qWarning("pb: %s; frames come through the CPU", why);
    gpuWindow().broken = true;
    post([](Canvas* c) { c->polish(); });
  }

  // A texture of px's size no frame in flight samples, else a new one; none if the device makes none. Under
  // GpuWindow::lock.
  Frame texture(const pb::View::Pixels& px) {
    GpuWindow& g = gpuWindow();
    const std::array<int, 2> size{px.width, px.height};
    Frame f;
    {
      // A texture no frame in flight samples (freeAt): the window's GPU may read one for two frames after it left
      // the screen, and writing it then shows a mix of two frames, a race no test catches.
      const std::lock_guard sl(m);
      const auto free = std::find_if(pool.begin(), pool.end(), [&](const Frame& p) {
        return pb::gpu::size(*p.tex) == size && p.freeAt <= g.frames;
      });
      if (free != pool.end()) f = *free, pool.erase(free);
      drawing = f.serial;
    }
    if (!f.tex && (f.tex = g.dev->texture(px.width, px.height))) f.serial = ++serials;
    return f;
  }
  // `out` into f's texture, handed over to the window, as RawFrame's renders (snapone's lease()). Under GpuWindow::lock.
  static void hand(const Frame& f, pb::gpu::Buffer& out, const pb::View::Pixels& px) {
    GpuWindow& g = gpuWindow();
    // ponytail: copy buffer→texture ~1.2 ms at 1080p on Iris Xe; write the shared image as a storage image (HAL)
    // when the stroke latency budget needs it
    g.dev->copy(out, *f.tex, 0, 0, px.width, px.height);
    g.dev->release(*f.tex);
    g.dev->flush();
  }

  // The job: V1's test card, through the GPU when asked and it can, else the CPU.
  void drawCard(const pb::View& v, bool gpu) {
    PROFILE_ZONE("canvas_frame");
    QElapsedTimer clock;
    clock.start();
    const pb::View::Pixels px = v.pixels();
    const pb::PatternConsts k = pb::pattern_consts(v);
    if (gpu) {
      GpuWindow& g = gpuWindow();
      const std::lock_guard l(g.lock);
      try {
        const Frame f = g.broken ? Frame{} : texture(px);
        if (f.tex) {
          const pb::gpu::BufferPtr out = g.dev->buffer(size_t(px.width) * px.height * 4);
          g.dev->dispatch(*g.pattern, {out.get()}, &k, sizeof k, uint32_t(px.width + 7) / 8, uint32_t(px.height + 7) / 8);
          hand(f, *out, px);
          return up(f, {}, shownOf({}, v, false), clock, true);
        }
      } catch (const std::exception& e) {  // out of device memory
        broke(e.what());
      }
    }
    QImage im(px.width, px.height, QImage::Format_RGBA8888_Premultiplied);
    uchar* bits = im.bits();
    const qsizetype stride = im.bytesPerLine();
    pb::parallel_for(px.height, [&](int begin, int end) {
      for (int j = begin; j < end; j++) {
        auto* row = reinterpret_cast<uint32_t*>(bits + j * stride);
        for (int i = 0; i < px.width; i++) row[i] = pb::pattern_pixel(k, i, j);
      }
    });
    up({}, std::move(im), shownOf({}, v, false), clock, false);
  }

  // The Painting's Hooks::target: a buffer for a texture (GPU, under GpuWindow::lock: Hooks::device) or a new image,
  // for the frame of the view asked for whose pixels these are.
  pb::Target target(const pb::View::Pixels& px, bool gpu) {
    PROFILE_ZONE("canvas_target");
    if (cur.open && gpu) broke("a frame failed on the GPU");  // out of device memory (V2b)
    cur = {};
    cur.clock.start();
    {
      const std::lock_guard l(m);
      const auto it = std::find_if(asked.rbegin(), asked.rend(), [&](const pb::View& v) { return same(v.pixels(), px); });
      if (it != asked.rend()) {
        cur.view = *it;
        asked.erase(asked.begin(), it.base() - 1);  // frames are of the newest view at their start: none of those
      }
    }
    pb::Target t;
    if (gpu) {
      if (gpuWindow().broken) throw std::runtime_error("frames come through the CPU");
      try {
        cur.f = texture(px);
        if (!cur.f.tex) throw std::runtime_error("no texture");
        cur.out = gpuWindow().dev->buffer(size_t(px.width) * px.height * 4);
      } catch (const std::exception& e) {
        broke(e.what());
        throw;
      }
      t.buffer = cur.out.get();
    } else {
      cur.image = QImage(px.width, px.height, QImage::Format_RGBA8888_Premultiplied);
      t.pixels = reinterpret_cast<uint32_t*>(cur.image.bits());  // rows packed: bytesPerLine() is width x 4
    }
    cur.open = true;
    return t;
  }
  // The Painting's Hooks::shown: Presenter::frame() returned (on the GPU: submitted); the frame into its texture, up.
  void shown(const pb::Presenter::Frame& pf, bool gpu) {
    PROFILE_ZONE("canvas_shown");
    const double at = now();
    cur.open = false;
    if (!gpu) return up({}, std::move(cur.image), shownOf(pf, cur.view, false), cur.clock, false, at);
    try {
      hand(cur.f, *cur.out, cur.view.pixels());
    } catch (const std::exception& e) {
      return broke(e.what());
    }
    cur.out.reset();
    up(cur.f, {}, shownOf(pf, cur.view, false), cur.clock, true, at);
  }

  // The frame up, a texture (f.tex) or an image, then the canvas told; `submitted`: a document frame's submit.
  void up(const Frame& f, QImage im, const QVariantMap& shown, const QElapsedTimer& clock, bool gpu,
          double submitted = 0) {
    const double ms = clock.nsecsElapsed() / 1e6;
    const std::lock_guard l(m);
    if (f.tex) {
      if (onScreen.tex) {
        // One the window never took (fresh: frames come faster than it syncs) has no frame in flight: free now, so
        // the stroke's frames reuse textures instead of making (and the window importing) one a frame.
        onScreen.freeAt = fresh ? 0 : offScreenFrom();
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
    upAt = submitted;
    if (canvas)
      QMetaObject::invokeMethod(canvas, [c = canvas, shown, ms, gpu] { c->up(shown, ms, gpu); }, Qt::QueuedConnection);
  }
  // The window swapped (render thread): with the frame it took last, if a document's.
  void swapped() {
    const std::lock_guard l(m);
    if (took > 0) toScreen.push_back((now() - took) * 1e3);
    took = 0;
  }
};

// play()'s thread and what it handed over.
struct Canvas::Player {
  std::thread thread;
  std::atomic<bool> stop{false}, done{false};
  std::vector<double> sent;  // each event's, as PenEvent::t; the thread's until done
  ~Player() {
    stop = true;
    if (thread.joinable()) thread.join();
  }
};

bool Canvas::Ask::operator==(const Ask& o) const {
  return same(view, o.view) && doc == o.doc && look == o.look && ready == o.ready && gpu == o.gpu;
}

Canvas::Canvas(QQuickItem* parent) : QQuickItem(parent), s_(std::make_shared<CanvasState>()) {
  s_->canvas = this;
  setFlag(ItemHasContents);
  // Every point of the mouse and the pen for strokes: Qt merges none (on Windows it then also hands over the pen's
  // frames that WM_POINTER put in one message). Here, as the app is up: its platform has set its own by then.
  QCoreApplication::setAttribute(Qt::AA_CompressHighFrequencyEvents, false);
  QCoreApplication::setAttribute(Qt::AA_CompressTabletEvents, false);
}

Canvas::~Canvas() {
  player_.reset();
  painting_.reset();  // first: it waits for its frame, whose hooks use s_; never under GpuWindow::lock (api/c3a.md)
  history_.reset();
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
    s->post([open, doc, preview, error](Canvas* c) {
      if (open != c->opens_) return;  // another source since
      c->error_ = error;
      c->setDocument(doc, preview);
    });
  });
}

void Canvas::setDocument(pb::DocPtr doc, std::shared_ptr<const pb::Preview> preview) {
  painting_.reset();  // before the history it paints into
  stroking_ = false;
  history_ = doc ? std::make_unique<pb::Document>(std::move(doc), "Open") : nullptr;
  preview_ = std::move(preview);
  reference_.clear();
  accepts();
  polish();
  emit documentChanged();
}

pb::DocPtr Canvas::document() const { return history_ ? history_->state() : nullptr; }

QSize Canvas::docSize() const {
  const pb::DocPtr d = document();
  return d ? QSize(d->width, d->height) : QSize(0, 0);
}

void Canvas::setPasteboard(const QColor& c) {
  if (c == pasteboard_) return;
  pasteboard_ = c;
  looks_++;
  polish();
  emit pasteboardChanged();
}

void Canvas::setTool(const QString& t) {
  if (t == tool_) return;
  tool_ = t;
  accepts();
  emit toolChanged();
}

void Canvas::setBrush(const QVariantMap& m) {
  if (m == brushMap_) return;
  brushMap_ = m;
  pb::BrushParams b;  // the core clamps what is out of range
  const std::pair<const char*, float*> numbers[] = {
      {"size", &b.size}, {"hardness", &b.hardness}, {"opacity", &b.opacity}, {"flow", &b.flow}, {"spacing", &b.spacing}};
  for (const auto& [key, to] : numbers) {
    bool ok = false;
    const double d = m.value(QLatin1String(key)).toDouble(&ok);
    if (ok && std::isfinite(d)) *to = float(d);
  }
  const std::pair<const char*, bool*> flags[] = {
      {"pressureSize", &b.pressure_size}, {"pressureOpacity", &b.pressure_opacity}, {"pressureFlow", &b.pressure_flow}};
  for (const auto& [key, to] : flags)
    if (m.contains(QLatin1String(key))) *to = m.value(QLatin1String(key)).toBool();
  const QColor c = m.value(QStringLiteral("color")).value<QColor>();
  if (c.isValid()) b.color = {float(c.redF()), float(c.greenF()), float(c.blueF())};
  brush_ = b;
  emit brushChanged();
}

// The left button paints while the tool does and there is a document to paint on, and until pen up once it began.
void Canvas::accepts() {
  const bool paints = history_ && (stroking_ || tool_ == u"brush" || tool_ == u"eraser");
  setAcceptedMouseButtons(paints ? Qt::LeftButton : Qt::NoButton);
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
  const pb::DocPtr doc = document();
  if (doc && level >= 0 && level < 32 && x >= 0 && y >= 0) {
    const std::array<int, 3> at{level, x >> pb::kTileLog2, y >> pb::kTileLog2};
    auto it = reference_.find(at);
    if (it == reference_.end())
      it = reference_.emplace(at, pb::composite_tile(pb::RenderInput{doc, {}}, level, at[1], at[2])).first;
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
  const pb::DocPtr doc = document();
  if (!doc) return QStringLiteral("no document");
  try {
    pb::save_pbd(*doc, local(path));
  } catch (const std::exception& e) {
    return QString::fromUtf8(e.what());
  }
  return {};
}

// ---- strokes ----
// The document's Painting on that route (one presenter each: the GPU's once warm() made it), made again on another.
pb::Painting* Canvas::painting(bool gpu) {
  if (painting_ && paintingGpu_ == gpu) return painting_.get();
  if (gpu && !s_->warmed) return nullptr;  // warm() polishes once it is through
  settle();
  painting_.reset();
  if (gpu && !s_->gpu) return nullptr;  // warm() failed: the CPU next
  if (!gpu && !s_->cpu) s_->cpu = std::make_unique<pb::Presenter>(nullptr, s_->ready());
  CanvasState* s = s_.get();  // outlives the Painting: the canvas ends it first
  pb::Painting::Hooks h;
  h.target = [s, gpu](const pb::View::Pixels& px) { return s->target(px, gpu); };
  h.shown = [s, gpu](const pb::Presenter::Frame& f, const pb::Target&) { s->shown(f, gpu); };
  h.merged = [s] { s->post([](Canvas* c) { c->commit(); }); };
  h.device = gpu ? &gpuWindow().lock : nullptr;  // the CPU's frames keep off the device
  s->cur.open = false;
  painting_ = std::make_unique<pb::Painting>(*history_, gpu ? *s->gpu : *s->cpu, std::move(h));
  paintingGpu_ = gpu;
  return painting_.get();
}

// A mouse event as a pen event: its point in document pixels, the pen's pressure and tilt when Qt made it of the
// tablet event its window just had (a mouse's: 1, none), its time now.
pb::PenEvent Canvas::penEvent(const QMouseEvent* e) const {
  pb::PenEvent p;
  p.t = now();
  const auto d = current().to_doc(e->position().x(), e->position().y());
  p.x = float(d[0]), p.y = float(d[1]);
  if (e->timestamp() == pen_.timestamp && e->device() && e->device()->type() != QInputDevice::DeviceType::Mouse)
    p.pressure = pen_.pressure, p.tilt_x = pen_.tiltX, p.tilt_y = pen_.tiltY, p.rotation = pen_.rotation;
  return p;
}

void Canvas::mousePressEvent(QMouseEvent* e) {
  PROFILE_ZONE("canvas_event");
  const pb::PenEvent p = penEvent(e);
  if (e->button() != Qt::LeftButton || !painting_) return e->ignore();  // no frame of the document yet
  settle();
  const pb::DocState& d = *history_->state();
  pb::BrushParams b = played_.value_or(brush_);
  if (!played_) b.erase = tool_ == u"eraser";
  played_.reset();
  if (d.mode == pb::Mode::Gray)  // the colour's grey, Rec. 601's
    b.color[0] = 0.299f * b.color[0] + 0.587f * b.color[1] + 0.114f * b.color[2];
  try {
    painting_->begin(d.selected.empty() ? 0 : d.selected[0], b);
  } catch (const std::exception& x) {  // no pixel layer selected
    qWarning("pb: %s", x.what());
    return e->ignore();
  }
  stroking_ = true;
  got_.assign(1, p.t);
  {
    const std::lock_guard l(s_->m);
    s_->toScreen.clear();
  }
  painting_->add(p);
}

void Canvas::mouseMoveEvent(QMouseEvent* e) {
  PROFILE_ZONE("canvas_event");
  if (!stroking_) return e->ignore();
  const pb::PenEvent p = penEvent(e);
  got_.push_back(p.t);
  painting_->add(p);
}

void Canvas::mouseReleaseEvent(QMouseEvent* e) {
  if (playing_) playing_->quit();
  if (!stroking_) return e->ignore();
  stroking_ = false;
  painting_->end();  // Qt moved it to the release's point first
  accepts();
}

void Canvas::mouseUngrabEvent() {  // a popup took the mouse, say
  if (playing_) playing_->quit();
  if (!stroking_) return;
  stroking_ = false;
  painting_->end();
  accepts();
}

// The window's tablet events, noted: no Qt Quick item gets one (pointer handlers do), and the mouse event Qt makes of
// one no handler took lacks the pen's pressure and tilt.
bool Canvas::eventFilter(QObject* o, QEvent* e) {
  switch (e->type()) {
    case QEvent::TabletPress:
    case QEvent::TabletMove:
    case QEvent::TabletRelease: {
      const auto* t = static_cast<const QTabletEvent*>(e);
      pen_ = {t->timestamp(), float(t->pressure()), float(t->xTilt()), float(t->yTilt()), float(t->rotation())};
      break;
    }
    case QEvent::MouseButtonPress:
    case QEvent::MouseMove:
    case QEvent::MouseButtonRelease: {
      const auto* m = static_cast<const QMouseEvent*>(e);
      if (m->timestamp() == pen_.timestamp && m->device() && m->device()->type() != QInputDevice::DeviceType::Mouse)
        pen_.moused = true;
      break;
    }
    default:
      break;
  }
  return QQuickItem::eventFilter(o, e);
}

void Canvas::settle() {
  if (!painting_) return;
  stroking_ = false;
  painting_->end();
  commit();
}

void Canvas::commit() {
  if (!painting_) return;
  const pb::DocPtr before = history_->state();
  painting_->commit();
  if (history_->state() == before) return;  // nothing ended, or the stroke failed
  reference_.clear();
  if (asked_) asked_->doc = history_->state();  // commit() asked for its frame
  emit painted(QString::fromStdString(history_->step(history_->current()).name));
}

bool Canvas::undo() {
  if (!history_) return false;
  settle();
  if (!history_->undo()) return false;
  reference_.clear();
  polish();
  return true;
}

bool Canvas::redo() {
  if (!history_) return false;
  settle();
  if (!history_->redo()) return false;
  reference_.clear();
  polish();
  return true;
}

QVariantMap Canvas::stats() const {
  QVariantMap m;
  if (painting_) {
    const pb::Painting::Stats s = painting_->stats();
    m = {{"added", int(s.added)},
         {"applied", int(s.applied)},
         {"shown", int(s.shown)},
         {"p50", percentile(s.latency, 0.5)},
         {"p95", percentile(s.latency, 0.95)},
         {"p99", percentile(s.latency, 0.99)},
         {"max", s.latency.empty() ? 0. : *std::max_element(s.latency.begin(), s.latency.end())},
         {"frames", int(s.frame_ms.size())},
         {"frameP50", percentile(s.frame_ms, 0.5)},
         {"frameP95", percentile(s.frame_ms, 0.95)},
         {"latency", list(s.latency)}};
  }
  {
    const std::lock_guard l(s_->m);
    m[QStringLiteral("screenP50")] = percentile(s_->toScreen, 0.5);
    m[QStringLiteral("screenP95")] = percentile(s_->toScreen, 0.95);
    m[QStringLiteral("screen")] = list(s_->toScreen);
    m[QStringLiteral("textures")] = int(s_->serials);
    m[QStringLiteral("imports")] = s_->imports;
  }
  if (player_ && player_->done) {
    const std::vector<double>& sent = player_->sent;
    m[QStringLiteral("sent")] = int(sent.size());
    if (sent.size() == got_.size()) {
      std::vector<double> qt(sent.size());
      for (size_t i = 0; i < qt.size(); i++) qt[i] = (got_[i] - sent[i]) * 1e3;
      m[QStringLiteral("qtP50")] = percentile(qt, 0.5);
      m[QStringLiteral("qtP95")] = percentile(qt, 0.95);
      m[QStringLiteral("qt")] = list(qt);
    }
  }
  return m;
}

void Canvas::wait() {
  settle();
  ensurePolished();  // the frame of what changed (undo, a view) asked for
  if (painting_) painting_->wait();
  QCoreApplication::sendPostedEvents(this, QEvent::MetaCall);  // its frame's up(): frames, shown, framed()
}

// Asked once, the pen up: a pen's hover handed over; does a mouse event come of it? The platform's Qt does the same
// for every window.
bool Canvas::penMice() {
  static int known = -1;
  if (known < 0) {
    QQuickWindow* w = window();
    pen_.moused = false;
    QWindowSystemInterface::handleTabletEvent(w, msNow(), stylus(), {-1, -1}, w->mapToGlobal(QPointF(-1, -1)),
                                              Qt::NoButton, 0, 0, 0, 0, 0, 0);
    QWindowSystemInterface::flushWindowSystemEvents();
    known = pen_.moused;
  }
  return known;
}

bool Canvas::pen(const QString& phase, QPointF at, qreal pressure) {
  QQuickWindow* w = window();
  if (!w) return false;
  const bool mice = penMice();
  const QPointF l = native(w, mapToScene(at)), g = native(w, w->mapToGlobal(mapToScene(at)));
  const bool up = phase == u"release";
  const ulong t = msNow();
  QWindowSystemInterface::handleTabletEvent(w, t, stylus(), l, g, up ? Qt::NoButton : Qt::LeftButton, pressure, 0, 0, 0,
                                            0, 0);
  if (!mice)
    QWindowSystemInterface::handleMouseEvent(w, t, stylus(), l, g, up ? Qt::NoButton : Qt::LeftButton,
                                             phase == u"move" ? Qt::NoButton : Qt::LeftButton,
                                             up                  ? QEvent::MouseButtonRelease
                                             : phase == u"press" ? QEvent::MouseButtonPress
                                                                 : QEvent::MouseMove,
                                             Qt::NoModifier, Qt::MouseEventSynthesizedByQt);
  QWindowSystemInterface::flushWindowSystemEvents();
  return mice;
}

QVariantMap Canvas::strokeLog(const QString& path) const {
  try {
    const pb::StrokeRecord r = pb::read_stroke_log(local(path).string());
    if (r.events.empty()) return {};
    float x0 = r.events[0].x, x1 = x0, y0 = r.events[0].y, y1 = y0;
    for (const pb::PenEvent& e : r.events)
      x0 = std::min(x0, e.x), x1 = std::max(x1, e.x), y0 = std::min(y0, e.y), y1 = std::max(y1, e.y);
    return {{"events", int(r.events.size())}, {"seconds", r.events.back().t - r.events[0].t},
            {"zoom", r.zoom},                 {"x", r.events[0].x},
            {"y", r.events[0].y},             {"cx", (x0 + x1) / 2},
            {"cy", (y0 + y1) / 2},            {"size", r.brush.size}};
  } catch (const std::exception& e) {
    qWarning("pb: %s", e.what());
    return {};
  }
}

int Canvas::play(const QString& path, const QString& kind, double pace) {
  QQuickWindow* w = window();
  pb::StrokeRecord r;
  try {
    r = pb::read_stroke_log(local(path).string());
  } catch (const std::exception& e) {
    qWarning("pb: %s", e.what());
  }
  if (!w || r.events.empty()) return 0;
  player_.reset();
  played_ = r.brush;
  struct At {
    double t;
    QPointF local, global;
    qreal pressure;
  };
  std::vector<At> at;  // in the window, as the view shows the document now
  for (const pb::PenEvent& e : r.events) {
    const auto c = current().to_canvas(e.x, e.y);
    const QPointF p = mapToScene(QPointF(c[0], c[1]));
    at.push_back({(e.t - r.events[0].t) * pace, native(w, p), native(w, w->mapToGlobal(p)), e.pressure});
  }
  const int timeout = int(at.back().t * 1000) + 10000;  // past the stroke's time: its pen up never came
  const bool tablet = kind == u"pen", mice = tablet && penMice();
  const QPointingDevice* device = stylus();
  player_ = std::make_shared<Player>();
  Player* q = player_.get();  // the thread ends before its Player
  q->thread = std::thread([q, at = std::move(at), tablet, mice, device, w] {
    using namespace std::chrono;
    const auto t0 = steady_clock::now() + milliseconds(20);
    for (size_t i = 0; i <= at.size() && !q->stop; i++) {
      const bool up = i == at.size();  // pen up at the last point
      const At& a = at[up ? i - 1 : i];
      if (!up) waitUntil(t0 + duration_cast<steady_clock::duration>(duration<double>(a.t)));
      if (up) q->done = true;
      const Qt::MouseButtons buttons = up ? Qt::NoButton : Qt::LeftButton;
      const Qt::MouseButton button = i == 0 || up ? Qt::LeftButton : Qt::NoButton;
      const QEvent::Type type = up ? QEvent::MouseButtonRelease : i == 0 ? QEvent::MouseButtonPress : QEvent::MouseMove;
      const ulong t = msNow();
      if (!up) q->sent.push_back(now());
      if (tablet)
        QWindowSystemInterface::handleTabletEvent(w, t, device, a.local, a.global, buttons, up ? 0 : a.pressure, 0, 0, 0,
                                                  0, 0);
      if (!tablet || !mice)  // the left button's, or what Qt makes of the pen's elsewhere (pen())
        QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::AsynchronousDelivery>(
            w, t, tablet ? device : QPointingDevice::primaryPointingDevice(), a.local, a.global, buttons, button, type,
            Qt::NoModifier, tablet ? Qt::MouseEventSynthesizedByQt : Qt::MouseEventNotSynthesized);
    }
  });
  QEventLoop loop;  // until pen up
  playing_ = &loop;
  QTimer::singleShot(timeout, &loop, &QEventLoop::quit);
  loop.exec();
  playing_ = nullptr;
  q->thread.join();
  return int(r.events.size());
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
    disconnect(swapped_);
    if (filtered_) filtered_->removeEventFilter(this);
    filtered_ = d.window;
    if (d.window) {
      d.window->installEventFilter(this);
      screen_ = connect(d.window, &QWindow::screenChanged, this, &Canvas::readScreen);
      swapped_ = connect(d.window, &QQuickWindow::frameSwapped, this, [s = s_] { s->swapped(); }, Qt::DirectConnection);
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

// A frame when what it shows changed since the last one asked for, nothing otherwise: a document's from its Painting
// (a .pbd's preview first, on a job of its own), V1's test card from the canvas's job.
void Canvas::updatePolish() {
  const pb::View v = current();
  const pb::DocPtr doc = document();
  if (!window() || !isVisible() || v.pixels().width == 0 || (!doc && !source_.isEmpty()) || previewing_) return;
  const GpuWindow& g = gpuWindow();
  const Ask a{v, doc, looks_, ready_, g.win && window() == g.win && !g.broken};
  if (asked_ && *asked_ == a) return;
  pb::Look look = look_;
  look.cell = std::max(1, int(std::lround(8 * v.dpr)));
  const QColor c = pasteboard_.toRgb();
  look.pasteboard = {uint8_t(c.red()), uint8_t(c.green()), uint8_t(c.blue())};
  if (doc && preview_) {  // up first; the document's frames once it is
    previewing_ = true;
    pb::submit(pb::Priority::Interactive, [s = s_, v, doc, look, p = std::move(preview_)] {
      QElapsedTimer clock;
      clock.start();
      s->up({}, previewFrame(*p, v, *doc, look), shownOf({}, v, true), clock, false);
      s->post([](Canvas* c) { c->previewing_ = false, c->polish(); });
    });
    return;
  }
  if (!doc) {
    asked_ = a;
    s_->job.submit([s = s_, v, gpu = a.gpu](const pb::CancelToken&) { s->drawCard(v, gpu); });
    return;
  }
  pb::Painting* p = painting(a.gpu);
  if (!p) return;
  asked_ = a;
  {
    const std::lock_guard l(s_->m);
    // ponytail: 64 views at most, oldest out: frames come about as fast as they are asked for
    if (s_->asked.size() >= 64) s_->asked.erase(s_->asked.begin());
    s_->asked.push_back(v);
  }
  p->frame(v, look);
}

// A frame up through a frame job: the scene graph takes it next.
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
    s.took = s.upAt;
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
  s.imports = root->imported;
  std::vector<uint64_t> keep{s.onScreen.serial, s.drawing};
  for (const Frame& f : s.pool) keep.push_back(f.serial);
  root->keep(keep);
  return root;
}
