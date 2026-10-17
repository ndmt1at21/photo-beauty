import QtQuick

// The document tabs, 28 high (docs/ui-tasks.md: U4 places them, U6 fills them): over the canvas.
Rectangle {
    implicitWidth: 1052
    implicitHeight: 28
    color: Theme.bg
    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.ln }
}
