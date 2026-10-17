// copied from snapone 6bdb5c8 (ui/qml/Link.qml)
import QtQuick
import QtQuick.Controls.Basic

// Text link of the prototype: accent unless `ink` says otherwise, underlined on hover.
AbstractButton {
    id: l
    property color ink: Theme.accent
    hoverEnabled: true
    font.pointSize: Theme.pt(12)
    background: null
    contentItem: Text {
        text: l.text
        color: !l.enabled ? Theme.t4 : l.hovered ? Qt.lighter(l.ink, 1.25) : l.ink
        font.pointSize: l.font.pointSize
        font.underline: l.hovered
    }
}
