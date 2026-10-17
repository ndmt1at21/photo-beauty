import QtQuick

// The toolbar, 44 wide (docs/ui-tasks.md: U4 places it, U5 fills it, 80 with two columns): left of the canvas.
Rectangle {
    implicitWidth: 44
    implicitHeight: 796
    color: Theme.p1
    Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.ln }
}
