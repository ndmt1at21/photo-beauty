import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import QtQuick.Layouts

// The dock, 308 wide (docs/ui-tasks.md, U8; Main.dc.html's "Dock panel", the Dock card of Controls, PanelsA, PanelsB):
// three groups of panels with tabs, Color, Swatches, Gradients; Properties, Adjustments, Retouch, 360 tall at most and
// scrolled past that; Layers, Channels, taking the height left. A double click on a tab folds its group to its tabs. »
// folds the dock to the icon column: the dock then takes no room, and SideColumn floats the Group picked there beside
// the column. The tab of each group, the groups folded, the dock folded and the group floating are Tools' (dockTab,
// dockMin, dockCollapsed, dockOpen), which the Window menu and F6, F7 set too. Ctrl 2–5 show the composite and colour
// channels, Ctrl 6–9 the alpha channels, the keys the Channels panel lists.
Rectangle {
    id: dock
    implicitWidth: Tools.dockCollapsed ? 0 : 308
    implicitHeight: 796
    Layout.preferredWidth: implicitWidth  // a layout takes an implicit width of 0 for none and keeps the width it had
    color: Theme.bg  // the 1 px between the groups

    Rectangle { visible: !Tools.dockCollapsed; width: 1; height: parent.height; color: Theme.ln }
    Column {
        x: 1
        width: 307
        spacing: 1
        visible: !Tools.dockCollapsed
        Group { key: "c"; width: parent.width; active: !Tools.dockCollapsed }
        // Properties leaves the Layers tabs in the dock (Retouch under Gradients at 1280 × 800), its panel scrolled.
        Group { id: props; key: "p"; width: parent.width; room: Math.max(30, Math.min(30 + 360, dock.height - props.y - 31)); active: !Tools.dockCollapsed }
        Group { id: layers; key: "l"; width: parent.width; room: Math.max(30, dock.height - layers.y); active: !Tools.dockCollapsed }
    }

    // Ctrl 2: the composite channel (Gray in a grey document); Ctrl 3–5: Red, Green, Blue; Ctrl 6–9: the alpha
    // channels. While a document is open and no space (Liquify…), which hides the row the dock is in.
    readonly property bool channelKeys: !!Docs.current && !!parent && parent.visible
    ChannelKey { n: 2; enabled: dock.channelKeys }
    ChannelKey { n: 3; enabled: dock.channelKeys }
    ChannelKey { n: 4; enabled: dock.channelKeys }
    ChannelKey { n: 5; enabled: dock.channelKeys }
    ChannelKey { n: 6; enabled: dock.channelKeys }
    ChannelKey { n: 7; enabled: dock.channelKeys }
    ChannelKey { n: 8; enabled: dock.channelKeys }
    ChannelKey { n: 9; enabled: dock.channelKeys }
    component ChannelKey: Shortcut {
        required property int n
        sequence: "Ctrl+" + n
        onActivated: {
            const d = Docs.current
            const c = n === 2 ? "RGB" : n > 5 ? d.alphas[n - 6] : d.mode === "RGB" ? ["Red", "Green", "Blue"][n - 3] : ""
            if (c)
                d.setChannel(c)
        }
    }

    // A group of panels under its tabs, in the dock or floating beside the icon column (SideColumn) while the dock is
    // folded. key: "c", "p", "l"; room: how tall it may be, the panel scrolled past that (Layers, Channels take all of
    // it); active: its panel made. The panel shown is the tab Tools keeps; its `menu` goes to the … of the tabs.
    component Group: FocusScope {
        id: g
        required property string key
        property real room: Infinity
        property bool active: true
        property bool floating: false
        readonly property var tabs: ({ c: ["Color", "Swatches", "Gradients"], p: ["Properties", "Adjustments", "Retouch"], l: ["Layers", "Channels"] })[key] || []
        readonly property bool open: !Tools.dockMin[key]
        readonly property string tab: Tools.dockTab[key] || tabs[0] || ""
        readonly property var panel: loader.item
        implicitWidth: 308
        implicitHeight: !open ? 30 : key === "l" ? room : Math.min(30 + (panel ? panel.implicitHeight : 0), room)

        // Each panel's Component, made once, by its tab's name ("Brush Settings" is BrushSettingsPanel).
        property var kinds: ({})
        function kind(tab) {
            if (tab && !kinds[tab])
                kinds[tab] = Qt.createComponent("PhotoBeauty", tab.replace(" ", "") + "Panel", Component.PreferSynchronous, g)
            return tab ? kinds[tab] : null
        }

        RectangularShadow {
            visible: g.floating
            z: -1
            anchors.fill: parent
            offset.y: 12
            blur: 32
            radius: 8
            color: Theme.shadow
        }
        Rectangle {
            anchors.fill: parent
            radius: g.floating ? 8 : 0
            color: Theme.p1
            border.color: g.floating ? Theme.ln : "transparent"
        }
        MouseArea {  // a press or the wheel on the group stays on it: floating, the canvas is below
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            onWheel: w => { w.accepted = true }
        }
        PanelTabs {
            width: parent.width
            tabs: g.tabs
            current: g.tab
            collapsed: !g.open
            menu: g.panel ? g.panel.menu || [] : []
            onPicked: t => Tools.showPanel(t)
            onTabDoubleClicked: Tools.dockMin = Object.assign({}, Tools.dockMin, { [g.key]: g.open })
            ToolToggle {
                visible: g.key === "c" && !g.floating
                icon.name: "keyboard_double_arrow_right"
                icon.width: 16
                ToolTip.text: qsTr("Thu gọn thành icon")
                Accessible.name: qsTr("Thu dock thành cột icon")
                onClicked: {
                    Tools.dockCollapsed = true
                    Tools.dockOpen = ""
                }
            }
        }
        Flickable {
            id: body
            y: 30
            width: parent.width
            height: parent.height - 30
            visible: g.open
            contentHeight: loader.height
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            acceptedButtons: Qt.NoButton  // the wheel and the scroll bar; drags are the panel's
            ScrollBar.vertical: ScrollBar {}
            Loader {
                id: loader
                width: body.width
                height: g.key === "l" ? body.height : g.panel ? g.panel.implicitHeight : 0
                active: g.active
                sourceComponent: g.kind(g.tab)
            }
        }
    }
}
