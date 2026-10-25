#pragma once
// The pen (I30 of docs/p1-tasks.md; DocCanvas reads its pressure, tilt and rotation, C3b): how Qt takes it in on
// Windows, through Windows Ink (WM_POINTER, Qt's own) or Wintab as well, as Thiết lập › Bút says from the next start;
// Windows' pen feedback off on the app's windows (press and hold as the right button, the tap's ripple, the barrel
// button's), as nothing of it is the app's; which end of the pen is in use, the tip or the eraser, for Viewport to
// turn the tool to the Eraser and back as Photoshop; a stroke's first dab with the next event's pressure when the pen
// came down with none (Windows Ink's, some drivers'). I31 (Linux) and I32 (macOS) add their platforms' parts here.
#include <QLoggingCategory>
#include <QObject>
#include <QPointF>
#include <QString>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class QQuickItem;
class QWindow;

Q_DECLARE_LOGGING_CATEGORY(lcPen)  // "pb.pen" (pen.cpp)

// main.cpp, before QGuiApplication: the tablet's way in for this start on Windows, PB_TABLET (ink, wintab) if set, else
// Prefs' tabletApi as the last start kept it (QSettings' [prefs] json), but the defaults' (Windows Ink) with
// --screenshot, as the language (U30). Wintab: QGuiApplication's constructor opens it beside Windows Ink, whose
// events of a pen Wintab has Pen keeps out (Qt 6.11.3's windows plugin has no option for one or the other, only
// QWindowsApplication::setWinTabEnabled()). Elsewhere nothing to choose.
void usePen(int argc, char** argv);
// Prefs' JSON as usePen() reads it: "wintab" when its tabletApi is 1, else "ink".
QString tabletApiOf(const QByteArray& prefsJson);
// DocCanvas: a stroke whose pen came down without pressure takes the next event's for its first dab (Pen.firstPressure).
bool penFirstPressure();

// In QML Pen. Every engine's reads the app's one state.
class Pen : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON
  // How Qt takes the pen in: "ink" (Windows Ink), "wintab" (Wintab opened beside it: Windows Ink's events of a pen that
  // Wintab has kept out, and the system's mouse made of it while it is near); "" elsewhere (Qt's own way). Set: on
  // Windows, Wintab opened or closed now ("ink" again when it will not open: no Wintab driver); elsewhere, for tests,
  // what is kept out as on Windows, with test pens of either way (send()).
  Q_PROPERTY(QString api READ api WRITE setApi NOTIFY apiChanged)
  Q_PROPERTY(QString asked READ asked CONSTANT)  // usePen()'s: "ink", "wintab"; "" where it was not called (tests)
  // The eraser end of the pen is the one in use: a tablet event of it, or it coming near (PointerType Eraser), until
  // the tip's.
  Q_PROPERTY(bool eraser READ eraser NOTIFY eraserChanged)
  // A stroke's first dab takes the next event's pressure when the pen's touch-down has none (0), so the stroke does not
  // start at its thinnest; PB_PEN_FIRST_PRESSURE=0 starts with it off (to log a pen's own first pressures).
  Q_PROPERTY(bool firstPressure READ firstPressure WRITE setFirstPressure NOTIFY firstPressureChanged)

 public:
  explicit Pen(QObject* parent = nullptr);
  QString api() const;
  void setApi(const QString&);
  QString asked() const;
  bool eraser() const;
  bool firstPressure() const { return penFirstPressure(); }
  void setFirstPressure(bool);

  // For tests: tabletApiOf() of this JSON text.
  Q_INVOKABLE QString tabletApi(const QString& prefsJson) const { return tabletApiOf(prefsJson.toUtf8()); }
  // Windows' pen feedback on `w` as GetWindowFeedbackSetting() reads it back: { barrel, tap, doubleTap, pressAndHold,
  // rightTap }, false when off, true when on; a key is missing when the window does not set it. {} but on Windows'
  // own windows (QT_QPA_PLATFORM windows).
  Q_INVOKABLE QVariantMap feedback(QWindow* w) const;
  // For tests: the pen as a platform hands it over (QWindowSystemInterface), at `item`'s point `at`, delivered before
  // it returns. `phase`: "near", "away" (in and out of proximity), "hover" (moving, up), "press", "move" (down),
  // "release". `with`: pressure (0..1, down; 1 unless given), tiltX, tiltY, rotation (degrees), eraser (its eraser end),
  // source ("ink" the device Windows Ink makes, "wintab" Wintab's; "ink" unless given). Where Qt makes no mouse event of
  // a tablet event nothing took (offscreen), it hands over the one Windows' Qt makes, at the same time, of the same
  // device. Whether Qt makes them.
  Q_INVOKABLE bool send(QQuickItem* item, const QString& phase, QPointF at, const QVariantMap& with = {});

 signals:
  void apiChanged();
  void eraserChanged();
  void firstPressureChanged();
};
