// pb-ui: the window of the PhotoBeauty module (docs/ui-tasks.md).
// copied from snapone 6bdb5c8 (ui/main.cpp: the flags, --screenshot), without its catalog, splash and core.
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QTimer>
#include <QtQml/qqmlextensionplugin.h>

#include "look.h"

Q_IMPORT_QML_PLUGIN(PhotoBeautyPlugin)

int main(int argc, char* argv[]) {
  // Screenshots render offscreen, in the software scene graph as the tests and CI, not in a window on the desktop.
  for (int i = 1; i < argc; i++) {
    const char* a = argv[i];
    while (*a == '-') a++;
    if (!qstrncmp(a, "screenshot", 10) && !qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
  }
  QGuiApplication app(argc, argv);
  app.setApplicationName("photo-beauty");
  app.setOrganizationName("photo-beauty");
  app.setApplicationVersion(PB_VERSION);
  applyLook();

  QCommandLineParser args;
  const QCommandLineOption shot("screenshot", "Save the window as PNG once it has rendered, then quit (offscreen).", "file");
  const QCommandLineOption settle("settle", "With --screenshot: ms to wait after the first frame.", "ms", "400");
  const QCommandLineOption size("size", "Window size.", "WxH");
  const QCommandLineOption theme("theme", "dark or light; else the one kept from the last start.", "name");
  const QCommandLineOption state("state", "The prototype's artboard to open: Main, Start, Retouch, a dialog or a space.", "name");
  const QCommandLineOption imports("import", "QML import folder searched first, as pb-ui-tests' (build/ui/ui/live: QML from the sources).", "dir");
  args.addOptions({shot, settle, size, theme, state, imports});
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

  QQmlApplicationEngine engine;
  for (const QString& dir : args.values(imports)) engine.addImportPath(dir);
  QVariantMap start{{"startState", args.value(state)},
                    {"startTheme", args.value(theme)},  // "" without --theme
                    {"persist", !args.isSet(shot)},     // screenshots start from the defaults
                    {"visible", false}};                // shown once set up
  if (args.isSet(size)) {
    start["width"] = w;
    start["height"] = h;
  }
  engine.setInitialProperties(start);
  engine.loadFromModule("PhotoBeauty", "Main");
  auto* win = engine.rootObjects().isEmpty() ? nullptr : qobject_cast<QQuickWindow*>(engine.rootObjects().first());
  if (!win) return 1;
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
