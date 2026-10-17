// copied from snapone 6bdb5c8 (ui/qml/IconButton.qml)
import QtQuick
import QtQuick.Controls.Basic

// Square icon button of the prototype's bars and dialogs: transparent, hover fills with `sel`.
// icon.name is a Material Symbols name, icon.width its pixel size, ink its resting colour; `on`
// is switched on (acc2, accent); framed adds the outline of the pick buttons at the end of a row.
AbstractButton {
    id: b
    property bool on: false
    property color ink: Theme.t2
    property bool framed: false
    implicitWidth: 28
    implicitHeight: 28
    icon.width: 18
    hoverEnabled: true
    ToolTip.visible: hovered && ToolTip.text !== ""
    ToolTip.delay: 600

    background: Rectangle {
        radius: b.height <= 24 ? 4 : 6
        color: !b.enabled ? "transparent" : b.on ? Theme.acc2 : b.hovered || b.down ? Theme.sel : "transparent"
        border.color: b.framed ? Theme.ln : "transparent"
    }
    contentItem: Icon {
        name: b.icon.name
        size: b.icon.width
        color: !b.enabled ? Theme.t4 : b.on ? Theme.accent : b.hovered ? Theme.t1 : b.ink
    }
}
