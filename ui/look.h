#pragma once
// copied from snapone 6bdb5c8 (ui/look.h); the fonts are every one of ui/fonts/.
// Style and fonts of the prototype, set before any QML engine exists. Shared with the tests so
// sizes and positions there match the app.
#include <QDir>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QQuickItem>
#include <QQuickStyle>

// Enter presses the button Tab reached, as in the prototype's browser; Qt's buttons take only
// Space. A button focused by a click leaves Enter to the shortcuts. In a dialog, Enter that nothing
// took (no field, which applies its value, no button Tab reached, no open list) presses its main
// button, as a desktop dialog's default one: the key goes up from the focus to the popup's item,
// and DialogBox's enterPressed() finds the button. A closed drop-down (ComboBox) takes Enter and
// does nothing with it: as a button, it opens when Tab reached it, else Enter goes on up.
class EnterPresses : public QObject {
 public:
  using QObject::QObject;
  bool eventFilter(QObject* o, QEvent* e) override {
    const QEvent::Type t = e->type();
    if (t != QEvent::KeyPress && t != QEvent::KeyRelease && t != QEvent::ShortcutOverride) return false;
    const auto* k = static_cast<QKeyEvent*>(e);
    if (k->key() != Qt::Key_Return && k->key() != Qt::Key_Enter) return false;
    const bool shut = closedList(o);
    const bool tabbed = (shut || o->inherits("QQuickAbstractButton")) && o->property("visualFocus").toBool();
    if (t == QEvent::KeyPress) {
      if (o->isWindowType()) {  // the window has it first, then the focus item and up
        fresh = true;
        return false;
      }
      if (fresh && qobject_cast<QQuickItem*>(o)) {  // where it starts (a field may move the focus on Enter)
        fresh = false;
        fieldTook = (o->inherits("QQuickTextInput") || o->inherits("QQuickTextEdit")) && !o->property("readOnly").toBool();
      }
      if (shut && !tabbed) {  // not to it: the delivery goes on to its parent
        e->ignore();
        return true;
      }
      QObject* popup = o->parent();
      if (o->inherits("QQuickPopupItem") && !fieldTook && popup && popup->metaObject()->indexOfMethod("enterPressed()") >= 0) {
        QMetaObject::invokeMethod(popup, "enterPressed");
        return true;
      }
    }
    if (!tabbed) return false;
    if (t == QEvent::ShortcutOverride) {  // the button gets the key, not a shortcut
      e->accept();
      return true;
    }
    QKeyEvent space(t, Qt::Key_Space, k->modifiers(), QStringLiteral(" "), k->isAutoRepeat());
    QCoreApplication::sendEvent(o, &space);
    return true;
  }

 private:
  static bool closedList(QObject* o) {  // a ComboBox, not editable, its list not shown
    if (!o->inherits("QQuickComboBox") || o->property("editable").toBool()) return false;
    const auto* list = o->property("popup").value<QObject*>();
    return !list || !list->property("visible").toBool();
  }
  bool fresh = false;      // an Enter on its way to the focus item
  bool fieldTook = false;  // the Enter going up started in a text field
};

inline void applyLook() {
  QQuickStyle::setStyle("Basic");  // the plainest style to restyle with the prototype's tokens
  const QDir fonts(QStringLiteral(":/qt/qml/PhotoBeauty/fonts"));
  for (const QString& f : fonts.entryList(QDir::Files)) QFontDatabase::addApplicationFont(fonts.filePath(f));
  QFont font("Space Grotesk");
  font.setPixelSize(12);
  // fontconfig's hintslight, which Linux has anyway: Windows offscreen (tests, shots) hints fully without fontconfig.
  font.setHintingPreference(QFont::PreferVerticalHinting);
  QGuiApplication::setFont(font);
  qApp->installEventFilter(new EnterPresses(qApp));
}
