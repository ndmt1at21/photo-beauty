import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic

// Folding section of a panel (.ph over .sec, Retouch): a 30 px title with its chevron under a p2 rule,
// `badge` at its right ("Đã có nhóm": the accent's tick, t3 text), the rows below (a ColumnLayout,
// 8 apart). `open` comes from the owner; toggled() asks it to flip.
ColumnLayout {
    id: s
    property string title
    property bool open: true
    property string badge
    default property alias content: body.data
    signal toggled()
    spacing: 0

    AbstractButton {
        id: head
        Layout.fillWidth: true
        implicitHeight: 30
        leftPadding: 10
        rightPadding: 10
        text: s.title
        font.pointSize: Theme.pt(12)
        font.weight: Font.Medium
        Accessible.role: Accessible.Button
        onClicked: s.toggled()
        background: Rectangle { height: 1; color: Theme.p2 }
        contentItem: RowLayout {
            spacing: 6
            Icon { name: s.open ? "expand_more" : "chevron_right"; size: 14; color: Theme.t3 }
            Text {
                Layout.fillWidth: true
                text: head.text
                font: head.font
                color: Theme.t1
                elide: Text.ElideRight
            }
            Icon { visible: s.badge !== ""; name: "check"; size: 14; color: Theme.accent }
            Text {
                visible: s.badge !== ""
                text: s.badge
                color: Theme.t3
                font.pointSize: Theme.pt(11)
            }
        }
    }
    ColumnLayout {
        id: body
        visible: s.open
        Layout.fillWidth: true
        Layout.leftMargin: 12
        Layout.rightMargin: 12
        Layout.topMargin: 2
        Layout.bottomMargin: 10
        spacing: 8
    }
}
