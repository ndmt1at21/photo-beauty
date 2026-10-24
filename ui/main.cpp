// pb-ui: the window of the PhotoBeauty module (docs/ui-tasks.md).
// copied from snapone 6bdb5c8 (ui/main.cpp: the flags, --screenshot) and 6ff61f0 (its Splash), without its catalog
// and core.
#include <QCommandLineParser>
#include <QDeadlineTimer>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QJsonDocument>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QRasterWindow>
#include <QScreen>
#include <QSettings>
#include <QTimer>
#include <QtQml/qqmlextensionplugin.h>

#include "brand/splash.h"
#include "look.h"

Q_IMPORT_QML_PLUGIN(PhotoBeautyPlugin)

// The start screen (ui/brand/splash.h, artboard Splash) while QML loads, closed on the window's first frame, as
// snapone's N10. A raster window: up before Qt Quick is.
class Splash : public QRasterWindow {
 public:
  void step(qreal to) {  // paints now: the next step blocks the event loop
    if (!isVisible()) return;
    done_ = to;
    update();
    QEvent e(QEvent::UpdateRequest);
    QCoreApplication::sendEvent(this, &e);
  }

 protected:
  void paintEvent(QPaintEvent*) override {
    QPainter p(this);
    p.setCompositionMode(QPainter::CompositionMode_Source);  // the corners outside the rounded box
    p.fillRect(QRect(QPoint(), size()), Qt::transparent);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    paintStart(p, size(), done_);
  }

 private:
  qreal done_ = 0.1;
};

int main(int argc, char* argv[]) {
  // Screenshots render offscreen, in the software scene graph as the tests and CI, not in a window on the desktop.
  for (int i = 1; i < argc; i++) {
    const char* a = argv[i];
    while (*a == '-') a++;
    if ((!qstrncmp(a, "screenshot", 10) || !qstrncmp(a, "start-screen", 12)) && !qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
      qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  // Qt 6.11.3's incremental JS GC can sweep a VariantAssociationObject (a QVariantMap seen from JS) whose memory another
  // object holds: SIGSEGV, SEGV_ACCERR in ~QVariantHash at libQt6Qml+0x2cd54b under load (U23d). The whole GC at once
  // instead, as before Qt 6.8. ponytail: longer GC pauses; drop this once Qt has fixed it.
  if (!qEnvironmentVariableIsSet("QV4_GC_TIMELIMIT")) qputenv("QV4_GC_TIMELIMIT", "0");
  QGuiApplication app(argc, argv);
  app.setApplicationName("photo-beauty");
  app.setOrganizationName("photo-beauty");
  app.setApplicationVersion(PB_VERSION);
  // U26 (C3c, G1): Thiết lập › Hiệu năng › Độ trễ thấp, Prefs' lowLatency kept by the last start: Qt Quick presents
  // without waiting for vsync. Vulkan takes MAILBOX where the driver has it, else IMMEDIATE (the Iris Xe has no MAILBOX:
  // may tear in full screen); D3D11 presents at interval 0. Qt turns vsync off for any value of QSG_NO_VSYNC, read when
  // the first window is made; one set before wins.
  if (!qEnvironmentVariableIsSet("QSG_NO_VSYNC") &&
      QJsonDocument::fromJson(QSettings().value("prefs/json").toByteArray())[u"lowLatency"].toBool())
    qputenv("QSG_NO_VSYNC", "1");
#ifndef Q_OS_WIN  // Windows windows take the exe's icon, ui/brand/photo-beauty.ico (U22)
  app.setWindowIcon(QIcon(QStringLiteral(":/qt/qml/PhotoBeauty/brand/icon.png")));
#endif
  applyLook();

  QCommandLineParser args;
  const QCommandLineOption shot("screenshot", "Save the window as PNG once it has rendered, then quit (offscreen).", "file");
  const QCommandLineOption settle("settle", "With --screenshot: ms to wait after the first frame.", "ms", "400");
  const QCommandLineOption size("size", "Window size.", "WxH");
  const QCommandLineOption theme("theme", "dark or light; else the one kept from the last start.", "name");
  const QCommandLineOption state("state", "The prototype's artboard to open: Main, Start, Retouch, a dialog or a space.", "name");
  const QCommandLineOption imports("import", "QML import folder searched first, as pb-ui-tests' (build/ui/ui/live: QML from the sources).", "dir");
  const QCommandLineOption startScreen("start-screen", "Save the start screen as PNG at --size (else 560x340), then quit.", "file");
  args.addOptions({shot, settle, size, theme, state, imports, startScreen});
  args.addPositionalArgument("files", "Files to open: JPEG, PNG, TIFF (N1).", "[files...]");
  args.addHelpOption();
  args.addVersionOption();
  args.setSingleDashWordOptionMode(QCommandLineParser::ParseAsLongOptions);  // -import as pb-ui-tests takes it
  args.process(app);
  const QStringList wh = args.value(size).split('x');
  const int w = wh.value(0).toInt(), h = wh.value(1).toInt();
  if (args.isSet(size) && (wh.size() != 2 || w <= 0 || h <= 0)) {
    qCritical("--size: WxH, as 1440x900");
    return 2;
  }
  if (args.isSet(theme) && args.value(theme) != u"dark" && args.value(theme) != u"light") {
    qCritical("--theme: dark or light");
    return 2;
  }
  if (args.isSet(startScreen)) {  // ui/brand/splash.h, to compare with the Splash artboard
    QImage image(args.isSet(size) ? QSize(w, h) : QSize(560, 340), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter p(&image);
    paintStart(p, image.size(), 0.46);
    p.end();
    const QString file = args.value(startScreen);
    QDir().mkpath(QFileInfo(file).absolutePath());
    return image.save(file) ? 0 : 2;
  }

  Splash splash;
  if (!args.isSet(shot) && app.platformName() != QLatin1String("offscreen")) {
    QSurfaceFormat format = splash.format();
    format.setAlphaBufferSize(8);
    splash.setFormat(format);
    splash.setFlags(Qt::SplashScreen | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    QRect r(0, 0, 560, 340);
    r.moveCenter(splash.screen()->availableGeometry().center());
    splash.setGeometry(r);
    splash.show();
    for (QDeadlineTimer wait(500); !splash.isExposed() && !wait.hasExpired();) app.processEvents();
    splash.step(0.1);
  }

  QQmlApplicationEngine engine;
  for (const QString& dir : args.values(imports)) engine.addImportPath(dir);
  QVariantMap start{{"startState", args.value(state)},
                    {"startTheme", args.value(theme)},  // "" without --theme
                    {"persist", !args.isSet(shot)},     // screenshots start from the defaults
                    {"startFiles", args.positionalArguments()},  // opened once the window is up
                    {"visible", false}};                // shown once set up
  if (args.isSet(size)) {
    start["width"] = w;
    start["height"] = h;
  }
  engine.setInitialProperties(start);
  engine.loadFromModule("PhotoBeauty", "Main");
  auto* win = engine.rootObjects().isEmpty() ? nullptr : qobject_cast<QQuickWindow*>(engine.rootObjects().first());
  if (!win) return 1;
  splash.step(0.9);
  QObject::connect(win, &QQuickWindow::frameSwapped, &splash, &QWindow::close, Qt::SingleShotConnection);
  QTimer::singleShot(3000, &splash, &QWindow::close);  // no frame comes when the window starts minimized
  win->show();
  if (args.isSet(shot)) {
    const QString file = args.value(shot);
    QDir().mkpath(QFileInfo(file).absolutePath());
    // ponytail: fixed settle time for async images and layout, wait on Image.status if shots flake
    QObject::connect(win, &QQuickWindow::frameSwapped, &app, [&app, win, file, ms = args.value(settle).toInt()] {
      QTimer::singleShot(ms, &app, [&app, win, file] { app.exit(win->grabWindow().save(file) ? 0 : 2); });
    }, Qt::SingleShotConnection);
  }
  return app.exec();
}
