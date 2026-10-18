import QtQuick
import QtQuick.Controls.Basic

// A radio button, the prototype's <input type=radio> (Stroke, the filters, Color Picker, Levels…): `checked` comes from
// the owner, clicked() asks it to pick this one. objectName "radio" for the tests.
AbstractButton {
    id: r
    objectName: "radio"
    implicitWidth: implicitContentWidth
    implicitHeight: 20
    hoverEnabled: true
    font.pointSize: Theme.pt(12)
    Accessible.role: Accessible.RadioButton
    Accessible.checked: checked
    contentItem: Row {
        spacing: 6
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: 13
            height: 13
            radius: 6.5
            color: "transparent"
            border.width: r.checked ? 1.5 : 1
            border.color: r.checked ? Theme.accent : r.hovered ? Theme.t2 : Theme.t3
            Rectangle {
                visible: r.checked
                anchors.centerIn: parent
                width: 7
                height: 7
                radius: 3.5
                color: Theme.accent
            }
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: r.text
            font: r.font
            color: Theme.t2
        }
    }
}
