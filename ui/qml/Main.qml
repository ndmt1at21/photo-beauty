import QtQuick
import QtQuick.Controls.Basic

// The app's window, empty until U4 builds the shell in it (docs/ui-tasks.md).
ApplicationWindow {
    id: win
    property string startState: ""  // --state: the artboard to open
    property string startTheme: ""  // --theme: "dark" or "light"; "" without it, the one kept from the last start
    // main.cpp: keep Prefs between starts; tests and screenshots start from the defaults.
    property bool persist: false
    width: 1440
    height: 900
    visible: true
    title: "photo-beauty"
    color: "#161617"  // Theme.bg once U1 has Theme
}
