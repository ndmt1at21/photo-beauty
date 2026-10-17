import QtQuick

// The dock, 308 wide (docs/ui-tasks.md: U4 places it by its implicit width, U8 fills it and gives it none when folded
// to icons): the panel groups at the right.
Rectangle {
    implicitWidth: 308
    implicitHeight: 796
    color: Theme.p1
    Rectangle { width: 1; height: parent.height; color: Theme.ln }
}
