import QtQuick

// The Brush Settings panel (docs/ui-tasks.md: U8 makes room for it, U13 fills it), opened beside the icon column
// (SidePanel): it is given its width and shown as tall as it is, scrolled past the column's height
// (build/orch/api/u8.md). Empty until then: its name in the middle.
Item {
    property var menu: []  // the items of the … of its tab strip (PopupMenu's)
    implicitWidth: 280
    implicitHeight: 868
    Text {
        anchors.centerIn: parent
        text: qsTr("Brush Settings")
        color: Theme.t4
        font.pointSize: Theme.pt(12)
    }
}
