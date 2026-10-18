pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import "Color.js" as Color

// The Swatches panel (docs/ui-tasks.md, U11; PanelsB's Swatches, the swc part of Main.dc.html), a tab of the dock's group
// Color, Swatches, Gradients: the 10 recent colours (Tools.recentSwatches), then the groups, which fold; a click makes a
// swatch the foreground colour (and the swatch picked, ringed). At the foot: a new group, the foreground added to the
// group of the swatch picked, the swatch picked deleted. The library is Prefs "swatches" (Color.swatches()).
Item {
    id: panel
    readonly property var lib: Color.swatches(Prefs.values.swatches)
    readonly property bool canDelete: !!lib.sel && lib.sel[1] >= 0
    // new only when the groups change: a new model rebuilds the grid (and a pick would lose the tile it was clicked on)
    readonly property var groups: JSON.parse(rev)
    readonly property string rev: JSON.stringify(lib.groups)
    property var menu: [
        { l: qsTr("Swatch mới"), act: () => panel.add() },
        { l: qsTr("Nhóm mới"), act: () => panel.newGroup() },
        { t: "sep" },
        { l: qsTr("Xóa swatch"), disabled: !canDelete, act: () => panel.remove() },
        { t: "sep" },
        { l: qsTr("Khôi phục swatch mặc định"), act: () => panel.set(undefined) }
    ]
    implicitWidth: 308
    implicitHeight: col.implicitHeight + 32

    function set(st) { Prefs.set("swatches", st) }
    function add() { set(Color.addSwatch(Prefs.values.swatches, Tools.fg)) }
    function newGroup() { set(Color.newSwatchGroup(Prefs.values.swatches, qsTr("Nhóm"))) }
    function remove() { set(Color.deleteSwatch(Prefs.values.swatches)) }
    function pick(g, i) {
        Tools.setFg(lib.groups[g][1][i], undefined, true)
        set({ groups: lib.groups, open: lib.open, sel: [g, i] })
    }
    function toggle(g) {
        const o = lib.open.slice()
        o[g] = !o[g]
        set({ groups: lib.groups, open: o, sel: lib.sel })
    }

    Column {
        id: col
        x: 12
        y: 8
        width: parent.width - 24
        spacing: 8
        bottomPadding: 10
        Row {
            spacing: 4
            Repeater {
                model: 10  // kept while the row reorders under a click
                Swatch {
                    id: sw
                    required property int index
                    readonly property string hex: Tools.recentSwatches[index] || ""
                    visible: hex !== ""
                    width: 20
                    height: 20
                    radius: 4
                    ring: Qt.rgba(1, 1, 1, 0.08)
                    color: hex || "transparent"
                    ToolTip.text: hex.toUpperCase()
                    Accessible.name: qsTr("Chọn màu %1").arg(hex.toUpperCase())
                    onClicked: Tools.setFg(sw.hex, undefined, true)
                }
            }
        }
        PresetGrid {
            width: parent.width
            spacing: 8  // .sec: 8 between the groups
            groups: panel.groups
            open: panel.lib.open
            current: panel.lib.sel || [-1, -1]
            tileWidth: 21  // 22 and 4 apart in the prototype: the same pitch with PresetGrid's 5, ten a row
            tileHeight: 21
            radius: 4
            delegate: Component {
                Rectangle {
                    required property var modelData
                    anchors.fill: parent
                    radius: 4
                    color: modelData
                    border.color: Qt.rgba(1, 1, 1, 0.08)
                }
            }
            onPicked: (g, i) => panel.pick(g, i)
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
            ToolTip.text: qsTr("Thêm swatch · Create new swatch")
            Accessible.name: qsTr("Thêm swatch")
            onClicked: panel.add()
        }
        ToolToggle {  // lit with none picked, as PanelsB's: then it deletes nothing
            icon.name: "delete"
            ToolTip.text: qsTr("Xóa swatch · Delete swatch")
            Accessible.name: qsTr("Xóa swatch")
            onClicked: panel.remove()
        }
    }
}
