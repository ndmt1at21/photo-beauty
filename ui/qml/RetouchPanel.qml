import QtQuick

// The Retouch panel (docs/ui-tasks.md: U8 makes room for it, U14 fills it), a tab of the dock's group Properties,
// Adjustments, Retouch: the dock gives it its width and shows 360 px of it at most, scrolled (build/orch/api/u8.md).
// Empty until then: its name in the middle.
Item {
    property var menu: []  // the items of the … of its tab strip (PopupMenu's)
    implicitWidth: 308
    implicitHeight: 489
    Text {
        anchors.centerIn: parent
        text: qsTr("Retouch")
        color: Theme.t4
        font.pointSize: Theme.pt(12)
    }
}
