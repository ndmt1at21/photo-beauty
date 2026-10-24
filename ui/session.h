#pragma once
// Files opened for real (N1 of docs/ui-tasks.md): Session reads JPEG, PNG and TIFF (open_image(), M1) and .pbd
// (open_pbd(), M2) in a Background job into a DocModel (docmodel.h), which Docs.qml puts in a Doc beside the mock documents; and image://pb, the
// thumbnails of layers and snapshots, made from mip() (K2) and composite_tile() on the provider's thread.
#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <cstdint>
#include <memory>

#include "doc.h"

class DocModel;
class QJSEngine;
class QQmlEngine;

class Session : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON

 public:
  // The singleton of `engine`, image://pb added to it.
  static Session* create(QQmlEngine* engine, QJSEngine* js);

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
  // The file's size and when it changed: { bytes, modified (ms since 1970) }; {} when it is not there.
  Q_INVOKABLE QVariantMap fileInfo(const QString& file) const;

 private:
  explicit Session(QJSEngine* js) : js_(js) {}
  QJSEngine* js_;
};

// Thumbnails for image://pb (session.cpp), by key: "layer/<key>" a layer's pixels fitted in the canvas, "doc/<key>" a
// state's composite; rendered on the provider's thread while what they show lives (the history holds it). A key stays
// the same while the grid or the state is the same object. Any thread.
namespace pb::thumbs {
uint64_t layer(const std::shared_ptr<const TileGrid>& pixels, int docW, int docH);
uint64_t document(const DocPtr& state);
}  // namespace pb::thumbs
