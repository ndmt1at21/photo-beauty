import QtQuick

// The icon column of the side panels, 36 wide (docs/ui-tasks.md: U4 places it, U8 fills it): between canvas and dock.
Rectangle {
    implicitWidth: 36
    implicitHeight: 796
    color: Theme.p1
    Rectangle { width: 1; height: parent.height; color: Theme.ln }
}
