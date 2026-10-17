pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import "ToolSpec.js" as ToolSpec
import "Presets.js" as Presets
import "Fmt.js" as Fmt

// The Brushes panel (docs/ui-tasks.md, U13; PanelsB.dc.html, the br part of Main.dc.html), beside the icon column with
// Brush Settings: a search, the size, the brush presets in groups that fold (tip, name, size, stroke), a new group, the
// brush in use saved as a preset, the preset in use deleted. A preset picked sets the size and hardness of the painting
// tool in use (Brush's, the tool turned to Brush, when the tool paints nothing), as BrushPicker's: "<bar>:size", ":hard"
// of ToolSpec.brushBar(). The presets are Tools' "brushes:groups" (Presets.BR_GROUPS until one is added or deleted),
// the groups open "brushes:open", the search "brushes:query"; the panel goes when its tab does.
Item {
    id: panel
    readonly property string bar: ToolSpec.brushBar(Tools.tool, Tools.xf)
    readonly property var start: ToolSpec.brush(bar)
    readonly property real size: Tools.value(bar + ":size", start.size)
    readonly property real hard: Tools.value(bar + ":hard", start.hard)
    readonly property var groups: Tools.value("brushes:groups", Presets.BR_GROUPS)
    readonly property var open: Tools.value("brushes:open", [true, true, false])
    readonly property string query: Tools.value("brushes:query", "")
    readonly property string folded: fold(query.trim())
    // [group, index] of the last preset of the brush in use (its size and hardness: one just saved), [-1, -1] for none.
    readonly property var inUse: {
        for (let g = groups.length - 1; g >= 0; g--) {
            for (let i = groups[g][1].length - 1; i >= 0; i--) {
                if (groups[g][1][i][1] === size && groups[g][1][i][2] === hard)
                    return [g, i]
            }
        }
        return [-1, -1]
    }
    property var menu: [
        { l: qsTr("Brush preset mới"), act: () => panel.save() },
        { l: qsTr("Nhóm mới"), act: () => panel.newGroup() },
        { l: qsTr("Xóa brush preset"), disabled: inUse[0] < 0, act: () => panel.remove() }
    ]
    implicitWidth: 280
    implicitHeight: col.implicitHeight

    // A name to search by: lower case, no marks, đ as d (the prototype's fold).
    function fold(t) { return String(t).toLowerCase().normalize("NFD").replace(/[\u0300-\u036f]/g, "").replace(/đ/g, "d") }
    function pick(b) {
        if (!ToolSpec.PAINT_T[Tools.tool])
            Tools.setTool("brush", "brush")
        Tools.setValues({ [bar + ":size"]: b[1], [bar + ":hard"]: b[2] })
    }
    function toggle(g) {
        const o = open.slice()
        o[g] = !o[g]
        Tools.setValue("brushes:open", o)
    }
    function save() {  // the brush in use, at the end of the last group, which opens
        const tip = Presets.TIPS[Tools.value(bar + ":tip", start.tip)] || Presets.TIPS[0]
        const gs = groups.map(g => [g[0], g[1].slice()]), o = open.slice()
        gs[gs.length - 1][1].push([tip[0] + " " + Fmt.vn(size), size, hard, ""])
        o[gs.length - 1] = true
        Tools.setValues({ "brushes:groups": gs, "brushes:open": o })
    }
    function newGroup() {
        const o = open.slice()
        o[groups.length] = true
        Tools.setValues({ "brushes:groups": groups.concat([[qsTr("Nhóm %1").arg(groups.length + 1), []]]), "brushes:open": o })
    }
    function remove() {  // the last preset of the brush in use
        const g = inUse[0], i = inUse[1]
        if (g >= 0)
            Tools.setValue("brushes:groups", groups.map((x, k) => k === g ? [x[0], x[1].filter((b, j) => j !== i)] : x))
    }

    Column {
        id: col
        width: parent.width
        Column {
            x: 12
            width: parent.width - 24
            topPadding: 8
            bottomPadding: 4
            spacing: 8
            TextBox {
                width: parent.width
                implicitHeight: 24
                leftPadding: 26
                placeholderText: qsTr("Tìm brush")
                text: panel.query
                Accessible.name: qsTr("Tìm brush")
                onTextEdited: Tools.setValue("brushes:query", text)
                Icon { x: 7; anchors.verticalCenter: parent.verticalCenter; name: "search"; size: 14; color: Theme.t3 }
            }
            SliderField {
                width: parent.width
                label: qsTr("Size")
                min: 1
                max: 5000
                unit: "px"
                value: panel.size
                onMoved: v => Tools.setValue(panel.bar + ":size", v)
            }
        }
        Repeater {
            model: panel.groups
            Column {
                id: grp
                required property var modelData
                required property int index
                readonly property var items: modelData[1].filter(b => !panel.folded || panel.fold(b[0]).indexOf(panel.folded) >= 0)
                readonly property bool shown: panel.folded ? items.length > 0 : !!panel.open[index]
                width: col.width
                AbstractButton {
                    id: head
                    width: parent.width
                    height: 28
                    leftPadding: 12
                    rightPadding: 12
                    text: grp.modelData[0]
                    font.pointSize: Theme.pt(12)
                    Accessible.role: Accessible.Button
                    onClicked: panel.toggle(grp.index)
                    background: Rectangle { height: 1; color: Theme.p2 }
                    contentItem: Row {
                        spacing: 6
                        Icon { anchors.verticalCenter: parent.verticalCenter; name: grp.shown ? "expand_more" : "chevron_right"; size: 14; color: Theme.t3 }
                        Icon { anchors.verticalCenter: parent.verticalCenter; name: "folder"; size: 16; color: Theme.t3 }
                        Text { anchors.verticalCenter: parent.verticalCenter; text: head.text; font: head.font; color: Theme.t2 }
                    }
                }
                Repeater {
                    model: grp.shown ? grp.items : []
                    AbstractButton {
                        id: row
                        required property var modelData
                        readonly property bool on: modelData[1] === panel.size && modelData[2] === panel.hard
                        width: grp.width
                        height: 40
                        leftPadding: 32
                        rightPadding: 12
                        text: modelData[0]
                        Accessible.checkable: true
                        Accessible.checked: on
                        onClicked: panel.pick(modelData)
                        background: Rectangle { color: row.on ? Theme.acc2 : "transparent" }
                        contentItem: Row {
                            spacing: 10
                            Item {
                                width: 28
                                height: 28
                                anchors.verticalCenter: parent.verticalCenter
                                TipShape.Glyph {
                                    readonly property int s: Math.max(6, Math.min(22, Math.round(Math.sqrt(row.modelData[1]) * 1.1)))
                                    anchors.centerIn: parent
                                    width: s
                                    height: s
                                    hard: row.modelData[2]
                                }
                            }
                            Column {
                                width: row.availableWidth - 28 - 84 - 20
                                anchors.verticalCenter: parent.verticalCenter
                                Text {
                                    width: parent.width
                                    text: row.modelData[0]
                                    color: Theme.t1
                                    font.pointSize: Theme.pt(12)
                                    elide: Text.ElideRight
                                }
                                Text {
                                    width: parent.width
                                    text: qsTr("%1 px").arg(Fmt.vn(row.modelData[1])) + row.modelData[3]
                                    color: Theme.t3
                                    font.family: Theme.mono
                                    font.pointSize: Theme.pt(10.5)
                                    elide: Text.ElideRight
                                }
                            }
                            StrokePreview {
                                width: 84
                                height: 18
                                anchors.verticalCenter: parent.verticalCenter
                                size: row.modelData[1]
                                hardness: row.modelData[2]
                                minSize: 0.2  // the pressure of the brush's Shape Dynamics, as the artboard's tapering stroke
                                color: Theme.t2
                                opacity: row.on ? 1 : 0.8
                            }
                        }
                    }
                }
            }
        }
        Item {  // .foot
            width: parent.width
            height: 32
            Rectangle { width: parent.width; height: 1; color: Theme.ln }
            Row {
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                ToolToggle {
                    icon.name: "create_new_folder"
                    ToolTip.text: qsTr("Nhóm mới · Create new group")
                    Accessible.name: qsTr("Nhóm mới")
                    onClicked: panel.newGroup()
                }
                ToolToggle {
                    icon.name: "add_box"
                    ToolTip.text: qsTr("Brush preset mới · Create new brush")
                    Accessible.name: qsTr("Lưu brush đang dùng thành preset")
                    onClicked: panel.save()
                }
                ToolToggle {
                    enabled: panel.inUse[0] >= 0
                    icon.name: "delete"
                    ToolTip.text: qsTr("Xóa brush preset · Delete brush")
                    Accessible.name: qsTr("Xóa brush preset")
                    onClicked: panel.remove()
                }
            }
        }
    }
}
