// pb-ui: the window of the PhotoBeauty module (docs/ui-tasks.md).
// copied from snapone 6bdb5c8 (ui/main.cpp: the flags, --screenshot) and 6ff61f0 (its Splash), without its catalog
// and core; the language (U30) as snapone 3891d67's.
#include <cstdio>

#include <QCommandLineParser>
#include <QDeadlineTimer>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QJsonDocument>
#include <QLocale>
#include <QMouseEvent>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRasterWindow>
#include <QScreen>
#include <QSettings>
#include <QTimer>
#include <QTranslator>
#include <QtQml/qqmlextensionplugin.h>

#include "brand/splash.h"
#include "look.h"
#include "pen.h"

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

// The UI's language (U30, snapone's decision 10), vi or en: `chosen` (--lang) when set; else, unless a screenshot (from
// the defaults, so in Vietnamese, the language of the sources), Thiết lập's (Prefs' language as the last start kept it:
// 0 Tiếng Việt, 1 English); else the system's first of the two; else English.
static QString uiLanguage(const QString& chosen, bool shot) {
  if (!chosen.isEmpty() || shot) return chosen.isEmpty() ? QStringLiteral("vi") : chosen;
  const QJsonValue kept = QJsonDocument::fromJson(QSettings().value("prefs/json").toByteArray())[u"language"];
  if (kept.isDouble() && (kept.toDouble() == 0 || kept.toDouble() == 1))
    return kept.toDouble() == 1 ? QStringLiteral("en") : QStringLiteral("vi");
  for (const QString& l : QLocale::system().uiLanguages()) {  // "vi-VN", "en-US", "fr-FR"…
    const QString base = l.section('-', 0, 0);
    if (base == u"vi" || base == u"en") return base;
  }
  return QStringLiteral("en");
}

// The texts shown in `item` and the items in it that are cut short (Text.truncated: elided) or, not elided, run over the
// sides of a control around them (a button, a field, a popup), for --truncated (U30).
static void cutTexts(QQuickItem* item, QStringList& cut, QStringList& over, QList<QQuickItem*> boxes = {}) {
  if (!item->isVisible() || item->opacity() == 0) return;
  const QString text = item->inherits("QQuickText") ? item->property("text").toString() : QString();
  if (!text.isEmpty() && item->property("truncated").toBool()) {
    cut << text;
  } else if (!text.isEmpty()) {
    for (QQuickItem* box : boxes) {
      const QRectF r = item->mapRectToItem(box, item->boundingRect());
      if (r.left() < -1 || r.right() > box->width() + 1) {
        over << text;
        break;
      }
    }
  }
  if (item->inherits("QQuickControl")) boxes << item;
  for (QQuickItem* c : item->childItems()) cutTexts(c, cut, over, boxes);
}

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
  // I30: Windows Ink or Wintab, Thiết lập › Bút's as the last start kept it (not with --screenshot), before the
  // application, whose platform takes the tablet in (pen.h)
  usePen(argc, argv);
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
  const QCommandLineOption lang("lang", "UI language, vi or en; else Thiết lập's, else the system's (U30).", "code");
  QCommandLineOption whichLang("which-lang", "Print the UI language this start takes, then quit, 1 when its strings do not load (the i18n test).");
  QCommandLineOption click("click", "With --screenshot: a click at x,y of the window once it is up, as on a menu's title.", "x,y");
  QCommandLineOption truncated("truncated", "With --screenshot: print the texts shown cut short (Text.truncated) or over their control's sides.");
  for (QCommandLineOption* o : {&whichLang, &click, &truncated}) o->setFlags(QCommandLineOption::HiddenFromHelp);
  args.addOptions({shot, settle, size, theme, state, imports, startScreen, lang, whichLang, click, truncated});
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
  if (args.isSet(lang) && args.value(lang) != u"vi" && args.value(lang) != u"en") {
    qCritical("--lang: vi or en");
    return 2;
  }
  if (args.isSet(click) && args.value(click).split(',').size() != 2) {
    qCritical("--click: x,y");
    return 2;
  }
  // The language before anything shows text: English from :/i18n/pb_en.qm (ui/i18n/pb_en.ts), Vietnamese the sources'
  // own; QLocale's default and Qt.uiLanguage (Fmt.js writes numbers as it says) follow it.
  const QString ui = uiLanguage(args.value(lang), args.isSet(shot));
  QTranslator strings;
  const bool translated = ui == u"vi" || (strings.load(":/i18n/pb_" + ui) && app.installTranslator(&strings));
  if (args.isSet(whichLang)) {  // the language, its strings loaded (else exit 1)
    std::fputs(qPrintable(ui + '\n'), stdout);
    return translated ? 0 : 1;
  }
  QLocale::setDefault(ui == u"vi" ? QLocale(QLocale::Vietnamese, QLocale::Vietnam) : QLocale(QLocale::English, QLocale::UnitedStates));
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
  engine.setUiLanguage(ui);  // Qt.uiLanguage
  QVariantMap start{{"startState", args.value(state)},
                    {"startTheme", args.value(theme)},  // "" without --theme
                    {"persist", !args.isSet(shot)},     // screenshots start from the defaults
                    {"machine", !args.isSet(shot)},     // this machine's numbers, budget, scratch (N2); not in screenshots
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
    const QStringList at = args.value(click).split(',');
    QObject::connect(win, &QQuickWindow::frameSwapped, &app, [&app, win, file, ms = args.value(settle).toInt(), at,
                                                              cut = args.isSet(truncated)] {
      if (at.size() == 2) {  // --click: pressed and let go where it says (U30: a menu open in any language)
        const QPointF p(at[0].toDouble(), at[1].toDouble());
        for (const QEvent::Type t : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease}) {
          QMouseEvent e(t, p, win->mapToGlobal(p), Qt::LeftButton, t == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton,
                        Qt::NoModifier);
          QCoreApplication::sendEvent(win, &e);
        }
      }
      QTimer::singleShot(ms, &app, [&app, win, file, cut] {
        if (cut) {  // --truncated: their counts, then one line each
          QStringList texts, over;
          cutTexts(win->contentItem(), texts, over);
          QString out = QStringLiteral("truncated: %1, over: %2\n").arg(texts.size()).arg(over.size());
          for (const QString& t : texts) out += "cut: " + t + '\n';
          for (const QString& t : over) out += "over: " + t + '\n';
          std::fputs(qPrintable(out), stdout);
        }
        app.exit(win->grabWindow().save(file) ? 0 : 2);
      });
    }, Qt::SingleShotConnection);
  }
  return app.exec();
}
