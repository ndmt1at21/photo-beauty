#pragma once
// The document's canvas (V1, V2b, C3b of docs/p0-tasks.md): frames of the item's device pixels, made off the UI thread
// (a document's through a Painting of frame.h, which lays the stroke being painted over it; V1's test card without
// one) and handed to Qt Quick as a texture; the Brush and the Eraser on it, from the mouse and the pen.
#include <QColor>
#include <QEventLoop>
#include <QImage>
#include <QPointF>
#include <QPointer>
#include <QQuickItem>
#include <QSize>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QQuickWindow>
#include <QtQml/qqmlregistration.h>

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include "present.h"
#include "render.h"
#include "stroke.h"

namespace pb {
class Document;
class Painting;
struct Preview;
}  // namespace pb
struct CanvasState;  // what a canvas shares with its frame job and the scene graph (canvas.cpp)

// Frames drawn on the GPU and handed over as a texture of Qt Quick's own device, when Qt Quick runs on Vulkan on the
// frames' GPU (with PB_CANVAS set, canvas.cpp); else through a QImage: offscreen, CI, PB_GPU_FRAMES=0, an import that
// failed. A frame is packed RGBA8 rows (R in the low byte), premultiplied, View::pixels() device pixels: the document
// as the Presenter shows it (its tiles at the view's mip level, coarser ones while those come, the screen's colours),
// a .pbd's preview until its tiles composite, or V1's test card (render.h) when there is no document and no source.
// A frame when what it shows changes: the view, the size, the document, the look, tiles the last frame lacked having
// come, each batch of a stroke's events; none otherwise. In QML DocCanvas: QtQuick has a Canvas, which the module's
// QML uses.
//
// Painting (C3b): with tool "brush" or "eraser" and a document up, the left button and the pen paint on its selected
// layer: each event as Qt hands it over (none dropped or coalesced: AA_CompressHighFrequencyEvents off), its point
// through current().to_doc(), its time on std::chrono::steady_clock as it comes, the pen's pressure and tilt (Qt
// Quick gives tablet events to pointer handlers only: the canvas reads them off its window and paints with the mouse
// events Qt makes of those no handler took). Pen up makes a step "Brush" or "Eraser" of the canvas's own history, a
// Document made of what setDocument() hands over, until N1's Session hands its own.
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
  // "brush", "eraser": the left button and the pen paint with it while a document is up; anything else, they pass
  // through to what is under the canvas.
  Q_PROPERTY(QString tool READ tool WRITE setTool NOTIFY toolChanged)
  // The Brush's and the Eraser's options as BrushParams (stroke.h): size (document pixels), hardness, opacity, flow
  // (0..1), spacing (a fraction of the size), pressureSize, pressureOpacity, pressureFlow (the pen's pressure scales
  // them), color (a QColor or its name; ponytail: as the document's own values, right for sRGB documents); BrushParams'
  // own for what it lacks. A stroke keeps the options it began with.
  Q_PROPERTY(QVariantMap brush READ brush WRITE setBrush NOTIFY brushChanged)

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
  QString tool() const { return tool_; }
  void setTool(const QString&);
  QVariantMap brush() const { return brushMap_; }
  void setBrush(const QVariantMap&);

  // What it shows (UI thread): a document state, a snapshot, and its .pbd's preview if any; null: V1's test card, or
  // nothing new while source opens. A history of its own begins there (a stroke not committed is dropped): strokes,
  // undo() and redo() go into it. ponytail: N1's Session hands its Document over instead.
  void setDocument(pb::DocPtr, std::shared_ptr<const pb::Preview> = {});
  pb::DocPtr document() const;  // the state up: the history's current one

  // Edit › Undo, Redo on the canvas's history, the stroke being painted ended and made a step first; false when there
  // is no step to go to.
  Q_INVOKABLE bool undo();
  Q_INVOKABLE bool redo();
  // The last stroke's, from its pen down: { added (events), applied, shown (in a frame), latency (ms, each shown
  // event's, from its arrival to the submit of the first frame with it) and its p50, p95, p99, max, frames, frameP50,
  // frameP95 (ms) }; screen (ms, each document frame's, from its submit to the window's swap with it) and its p50,
  // p95; sent (play()'s events), qt (ms, each one's from play() handing it to Qt to its arrival) and its qtP50, qtP95
  // once play() is through, while they are the events the stroke got; textures (GPU frames': made so far) and imports
  // (the window's of them: each once while it is kept).
  Q_INVOKABLE QVariantMap stats() const;
  // For tests: until the stroke is ended and its step made and a frame showing everything is up.
  Q_INVOKABLE void wait();
  // For tests: a pen's "press", "move" or "release" at the item's point `at`, through Qt as a platform hands a tablet's
  // over (QWindowSystemInterface), delivered before it returns. Where Qt makes no mouse event of a tablet event no
  // handler took (offscreen: the platform would), it hands over the one Windows' Qt makes; false then.
  Q_INVOKABLE bool pen(const QString& phase, QPointF at, qreal pressure);
  // For tests and measures: C4's stroke log at `path` (strokelog.h): { events, seconds, zoom, x, y (its first point),
  // cx, cy (the middle of its points), size (its brush's) }; {} if it cannot be read.
  Q_INVOKABLE QVariantMap strokeLog(const QString& path) const;
  // ... played from a thread through Qt as a platform hands input over (QWindowSystemInterface, queued), each event at
  // the window point that shows its document point now, at its own pace (`pace` 1; 0: all at once): kind "pen" a
  // tablet's with its pressure (and the mouse event as pen()), "mouse" the left button's; its stroke with its brush.
  // The UI thread runs its event loop meanwhile, as the app's, until the pen is up: how many events it handed over.
  Q_INVOKABLE int play(const QString& path, const QString& kind, double pace = 1);

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
  void toolChanged();
  void brushChanged();
  void painted(const QString& step);  // a stroke became a step of the history: "Brush", "Eraser"

 protected:
  void geometryChange(const QRectF& now, const QRectF& old) override;
  void itemChange(ItemChange, const ItemChangeData&) override;
  void updatePolish() override;
  QSGNode* updatePaintNode(QSGNode*, UpdatePaintNodeData*) override;
  void mousePressEvent(QMouseEvent*) override;
  void mouseMoveEvent(QMouseEvent*) override;
  void mouseReleaseEvent(QMouseEvent*) override;
  void mouseUngrabEvent() override;
  bool eventFilter(QObject*, QEvent*) override;  // its window's tablet events

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
  pb::Painting* painting(bool gpu);  // the document's, on that route; null while the GPU's presenter is being made
  void accepts();                    // the left button while it paints
  pb::PenEvent penEvent(const QMouseEvent*) const;
  void settle();  // the stroke ended, then committed
  void commit();  // an ended stroke's step, once merged

  QVariantMap viewMap_;
  pb::View view_;  // x, y, zoom, angle of viewMap_
  QString source_, error_;
  uint64_t opens_ = 0;  // setSource()'s: the open its result belongs to
  std::unique_ptr<pb::Document> history_;
  std::unique_ptr<pb::Painting> painting_;  // of history_, after it; made on the first frame, again on another route
  bool paintingGpu_ = false;                // painting_'s route
  std::shared_ptr<const pb::Preview> preview_;  // up before the document's first frame, then dropped
  bool previewing_ = false;                     // until it is: the document's frames wait
  QString tool_;
  QVariantMap brushMap_;
  pb::BrushParams brush_;
  std::optional<pb::BrushParams> played_;  // play()'s stroke's
  bool stroking_ = false;                  // pen down, its stroke begun
  struct Pen {  // the last tablet event its window had: what the mouse event Qt makes of it lacks
    quint64 timestamp = 0;
    float pressure = 1, tiltX = 0, tiltY = 0, rotation = 0;
    bool moused = false;  // Qt made a mouse event of it
  } pen_;
  bool penMice();  // pen(), play(): Qt makes mouse events of tablet events no handler takes
  QPointer<QQuickWindow> filtered_;  // whose tablet events it reads
  std::vector<double> got_;          // the stroke's events' arrival, PenEvent::t
  struct Player;
  std::shared_ptr<Player> player_;
  QEventLoop* playing_ = nullptr;  // play()'s, until pen up
  QColor pasteboard_{40, 40, 40};
  pb::Look look_;
  uint64_t looks_ = 0, ready_ = 0;
  std::optional<Ask> asked_;  // the last frame asked for; none: the next polish asks
  QMetaObject::Connection screen_, swapped_;  // the window's screenChanged, frameSwapped
  std::shared_ptr<CanvasState> s_;
  int frames_ = 0;
  bool gpu_ = false;
  double frameMs_ = 0;
  QVariantMap shown_;
  mutable std::map<std::array<int, 3>, pb::TilePtr> reference_;  // composite()'s, of document()
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
  // PB_STROKES: the folder of the D1 strokes pb-bench make-strokes writes (d1-1.jsonl ...), which tst_c3b plays
  Q_PROPERTY(QString strokes READ strokes CONSTANT)

 public:
  using QObject::QObject;
  bool pattern() const { return qgetenv("PB_CANVAS") == "pattern"; }
  QString document() const { return pattern() ? QString() : qEnvironmentVariable("PB_CANVAS"); }
  bool gpuFrames() const { return qgetenv("PB_GPU_FRAMES") != "0"; }
  QString strokes() const { return qEnvironmentVariable("PB_STROKES"); }
};
