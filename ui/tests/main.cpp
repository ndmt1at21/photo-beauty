// Runs tests/tst_*.qml against the PhotoBeauty module (docs/ui-tasks.md), offscreen unless QT_QPA_PLATFORM
// says otherwise. copied from snapone 6bdb5c8 (ui/tests/main.cpp: Setup, main), without its Tests object.
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageWriter>
#include <QQmlContext>
#include <QQmlEngine>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>
#include <QtEndian>
#include <QtQml/qqmlextensionplugin.h>
#include <QtQuickTest>

#include "../look.h"

Q_IMPORT_QML_PLUGIN(PhotoBeautyPlugin)

static QVariant gcLimitFound;  // QV4_GC_TIMELIMIT as the environment had it, null when unset (tst_u23d)

// Image files for tests of files opened for real (tst_n1, tst_n2, tst_u27), in a folder of their own that goes with the
// process, pb-ui-tests-XXXXXX in the system's temporary folder: small ones made here, none in git. A write that fails
// says why in a warning: on Windows a run of the whole suite once found no file made, for no reason it gave (q2).
class Fixtures : public QObject {
  Q_OBJECT
 public:
  // A w x h image of `kind` "png", "jpg" or "tif" at 8 or 16 bits (JPEG: 8), grey or RGB, with alpha or not (JPEG:
  // without), its path ("" when it cannot be written, a warning saying why): R = 255 x / w, G = 255 y / h, B = (x + y)
  // mod 256 (grey: R), alpha 255 on the left half, 128 on the right; at 16 bits each sample times 257. "broken": a PNG
  // cut short.
  Q_INVOKABLE QString image(const QString& name, const QString& kind, int w, int h, int bits, bool gray, bool alpha) {
    const QString path = dir_.filePath(name);
    if (!ready(path)) return {};
    QImage img(w, h, bits == 16 ? QImage::Format_RGBA64 : QImage::Format_RGBA8888);
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++) {
        const int r = 255 * x / std::max(1, w), g = gray ? r : 255 * y / std::max(1, h), b = gray ? r : (x + y) & 255;
        img.setPixelColor(x, y, QColor(r, g, b, alpha ? (x < w / 2 ? 255 : 128) : 255));
      }
    QString why;
    if (kind == u"tif") return tiff(path, img, bits, gray, alpha, &why) ? path : failed(path, why);
    QImage out = img;
    if (gray) out = img.convertToFormat(bits == 16 ? QImage::Format_Grayscale16 : QImage::Format_Grayscale8);
    else if (!alpha) out = img.convertToFormat(bits == 16 ? QImage::Format_RGBX64 : QImage::Format_RGB888);
    if (kind == u"broken") {
      QByteArray png;
      QBuffer buf(&png);
      buf.open(QIODevice::WriteOnly);
      out.save(&buf, "png");
      return written(path, png.left(png.size() / 2), &why) ? path : failed(path, why);
    }
    QImageWriter writer(path, kind == u"jpg" ? "jpg" : "png");
    if (kind == u"jpg") writer.setQuality(95);
    return writer.write(out) ? path : failed(path, writer.errorString());
  }
  Q_INVOKABLE QString folder() const { return dir_.path(); }
  // A local file's file: URL, as a drop or Mở… gives it: "file://" + a path is not one on Windows, where C: is its host.
  Q_INVOKABLE QUrl url(const QString& path) const { return QUrl::fromLocalFile(path); }

 private:
  // The file's folder made before it is written; the process's own folder made again, and said so, when it has gone.
  bool ready(const QString& path) {
    if (!dir_.isValid()) {
      qWarning("fixtures: no folder of its own: %s", qPrintable(dir_.errorString()));
      return false;
    }
    if (path.isEmpty()) return false;  // an absolute name, which QTemporaryDir::filePath() has warned of
    const bool gone = !QFileInfo(dir_.path()).isDir();
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
      failed(path, QStringLiteral("its folder cannot be made"));
      return false;
    }
    if (gone) qWarning("fixtures: %s had gone, made again", qPrintable(QDir::toNativeSeparators(dir_.path())));
    return true;
  }
  // A warning: why `path` was not written, and whether the folder is still there. "", image()'s answer then.
  QString failed(const QString& path, const QString& why) const {
    const QFileInfo dir(dir_.path());
    qWarning("fixtures: %s not written: %s; the folder %s", qPrintable(QDir::toNativeSeparators(path)), qPrintable(why),
             !dir.isDir() ? "has gone" : dir.isWritable() ? "is there, writable" : "is there, not writable");
    return {};
  }
  static bool written(const QString& path, const QByteArray& bytes, QString* why) {
    QFile f(path);
    if (f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size() && f.flush()) return true;
    *why = f.errorString();
    return false;
  }
  // A baseline TIFF, little-endian, one strip, samples interleaved; alpha unassociated (ExtraSamples 2).
  static bool tiff(const QString& path, const QImage& rgba, int bits, bool gray, bool alpha, QString* why) {
    const int w = rgba.width(), h = rgba.height(), spp = (gray ? 1 : 3) + (alpha ? 1 : 0), bps = bits == 16 ? 2 : 1;
    QByteArray data;
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++) {
        const QColor c = rgba.pixelColor(x, y);
        const int v[4] = {c.red(), c.green(), c.blue(), c.alpha()};
        for (int k = 0; k < spp; k++) {
          const int s = gray ? (k == 0 ? v[0] : v[3]) : v[k];
          if (bps == 2) {
            const quint16 x16 = quint16(s * 257);
            data.append(char(x16 & 255)).append(char(x16 >> 8));
          } else {
            data.append(char(s));
          }
        }
      }
    struct Tag { quint16 tag, type; quint32 count, value; };
    QByteArray extra;  // BitsPerSample's array, after the IFD
    const int entries = alpha ? 11 : 10;
    const quint32 ifd = 8, after = ifd + 2 + entries * 12 + 4, bitsAt = after, dataAt = after + (spp > 2 ? 2 * spp : 0);
    for (int k = 0; k < spp; k++) extra.append(char(bits)).append(char(0));
    std::vector<Tag> tags = {{256, 4, 1, quint32(w)}, {257, 4, 1, quint32(h)},
                             {258, 3, quint32(spp), spp > 2 ? bitsAt : quint32(bits) | (spp == 2 ? quint32(bits) << 16 : 0)},
                             {259, 3, 1, 1}, {262, 3, 1, gray ? 1u : 2u}, {273, 4, 1, dataAt}, {277, 3, 1, quint32(spp)},
                             {278, 4, 1, quint32(h)}, {279, 4, 1, quint32(data.size())}, {284, 3, 1, 1}};
    if (alpha) tags.push_back({338, 3, 1, 2});
    QByteArray file("II*\0", 4);
    const auto u16 = [&](quint16 v) { file.append(char(v & 255)).append(char(v >> 8)); };
    const auto u32 = [&](quint32 v) { u16(quint16(v & 0xffff)), u16(quint16(v >> 16)); };
    u32(ifd);
    u16(quint16(tags.size()));
    for (const Tag& t : tags) {
      u16(t.tag), u16(t.type), u32(t.count);
      if (t.type == 3 && t.count == 1) u16(quint16(t.value)), u16(0);
      else u32(t.value);
    }
    u32(0);
    if (spp > 2) file.append(extra);
    file.append(data);
    return written(path, file, why);
  }
  // Named for this program: Qt's default, qt_temp-XXXXXX (no application yet), is every Qt program's.
  QTemporaryDir dir_{QDir::tempPath() + QStringLiteral("/pb-ui-tests-XXXXXX")};
};

class Setup : public QObject {
  Q_OBJECT
 public slots:
  void applicationAvailable() {
    applyLook();
    // Main's QtCore Settings (with persist) write to ~/.qttest, not the app's own file.
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName("photo-beauty");
  }
  void qmlEngineAvailable(QQmlEngine* engine) {
    engine->rootContext()->setContextProperty("fixtures", &fixtures);
    engine->rootContext()->setContextProperty("gcLimitFound", gcLimitFound);
    engine->rootContext()->setContextProperty("gcLimit", qEnvironmentVariable("QV4_GC_TIMELIMIT"));
  }

 private:
  Fixtures fixtures;
};

int main(int argc, char** argv) {
  if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
  if (qEnvironmentVariableIsSet("QV4_GC_TIMELIMIT")) gcLimitFound = qEnvironmentVariable("QV4_GC_TIMELIMIT");
  // As ui/main.cpp: the whole JS GC at once, Qt 6.11.3's incremental one crashes under load (U23d).
  if (!qEnvironmentVariableIsSet("QV4_GC_TIMELIMIT")) qputenv("QV4_GC_TIMELIMIT", "0");
  QTEST_SET_MAIN_SOURCE_PATH
  Setup setup;
  return quick_test_main_with_setup(argc, argv, "pb_ui", QUICK_TEST_SOURCE_DIR, &setup);
}
#include "main.moc"
