// copied from snapone 6bdb5c8 (ui/qml/TextBox.qml)
import QtQuick
import QtQuick.Controls.Basic

// Text input of the prototype (.fld of the dialogs): names, search boxes. A search icon goes in
// as a child with leftPadding to clear it.
TextField {
    id: f
    implicitHeight: 28
    leftPadding: 8
    rightPadding: 8
    topPadding: 0
    bottomPadding: 0
    verticalAlignment: TextInput.AlignVCenter
    font.pointSize: Theme.pt(12)
    color: enabled ? Theme.t1 : Theme.t4
    placeholderTextColor: Theme.t4
    selectionColor: Theme.accent
    selectedTextColor: Theme.acct
    selectByMouse: true
    background: Rectangle {
        radius: f.height <= 24 ? 4 : 6
        color: Theme.p2
        border.color: f.activeFocus ? Theme.t3 : "transparent"
    }
}
