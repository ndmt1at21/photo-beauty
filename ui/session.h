#pragma once
// Files opened for real (N1 of docs/ui-tasks.md): Session reads JPEG, PNG and TIFF (open_image(), M1) and .pbd
// (open_pbd(), M2) in a Background job into a DocModel (docmodel.h), which Docs.qml puts in a Doc beside the mock documents; and image://pb, the
// thumbnails of layers and snapshots, made from mip() (K2) and composite_tile() on the provider's thread. N2: it saves
// them as .pbd (save_pbd(), M2, M3) in a Background job, and tells the status bar and Thiết lập › Hiệu năng what the
// tile store holds (K1) and what this machine has, and sets the store's budget and scratch folder.
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <cstdint>
#include <deque>
#include <memory>

#include "doc.h"
#include "jobs.h"

class DocModel;
class QJSEngine;
class QQmlEngine;

class Session : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON
  Q_PROPERTY(bool saving READ saving NOTIFY savingChanged)    // a save() runs
  Q_PROPERTY(int waiting READ waiting NOTIFY savingChanged)  // saves waiting for it
  // For tests: the next save() cancels itself, as Hủy would, once this share of it (0..1) is done, then back to 2 (never).
  Q_PROPERTY(double cancelAt MEMBER cancelAt_)
  // The GPU found by findGpu(): { name, api, found (a usable one) }; {} until it has looked.
  Q_PROPERTY(QVariantMap gpu READ gpu NOTIFY gpuChanged)
  Q_PROPERTY(QString scratchDir READ scratchDir NOTIFY scratchDirChanged)  // the tile store's, as useScratch() set it
  Q_PROPERTY(QVariantMap found READ found NOTIFY foundChanged)  // machine() as findMachine() read it last; {} before

 public:
  // The singleton of `engine`, image://pb added to it.
  static Session* create(QQmlEngine* engine, QJSEngine* js);
  // Waits for a save still running, then makes those waiting: quitting cuts none short, drops none.
  ~Session() override;

  // What open() takes: a file there whose first bytes are a JPEG's, a PNG's, a TIFF's or a .pbd's, whatever its name
  // says; a path or a file: URL. The mock opens others by their name.
  Q_INVOKABLE bool canOpen(const QString& file) const;
  // The file as a document, at once: `loading` while a Background job reads it, then its Background layer (a .pbd: its
  // layers as saved, its composite up on the canvas first), or `error` (why it did not open); null when canOpen() says
  // no. Its view fitted to a frame of viewW x viewH px.
  Q_INVOKABLE DocModel* open(const QString& file, int viewW, int viewH);
  // A new document of `doc`'s state shown, named `name` (Image › Nhân bản, History's Tài liệu mới từ bước này): one
  // step, "Duplicate"; its view as doc's. Null while doc is read.
  Q_INVOKABLE DocModel* duplicate(DocModel* doc, const QString& name);
  // Done with: destroyed once the event loop gets there (a canvas lets go of it first, on DocModel::closing()).
  Q_INVOKABLE void close(DocModel* doc);
  // The local path of a path or a file: URL; "" for another kind of URL.
  Q_INVOKABLE QString localPath(const QString& file) const;
  Q_INVOKABLE QUrl fileUrl(const QString& path) const;  // a local path's file: URL (FileDialog takes URLs)
  // The file's size and when it changed: { bytes, modified (ms since 1970) }; {} when it is not there.
  Q_INVOKABLE QVariantMap fileInfo(const QString& file) const;

  // ---- saving (N2) ----
  // doc's state shown, saved as a .pbd at `file` (a path or file: URL) by a Background job: appended to the file when it
  // is the .pbd some of the state's tiles lie in (the one doc was opened from or saved into, M3), else a full save into
  // a new file moved over it; the document goes on changing meanwhile, as it hands over its state and no more. Done, doc
  // is saved at that step, its path the file's (copy: neither, a copy elsewhere). One at a time: one asked for while
  // another runs waits for it, with the state of now (the same document, file and copy waiting already: in its place).
  // saveStarted(), saveProgress(), saveEnded() tell how it goes. False for no document (or one being read), or no path.
  Q_INVOKABLE bool save(DocModel* doc, const QString& file, bool copy = false);
  // The save running stops soon, its file as it was (a full save's) or as saved last (an append's); those waiting go.
  Q_INVOKABLE void cancelSave();
  // Where a save of doc, not a copy, running or waiting goes: "" when none (Lưu of a document whose Lưu thành is on its
  // way goes there too).
  Q_INVOKABLE QString savingTo(DocModel* doc) const;
  bool saving() const { return saving_; }
  int waiting() const { return int(waiting_.size()); }

  // ---- the tile store and this machine (N2) ----
  // What the tile store holds, bytes (tile_stats(), K1): { raw, compressed, scratch, pinned, charged, ram (what the
  // budget counts: raw, compressed and charged), budget (tile_budget()) }.
  Q_INVOKABLE QVariantMap stats() const;
  // This machine: { ram (bytes), temp (the system's temporary folder), disks: [{ name ("C:", "/mnt/d"), kind (its
  // label and file system), free, total (bytes), dir (where scratch goes on it: the temporary folder on its disk, else
  // <root>/photo-beauty-scratch) }] }, the temporary folder's disk first, local disks that can be written, each once.
  Q_INVOKABLE QVariantMap machine() const;
  // machine() read again in a Background job, into `found` (a disk slow to answer, as a network drive offline, keeps
  // the UI waiting none); foundChanged() once it is, each time.
  Q_INVOKABLE void findMachine();
  QVariantMap found() const { return found_; }
  Q_INVOKABLE void setBudget(double bytes);  // the tile store's budget (set_tile_budget()), at least 1 MB
  // The first of `dirs` that is or can be made a folder to write in becomes the tile store's scratch folder
  // (set_scratch_dir()); none: the system's temporary folder. Which, as scratchDir.
  Q_INVOKABLE QString useScratch(const QStringList& dirs);
  QString scratchDir() const { return scratchDir_; }
  // Looks for the GPU in a Background job, as the canvas would take it (gpu::Device::create(): no software renderer);
  // gpuChanged() once it has, each time.
  Q_INVOKABLE void findGpu();
  QVariantMap gpu() const { return gpu_; }

 signals:
  void savingChanged();
  void saveStarted(DocModel* doc, const QString& path, bool copy);  // doc: null once closed
  void saveProgress(double part);                                   // the save running, 0..1
  // The save of `path` ended: done when error is "" and not cancelled; error, why not (the core's, in English).
  void saveEnded(DocModel* doc, const QString& path, const QString& error, bool cancelled, bool copy);
  void gpuChanged();
  void foundChanged();
  void scratchDirChanged();

 private:
  explicit Session(QJSEngine* js);
  struct Save {  // what save() was asked: the state then, its step
    pb::DocPtr state;
    uint64_t serial = 0;
    QString path;
    bool copy = false;
    QPointer<DocModel> doc;
  };
  void start(Save s);
  void finish(const Save& s, const QString& error, bool cancelled);  // on the UI thread, once s ended
  QJSEngine* js_;
  bool saving_ = false;
  Save running_;
  std::deque<Save> waiting_;
  double cancelAt_ = 2;
  pb::CancelToken cancel_;
  struct Flights;  // the saves running, for the destructor to wait on
  std::shared_ptr<Flights> flights_;
  QVariantMap gpu_, found_;
  QString scratchDir_;
};

// Thumbnails for image://pb (session.cpp), by key: "layer/<key>" a layer's pixels fitted in the canvas, "doc/<key>" a
// state's composite; rendered on the provider's thread while what they show lives (the history holds it). A key stays
// the same while the grid or the state is the same object. Any thread.
namespace pb::thumbs {
uint64_t layer(const std::shared_ptr<const TileGrid>& pixels, int docW, int docH);
uint64_t document(const DocPtr& state);
}  // namespace pb::thumbs
