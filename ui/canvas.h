#pragma once
// The document's canvas (V1 of docs/p0-tasks.md): frames of the item's device pixels handed to Qt Quick as a texture.
#include <QImage>
#include <QPointF>
#include <QQuickItem>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "render.h"

namespace pb::gpu {
struct Texture;
}

// Frames drawn on the GPU and handed over as a texture of Qt Quick's own device, when Qt Quick runs on Vulkan on the
// frames' GPU (with PB_CANVAS set, canvas.cpp); else through a QImage: offscreen, CI, PB_GPU_FRAMES=0, an import that
// failed. A frame is packed RGBA8 rows (R in the low byte), premultiplied, View::pixels() device pixels; V1 draws the
// test card of render.h. In QML DocCanvas: QtQuick has a Canvas, which the module's QML uses.
class Canvas : public QQuickItem {
  Q_OBJECT
  QML_NAMED_ELEMENT(DocCanvas)
  // Where it shows the document: { x, y, zoom, angle } as View.js's view (Viewport.view); the frame is the item, its
  // dpr the window's.
  Q_PROPERTY(QVariantMap view READ view WRITE setView NOTIFY viewChanged)
  Q_PROPERTY(int frames READ frames NOTIFY framed)        // drawn so far: none while nothing changes
  Q_PROPERTY(bool gpu READ gpu NOTIFY framed)             // the last frame went through the GPU
  Q_PROPERTY(double frameMs READ frameMs NOTIFY framed)   // the last frame's drawing, until the GPU was done with it

 public:
  explicit Canvas(QQuickItem* parent = nullptr);
  ~Canvas() override;
  QVariantMap view() const { return viewMap_; }
  void setView(const QVariantMap&);
  int frames() const { return frames_; }
  bool gpu() const { return gpu_; }
  double frameMs() const { return frameMs_; }
  // View.js's toCanvas(), toDoc() here: the item's logical pixels to document pixels and back.
  Q_INVOKABLE QPointF toCanvas(QPointF doc) const;
  Q_INVOKABLE QPointF toDoc(QPointF canvas) const;
  pb::View current() const;  // view at the item's size and the window's dpr

 signals:
  void viewChanged();
  void framed();

 protected:
  void geometryChange(const QRectF& now, const QRectF& old) override;
  void itemChange(ItemChange, const ItemChangeData&) override;
  void updatePolish() override;
  QSGNode* updatePaintNode(QSGNode*, UpdatePaintNodeData*) override;

 private:
  struct Frame {  // a texture of the frames' device
    std::shared_ptr<pb::gpu::Texture> tex;
    uint64_t serial = 0;  // its import's key in the paint node
    uint64_t freeAt = 0;  // the window's frame from which none in flight samples it
  };
  bool drawGpu(const pb::View&);  // false where frames cannot go through the GPU
  void drawCpu(const pb::View&);

  QVariantMap viewMap_;
  pb::View view_;   // x, y, zoom, angle of viewMap_
  pb::View drawn_;  // of the frame up; width 0: none
  QImage image_;    // the frame, through the CPU
  Frame shown_;     // the frame, through the GPU
  std::vector<Frame> pool_;  // off screen, for the next frames of their size
  uint64_t serials_ = 0;
  bool fresh_ = false;  // a frame the scene graph has not taken yet
  int frames_ = 0;
  bool gpu_ = false;
  double frameMs_ = 0;
};

// Which canvas Viewport shows (V1): DocCanvas with its test card when PB_CANVAS=pattern, else MockCanvas.
class CanvasEnv : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON
  Q_PROPERTY(bool pattern READ pattern CONSTANT)
  Q_PROPERTY(bool gpuFrames READ gpuFrames CONSTANT)  // PB_GPU_FRAMES is not 0: frames may go through the GPU

 public:
  using QObject::QObject;
  bool pattern() const { return qgetenv("PB_CANVAS") == "pattern"; }
  bool gpuFrames() const { return qgetenv("PB_GPU_FRAMES") != "0"; }
};
