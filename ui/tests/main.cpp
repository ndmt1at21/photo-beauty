// Runs tests/tst_*.qml against the PhotoBeauty module (docs/ui-tasks.md), offscreen unless QT_QPA_PLATFORM
// says otherwise. copied from snapone 6bdb5c8 (ui/tests/main.cpp: Setup, main), without its Tests object.
#include <QQmlContext>
#include <QQmlEngine>
#include <QStandardPaths>
#include <QtQml/qqmlextensionplugin.h>
#include <QtQuickTest>

#include "../look.h"

Q_IMPORT_QML_PLUGIN(PhotoBeautyPlugin)

static QVariant gcLimitFound;  // QV4_GC_TIMELIMIT as the environment had it, null when unset (tst_u23d)

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
    engine->rootContext()->setContextProperty("gcLimitFound", gcLimitFound);
    engine->rootContext()->setContextProperty("gcLimit", qEnvironmentVariable("QV4_GC_TIMELIMIT"));
  }
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
