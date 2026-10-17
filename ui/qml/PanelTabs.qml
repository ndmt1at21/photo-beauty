pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic

// Tab strip of a panel group (.pnlh, .tab): 30 px, the open tab in t1 on a 2 px accent bar, the
// others t3 (t2 under the mouse); tools (the dock's collapse, a close button…) sit before the panel
// menu button at the right, which opens `menu` (PopupMenu items). `current` and `collapsed` (no rule
// under the strip) come from the owner; picked(tab) and tabDoubleClicked(tab) (fold the group) ask it.
Item {
    id: p
    property var tabs: []
    property string current
    property bool collapsed: false
    property var menu: []
    default property alias tools: tools.data
    signal picked(string tab)
    signal tabDoubleClicked(string tab)

    implicitWidth: row.implicitWidth + tools.implicitWidth + 40
    implicitHeight: 30

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: p.collapsed ? "transparent" : Theme.ln
    }
    Row {
        id: row
        x: 4
        height: 30
        Repeater {
            model: p.tabs
            AbstractButton {
                id: t
                required property string modelData
                readonly property bool on: modelData === p.current
                height: 30
                leftPadding: 8
                rightPadding: 8
                hoverEnabled: true
                text: modelData
                font.pointSize: Theme.pt(12)
                font.weight: on ? Font.Medium : Font.Normal
                ToolTip.visible: hovered
                ToolTip.delay: 600
                ToolTip.text: qsTr("%1 · nhấp đúp để thu gọn nhóm").arg(modelData)
                Accessible.role: Accessible.PageTab
                Accessible.selected: on
                onClicked: p.picked(modelData)
                onDoubleClicked: p.tabDoubleClicked(modelData)
                background: Item {
                    Rectangle {
                        visible: t.on
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: 2
                        color: Theme.accent
                    }
                }
                contentItem: Text {
                    text: t.text
                    font: t.font
                    color: t.on ? Theme.t1 : t.hovered ? Theme.t2 : Theme.t3
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }
    Row {
        id: tools
        anchors.right: more.left
        anchors.verticalCenter: parent.verticalCenter
    }
    ToolToggle {
        id: more
        anchors.right: parent.right
        anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        icon.name: "more_horiz"
        icon.width: 16
        ToolTip.text: qsTr("Menu panel")
        onClicked: if (p.menu.length) pop.show(p.menu, more, 0, more.height + 2)
    }
    PopupMenu { id: pop }
}
