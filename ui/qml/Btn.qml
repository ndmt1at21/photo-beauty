// copied from snapone 6bdb5c8 (ui/qml/Btn.qml)
import QtQuick
import QtQuick.Controls.Basic

// Text button of the prototype (.btn): plain, `on` (accent on acc2, a button switched on) or
// `primary` (accent fill, .btnp), with an optional icon.name in front of the text. 28 high as in
// dialogs; a bar sets implicitHeight 24 and padding 10, and the corners follow (6, 4 at 24).
AbstractButton {
    id: b
    property bool primary: false
    property bool on: false
    readonly property color fill: !enabled ? Theme.p2 : primary ? Theme.accent : on ? Theme.acc2 : hovered || down ? Theme.p3 : Theme.p2
    readonly property color ink: !enabled ? Theme.t4 : primary ? Theme.acct : on ? Theme.accent : Theme.t1

    implicitWidth: implicitContentWidth + leftPadding + rightPadding
    implicitHeight: 28
    leftPadding: 14
    rightPadding: 14
    hoverEnabled: true
    font.pointSize: Theme.pt(12)
    font.weight: primary ? Font.DemiBold : Font.Normal
    ToolTip.visible: hovered && ToolTip.text !== ""
    ToolTip.delay: 600

    background: Rectangle {
        radius: b.height <= 24 ? 4 : 6
        color: b.fill
    }
    contentItem: Item {
        implicitWidth: row.implicitWidth
        implicitHeight: row.implicitHeight
        Row {
            id: row
            anchors.centerIn: parent
            spacing: 6
            Icon {
                visible: b.icon.name !== ""
                name: b.icon.name
                size: 16
                color: b.ink
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: b.text
                font: b.font
                color: b.ink
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }
}
