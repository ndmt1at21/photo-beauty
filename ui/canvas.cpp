// copied from snapone ac04c50 (ui/rawframe.cpp: frames on the GPU: GpuWindow, acquire(), FrameNode, the two
// Core::useGpu() as useGpu() and useWindow(), RawFrame's texture pool), without its tiles, stand-ins and renders.
#include "canvas.h"

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QPlatformSurfaceEvent>
#include <QPointer>
#include <QQuickGraphicsConfiguration>
#include <QQuickGraphicsDevice>
#include <QQuickWindow>
#include <QSGImageNode>
#include <QSGTransformNode>
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
#include <map>
#include <utility>

#include "gpu.h"
#include "jobs.h"
#include "pattern.spv.h"
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

}  // namespace

Canvas::Canvas(QQuickItem* parent) : QQuickItem(parent) { setFlag(ItemHasContents); }
Canvas::~Canvas() = default;

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

void Canvas::geometryChange(const QRectF& now, const QRectF& old) {
  QQuickItem::geometryChange(now, old);
  polish();
  update();  // the frame's rect, until the next frame
}

void Canvas::itemChange(ItemChange c, const ItemChangeData& d) {
  QQuickItem::itemChange(c, d);
  if (c == ItemSceneChange) drawn_ = {};  // another window: its own textures
  if (c == ItemSceneChange || c == ItemVisibleHasChanged || c == ItemDevicePixelRatioHasChanged) polish();
}

// A frame when what it shows changed since the last one; nothing otherwise.
void Canvas::updatePolish() {
  const pb::View v = current();
  if (!window() || !isVisible() || v.pixels().width == 0 || (drawn_.width > 0 && same(v, drawn_))) return;
  PROFILE_ZONE("canvas_frame");
  QElapsedTimer clock;
  clock.start();
  gpu_ = drawGpu(v);
  if (!gpu_) drawCpu(v);
  frameMs_ = clock.nsecsElapsed() / 1e6;
  drawn_ = v;
  fresh_ = true;
  frames_++;
  qCDebug(lcGpu, "frame %d: %d x %d through the %s, %.3f ms", frames_, v.pixels().width, v.pixels().height,
          gpu_ ? "GPU" : "CPU", frameMs_);
  update();
  emit framed();
}

// Into a texture the window imports, as RawFrame's renders (snapone's lease()): one free of the frames in flight.
bool Canvas::drawGpu(const pb::View& v) {
  GpuWindow& g = gpuWindow();
  if (!g.win || window() != g.win || g.broken) return false;
  const pb::View::Pixels px = v.pixels();
  const std::array<int, 2> size{px.width, px.height};
  Frame f;
  try {
    // A texture no frame in flight samples (freeAt): the window's GPU may read one for two frames after it left the
    // screen, and writing it then shows a mix of two frames, a race no test catches.
    const auto free = std::find_if(pool_.begin(), pool_.end(), [&](const Frame& p) {
      return pb::gpu::size(*p.tex) == size && p.freeAt <= g.frames;
    });
    if (free != pool_.end()) {
      f = *free;
      pool_.erase(free);
    } else if (!(f.tex = g.dev->texture(px.width, px.height))) {
      return false;
    } else {
      f.serial = ++serials_;
    }
    const pb::PatternConsts k = pb::pattern_consts(v);
    const pb::gpu::BufferPtr out = g.dev->buffer(size_t(px.width) * px.height * 4);
    g.dev->dispatch(*g.pattern, {out.get()}, &k, sizeof k, uint32_t(px.width + 7) / 8, uint32_t(px.height + 7) / 8);
    // ponytail: copy buffer→texture ~1.2 ms at 1080p on Iris Xe; write the shared image as a storage image (HAL) when
    // the stroke latency budget needs it
    g.dev->copy(*out, *f.tex, 0, 0, px.width, px.height);
    g.dev->release(*f.tex);
    g.dev->flush();
  } catch (const std::exception& e) {  // out of device memory
    qWarning("pb: %s; frames come through the CPU", e.what());
    g.broken = true;
    return false;
  }
  if (shown_.tex) {
    shown_.freeAt = offScreenFrom();
    pool_.push_back(shown_);
  }
  // ponytail: 4 textures off screen at most, oldest out; a byte budget if frames ever come in many sizes at once
  if (pool_.size() > 4) pool_.erase(pool_.begin());
  shown_ = f;
  image_ = {};
  return true;
}

void Canvas::drawCpu(const pb::View& v) {
  const pb::View::Pixels px = v.pixels();
  QImage im(px.width, px.height, QImage::Format_RGBA8888_Premultiplied);  // a new one: the last may be uploading
  uchar* bits = im.bits();
  const qsizetype stride = im.bytesPerLine();
  const pb::PatternConsts k = pb::pattern_consts(v);
  pb::parallel_for(px.height, [&](int begin, int end) {
    for (int j = begin; j < end; j++) {
      auto* row = reinterpret_cast<uint32_t*>(bits + j * stride);
      for (int i = 0; i < px.width; i++) row[i] = pb::pattern_pixel(k, i, j);
    }
  });
  image_ = std::move(im);
  shown_ = {};
  pool_.clear();
}

QSGNode* Canvas::updatePaintNode(QSGNode* old, UpdatePaintNodeData*) {
  auto* root = old ? static_cast<FrameNode*>(old) : new FrameNode;
  if (!old) fresh_ = drawn_.width > 0;  // a new tree: the frame up shows again
  if (fresh_) {
    fresh_ = false;
    while (QSGNode* n = root->firstChild()) {
      root->removeChildNode(n);
      delete n;
    }
    QSGTexture* tex = shown_.tex ? root->texture(window(), shown_.serial, *shown_.tex, true)
                                 : window()->createTextureFromImage(image_);
    if (tex) {
      QSGImageNode* n = window()->createImageNode();
      n->setTexture(tex);
      n->setOwnsTexture(!shown_.tex);  // an import stays with the root, for the next time it shows
      n->setFiltering(QSGTexture::Nearest);  // a texel a device pixel
      n->setSourceRect(QRectF(QPointF(0, 0), tex->textureSize()));
      root->appendChildNode(n);
    } else {  // the import failed: this frame again, through the CPU
      gpuWindow().broken = true;
      drawn_ = {};
      QMetaObject::invokeMethod(this, &QQuickItem::polish, Qt::QueuedConnection);
    }
  }
  if (auto* n = static_cast<QSGImageNode*>(root->firstChild())) n->setRect(boundingRect());
  std::vector<uint64_t> keep{shown_.serial};
  for (const Frame& f : pool_) keep.push_back(f.serial);
  root->keep(keep);
  return root;
}
