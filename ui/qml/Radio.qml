import QtQuick
import QtQuick.Controls.Basic

// A radio button, the prototype's <input type=radio> (Stroke, the filters, Color Picker, Levels…) drawn as Chrome draws it
// (a 16 px row; checked: an accent ring and dot, off: the whole row faded): `checked` comes from the owner, clicked()
// asks it to pick this one. objectName "radio" for the tests.
AbstractButton {
    id: r
    objectName: "radio"
    implicitWidth: implicitContentWidth
    implicitHeight: 16
    opacity: enabled ? 1 : 0.4
    spacing: 6  // the label's gap: 8 in the dialogs' .ck rows
    hoverEnabled: true
    font.pointSize: Theme.pt(12)
    Accessible.role: Accessible.RadioButton
    Accessible.checkable: true
    Accessible.checked: checked
    contentItem: Row {
        spacing: r.spacing
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: 13
            height: 13
            radius: 6.5
            color: Theme.checkFill
            border.color: r.checked ? Theme.accent : r.hovered ? Theme.t2 : Theme.checkLine
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
