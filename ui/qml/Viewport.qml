import QtQuick

// The canvas and its rulers (docs/ui-tasks.md: U4 places it, U6 fills it): what the bars and panels leave, the grey
// around the image, black in Toàn màn hình.
Rectangle {
    implicitWidth: 1052
    implicitHeight: 768
    color: Tools.screen === 2 ? Theme.views[0] : Theme.view
}
