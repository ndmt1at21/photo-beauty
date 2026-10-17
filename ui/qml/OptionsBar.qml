import QtQuick

// The options bar, 36 high (docs/ui-tasks.md: U4 places it, U5 fills it): the tool's options under the top bar.
Rectangle {
    implicitWidth: 1440
    implicitHeight: 36
    color: Theme.p1
    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.ln }
}
