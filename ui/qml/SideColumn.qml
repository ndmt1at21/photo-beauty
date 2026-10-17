pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic

// The icon column, 36 wide (docs/ui-tasks.md, U8; Main.dc.html's "Panel thu gọn", the Dock card of Controls): while
// the dock is folded, « unfolds it and an icon for each of its groups floats that group (Dock.Group) beside the
// column; then the side panels, History; Brushes, Brush Settings; Navigator, Histogram, Info; Character, Paragraph,
// each opening SidePanel beside the column with the tabs of its group. One floats at a time; a press on the canvas
// closes it and goes no further, Esc in it closes it and takes the focus back to its icon. What floats is Tools'
// (sidePanel, dockOpen), which the Window menu and F5, F8 set too.
Rectangle {
    id: col
    implicitWidth: 36
    implicitHeight: 796
    color: Theme.p1

    // The prototype's SIDE (0 a rule) and SIDE_KEY, and the dock's groups as the folded dock shows them.
    readonly property var side: [["History", "history"], ["Brushes", "brush"], ["Brush Settings", "instant_mix"], 0, ["Navigator", "explore"], ["Histogram", "area_chart"],
        ["Info", "info"], 0, ["Character", "text_fields"], ["Paragraph", "format_paragraph"]]
    readonly property var sideKeys: ({ "Brush Settings": "F5", Info: "F8" })
    readonly property var groups: [["c", "Color, Swatches, Gradients", "palette"], ["p", "Properties, Adjustments, Retouch", "tune"], ["l", "Layers, Channels", "layers"]]

    readonly property bool floating: Tools.sidePanel !== "" || (Tools.dockCollapsed && Tools.dockOpen !== "")
    // The canvas, DocTabs over Viewport: the item before this column in Main's row (U4).
    readonly property Item canvas: {
        const s = parent ? parent.children : []
        for (let i = 1; i < s.length; i++)
            if (s[i] === col)
                return s[i - 1]
        return null
    }

    // What floats, closed; with the tag of an icon ("icon:History", "icon:l"), the focus goes to that icon.
    function close(tag) {
        Tools.sidePanel = ""
        Tools.dockOpen = ""
        const find = (item) => {
            for (const c of item.children) {
                const f = c.objectName === tag ? c : find(c)
                if (f)
                    return f
            }
            return null
        }
        const icon = tag ? find(icons) : null
        if (icon)
            icon.forceActiveFocus(Qt.ShortcutFocusReason)
    }
    // Opened from the keyboard: the focus goes on into what floats.
    function enter(box) {
        Qt.callLater(() => {
            const f = box.nextItemInFocusChain(true)
            let p = f
            while (p && p !== box)
                p = p.parent
            if (p)
                f.forceActiveFocus(Qt.TabFocusReason)
        })
    }
    Connections {  // one floats at a time
        target: Tools
        function onSidePanelChanged() { if (Tools.sidePanel) Tools.dockOpen = "" }
        function onDockOpenChanged() { if (Tools.dockOpen) Tools.sidePanel = "" }
    }

    Rectangle { width: 1; height: parent.height; color: Theme.ln }
    // While something floats, a press on the canvas closes it and stays here, as the prototype's scrim. Hover goes on to
    // the canvas: Info (U12) reads the pixel under the pointer, the rulers mark it.
    MouseArea {
        readonly property Item area: col.canvas
        visible: col.floating && !!area
        x: area ? area.x - col.x : 0
        y: area ? area.y - col.y : 0
        width: area ? area.width : 0
        height: area ? area.height : 0
        acceptedButtons: Qt.AllButtons
        cursorShape: Qt.ArrowCursor
        onPressed: col.close("")
    }

    Column {
        id: icons
        x: 4
        y: 6
        spacing: 2
        ToolToggle {
            visible: Tools.dockCollapsed
            implicitWidth: 28
            icon.name: "keyboard_double_arrow_left"
            icon.width: 16
            ToolTip.text: qsTr("Mở rộng dock")
            Accessible.name: ToolTip.text
            onClicked: {
                Tools.dockCollapsed = false
                Tools.dockOpen = ""
            }
        }
        Repeater {
            model: col.groups
            IconButton {
                required property var modelData
                visible: Tools.dockCollapsed
                objectName: "icon:" + modelData[0]
                on: Tools.dockOpen === modelData[0]
                icon.name: modelData[2]
                ToolTip.text: modelData[1]
                Accessible.name: modelData[1]
                onClicked: {
                    const keys = visualFocus
                    Tools.dockOpen = Tools.dockOpen === modelData[0] ? "" : modelData[0]
                    if (keys && on)
                        col.enter(fly)
                }
            }
        }
        Rule { visible: Tools.dockCollapsed }
        Repeater {
            model: col.side
            Item {
                id: entry
                required property var modelData
                width: 28
                height: modelData ? 28 : 9
                Rule { visible: !entry.modelData }
                IconButton {
                    readonly property string name: entry.modelData ? entry.modelData[0] : ""
                    visible: !!entry.modelData
                    objectName: "icon:" + name
                    on: Tools.sidePanel === name
                    icon.name: entry.modelData ? entry.modelData[1] : ""
                    ToolTip.text: col.sideKeys[name] ? "%1 (%2)".arg(name).arg(col.sideKeys[name]) : name
                    Accessible.name: ToolTip.text
                    onClicked: {
                        const keys = visualFocus
                        Tools.showPanel(name)
                        if (keys && on)
                            col.enter(pop)
                    }
                }
            }
        }
    }

    SidePanel {
        id: pop
        x: -width - 4
        y: 6
        maxHeight: col.height - 12
        Keys.onShortcutOverride: e => { e.accepted = e.key === Qt.Key_Escape }
        Keys.onEscapePressed: col.close("icon:" + Tools.sidePanel)
    }
    // The dock's group whose icon is on, beside the column while the dock is folded.
    Dock.Group {
        id: fly
        key: Tools.dockOpen || "c"
        floating: true
        visible: Tools.dockCollapsed && Tools.dockOpen !== ""
        active: visible
        x: -width - 1
        y: 6
        width: 308
        room: col.height - 16
        Keys.onShortcutOverride: e => { e.accepted = e.key === Qt.Key_Escape }
        Keys.onEscapePressed: col.close("icon:" + Tools.dockOpen)
    }

    component Rule: Item {  // 18 × 1, 4 px above and below
        width: 28
        height: 9
        Rectangle { anchors.centerIn: parent; width: 18; height: 1; color: Theme.ln }
    }
}
