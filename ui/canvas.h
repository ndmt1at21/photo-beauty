#pragma once
// The document's canvas (V1, V2b of docs/p0-tasks.md): frames of the item's device pixels, made off the UI thread by a
// frame job (the Presenter of present.h; V1's test card without a document) and handed to Qt Quick as a texture.
#include <QColor>
#include <QImage>
#include <QPointF>
#include <QQuickItem>
#include <QSize>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>

#include "present.h"
#include "render.h"

namespace pb {
struct Preview;
}
struct CanvasState;  // what a canvas shares with its frame job and the scene graph (canvas.cpp)

// Frames drawn on the GPU and handed over as a texture of Qt Quick's own device, when Qt Quick runs on Vulkan on the
// frames' GPU (with PB_CANVAS set, canvas.cpp); else through a QImage: offscreen, CI, PB_GPU_FRAMES=0, an import that
// failed. A frame is packed RGBA8 rows (R in the low byte), premultiplied, View::pixels() device pixels: the document
// as the Presenter shows it (its tiles at the view's mip level, coarser ones while those come, the screen's colours),
// a .pbd's preview until its tiles composite, or V1's test card (render.h) when there is no document and no source.
// A frame when what it shows changes: the view, the size, the document, the look, tiles the last frame lacked having
// come; none otherwise. In QML DocCanvas: QtQuick has a Canvas, which the module's QML uses.
class Canvas : public QQuickItem {
  Q_OBJECT
  QML_NAMED_ELEMENT(DocCanvas)
  // Where it shows the document: { x, y, zoom, angle } as View.js's view (Viewport.view); the frame is the item, its
  // dpr the window's.
  Q_PROPERTY(QVariantMap view READ view WRITE setView NOTIFY viewChanged)
  // A file it opens in a Background job and shows: .pbd (open_pbd(); its preview up until the tiles composite), JPEG,
  // PNG, TIFF (open_image()); a path or a file: URL. What is up stays while it opens. Viewport's is PB_CANVAS=<path>.
  // ponytail: a flag's way in, until N1's Session hands documents over with setDocument().
  Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
  Q_PROPERTY(QString error READ error NOTIFY documentChanged)    // why source did not open; "" when it did
  Q_PROPERTY(QSize docSize READ docSize NOTIFY documentChanged)  // the document's, pixels; 0 x 0 without one
  Q_PROPERTY(QColor pasteboard READ pasteboard WRITE setPasteboard NOTIFY pasteboardChanged)  // past the canvas
  Q_PROPERTY(int frames READ frames NOTIFY framed)       // up so far: none while nothing changes
  Q_PROPERTY(bool gpu READ gpu NOTIFY framed)            // the last frame went through the GPU
  Q_PROPERTY(double frameMs READ frameMs NOTIFY framed)  // the last frame's drawing, until the GPU was done with it
  // The last frame: the view it shows (x, y, zoom, angle), { tiles, coarser, missing, built, pending } as
  // Presenter::Frame (pending: tiles on their way, a sharper frame follows), preview (the .pbd's).
  Q_PROPERTY(QVariantMap shown READ shown NOTIFY framed)

 public:
  explicit Canvas(QQuickItem* parent = nullptr);
  ~Canvas() override;
  QVariantMap view() const { return viewMap_; }
  void setView(const QVariantMap&);
  QString source() const { return source_; }
  void setSource(const QString&);
  QString error() const { return error_; }
  QSize docSize() const;
  QColor pasteboard() const { return pasteboard_; }
  void setPasteboard(const QColor&);
  int frames() const { return frames_; }
  bool gpu() const { return gpu_; }
  double frameMs() const { return frameMs_; }
  QVariantMap shown() const { return shown_; }

  // What it shows (UI thread): a document state, a snapshot, and its .pbd's preview if any; null: V1's test card, or
  // nothing new while source opens. N1 hands documents over here; C3 adds the stroke.
  void setDocument(pb::DocPtr, std::shared_ptr<const pb::Preview> = {});
  pb::DocPtr document() const { return doc_; }

  // View.js's toCanvas(), toDoc() here: the item's logical pixels to document pixels and back.
  Q_INVOKABLE QPointF toCanvas(QPointF doc) const;
  Q_INVOKABLE QPointF toDoc(QPointF canvas) const;
  pb::View current() const;  // view at the item's size and the window's dpr
  // For tests: the composite's pixel (x, y) at mip level `level`, [R, G, B, A] 0..65535 as composite_tile() makes it on
  // the CPU (render.h), zeros past the canvas; what frames are checked against. Slow (tiles cached while the document
  // stays).
  Q_INVOKABLE QVariantList composite(int level, int x, int y) const;
  // For tests: the document saved as a .pbd at `path` (save_pbd()), here and now; "" or why not. N2's Session saves.
  Q_INVOKABLE QString save(const QString& path) const;

 signals:
  void viewChanged();
  void sourceChanged();
  void documentChanged();
  void pasteboardChanged();
  void framed();

 protected:
  void geometryChange(const QRectF& now, const QRectF& old) override;
  void itemChange(ItemChange, const ItemChangeData&) override;
  void updatePolish() override;
  QSGNode* updatePaintNode(QSGNode*, UpdatePaintNodeData*) override;

 private:
  friend struct CanvasState;
  struct Ask {  // what a frame shows: another when any of it changes
    pb::View view;
    pb::DocPtr doc;
    uint64_t look = 0, ready = 0;  // look_'s changes, tiles come
    bool gpu = false;              // the route it asks for
    bool operator==(const Ask&) const;
  };
  void readScreen();  // look_.screen: the window's screen's profile
  void up(const QVariantMap& shown, double ms, bool gpu);  // a frame went up

  QVariantMap viewMap_;
  pb::View view_;  // x, y, zoom, angle of viewMap_
  QString source_, error_;
  uint64_t opens_ = 0;  // setSource()'s: the open its result belongs to
  pb::DocPtr doc_;
  std::shared_ptr<const pb::Preview> preview_;
  QColor pasteboard_{40, 40, 40};
  pb::Look look_;
  uint64_t looks_ = 0, ready_ = 0;
  std::optional<Ask> asked_;  // the last frame asked for; none: the next polish asks
  QMetaObject::Connection screen_;
  std::shared_ptr<CanvasState> s_;
  int frames_ = 0;
  bool gpu_ = false;
  double frameMs_ = 0;
  QVariantMap shown_;
  mutable std::map<std::array<int, 3>, pb::TilePtr> reference_;  // composite()'s, of doc_
};

// Which canvas Viewport shows (V1, V2b): DocCanvas with its test card when PB_CANVAS=pattern, with the file it names
// otherwise (PB_CANVAS=<path>), else MockCanvas.
class CanvasEnv : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON
  Q_PROPERTY(bool pattern READ pattern CONSTANT)
  Q_PROPERTY(QString document READ document CONSTANT)  // PB_CANVAS's file; "" when unset or "pattern"
  Q_PROPERTY(bool gpuFrames READ gpuFrames CONSTANT)   // PB_GPU_FRAMES is not 0: frames may go through the GPU

 public:
  using QObject::QObject;
  bool pattern() const { return qgetenv("PB_CANVAS") == "pattern"; }
  QString document() const { return pattern() ? QString() : qEnvironmentVariable("PB_CANVAS"); }
  bool gpuFrames() const { return qgetenv("PB_GPU_FRAMES") != "0"; }
};
