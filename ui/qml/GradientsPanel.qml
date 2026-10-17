pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import "Color.js" as Color
import "Presets.js" as Presets

// The Gradients panel (docs/ui-tasks.md, U11; PanelsB's Gradients, the grc part of Main.dc.html), a tab of the dock's
// group Color, Swatches, Gradients: the 5 recent gradients (Tools.recentGradients), then the groups, which fold; a click
// makes one the gradient (Tools.pickGradient), ringed. At the foot: a new group, a gradient from the foreground to the
// background colour added to the group of the one picked, the one picked deleted. The library is Presets.GR_GROUPS,
// which only grows (Color.js says why), with Prefs "gradients" (Color.gradients()) for what is hidden, open, added to.
Item {
    id: panel
    readonly property var lib: Color.gradients(Prefs.values.gradients)
    // the groups as shown: [name, [[name, CSS, index in Presets]…]], hidden ones left out; new only when they change, as
    // a new model rebuilds the grid (and a pick would lose the tile it was clicked on)
    readonly property string rev: JSON.stringify(Presets.GR_GROUPS.map((g, gi) => [g[0], g[1].map((x, i) => [x[0], x[1], i]).filter(x => lib.hidden.indexOf(gi + "," + x[2]) < 0)]))
    readonly property var groups: JSON.parse(rev)
    readonly property var current: [Tools.gradient[0], groups[Tools.gradient[0]] ? groups[Tools.gradient[0]][1].findIndex(x => x[2] === Tools.gradient[1]) : -1]
    readonly property bool canDelete: current[1] >= 0
    readonly property var recent: Tools.recentGradients.filter(p => !!panel.item(p[0], p[1]))
    readonly property int recentWidth: Math.floor((width - 24 - 5 * 5) / 6)
    property var menu: [
        { l: qsTr("Gradient mới"), act: () => panel.add() },
        { l: qsTr("Nhóm mới"), act: () => panel.newGroup() },
        { t: "sep" },
        { l: qsTr("Xóa gradient"), disabled: !canDelete, act: () => panel.remove() },
        { t: "sep" },
        { l: qsTr("Khôi phục gradient mặc định"), act: () => panel.restore() }
    ]
    implicitWidth: 308
    implicitHeight: col.implicitHeight + 32

    function item(g, i) {  // [name, CSS] of a gradient not hidden, else null
        const grp = Presets.GR_GROUPS[g]
        return grp && grp[1][i] && lib.hidden.indexOf(g + "," + i) < 0 ? grp[1][i] : null
    }
    function set(patch) { Prefs.set("gradients", Object.assign({}, lib, patch)) }
    function pick(g, i) {  // i: in Presets
        Tools.pickGradient(g, i)
        set({ group: g })
    }
    function add() {
        const g = lib.group >= 0 ? lib.group : Presets.GR_GROUPS[Tools.gradient[0]] ? Tools.gradient[0] : Presets.GR_GROUPS.length - 1
        const i = Color.addGradient(g, Color.nextName(Presets.GR_GROUPS[g][1], qsTr("Gradient")), "linear-gradient(90deg, " + Tools.fg + ", " + Tools.bg + ")")
        const o = lib.open.slice()
        o[g] = true
        set({ open: o, group: g })
        Tools.pickGradient(g, i)
    }
    function newGroup() {
        const g = Color.newGradientGroup(qsTr("Nhóm"))
        set({ open: lib.open.concat([true]), group: g })
    }
    function remove() {
        if (canDelete)
            set({ hidden: lib.hidden.concat([Tools.gradient[0] + "," + Tools.gradient[1]]) })
    }
    function restore() {  // as the presets come: what was added or deleted goes
        Color.restoreGradients()
        Prefs.set("gradients", undefined)
    }
    function toggle(g) {
        const o = lib.open.slice()
        o[g] = !o[g]
        set({ open: o })
    }

    // A gradient's tile from its CSS: left to right, as Qt Quick's Gradient draws it, over the transparency checker.
    component Tile: Item {
        id: tile
        property string css
        readonly property var grad: Color.gradient(css)
        Canvas {  // .ck: 4 px squares, white then grey
            visible: Color.seeThrough(tile.grad)
            anchors.fill: parent
            onWidthChanged: requestPaint()
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                ctx.beginPath()
                ctx.roundedRect(0, 0, width, height, 4, 4)
                ctx.clip()
                ctx.fillStyle = "#ffffff"
                ctx.fillRect(0, 0, width, height)
                ctx.fillStyle = "#c9c9c9"
                for (let y = 0; y < height; y += 4)
                    for (let x = y % 8 ? 0 : 4; x < width; x += 8)
                        ctx.fillRect(x, y, 4, 4)
            }
        }
        Row {
            Repeater {
                model: Color.pieces(tile.grad)
                Rectangle {
                    id: piece
                    required property var modelData
                    required property int index
                    readonly property bool first: index === 0
                    readonly property bool last: modelData.x + modelData.w >= 1
                    width: modelData.w * tile.width
                    height: tile.height
                    topLeftRadius: first ? 4 : 0
                    bottomLeftRadius: first ? 4 : 0
                    topRightRadius: last ? 4 : 0
                    bottomRightRadius: last ? 4 : 0
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0; color: piece.modelData.a }
                        GradientStop { position: 1; color: piece.modelData.b }
                    }
                }
            }
        }
        Rectangle {  // the 1 px ring of .gt
            anchors.fill: parent
            radius: 4
            color: "transparent"
            border.color: Theme.ln
        }
    }

    Column {
        id: col
        x: 12
        y: 8
        width: parent.width - 24
        spacing: 8
        bottomPadding: 10
        Row {
            spacing: 5
            Repeater {
                model: 5  // kept while the row reorders under a click
                AbstractButton {
                    id: rt
                    required property int index
                    readonly property var at: panel.recent[index]
                    readonly property var gr: at ? panel.item(at[0], at[1]) : null
                    readonly property bool on: !!at && at[0] === Tools.gradient[0] && at[1] === Tools.gradient[1]
                    visible: !!gr
                    width: panel.recentWidth
                    height: 30
                    hoverEnabled: true
                    ToolTip.visible: hovered && !!gr
                    ToolTip.delay: 600
                    ToolTip.text: gr ? gr[0] : ""
                    Accessible.name: qsTr("Gradient gần đây %1").arg(gr ? gr[0] : "")
                    onClicked: panel.pick(rt.at[0], rt.at[1])
                    background: Tile { css: rt.gr ? rt.gr[1] : "" }
                    Rectangle {  // ringed in the accent while it is the gradient
                        visible: rt.on
                        anchors.fill: parent
                        anchors.margins: -2
                        radius: 6
                        color: "transparent"
                        border.width: 2
                        border.color: Theme.accent
                    }
                }
            }
        }
        PresetGrid {
            width: parent.width
            spacing: 8  // .sec: 8 between the groups
            groups: panel.groups
            open: panel.lib.open
            current: panel.current
            tileWidth: Math.floor((width - 20 - 5 * 5) / 6)
            tileHeight: 30
            radius: 4
            delegate: Component {
                Tile {
                    required property var modelData
                    anchors.fill: parent
                    css: modelData[1]
                }
            }
            onPicked: (g, i) => panel.pick(g, panel.groups[g][1][i][2])
            onToggled: g => panel.toggle(g)
        }
    }

    // .foot
    Rectangle {
        y: parent.height - 32
        width: parent.width
        height: 1
        color: Theme.ln
    }
    Row {
        anchors.right: parent.right
        anchors.rightMargin: 8
        y: parent.height - 28
        spacing: 2
        ToolToggle {
            icon.name: "create_new_folder"
            ToolTip.text: qsTr("Nhóm mới · Create new group")
            Accessible.name: qsTr("Nhóm mới")
            onClicked: panel.newGroup()
        }
        ToolToggle {
            icon.name: "add_box"
            ToolTip.text: qsTr("Thêm gradient · Create new gradient")
            Accessible.name: qsTr("Thêm gradient")
            onClicked: panel.add()
        }
        ToolToggle {
            enabled: panel.canDelete
            icon.name: "delete"
            ToolTip.text: qsTr("Xóa gradient · Delete gradient")
            Accessible.name: qsTr("Xóa gradient")
            onClicked: panel.remove()
        }
    }
}
