// copied from snapone 6bdb5c8 (ui/qml/Toggle.qml)
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic

// Switch row (the prototype's .sw, .tg): label on the left, the whole row clicks. `checked`
// comes from the model; clicked() asks it to flip. A `sub` line under the label. An empty label
// leaves the bare switch.
AbstractButton {
    id: t
    property string sub
    implicitWidth: implicitContentWidth
    implicitHeight: implicitContentHeight + topPadding + bottomPadding
    topPadding: 5
    bottomPadding: 5
    hoverEnabled: true
    font.pointSize: Theme.pt(12)
    Accessible.role: Accessible.CheckBox
    Accessible.checkable: true

    contentItem: RowLayout {
        spacing: 8
        Column {
            id: col
            visible: t.text !== "" || t.sub !== ""
            Layout.fillWidth: true
            spacing: 2
            Text {
                width: Math.min(implicitWidth, col.width)
                text: t.text
                font: t.font
                color: !t.enabled ? Theme.t4 : t.hovered ? Theme.t1 : Theme.t2
                elide: Text.ElideRight
            }
            Text {
                visible: t.sub !== ""
                width: col.width
                text: t.sub
                color: Theme.t3
                font.pointSize: Theme.pt(11)
                lineHeight: 1.35
                wrapMode: Text.Wrap
            }
        }
        Rectangle {
            implicitWidth: 30
            implicitHeight: 18
            radius: 9
            opacity: t.enabled ? 1 : 0.4
            color: t.checked ? Theme.accent : Theme.trk
            Rectangle {
                x: t.checked ? 14 : 2
                y: 2
                width: 14
                height: 14
                radius: 7
                color: Theme.knob
            }
        }
    }
}
