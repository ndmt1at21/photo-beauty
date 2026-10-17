// Runs tests/tst_*.qml against the PhotoBeauty module (docs/ui-tasks.md), offscreen unless QT_QPA_PLATFORM
// says otherwise. copied from snapone 6bdb5c8 (ui/tests/main.cpp: Setup, main), without its Tests object.
#include <QStandardPaths>
#include <QtQml/qqmlextensionplugin.h>
#include <QtQuickTest>

#include "../look.h"

Q_IMPORT_QML_PLUGIN(PhotoBeautyPlugin)

class Setup : public QObject {
  Q_OBJECT
 public slots:
  void applicationAvailable() {
    applyLook();
    // Main's QtCore Settings (with persist) write to ~/.qttest, not the app's own file.
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName("photo-beauty");
  }
};

int main(int argc, char** argv) {
  if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
  QTEST_SET_MAIN_SOURCE_PATH
  Setup setup;
  return quick_test_main_with_setup(argc, argv, "pb_ui", QUICK_TEST_SOURCE_DIR, &setup);
}
#include "main.moc"
