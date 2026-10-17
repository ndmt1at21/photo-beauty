#pragma once
// copied from snapone 6bdb5c8 (ui/look.h); the fonts are every one of ui/fonts/.
// Style and fonts of the prototype, set before any QML engine exists. Shared with the tests so
// sizes and positions there match the app.
#include <QDir>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QQuickStyle>

// Enter presses the button Tab reached, as in the prototype's browser; Qt's buttons take only
// Space. A button focused by a click leaves Enter to the shortcuts.
class EnterPresses : public QObject {
 public:
  using QObject::QObject;
  bool eventFilter(QObject* o, QEvent* e) override {
    const QEvent::Type t = e->type();
    if (t != QEvent::KeyPress && t != QEvent::KeyRelease && t != QEvent::ShortcutOverride) return false;
    const auto* k = static_cast<QKeyEvent*>(e);
    if ((k->key() != Qt::Key_Return && k->key() != Qt::Key_Enter) || !o->inherits("QQuickAbstractButton") ||
        !o->property("visualFocus").toBool())
      return false;
    if (t == QEvent::ShortcutOverride) {  // the button gets the key, not a shortcut
      e->accept();
      return true;
    }
    QKeyEvent space(t, Qt::Key_Space, k->modifiers(), QStringLiteral(" "), k->isAutoRepeat());
    QCoreApplication::sendEvent(o, &space);
    return true;
  }
};

inline void applyLook() {
  QQuickStyle::setStyle("Basic");  // the plainest style to restyle with the prototype's tokens
  const QDir fonts(QStringLiteral(":/qt/qml/PhotoBeauty/fonts"));
  for (const QString& f : fonts.entryList(QDir::Files)) QFontDatabase::addApplicationFont(fonts.filePath(f));
  QFont font("Space Grotesk");
  font.setPixelSize(12);
  QGuiApplication::setFont(font);
  qApp->installEventFilter(new EnterPresses(qApp));
}
