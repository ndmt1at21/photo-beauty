pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic

// Foreground and background colours: the foreground swatch over the background one (Color panel,
// 26 px); compact is the toolbar's 34 px pair with its swap (X) and default (D) buttons. Clicking a
// swatch emits fgClicked() or bgClicked() (open the Color Picker), the small buttons swapClicked()
// and resetClicked(); the owner changes fg and bg.
Item {
    id: p
    property color fg: "#000000"
    property color bg: "#ffffff"
    property bool compact: false
    signal fgClicked()
    signal bgClicked()
    signal swapClicked()
    signal resetClicked()
    readonly property int cell: compact ? 19 : 26

    implicitWidth: compact ? 34 : 40
    implicitHeight: implicitWidth

    component Cell: AbstractButton {
        id: c
        property color color
        property bool front
        width: p.cell
        height: p.cell
        hoverEnabled: true
        ToolTip.visible: hovered
        ToolTip.delay: 600
        background: Item {
            Rectangle {  // the gap that lifts the foreground off the background swatch
                visible: c.front
                anchors.fill: parent
                anchors.margins: p.compact ? -2 : -3
                radius: p.compact ? 5 : 7
                color: Theme.p1
            }
            Rectangle {
                anchors.fill: parent
                anchors.margins: -1
                radius: p.compact ? 4 : 5
                color: p.compact ? Theme.t3 : Theme.t4
            }
            Rectangle {
                anchors.fill: parent
                radius: p.compact ? 3 : 4
                color: c.color
            }
        }
    }
    Cell {
        x: p.compact ? 13 : 14
        y: x
        color: p.bg
        ToolTip.text: qsTr("Màu sau · Background Color")
        onClicked: p.bgClicked()
    }
    Cell {
        x: p.compact ? 2 : 0
        y: x
        front: true
        color: p.fg
        ToolTip.text: qsTr("Màu trước · Foreground Color")
        onClicked: p.fgClicked()
    }
    AbstractButton {
        id: swap
        visible: p.compact
        x: 23
        y: -2
        width: 14
        height: 14
        hoverEnabled: true
        ToolTip.visible: hovered
        ToolTip.delay: 600
        ToolTip.text: qsTr("Đổi màu trước, sau · Switch Foreground and Background Colors (X)")
        onClicked: p.swapClicked()
        contentItem: Icon { name: "swap_fgbg"; size: 13; color: swap.hovered ? Theme.t1 : Theme.t2 }
    }
    AbstractButton {
        visible: p.compact
        x: -2
        y: 23
        width: 13
        height: 13
        hoverEnabled: true
        ToolTip.visible: hovered
        ToolTip.delay: 600
        ToolTip.text: qsTr("Màu mặc định · Default Foreground and Background Colors (D)")
        onClicked: p.resetClicked()
        contentItem: Item {  // white behind black, each 7 px in a 1 px t3 edge
            Rectangle { x: 4; y: 4; width: 9; height: 9; color: Theme.t3; Rectangle { x: 1; y: 1; width: 7; height: 7; color: "#ffffff" } }
            Rectangle { width: 9; height: 9; color: Theme.t3; Rectangle { x: 1; y: 1; width: 7; height: 7; color: "#000000" } }
        }
    }
}
