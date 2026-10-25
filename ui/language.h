#pragma once
// The UI's translations as QML can read them (U30), for tests: Language.translate("en", "MenuSpec", "Tạo mới…") is the
// label pb-ui shows in English, looked up in :/i18n/pb_en.qm (ui/i18n/pb_en.ts, built into the module) as qsTr() would.
// A QTranslator of its own, never installed: the app's strings stay in the language it started in (Vietnamese in tests).
#include <QObject>
#include <QString>
#include <QTranslator>
#include <QtQml/qqmlregistration.h>

#include <map>
#include <memory>

class Language : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON

 public:
  // `source` of `context` (a QML or JS file's name without extension, as "MenuSpec"; a C++ class's), with qsTr()'s
  // disambiguation `comment` and, for a plural, the count `n`, in `lang` ("en"); "" when there is no such translation.
  Q_INVOKABLE QString translate(const QString& lang, const QString& context, const QString& source,
                                const QString& comment = {}, int n = -1) {
    std::unique_ptr<QTranslator>& t = loaded_[lang];
    if (!t) {
      t = std::make_unique<QTranslator>();
      if (!t->load(QStringLiteral(":/i18n/pb_") + lang)) return {};
    }
    const QByteArray c = context.toUtf8(), s = source.toUtf8(), d = comment.toUtf8();
    return t->translate(c.constData(), s.constData(), comment.isEmpty() ? nullptr : d.constData(), n);
  }

 private:
  std::map<QString, std::unique_ptr<QTranslator>> loaded_;
};
