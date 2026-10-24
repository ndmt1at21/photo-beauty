#pragma once
// A document opened for real (N1 of docs/ui-tasks.md): its history (Document, core/history.h) and what the panels read
// of it, with the API of the mock document (Mock.makeDoc() in ui/qml/Mock.js), so that Doc.qml takes either as its
// `core` and the panels cannot tell them apart. Session (session.h) makes it and reads its file in a Background job.
//
// run() takes T1's commands by their JSON names (core/doc.h; others through command_from_json()) and the panels' own
// made of them as the mock has them (newLayer, arrange, deleteLayers choosing the layer next to those gone, a copy
// named "… bản sao"), each one step of the history, named as Photoshop's History names it (English, as the core's
// "Brush"); the panels show the steps in Vietnamese with an icon. What the core cannot do yet, run() refuses and can()
// says no to: greyed and locked, not hidden (decision 4 of docs/ui-tasks.md). Lists and objects are JS values, the
// same object while what they show stays (Doc.qml copies a field when it is another), made on the UI thread from the
// document's immutable state; a layer's are made again only when its node is new. UI thread only.
#include <QElapsedTimer>
#include <QJSValue>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "history.h"

class QJSEngine;
namespace pb {
struct Preview;
}

class DocModel : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Session opens documents")
  Q_PROPERTY(bool real READ real CONSTANT)          // a document of the core, unlike the mock's (N3 locks tools by it)
  Q_PROPERTY(QString path READ path NOTIFY changed)  // the file's, local: opened from, then saved as; "" for neither
  // path is a .pbd it was opened from or saved into: Lưu saves there (appending, M3); else Lưu is Lưu thành (N2)
  Q_PROPERTY(bool pbd READ pbd NOTIFY changed)
  Q_PROPERTY(bool loading READ loading NOTIFY changed)  // the file being read: no layers, commands refused
  Q_PROPERTY(QString error READ error NOTIFY changed)   // why the file did not open; "" when it did or still may
  // For CanvasBar (Overlays.qml): the notice bar's text, what runs in the background and its share done (0..1, below 0
  // none). The P1 tasks set them through setNotice(), setTask().
  Q_PROPERTY(QString notice READ notice NOTIFY changed)
  Q_PROPERTY(QString task READ task NOTIFY changed)
  Q_PROPERTY(double progress READ progress NOTIFY changed)
  Q_PROPERTY(int version READ version NOTIFY changed)  // up by one at every change of what it shows
  Q_PROPERTY(qint64 bytes READ bytes NOTIFY changed)   // the file's size, 0 without one

  // Mock.FIELDS, as Mock.makeDoc() has them
  Q_PROPERTY(QString file READ file NOTIFY changed)  // the name shown on the tab
  Q_PROPERTY(QString mem READ mem NOTIFY changed)    // "148 MB / 1,02 GB": flattened, all layers
  Q_PROPERTY(bool dirty READ dirty NOTIFY changed)   // the step shown is not the one saved (Step::serial)
  // The steps between the one saved and the one shown, what closing unsaved loses (SaveChanges); 0 when not dirty, the
  // steps since the first when the step saved is gone (a command after undoing past it, or trimmed).
  Q_PROPERTY(int unsaved READ unsaved NOTIFY changed)
  Q_PROPERTY(QJSValue view READ view NOTIFY changed)  // { x, y, zoom, angle } as View.js
  Q_PROPERTY(QJSValue state READ state NOTIFY changed)  // { w, h, ppi, mode, bits, profile, root, selected, nextId… }
  Q_PROPERTY(QJSValue tree READ tree NOTIFY changed)    // the root: { id: 0, kind: "group", children } bottom first
  Q_PROPERTY(QJSValue layers READ layers NOTIFY changed)  // top first: { id, kind, props, depth, parent, index, count }
  Q_PROPERTY(QJSValue rows READ rows NOTIFY changed)      // layers but those inside a collapsed group
  Q_PROPERTY(QJSValue active READ active NOTIFY changed)  // the entry of layers selected last, null without
  Q_PROPERTY(QJSValue selected READ selected NOTIFY changed)  // ids, the active one last
  Q_PROPERTY(QJSValue collapsed READ collapsed NOTIFY changed)  // { group id: true }
  Q_PROPERTY(bool editMask READ no CONSTANT)  // P1 (masks)
  Q_PROPERTY(int w READ w NOTIFY changed)
  Q_PROPERTY(int h READ h NOTIFY changed)
  Q_PROPERTY(double ppi READ ppi CONSTANT)  // 72 until the core keeps one (A40)
  Q_PROPERTY(QString mode READ mode NOTIFY changed)  // "RGB", "Gray"
  Q_PROPERTY(int bits READ bits NOTIFY changed)      // 8, 16
  Q_PROPERTY(QString profile READ profile NOTIFY changed)  // the ICC profile's description
  Q_PROPERTY(QJSValue photo READ null CONSTANT)  // the mock's sample photo: none
  Q_PROPERTY(bool faces READ no CONSTANT)
  Q_PROPERTY(QJSValue selection READ null CONSTANT)  // P1 (selections)
  Q_PROPERTY(QJSValue prevSelection READ null CONSTANT)
  Q_PROPERTY(bool inverse READ no CONSTANT)
  Q_PROPERTY(QJSValue alphas READ alphas CONSTANT)  // [] until P1
  Q_PROPERTY(QString channel READ channel NOTIFY changed)
  Q_PROPERTY(QJSValue hiddenChannels READ hiddenChannels NOTIFY changed)
  Q_PROPERTY(int rot READ zero CONSTANT)  // the mock's turns: P1 (Image Rotation, Transform)
  Q_PROPERTY(bool flipH READ no CONSTANT)
  Q_PROPERTY(bool flipV READ no CONSTANT)
  Q_PROPERTY(int layerRot READ zero CONSTANT)
  Q_PROPERTY(bool layerFlipH READ no CONSTANT)
  Q_PROPERTY(bool layerFlipV READ no CONSTANT)
  Q_PROPERTY(QJSValue history READ history NOTIFY changed)  // steps, oldest first: { name, icon, json }
  Q_PROPERTY(int index READ index NOTIFY changed)            // the step shown
  Q_PROPERTY(QJSValue snapshots READ snapshots NOTIFY changed)  // { name, look: "none", url (its thumbnail) }
  Q_PROPERTY(int snapshot READ snapshot NOTIFY changed)         // the snapshot shown, -1 for none

 public:
  // Opening `path` (Session reads it, then opened() or failed()), a .pbd or not, the view fitted to a frame of viewW x
  // viewH px.
  DocModel(QJSEngine* js, const QString& path, const QString& file, qint64 bytes, bool pbd, int w, int h, int viewW,
           int viewH, QObject* parent);
  ~DocModel() override;

  // ---- C++: Session, Canvas ----
  // The history: null while the file is read. The canvas paints into it (Painting), after aboutToChange() made it
  // end its stroke, and calls touched() once a stroke is a step.
  pb::Document* document() { return doc_.get(); }
  pb::DocPtr current() const { return doc_ ? doc_->state() : nullptr; }
  // The file read: its state as the history's first step, `step` ("Open", "Duplicate"); its one layer made the
  // Background (at the bottom, locked) when `background`, as Photoshop opens a JPEG; a .pbd's composite as `preview`.
  void opened(pb::DocPtr state, const char* step, bool background, std::shared_ptr<const pb::Preview> preview = {});
  // The .pbd's composite, once: the first canvas to show the document shows it until its first frame (V2b).
  std::shared_ptr<const pb::Preview> takePreview() { return std::move(preview_); }
  void failed(const QString& why);  // the file did not open: error, no document
  void touched();                   // the history changed outside run() (a stroke's step): what it shows follows
  // Session saved the state of step `serial` (Step::serial) as the .pbd at `path`: that step is the one saved, path the
  // document's (its tab named after it).
  void saved(uint64_t serial, const QString& path);

  // ---- QML ----
  bool real() const { return true; }
  QString path() const { return path_; }
  bool pbd() const { return pbd_; }
  bool loading() const { return !doc_ && error_.isEmpty(); }
  QString error() const { return error_; }
  QString notice() const { return notice_; }
  QString task() const { return task_; }
  double progress() const { return progress_; }
  int version() const { return version_; }
  qint64 bytes() const { return bytes_; }
  QString file() const { return file_; }
  QString mem() const { return mem_; }
  bool dirty() const;
  int unsaved() const;
  QJSValue view() const { return view_; }
  QJSValue state() const { return state_; }
  QJSValue tree() const { return tree_; }
  QJSValue layers() const { return layers_; }
  QJSValue rows() const { return rows_; }
  QJSValue active() const { return active_; }
  QJSValue selected() const { return selected_; }
  QJSValue collapsed() const { return collapsed_; }
  int w() const { return w_; }
  int h() const { return h_; }
  double ppi() const { return 72; }
  QString mode() const { return mode_; }
  int bits() const { return bits_; }
  QString profile() const { return profile_; }
  QJSValue alphas() const { return alphas_; }
  QString channel() const { return channel_; }
  QJSValue hiddenChannels() const { return hiddenChannels_; }
  QJSValue history() const { return history_; }
  int index() const { return doc_ ? int(doc_->current()) : 0; }
  QJSValue snapshots() const { return snapshots_; }
  int snapshot() const { return snapshot_; }
  bool no() const { return false; }
  int zero() const { return 0; }
  QJSValue null() const { return QJSValue(QJSValue::NullValue); }

  // One command, { op, …fields } as the mock's (Mock.js lists them): a History step, but for selectLayers and within a
  // gesture; false, nothing changed, when it cannot run, would change nothing, or the core has no such command yet.
  Q_INVOKABLE bool run(const QJSValue& cmd);
  // Whether the core has `name`: an op of run(), a menu action ("@merge:down"), "dialog:<name>" or "space:<name>" (only
  // "dialog:SaveOptions": Lưu thành, Lưu bản sao, which Main turns into its FileDialog for a .pbd), "panel:<name>" for a
  // panel greyed whole, none of its controls the core's yet (Properties, Retouch, Channels);
  // a menu requirement (Mock.CX: "layer", "!bgl") is the mock's to judge: true. Doc.can() asks this and then the mock's
  // requirement.
  Q_INVOKABLE bool can(const QString& name) const;
  Q_INVOKABLE void undo();
  Q_INVOKABLE void redo();
  Q_INVOKABLE void jumpTo(int step);  // as a click on it in History
  Q_INVOKABLE void toggleLast();      // between the last step and the one before (Ctrl Alt Z)
  Q_INVOKABLE void revert();          // to the first step
  Q_INVOKABLE void jumpToSnapshot(int i);
  Q_INVOKABLE void newSnapshot();
  Q_INVOKABLE void deleteStep();  // the step shown and those after it
  Q_INVOKABLE void beginGesture();
  Q_INVOKABLE void endGesture();
  Q_INVOKABLE void markSaved();   // the step shown is the one saved (a save of Session marks its own step: saved())
  Q_INVOKABLE void setView(const QJSValue& patch);  // some of { x, y, zoom, angle }
  Q_INVOKABLE void zoomTo(double pct);              // %, 1 to 12.800
  Q_INVOKABLE void toggleOpen(int group);           // a group's rows shown in Layers, or not
  Q_INVOKABLE void setEditMask(bool) {}             // P1: no masks yet
  Q_INVOKABLE void setChannel(const QString& name);
  Q_INVOKABLE void toggleChannel(const QString& name);
  // For the P1 tasks: the notice bar's text ("" none), what runs and its share done (below 0: nothing runs).
  Q_INVOKABLE void setNotice(const QString& text);
  Q_INVOKABLE void setTask(const QString& task, double progress);
  Q_INVOKABLE void cancelTask() { emit canceled(); }  // CanvasBar's cancel (a file being read: Docs closes it)
  // For tests: the core's state shown, as JSON: { width, height, selected, nextId, layers: [{ id, kind, props
  // (to_json()), tiles: [ids] of a pixel layer, layers of a group }] } bottom first; "" while the file is read.
  Q_INVOKABLE QString dump() const;
  // For tests: a hash of the layers' pixels over the canvas, bottom first, as read_rect() reads them (however tiles hold
  // them: in RAM, compressed, in a file), with each layer's kind and format; "" while the file is read.
  Q_INVOKABLE QString digest() const;

 signals:
  void changed();        // anything it shows; Doc.qml copies the fields, the canvas asks for a frame
  void aboutToChange();  // before the history changes: the canvas ends its stroke, which becomes a step first
  void closing();        // in its destructor, while the history is still there: the canvas lets go of it
  void canceled();       // the task shown is to stop (P1's tasks)

 private:
  bool apply(const QJsonObject& c);  // run()'s work; throws std::invalid_argument for what cannot run
  void step(const pb::Command& c, const char* name);  // one more command into the history
  void derive();  // the JS values of what changed
  QJSValue layerEntry(const pb::LayerPtr& l, int depth, pb::LayerId parent, int index);
  QJSValue node(const pb::LayerPtr& l);
  QJSValue props(const pb::Layer& l);
  QString thumbUrl(const pb::Layer& l) const;
  void fit(int viewW, int viewH);
  void bump();  // version, changed()
  void estimate();  // progress while the file is read

  QJSEngine* js_;
  QJSValue parse_;  // JSON.parse of js_: a layer's props as one call
  QString path_, file_, error_, notice_, task_;
  qint64 bytes_ = 0;
  bool pbd_ = false;
  double progress_ = -1;
  QElapsedTimer reading_;
  QTimer ticker_;  // progress while reading
  int version_ = 0;
  std::unique_ptr<pb::Document> doc_;
  std::shared_ptr<const pb::Preview> preview_;
  uint64_t saved_ = 0;  // the serial of the step saved
  int snapshot_ = -1;
  int viewW_ = 0, viewH_ = 0;
  int w_ = 0, h_ = 0, bits_ = 8;
  QString mode_ = QStringLiteral("RGB"), profile_, mem_, channel_ = QStringLiteral("RGB");
  QJSValue view_, state_, tree_, layers_, rows_, active_, selected_, collapsed_, alphas_, hiddenChannels_, history_,
      snapshots_;
  std::vector<pb::LayerId> collapsedIds_;
  // What derive() made last, to make again only what changed: the state and the root it showed, each node's JS values
  // by the node (held, so no other node takes its address), the steps' by serial.
  pb::DocPtr shown_;
  struct Made {
    pb::LayerPtr layer;
    QJSValue props, node, entry;
    int depth = -1, index = -1;
    pb::LayerId parent = 0;
    bool used = false;
  };
  std::unordered_map<const pb::Layer*, Made> made_;
  std::vector<uint64_t> stepsShown_;
  std::unordered_map<uint64_t, QJSValue> stepEntries_;  // History's entries by serial
  size_t snapshotsShown_ = SIZE_MAX;
};
