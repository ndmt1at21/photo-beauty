import QtQuick

// The Color panel (docs/ui-tasks.md: U8 makes room for it, U11 fills it), a tab of the dock's group Color, Swatches,
// Gradients: the dock gives it its width and lets it be as tall as it is (build/orch/api/u8.md). Empty until then: its
// name in the middle.
Item {
    property var menu: []  // the items of the … of its tab strip (PopupMenu's)
    implicitWidth: 308
    implicitHeight: 124
    Text {
        anchors.centerIn: parent
        text: qsTr("Color")
        color: Theme.t4
        font.pointSize: Theme.pt(12)
    }
}
