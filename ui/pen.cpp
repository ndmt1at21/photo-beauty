// I30: the pen on Windows (Windows Ink or Wintab, Windows' feedback off), the end of the pen in use, the first dab's
// pressure, the test pen (pen.h).
#include "pen.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonValue>
#include <QLoggingCategory>
#include <QMouseEvent>
#include <QPlatformSurfaceEvent>
#include <QPointer>
#include <QPointingDevice>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSettings>
#include <QTabletEvent>
#include <QWindow>
// QPA, for send(): input handed over as a platform does (as canvas.cpp). ponytail: the path names Qt's version.
#include <QtGui/6.11.3/QtGui/qpa/qwindowsysteminterface.h>
#ifdef Q_OS_WIN
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
// Qt's private QWindowsApplication (Qt6::GuiPrivate, linked on Windows alone): Wintab is in no option of its windows
// plugin. Private API: a Qt update may change it, and this then no longer builds; check it with each Qt.
#include <QtGui/private/qguiapplication_p.h>
#endif

#include <algorithm>
#include <chrono>
#include <utility>
#include <vector>

// QT_LOGGING_RULES: "pb.pen.info=true" the way in, the pens, the ends; "pb.pen=true" each tablet event too
Q_LOGGING_CATEGORY(lcPen, "pb.pen", QtWarningMsg)

namespace {

#ifdef Q_OS_WIN
// The pen's feedback Windows draws on a window: the barrel button's, the tap's and the double tap's ripples, press and
// hold's ring (the right button's), the right tap's.
const std::pair<const char*, FEEDBACK_TYPE> kFeedback[] = {
    {"barrel", FEEDBACK_PEN_BARRELVISUALIZATION}, {"tap", FEEDBACK_PEN_TAP},           {"doubleTap", FEEDBACK_PEN_DOUBLETAP},
    {"pressAndHold", FEEDBACK_PEN_PRESSANDHOLD},  {"rightTap", FEEDBACK_PEN_RIGHTTAP}};
#endif

// The app's pen, one for every engine's Pen: an event filter on the application, from its constructor on.
struct Hub : QObject {
  QString asked;  // usePen()'s
  QString api;    // Pen.api
  bool eraser = false;
  bool firstPressure = qgetenv("PB_PEN_FIRST_PRESSURE") != "0";
  // With Wintab beside Windows Ink: Wintab has had the pen; it is near (in proximity), last there (global, logical).
  bool wintabHas = false, wintabNear = false;
  QPointF wintabAt;
  std::vector<const QInputDevice*> known;  // the pens logged
  std::vector<QPointer<Pen>> pens;         // every engine's
  bool probing = false;                    // send()'s question to Qt, not a pen of the user's
  quint64 moused = 0;                      // the timestamp of the last mouse event a pen's tablet event made

  template <typename F>
  void each(F f) {
    for (const QPointer<Pen>& p : pens)
      if (p) f(p.data());
  }
  void setApi(const QString& a) {
    wintabHas = wintabNear = false;
    if (a == api) return;
    api = a;
    qCInfo(lcPen, "the pen through %s", a.isEmpty() ? "Qt's own way" : qPrintable(a));
    each([](Pen* p) { emit p->apiChanged(); });
  }
  void setEraser(bool e) {
    if (e == eraser) return;
    eraser = e;
    qCInfo(lcPen, "the pen's %s end", e ? "eraser" : "tip");
    each([](Pen* p) { emit p->eraserChanged(); });
  }
#ifdef Q_OS_WIN
  // Wintab opened or closed: Qt's QWindowsApplication::setWinTabEnabled(), which opens a Wintab context when the
  // tablet's driver gives wintab32.dll.
  void useWintab(bool on) {
    using QNativeInterface::Private::QWindowsApplication;
    auto* a = qGuiApp ? qGuiApp->nativeInterface<QWindowsApplication>() : nullptr;
    if (a) a->setWinTabEnabled(on);
    const bool is = a && a->isWinTabEnabled();
    if (on && !is) qCWarning(lcPen, "Wintab: none here (no Wintab driver, wintab32.dll); the pen through Windows Ink");
    setApi(is ? QStringLiteral("wintab") : QStringLiteral("ink"));
  }
#endif
  bool eventFilter(QObject* o, QEvent* e) override;
};

Hub& hub() {
  static Hub h;
  return h;
}

bool Hub::eventFilter([[maybe_unused]] QObject* o, QEvent* e) {
  switch (e->type()) {
#ifdef Q_OS_WIN
    case QEvent::PlatformSurface:  // a native window made: Windows' pen feedback off on it
      if (static_cast<QPlatformSurfaceEvent*>(e)->surfaceEventType() == QPlatformSurfaceEvent::SurfaceCreated &&
          o->isWindowType() && QGuiApplication::platformName() == u"windows") {
        const BOOL off = FALSE;
        for (const auto& [name, type] : kFeedback)
          if (!SetWindowFeedbackSetting(HWND(static_cast<QWindow*>(o)->winId()), type, 0, sizeof(off), &off))
            qCWarning(lcPen, "SetWindowFeedbackSetting(%s): error %lu", name, GetLastError());
      }
      break;
#endif
    case QEvent::TabletEnterProximity:
    case QEvent::TabletLeaveProximity:
    case QEvent::TabletPress:
    case QEvent::TabletMove:
    case QEvent::TabletRelease: {
      auto* t = static_cast<QTabletEvent*>(e);
      const QPointingDevice* d = t->pointingDevice();
      if (probing || !d) break;
      qCDebug(lcPen) << t->type() << d->name() << t->pointerType() << "pressure" << t->pressure() << "tilt" << t->xTilt()
                     << t->yTilt() << "rotation" << t->rotation() << "buttons" << t->buttons() << t->globalPosition();
      if (std::find(known.begin(), known.end(), d) == known.end()) {
        known.push_back(d);
        qCInfo(lcPen) << "a pen:" << d->name() << d->type() << d->pointerType() << d->capabilities();
      }
      if (api == u"wintab") {
        if (d->name() != u"wmpointer") {  // Wintab's (Windows Ink's devices are Qt's "wmpointer")
          wintabHas = true;
          wintabNear = e->type() != QEvent::TabletLeaveProximity;
          wintabAt = t->globalPosition();
        } else if (wintabHas) {  // Windows Ink's of the pen Wintab has: kept out, no mouse event made of it either
          e->accept();
          return true;
        }
      }
      // the end in use: as it comes near, before its press reaches the canvas (Viewport turns the tool by it)
      if (t->pointerType() == QPointingDevice::PointerType::Eraser || t->pointerType() == QPointingDevice::PointerType::Pen)
        setEraser(t->pointerType() == QPointingDevice::PointerType::Eraser);
      break;
    }
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick:
    case QEvent::MouseMove: {
      const auto* m = static_cast<const QMouseEvent*>(e);
      const QInputDevice* d = m->device();
      if (d && d->type() != QInputDevice::DeviceType::Mouse) moused = m->timestamp();
      // Wintab: the system's mouse made of the pen near it (a driver's, Windows Ink off) beside Qt's of Wintab's events;
      // a mouse away from it means the pen has gone
      if (api == u"wintab" && wintabNear && d && d->type() == QInputDevice::DeviceType::Mouse) {
        if ((m->globalPosition() - wintabAt).manhattanLength() <= 8) {
          e->accept();
          return true;
        }
        wintabNear = false;
      }
      break;
    }
    default:
      break;
  }
  return false;
}

// In the QGuiApplication constructor, its platform up, before any window.
void startPen() {
  Hub& h = hub();
  QCoreApplication::instance()->installEventFilter(&h);
#ifdef Q_OS_WIN
  if (QGuiApplication::platformName() == u"windows") {
    h.setApi(QStringLiteral("ink"));
    if (h.asked == u"wintab") h.useWintab(true);
  }
#endif
}
Q_COREAPP_STARTUP_FUNCTION(startPen)

// send()'s: the clock of its events (ms), never the same twice.
ulong msNow() {
  static ulong last = 0;
  const auto ms = ulong(std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count());
  return last = std::max(ms, last + 1);
}

// send()'s pens: Windows Ink's ("wmpointer", as Qt names its devices) or Wintab's, the tip or the eraser; another for
// probing. UI thread.
const QPointingDevice* testPen(int kind, bool eraser) {
  static const QPointingDevice* made[3][2] = {};
  const QPointingDevice*& d = made[kind][eraser];
  if (!d) {
    using C = QInputDevice::Capability;
    const QString name = kind == 0 ? QStringLiteral("wmpointer") : kind == 1 ? QStringLiteral("wintab") : QStringLiteral("pb probe");
    const qint64 id = 0x7065 + 2 * kind + eraser;
    auto* p = new QPointingDevice(name, id, QInputDevice::DeviceType::Stylus,
                                  eraser ? QPointingDevice::PointerType::Eraser : QPointingDevice::PointerType::Pen,
                                  C::Position | C::Pressure | C::XTilt | C::YTilt | C::Rotation | C::Hover | C::MouseEmulation,
                                  1, 3, QString(), QPointingDeviceUniqueId::fromNumericId(id));
    QWindowSystemInterface::registerInputDevice(p);
    d = p;
  }
  return d;
}

// A window's logical point as its platform has it: logical x dpr, as on Windows, X11 and offscreen (canvas.cpp's).
QPointF native(const QWindow* w, QPointF p) { return p * w->devicePixelRatio(); }

// Does Qt make mouse events of the tablet events nothing takes, here (QGuiApplication, where the platform says so:
// Windows, X11), or not (offscreen)? Asked once: a pen's hover handed over off the window's items.
bool qtMakesMice(QWindow* w) {
  static int known = -1;
  if (known < 0) {
    Hub& h = hub();
    const ulong t = msNow();
    h.probing = true;
    QWindowSystemInterface::handleTabletEvent(w, t, testPen(2, false), {-1, -1}, w->mapToGlobal(QPointF(-1, -1)),
                                              Qt::NoButton, 0, 0, 0, 0, 0, 0);
    QWindowSystemInterface::flushWindowSystemEvents();
    h.probing = false;
    known = h.moused == t;
  }
  return known;
}

// Whether a window's items take the tablet event it gets (QGuiApplication makes a mouse event of one they leave, where
// it does): the window's first filter, installed last, sends it on through the others (DocCanvas's) to the window, then
// reads it, which nothing else does after it.
struct Watch : QObject {
  bool seen = false, took = false;
  bool eventFilter(QObject* o, QEvent* e) override {
    if (seen || (e->type() != QEvent::TabletPress && e->type() != QEvent::TabletMove && e->type() != QEvent::TabletRelease))
      return false;
    seen = true;
    o->removeEventFilter(this);
    QCoreApplication::sendEvent(o, e);  // the application's filters see it again (Hub's: the same once more)
    took = e->isAccepted();
    return true;
  }
};

}  // namespace

void usePen(int argc, char** argv) {
#ifdef Q_OS_WIN
  bool shot = false;
  for (int i = 1; i < argc; i++) {
    const char* a = argv[i];
    while (*a == '-') a++;
    shot = shot || !qstrncmp(a, "screenshot", 10);
  }
  QString api = qEnvironmentVariable("PB_TABLET");
  if (api != u"ink" && api != u"wintab") {
    // QSettings before the application: its names first (main.cpp gives them again)
    QCoreApplication::setOrganizationName(QStringLiteral("photo-beauty"));
    QCoreApplication::setApplicationName(QStringLiteral("photo-beauty"));
    api = shot ? QStringLiteral("ink") : tabletApiOf(QSettings().value("prefs/json").toByteArray());
  }
  hub().asked = api;
#else
  Q_UNUSED(argc);
  Q_UNUSED(argv);
#endif
}

QString tabletApiOf(const QByteArray& prefsJson) {
  const QJsonValue v = QJsonDocument::fromJson(prefsJson)[u"tabletApi"];
  return v.isDouble() && v.toDouble() == 1 ? QStringLiteral("wintab") : QStringLiteral("ink");
}

bool penFirstPressure() { return hub().firstPressure; }

Pen::Pen(QObject* parent) : QObject(parent) { hub().pens.emplace_back(this); }

QString Pen::api() const { return hub().api; }

void Pen::setApi(const QString& a) {
#ifdef Q_OS_WIN
  if (QGuiApplication::platformName() == u"windows") return hub().useWintab(a == u"wintab");
#endif
  hub().setApi(a == u"wintab" || a == u"ink" ? a : QString());  // tests: what the filter keeps out, alone
}

QString Pen::asked() const { return hub().asked; }

bool Pen::eraser() const { return hub().eraser; }

void Pen::setFirstPressure(bool on) {
  Hub& h = hub();
  if (on == h.firstPressure) return;
  h.firstPressure = on;
  h.each([](Pen* p) { emit p->firstPressureChanged(); });
}

QVariantMap Pen::feedback(QWindow* w) const {
  QVariantMap m;
#ifdef Q_OS_WIN
  if (!w || QGuiApplication::platformName() != u"windows") return m;
  for (const auto& [name, type] : kFeedback) {
    BOOL on = TRUE;
    UINT32 size = sizeof(on);
    if (GetWindowFeedbackSetting(HWND(w->winId()), type, 0, &size, &on) && size == sizeof(on))  // 0: not set
      m[QLatin1String(name)] = bool(on);
  }
#else
  Q_UNUSED(w);
#endif
  return m;
}

bool Pen::send(QQuickItem* item, const QString& phase, QPointF at, const QVariantMap& with) {
  QQuickWindow* w = item ? item->window() : nullptr;
  if (!w) return false;
  const bool mice = qtMakesMice(w), wintab = with.value(QStringLiteral("source")).toString() == u"wintab";
  const QPointingDevice* d = testPen(wintab, with.value(QStringLiteral("eraser")).toBool());
  const QPointF scene = item->mapToScene(at), l = native(w, scene), g = native(w, w->mapToGlobal(scene));
  const ulong t = msNow();
  if (phase == u"near" || phase == u"away") {
    // Windows Ink's pen enters the window under it on its first move, leaves it as it goes (WM_POINTERLEAVE)
    if (phase == u"away" && !wintab) QWindowSystemInterface::handleLeaveEvent(w);
    QWindowSystemInterface::handleTabletEnterLeaveProximityEvent(w, t, d, phase == u"near", l, g);
    if (phase == u"near" && !wintab) QWindowSystemInterface::handleEnterEvent(w, l, g);
    QWindowSystemInterface::flushWindowSystemEvents();
    return mice;
  }
  const bool down = phase == u"press" || phase == u"move";
  const auto number = [&](const char* key, double otherwise) {
    bool ok = false;
    const double v = with.value(QLatin1String(key)).toDouble(&ok);
    return ok ? v : otherwise;
  };
  Watch watch;  // where Qt makes no mouse event: does Windows'?
  if (!mice) w->installEventFilter(&watch);
  QWindowSystemInterface::handleTabletEvent(w, t, d, l, g, down ? Qt::LeftButton : Qt::NoButton,
                                            down ? number("pressure", 1) : 0, number("tiltX", 0), number("tiltY", 0), 0,
                                            number("rotation", 0), 0);
  QWindowSystemInterface::flushWindowSystemEvents();
  w->removeEventFilter(&watch);
  if (!mice && watch.seen && !watch.took) {  // as QGuiApplication makes it: the tablet event's time, device, points
    const bool press = phase == u"press", up = phase == u"release";
    QWindowSystemInterface::handleMouseEvent(w, t, d, l, g, down ? Qt::LeftButton : Qt::NoButton,
                                             press || up ? Qt::LeftButton : Qt::NoButton,
                                             press ? QEvent::MouseButtonPress : up ? QEvent::MouseButtonRelease : QEvent::MouseMove,
                                             Qt::NoModifier, Qt::MouseEventNotSynthesized);
    QWindowSystemInterface::flushWindowSystemEvents();
  }
  return mice;
}
