import QtQuick

// The Layers panel (docs/ui-tasks.md: U8 makes room for it, U9 fills it), a tab of the dock's group Layers, Channels:
// the dock gives it its width and the height the group has left (build/orch/api/u8.md). Empty until then: its name in
// the middle.
Item {
    property var menu: []  // the items of the … of its tab strip (PopupMenu's)
    implicitWidth: 308
    implicitHeight: 320
    Text {
        anchors.centerIn: parent
        text: qsTr("Layers")
        color: Theme.t4
        font.pointSize: Theme.pt(12)
    }
}
