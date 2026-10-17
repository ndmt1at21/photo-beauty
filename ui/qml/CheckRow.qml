import QtQuick
import QtQuick.Controls.Basic

// Check box and its label (<input type=checkbox> with accent-color in the prototype): `checked`
// comes from the owner, clicked() asks it to flip.
AbstractButton {
    id: c
    implicitWidth: implicitContentWidth + leftPadding + rightPadding
    implicitHeight: 20
    padding: 0
    hoverEnabled: true
    font.pointSize: Theme.pt(12)
    Accessible.role: Accessible.CheckBox
    Accessible.checkable: true
    Accessible.checked: checked

    contentItem: Row {
        spacing: 6
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: 13
            height: 13
            radius: 3
            color: c.checked ? (c.enabled ? Theme.accent : Theme.t4) : "transparent"
            border.color: c.checked ? color : c.enabled && c.hovered ? Theme.t2 : Theme.t3
            Icon { anchors.centerIn: parent; visible: c.checked; name: "check"; size: 12; color: Theme.acct }
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: c.text
            font: c.font
            color: c.enabled ? Theme.t2 : Theme.t4
        }
    }
}
