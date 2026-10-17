pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects

// A side panel open beside the icon column (docs/ui-tasks.md, U8; the .pop "Panel …" of Main.dc.html): 280 wide, the
// tabs of its group, × to close, under them the panel as tall as it is up to maxHeight, scrolled past that. Which one is
// Tools.sidePanel ("" for none); SideColumn places it and closes it on Esc or a press on the canvas.
FocusScope {
    id: sp
    property real maxHeight: 784
    readonly property string name: Tools.sidePanel
    // The prototype's SIDE_GROUP: the tabs each side panel opens with.
    readonly property var groups: ({ History: ["History"], Brushes: ["Brushes", "Brush Settings"], "Brush Settings": ["Brushes", "Brush Settings"],
        Navigator: ["Navigator", "Histogram", "Info"], Histogram: ["Navigator", "Histogram", "Info"], Info: ["Navigator", "Histogram", "Info"],
        Character: ["Character", "Paragraph"], Paragraph: ["Character", "Paragraph"] })
    readonly property var panel: loader.item
    implicitWidth: 280
    implicitHeight: Math.min(30 + loader.height, maxHeight)
    visible: name !== ""

    // Each panel's Component, made once, by its name ("Brush Settings" is BrushSettingsPanel).
    property var kinds: ({})
    function kind(tab) {
        if (tab && !kinds[tab])
            kinds[tab] = Qt.createComponent("PhotoBeauty", tab.replace(" ", "") + "Panel", Component.PreferSynchronous, sp)
        return tab ? kinds[tab] : null
    }

    RectangularShadow {
        z: -1
        anchors.fill: parent
        offset.y: 12
        blur: 32
        radius: 8
        color: Theme.shadow
    }
    Rectangle {
        anchors.fill: parent
        radius: 8
        color: Theme.p2
        border.color: Theme.ln
    }
    MouseArea {  // a press or the wheel on it stays on it, not on the canvas below
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onWheel: w => { w.accepted = true }
    }
    PanelTabs {
        width: parent.width
        tabs: sp.groups[sp.name] || []
        current: sp.name
        menu: sp.panel ? sp.panel.menu || [] : []
        onPicked: t => Tools.sidePanel = t
        onTabDoubleClicked: Tools.sidePanel = ""  // folds the group back to its icon
        ToolToggle {
            icon.name: "close"
            icon.width: 16
            ToolTip.text: qsTr("Đóng")
            Accessible.name: qsTr("Đóng panel")
            onClicked: Tools.sidePanel = ""
        }
    }
    Flickable {
        id: body
        y: 30
        width: parent.width
        height: parent.height - 30
        contentHeight: loader.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        acceptedButtons: Qt.NoButton  // the wheel and the scroll bar; drags are the panel's
        ScrollBar.vertical: ScrollBar {}
        Loader {
            id: loader
            width: body.width
            height: sp.panel ? sp.panel.implicitHeight : 0
            sourceComponent: sp.kind(sp.name)
        }
    }
}
