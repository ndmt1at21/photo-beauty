import QtQuick
import QtQuick.Shapes
import QtQuick.Controls.Basic

// A button of the toolbar (docs/ui-tasks.md, U5; artboard Toolbar, the aside of Main.dc.html): 32 × 32, corners 6, its
// icon 20 in t2. checked: the tool in use (p3, t1); lit: a mode that is on (Quick Mask: acc2, the accent); hover: sel
// under the mouse (the screen mode's .hv). many: a group of tools, a small triangle in the corner; a long press (350 ms)
// or the right button emits menu() for its flyout, and the release ending that long press is no click. picked() is a
// click; a click does not take the keyboard focus, so the tools' keys and Space, Alt, Ctrl go on working.
AbstractButton {
    id: b
    property bool many
    property bool corner: many  // its ◢ drawn: none in the two-column bar, as the Toolbar artboard
    property bool lit
    property bool hover
    property bool held  // the press that opened the flyout
    signal picked()
    signal menu()

    implicitWidth: 32
    implicitHeight: 32
    focusPolicy: Qt.TabFocus
    hoverEnabled: true
    icon.width: 20
    ToolTip.visible: hovered && ToolTip.text !== ""
    ToolTip.delay: 600

    onPressed: {
        held = false
        if (many)
            hold.restart()
    }
    onReleased: hold.stop()
    onCanceled: hold.stop()
    onClicked: {
        if (held)
            held = false
        else
            picked()
    }
    Timer {
        id: hold
        interval: 350
        onTriggered: {
            b.held = true
            b.menu()
        }
    }
    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: if (b.many) b.menu()
    }

    background: Rectangle {
        radius: 6
        color: b.checked ? Theme.p3 : b.lit ? Theme.acc2 : b.hover && b.hovered ? Theme.sel : "transparent"
        Shape {  // ◢ 4 px, 3 px off the corner
            visible: b.corner
            x: parent.width - 7
            y: parent.height - 7
            width: 4
            height: 4
            ShapePath {
                fillColor: Theme.t3
                strokeColor: "transparent"
                startX: 4
                startY: 0
                PathLine { x: 4; y: 4 }
                PathLine { x: 0; y: 4 }
            }
        }
    }
    contentItem: Icon {
        name: b.icon.name
        size: b.icon.width
        color: b.checked ? Theme.t1 : b.lit ? Theme.accent : Theme.t2
    }
}
