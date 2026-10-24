// Files opened for real and image://pb (session.h).
#include "session.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJSEngine>
#include <QPainter>
#include <QPointer>
#include <QQmlEngine>
#include <QQuickImageProvider>
#include <QStorageInfo>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <unordered_map>
#include <utility>

#include "docmodel.h"
#include "gpu.h"
#include "jobs.h"
#include "mip.h"
#include "open.h"
#include "pbd.h"
#include "profile.h"
#include "render.h"
#include "tile.h"
#include "tilecache.h"

namespace {

// ---- what a file is ----
// A JPEG, a PNG, a TIFF or a .pbd by its first bytes, and its size as its header gives it (0 x 0 where it does not say
// early, as a .pbd: the document's own once read).
struct Header {
  bool image = false;  // Session opens it
  int w = 0, h = 0;
  bool pbd = false;  // photo-beauty's own (M2): open_pbd(), not open_image()
};
uint32_t be(const uchar* p, int n) {
  uint32_t v = 0;
  for (int i = 0; i < n; i++) v = v << 8 | p[i];
  return v;
}
uint32_t le(const uchar* p, int n) {
  uint32_t v = 0;
  for (int i = n; i-- > 0;) v = v << 8 | p[i];
  return v;
}
Header sniff(const QString& path) {
  Header h;
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) return h;
  const QByteArray head = f.read(256 << 10);  // a JPEG's EXIF and ICC segments come before its frame header
  const auto* d = reinterpret_cast<const uchar*>(head.constData());
  const qsizetype n = head.size();
  if (n >= 8 && head.startsWith(QByteArrayView("\x89PBD\r\n\x1a\n", 8))) {  // pbd.cpp's kMagic
    h.image = h.pbd = true;
  } else if (n >= 24 && head.startsWith("\x89PNG\r\n\x1a\n") && head.mid(12, 4) == "IHDR") {
    h = {true, int(be(d + 16, 4)), int(be(d + 20, 4))};
  } else if (n >= 3 && d[0] == 0xff && d[1] == 0xd8 && d[2] == 0xff) {
    h.image = true;
    for (qsizetype i = 2; i + 9 < n;) {  // segments up to a frame header (SOF0..SOF15 but DHT, JPG, DAC)
      if (d[i] != 0xff) break;
      const uchar m = d[i + 1];
      if (m == 0xff) {
        i++;
        continue;
      }
      if (m == 0xd8 || m == 0x01 || (m >= 0xd0 && m <= 0xd7)) {
        i += 2;
        continue;
      }
      if (m >= 0xc0 && m <= 0xcf && m != 0xc4 && m != 0xc8 && m != 0xcc) {
        h.h = int(be(d + i + 5, 2)), h.w = int(be(d + i + 7, 2));
        break;
      }
      i += 2 + be(d + i + 2, 2);
    }
  } else if (n >= 8 && (head.startsWith("II*") || head.startsWith("MM\0*"))) {
    h.image = true;
    const bool little = d[0] == 'I';
    const auto num = [&](const uchar* p, int k) { return little ? le(p, k) : be(p, k); };
    if (f.seek(num(d + 4, 4))) {  // the first IFD: ImageWidth (256), ImageLength (257)
      const QByteArray count = f.read(2);
      if (count.size() == 2) {
        const int entries = int(num(reinterpret_cast<const uchar*>(count.constData()), 2));
        const QByteArray ifd = f.read(qint64(entries) * 12);
        const auto* e = reinterpret_cast<const uchar*>(ifd.constData());
        for (int i = 0; i + 1 <= entries && (i + 1) * 12 <= ifd.size(); i++, e += 12) {
          const uint32_t tag = num(e, 2), type = num(e + 2, 2);
          const uint32_t v = type == 3 ? num(e + 8, 2) : num(e + 8, 4);
          if (tag == 256) h.w = int(v);
          if (tag == 257) h.h = int(v);
        }
      }
    }
  }
  if (h.w < 0 || h.h < 0 || h.w > 65000 || h.h > 65000) h.w = h.h = 0;  // what open_image() refuses says why later
  return h;
}

QString why(const pb::DecodeError& e) {
  switch (e.kind) {
    case pb::DecodeError::Kind::Codec:
      if (e.codec == "pbd_version") return QCoreApplication::translate("Session", "file .pbd của bản mới hơn");
      return QCoreApplication::translate("Session", "kiểu %1 chưa hỗ trợ").arg(QString::fromStdString(e.codec));
    case pb::DecodeError::Kind::TooBig:
      return QCoreApplication::translate("Session", "ảnh quá lớn (tối đa 65.000 px mỗi cạnh, 512 MP)");
    default:
      return QCoreApplication::translate("Session", "file hỏng hay không phải ảnh");
  }
}

// ---- image://pb ----
struct Shown {
  std::weak_ptr<const pb::TileGrid> grid;  // "layer/<key>"
  std::weak_ptr<const pb::DocState> doc;   // "doc/<key>"
  int w = 0, h = 0;                        // the canvas a layer is fitted in
};
std::mutex shownLock;
std::unordered_map<uint64_t, Shown> shownByKey;
std::unordered_map<const void*, uint64_t> keyByObject;  // while the object lives: an address another may take after
uint64_t lastKey = 0;

uint64_t keyOf(const void* object, const Shown& s, bool fresh) {
  const std::lock_guard l(shownLock);
  const auto it = keyByObject.find(object);
  if (it != keyByObject.end() && !fresh) return it->second;
  const uint64_t k = ++lastKey;
  if (k % 256 == 0)  // drop what died
    for (auto i = shownByKey.begin(); i != shownByKey.end();) {
      const bool dead = i->second.grid.expired() && i->second.doc.expired();
      if (dead) {
        const auto o = std::find_if(keyByObject.begin(), keyByObject.end(), [&](const auto& x) { return x.second == i->first; });
        if (o != keyByObject.end()) keyByObject.erase(o);
      }
      i = dead ? shownByKey.erase(i) : std::next(i);
    }
  shownByKey[k] = s;
  keyByObject[object] = k;
  return k;
}

// Tiles of `level` over the canvas (w x h at level 0) into an RGBA8888 image, straight alpha: tile(tx, ty) gives each
// (null: transparent), of 2 or 4 channels at 8 or 16 bits.
template <class TileAt>
QImage gather(int w, int h, int level, TileAt tile) {
  const int cw = std::max(1, (w + (1 << level) - 1) >> level), ch = std::max(1, (h + (1 << level) - 1) >> level);
  QImage img(cw, ch, QImage::Format_RGBA8888);
  img.fill(Qt::transparent);
  for (int ty = 0; ty << pb::kTileLog2 < ch; ty++)
    for (int tx = 0; tx << pb::kTileLog2 < cw; tx++) {
      const pb::TilePtr t = tile(tx, ty);
      if (!t) continue;
      const pb::Format f = t->format();
      const int c = f.channels, wide = f.depth == pb::Depth::U16 ? 2 : 1;
      const pb::Pin pin = t->pin();
      const pb::Pixel v = t->value();
      for (int y = 0; y < pb::kTileSize; y++) {
        const int iy = (ty << pb::kTileLog2) + y;
        if (iy >= ch) break;
        uchar* row = img.scanLine(iy);
        for (int x = 0; x < pb::kTileSize; x++) {
          const int ix = (tx << pb::kTileLog2) + x;
          if (ix >= cw) break;
          uchar s[4];
          for (int k = 0; k < c; k++) {
            if (!pin.data()) {
              s[k] = uchar(wide == 2 ? v[size_t(k)] >> 8 : v[size_t(k)]);
            } else {
              const uint8_t* p = pin.data() + (size_t(y) * pb::kTileSize + size_t(x)) * size_t(c * wide) + size_t(k * wide);
              s[k] = wide == 2 ? uchar((p[0] | p[1] << 8) >> 8) : p[0];
            }
          }
          uchar* o = row + ix * 4;
          if (c >= 3)
            o[0] = s[0], o[1] = s[1], o[2] = s[2], o[3] = c == 4 ? s[3] : 255;
          else
            o[0] = o[1] = o[2] = s[0], o[3] = c == 2 ? s[1] : 255;
        }
      }
    }
  return img;
}

// The mip level whose canvas just covers `side` px.
int levelFor(int w, int h, int side) {
  int l = 0;
  while (l < 12 && (std::max(w, h) >> (l + 1)) >= side) l++;
  return l;
}

// img fitted in a square of `side` over a checker where the canvas is, as Photoshop's thumbnails.
QImage framed(const QImage& img, int side) {
  QImage out(side, side, QImage::Format_ARGB32_Premultiplied);
  out.fill(Qt::transparent);
  QPainter p(&out);
  const double k = std::min(double(side) / img.width(), double(side) / img.height());
  const QRectF r((side - img.width() * k) / 2, (side - img.height() * k) / 2, img.width() * k, img.height() * k);
  const double cell = std::max(2., side / 8.);
  p.setClipRect(r);
  p.fillRect(r, QColor(255, 255, 255));
  for (int y = 0; y * cell < side; y++)
    for (int x = (y & 1); x * cell < side; x += 2) p.fillRect(QRectF(x * cell, y * cell, cell, cell), QColor(204, 204, 204));
  p.setRenderHint(QPainter::SmoothPixmapTransform);
  p.drawImage(r, img);
  return out;
}

class Thumbs : public QQuickImageProvider {
 public:
  Thumbs() : QQuickImageProvider(QQuickImageProvider::Image, QQuickImageProvider::ForceAsynchronousImageLoading) {}
  QImage requestImage(const QString& id, QSize* size, const QSize& requested) override {
    PROFILE_ZONE("thumb");
    const int side = std::clamp(std::max(requested.width(), requested.height()), 8, 512);
    Shown s;
    {
      const std::lock_guard l(shownLock);
      const auto it = shownByKey.find(id.section(u'/', 1).toULongLong());
      if (it != shownByKey.end()) s = it->second;
    }
    QImage out(side, side, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    try {
      if (id.startsWith(QLatin1String("layer/"))) {
        if (const auto g = s.grid.lock()) {
          const int level = levelFor(s.w, s.h, side);
          out = framed(gather(s.w, s.h, level, [&](int tx, int ty) { return pb::mip(*g, level, tx, ty); }), side);
        }
      } else if (const auto d = s.doc.lock()) {  // the composite, covering the size asked for (History's 26 x 32)
        const int level = levelFor(d->width, d->height, side);
        const pb::RenderInput in{d, {}};
        const QImage img = gather(d->width, d->height, level, [&](int tx, int ty) { return pb::composite_tile(in, level, tx, ty); });
        out = img.scaled(requested.isValid() ? requested : QSize(side, side), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
      }
    } catch (const std::exception& e) {  // a tile that would not read back (K1): no thumbnail
      qWarning("image://pb/%s: %s", qPrintable(id), e.what());
    }
    if (size) *size = out.size();
    return out;
  }
};

}  // namespace

namespace pb::thumbs {
uint64_t layer(const std::shared_ptr<const TileGrid>& pixels, int docW, int docH) {
  bool fresh = false;
  {
    const std::lock_guard l(shownLock);
    const auto it = keyByObject.find(pixels.get());
    const auto s = it == keyByObject.end() ? shownByKey.end() : shownByKey.find(it->second);
    fresh = s == shownByKey.end() || s->second.grid.expired() || s->second.w != docW || s->second.h != docH;
  }
  return keyOf(pixels.get(), {pixels, {}, docW, docH}, fresh);
}
uint64_t document(const DocPtr& state) {
  bool fresh = false;
  {
    const std::lock_guard l(shownLock);
    const auto it = keyByObject.find(state.get());
    const auto s = it == keyByObject.end() ? shownByKey.end() : shownByKey.find(it->second);
    fresh = s == shownByKey.end() || s->second.doc.expired();
  }
  return keyOf(state.get(), {{}, state, 0, 0}, fresh);
}
}  // namespace pb::thumbs

struct Session::Flights {
  std::mutex lock;
  std::condition_variable done;
  int running = 0;
};

Session::Session(QJSEngine* js) : js_(js), flights_(std::make_shared<Flights>()) {}

Session::~Session() {
  {
    std::unique_lock l(flights_->lock);
    flights_->done.wait(l, [&] { return flights_->running == 0; });
  }
  for (const Save& s : waiting_) try {  // no event loop now: here, one after the other
      pb::save_pbd(*s.state, QFileInfo(s.path).filesystemAbsoluteFilePath());
    } catch (const std::exception& e) {
      qWarning("pb: %s not saved: %s", qPrintable(s.path), e.what());
    }
}

Session* Session::create(QQmlEngine* engine, QJSEngine* js) {
  if (!engine->imageProvider(QStringLiteral("pb"))) engine->addImageProvider(QStringLiteral("pb"), new Thumbs);
  return new Session(js);
}

QString Session::localPath(const QString& file) const {
  const QUrl url(file);
  if (url.isLocalFile()) return QFileInfo(url.toLocalFile()).absoluteFilePath();
  if (file.isEmpty() || url.scheme().size() > 1) return {};  // "C:/x.png" has a scheme of one letter
  return QFileInfo(file).absoluteFilePath();
}

QUrl Session::fileUrl(const QString& path) const { return QUrl::fromLocalFile(path); }

bool Session::canOpen(const QString& file) const {
  const QString path = localPath(file);
  return !path.isEmpty() && QFileInfo(path).isFile() && sniff(path).image;
}

QVariantMap Session::fileInfo(const QString& file) const {
  const QFileInfo f(localPath(file));
  if (!f.isFile()) return {};
  return {{QStringLiteral("bytes"), f.size()}, {QStringLiteral("modified"), f.lastModified().toMSecsSinceEpoch()}};
}

DocModel* Session::open(const QString& file, int viewW, int viewH) {
  const QString path = localPath(file);
  if (path.isEmpty() || !QFileInfo(path).isFile()) return nullptr;
  const Header head = sniff(path);
  if (!head.image) return nullptr;
  const QFileInfo f(path);
  auto* m = new DocModel(js_, f.absoluteFilePath(), f.fileName(), f.size(), head.pbd, head.w, head.h, viewW, viewH, this);
  QQmlEngine::setObjectOwnership(m, QQmlEngine::CppOwnership);  // Session's, until close()
  pb::submit(pb::Priority::Background, [to = QPointer<DocModel>(m), p = f.filesystemAbsoluteFilePath(), pbd = head.pbd] {
    PROFILE_ZONE("session_open");
    pb::DocPtr doc;
    std::shared_ptr<const pb::Preview> preview;
    QString error;
    try {
      if (pbd) {  // its layers as saved; the composite it keeps up until the canvas's first frame
        pb::OpenedPbd o = pb::open_pbd(p);
        doc = std::move(o.doc);
        if (o.preview.width > 0) preview = std::make_shared<const pb::Preview>(std::move(o.preview));
      } else {
        doc = pb::open_image(p);
      }
    } catch (const pb::DecodeError& e) {
      error = why(e);
    } catch (const std::exception& e) {
      error = QString::fromUtf8(e.what());
    }
    // ponytail: the app may be going away as a file is read; the result then goes nowhere
    if (QCoreApplication* app = QCoreApplication::instance())
      QMetaObject::invokeMethod(app, [to, doc, preview, error, pbd] {
        if (!to) return;  // closed meanwhile
        if (doc)
          to->opened(doc, "Open", !pbd, preview);
        else
          to->failed(error);
      }, Qt::QueuedConnection);
  });
  return m;
}

DocModel* Session::duplicate(DocModel* doc, const QString& name) {
  if (!doc || !doc->document()) return nullptr;
  const pb::DocPtr state = doc->current();
  auto* m = new DocModel(js_, QString(), name, 0, false, state->width, state->height, 0, 0, this);
  QQmlEngine::setObjectOwnership(m, QQmlEngine::CppOwnership);
  m->opened(state, "Duplicate", false);
  m->setView(doc->view());
  return m;
}

void Session::close(DocModel* doc) {
  if (doc && doc->parent() == this) doc->deleteLater();
}

// ---- saving ----

bool Session::save(DocModel* doc, const QString& file, bool copy) {
  const QString path = localPath(file);
  if (!doc || !doc->document() || path.isEmpty() || QFileInfo(path).isDir()) return false;
  const pb::Document& history = *doc->document();
  // all the UI thread does: the step's state, shared, and its serial
  Save s{history.state(), history.step(history.current()).serial, QFileInfo(path).absoluteFilePath(), copy, doc};
  if (!saving_) {
    start(std::move(s));
    return true;
  }
  const auto same = std::find_if(waiting_.begin(), waiting_.end(),
                                 [&](const Save& w) { return w.doc == doc && w.path == s.path && w.copy == copy; });
  if (same != waiting_.end())
    *same = std::move(s);
  else
    waiting_.push_back(std::move(s));
  emit savingChanged();
  return true;
}

void Session::start(Save s) {
  cancel_ = pb::make_token();
  saving_ = true;
  running_ = s;
  const double stopAt = std::exchange(cancelAt_, 2.);
  {
    const std::lock_guard l(flights_->lock);
    flights_->running++;
  }
  pb::submit(pb::Priority::Background, [s, stopAt, cancel = cancel_, flights = flights_, self = QPointer<Session>(this),
                                        where = QFileInfo(s.path).filesystemAbsoluteFilePath()] {
    PROFILE_ZONE("session_save");
    QString error;
    bool cancelled = false;
    int shown = -1;  // the percent last told: a hundred posts at most
    pb::SaveOptions o;
    o.cancel = cancel;
    o.progress = [&](double part) {
      if (part >= stopAt) cancel->store(true);
      const int percent = int(part * 100);
      if (percent == shown) return;
      shown = percent;
      if (QCoreApplication* app = QCoreApplication::instance())
        QMetaObject::invokeMethod(app, [self, part] {
          if (self && self->saving_) emit self->saveProgress(part);
        }, Qt::QueuedConnection);
    };
    try {
      pb::save_pbd(*s.state, where, o);
    } catch (const pb::Cancelled&) {
      cancelled = true;
    } catch (const std::exception& e) {
      error = QString::fromUtf8(e.what());
      if (error.isEmpty()) error = QStringLiteral("?");
    }
    if (QCoreApplication* app = QCoreApplication::instance())
      QMetaObject::invokeMethod(app, [self, s, error, cancelled] {
        if (self) self->finish(s, error, cancelled);
      }, Qt::QueuedConnection);
    const std::lock_guard l(flights->lock);
    flights->running--;
    flights->done.notify_all();
  });
  emit savingChanged();
  emit saveStarted(s.doc, s.path, s.copy);
}

void Session::finish(const Save& s, const QString& error, bool cancelled) {
  saving_ = false;
  cancel_.reset();
  running_ = {};
  if (s.doc && error.isEmpty() && !cancelled && !s.copy) s.doc->saved(s.serial, s.path);
  if (waiting_.empty()) emit savingChanged();
  emit saveEnded(s.doc, s.path, error, cancelled, s.copy);
  if (!saving_ && !waiting_.empty()) {  // saveEnded()'s listeners may have started one
    Save next = std::move(waiting_.front());
    waiting_.pop_front();
    start(std::move(next));
  }
}

void Session::cancelSave() {
  if (cancel_) cancel_->store(true);
  if (waiting_.empty()) return;
  waiting_.clear();
  emit savingChanged();
}

QString Session::savingTo(DocModel* doc) const {
  if (saving_ && running_.doc == doc && !running_.copy) return running_.path;
  for (const Save& w : waiting_)
    if (w.doc == doc && !w.copy) return w.path;
  return {};
}

// ---- the tile store, this machine ----

QVariantMap Session::stats() const {
  const pb::TileStats t = pb::tile_stats();
  const auto n = [](size_t v) { return QVariant(double(v)); };
  return {{QStringLiteral("raw"), n(t.raw)},
          {QStringLiteral("compressed"), n(t.compressed)},
          {QStringLiteral("scratch"), n(t.scratch)},
          {QStringLiteral("pinned"), n(t.pinned)},
          {QStringLiteral("charged"), n(t.charged)},
          {QStringLiteral("ram"), n(t.raw + t.compressed + t.charged)},
          {QStringLiteral("budget"), n(pb::tile_budget())}};
}

QVariantMap Session::machine() const {
  const QString temp = QDir::tempPath();
  const QStorageInfo tempDisk(temp);
  // ponytail: file systems that are not disks by their type; a list per OS if one shows up on the page
  static const QByteArray kNotDisks[] = {"tmpfs", "devtmpfs", "ramfs", "squashfs", "overlay", "proc", "sysfs",
                                         "cgroup", "cgroup2", "autofs", "devpts", "fuse.snapfuse"};
  QVariantList disks;
  QList<QByteArray> devices;
  QList<QStorageInfo> volumes = QStorageInfo::mountedVolumes();
  std::stable_sort(volumes.begin(), volumes.end(), [&](const QStorageInfo& a, const QStorageInfo& b) {
    const bool ta = a.rootPath() == tempDisk.rootPath(), tb = b.rootPath() == tempDisk.rootPath();
    return ta != tb ? ta : a.rootPath().size() != b.rootPath().size() ? a.rootPath().size() < b.rootPath().size()
                                                                       : a.rootPath() < b.rootPath();
  });
  for (const QStorageInfo& v : volumes) {
    if (!v.isValid() || !v.isReady() || v.isReadOnly() || v.bytesTotal() < (qint64(1) << 30) ||
        std::find(std::begin(kNotDisks), std::end(kNotDisks), v.fileSystemType()) != std::end(kNotDisks) ||
        devices.contains(v.device()))  // the same disk mounted again further down (WSL's /mnt/wslg/distro)
      continue;
    devices.push_back(v.device());
    QString name = v.rootPath();
    if (name.size() == 3 && name.endsWith(QLatin1String(":/"))) name.chop(1);  // "C:/" → "C:"
    const QString fs = QString::fromUtf8(v.fileSystemType());
    const QString label = v.name();
    const bool hasTemp = v.rootPath() == tempDisk.rootPath();
    disks.push_back(QVariantMap{
        {QStringLiteral("name"), name},
        {QStringLiteral("kind"), label.isEmpty() || label == fs ? fs : label + QStringLiteral(" · ") + fs},
        {QStringLiteral("free"), double(v.bytesAvailable())},
        {QStringLiteral("total"), double(v.bytesTotal())},
        {QStringLiteral("dir"), hasTemp ? temp : QDir(v.rootPath()).filePath(QStringLiteral("photo-beauty-scratch"))}});
  }
  return {{QStringLiteral("ram"), double(pb::physical_memory())},
          {QStringLiteral("temp"), temp},
          {QStringLiteral("disks"), disks}};
}

void Session::findMachine() {
  pb::submit(pb::Priority::Background, [self = QPointer<Session>(this)] {
    if (!self) return;
    QVariantMap m = self->machine();  // touches nothing of Session's
    if (QCoreApplication* app = QCoreApplication::instance())
      QMetaObject::invokeMethod(app, [self, m] {
        if (!self) return;
        self->found_ = m;
        emit self->foundChanged();
      }, Qt::QueuedConnection);
  });
}

void Session::setBudget(double bytes) { pb::set_tile_budget(size_t(std::max(bytes, double(1 << 20)))); }

QString Session::useScratch(const QStringList& dirs) {
  QString use = QDir::tempPath();
  for (const QString& d : dirs)
    if (!d.isEmpty() && QDir().mkpath(d) && QFileInfo(d).isWritable()) {
      use = QFileInfo(d).absoluteFilePath();
      break;
    }
  if (use != scratchDir_) {
    pb::set_scratch_dir(QFileInfo(use).filesystemAbsoluteFilePath());
    scratchDir_ = use;
    emit scratchDirChanged();
  }
  return use;
}

void Session::findGpu() {
  pb::submit(pb::Priority::Background, [self = QPointer<Session>(this)] {
    QVariantMap gpu{{QStringLiteral("found"), false}};
    try {
      if (const auto dev = pb::gpu::Device::create()) {
        gpu[QStringLiteral("found")] = true;
        gpu[QStringLiteral("name")] = QString::fromStdString(dev->name());
        const QString driver = QString::fromStdString(dev->driver());
        gpu[QStringLiteral("api")] = driver.isEmpty() ? QStringLiteral("Vulkan") : QStringLiteral("Vulkan · ") + driver;
      }
    } catch (const std::exception& e) {
      qWarning("pb: no GPU: %s", e.what());
    }
    if (QCoreApplication* app = QCoreApplication::instance())
      QMetaObject::invokeMethod(app, [self, gpu] {
        if (!self) return;
        self->gpu_ = gpu;
        emit self->gpuChanged();
      }, Qt::QueuedConnection);
  });
}
