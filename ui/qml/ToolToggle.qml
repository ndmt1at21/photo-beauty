import QtQuick
import QtQuick.Controls.Basic

// Small icon button of the prototype's bars and panel feet (.tb): 26 × 24, `checked` (from the owner)
// fills it with the accent; one never checked just acts. icon.name, icon.width: 17 in bars, 15 or 16
// elsewhere; clicked() is the owner's to answer.
AbstractButton {
    id: b
    implicitWidth: 26
    implicitHeight: 24
    icon.width: 17
    hoverEnabled: true
    ToolTip.visible: hovered && ToolTip.text !== ""
    ToolTip.delay: 600

    background: Rectangle {
        radius: 4
        color: !b.enabled ? "transparent" : b.checked ? Theme.acc2 : b.hovered ? Theme.sel : "transparent"
    }
    contentItem: Icon {
        name: b.icon.name
        size: b.icon.width
        color: !b.enabled ? Theme.t4 : b.checked ? Theme.accent : b.hovered ? Theme.t1 : Theme.t2
    }
}
