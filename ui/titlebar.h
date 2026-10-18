#pragma once
// copied from snapone 2571122 (Core::useTitleBar, Core::titleHit of ui/core.h), as the singleton TitleBar.
#include <QObject>
#include <QPointF>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtQml/qqmlregistration.h>

// The top bar as the window's title bar (titlebar.cpp; docs/ui-tasks.md U4, as N12 of snapone's docs/ui-plan.md).
class TitleBar : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON

 public:
  using QObject::QObject;
  enum Hit { Quick, Move, Maximize };
  Q_ENUM(Hit)
  // On Windows, before win shows (Main's onCompleted): no system title bar; the item named "titleBar" gets titleBar =
  // true and moves the window where none of its controls is, and its control named "maximize" is the system's maximize
  // button, with Snap Layouts on hover. Elsewhere the system's title bar stays.
  Q_INVOKABLE void use(QQuickWindow* win);
  // Where a press at x, y (window coordinates) goes: to Qt Quick (off the bar, a control, or a menu or dialog is open),
  // to the system to move the window, or to the maximize button.
  Q_INVOKABLE Hit hit(QQuickItem* bar, qreal x, qreal y) const { return hit(bar, QPointF(x, y)); }
  static Hit hit(QQuickItem* bar, QPointF p);
  // The title bar of win now: the visible item named "titleBar", the top bar or the header of a space open over it
  // (Workspace); null when none shows.
  Q_INVOKABLE QQuickItem* bar(QQuickWindow* win) const;
  // Whether a popup is open on item's window that a press outside it closes and lets through (not modal), tooltips
  // aside: Main's scrim keeps that press from the canvas.
  Q_INVOKABLE bool popupOpen(QQuickItem* item) const;
};
