pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import "ToolSpec.js" as ToolSpec
import "Presets.js" as Presets
import "Fmt.js" as Fmt

// The Brush Settings panel (docs/ui-tasks.md, U13; PanelsB.dc.html, the bset part of Main.dc.html; F5), beside the icon
// column with Brushes: the brush of the painting tool in use (ToolSpec.brushBar(): Brush's when the tool paints
// nothing). Brush Tip Shape: the six tips, Size, Flip X, Y, angle and roundness (TipShape and two fields), Hardness,
// Spacing; Shape Dynamics and Transfer (jitters, their Control, minimums), Smoothing, Build-up, Noise, Wet Edges, each
// with its box; the stroke the brush draws (StrokePreview); the brush saved as a preset. Values in Tools, by
// "<bar>:<key>" of DEF: size, hard, spacing, tip as BrushPicker's (ToolSpec.brush()); the rest this panel's, where angle
// and roundness not set are the tip's (a tip picked takes them back). The sections open: "bset:open".
Item {
    id: panel
    readonly property string bar: ToolSpec.brushBar(Tools.tool, Tools.xf)
    readonly property var start: ToolSpec.brush(bar)
    readonly property int tip: Tools.value(bar + ":tip", start.tip)
    readonly property var tipRow: Presets.TIPS[tip] || Presets.TIPS[0]
    readonly property var def: {
        const o = { size: start.size, hard: start.hard, spacing: start.spacing, tip: start.tip, spacingOn: true, flipX: false, flipY: false,
            angle: -tipRow[3], roundness: tipRow[4] * 100,  // TIPS turns as CSS does, clockwise
            sizeJitter: 0, sizeControl: "Pen Pressure", minDiameter: 20, angleJitter: 0, angleControl: "Pen Tilt", roundJitter: 0, roundControl: "Off",
            minRoundness: 25, opacityJitter: 0, opacityControl: "Pen Pressure", opacityMin: 0, flowJitter: 0, flowControl: "Pen Pressure", flowMin: 0 }
        for (const r of Presets.BS_ROWS)
            o[r[0]] = r[2] === 1  // shape, transfer, smooth, build, noise, wet: on
        return o
    }
    // What Shape Dynamics and Transfer open onto: [key, label] for a slider (%), [key] for its Control.
    readonly property var more: ({
        shape: [["sizeJitter", qsTr("Size Jitter")], ["sizeControl"], ["minDiameter", qsTr("Minimum Diameter")], ["angleJitter", qsTr("Angle Jitter")],
            ["angleControl"], ["roundJitter", qsTr("Roundness Jitter")], ["roundControl"], ["minRoundness", qsTr("Minimum Roundness")]],
        transfer: [["opacityJitter", qsTr("Opacity Jitter")], ["opacityControl"], ["opacityMin", qsTr("Minimum")], ["flowJitter", qsTr("Flow Jitter")],
            ["flowControl"], ["flowMin", qsTr("Minimum")]]
    })
    readonly property var opened: Tools.value("bset:open", { shape: true })
    property var menu: [{ l: qsTr("Lưu thành Brush preset mới"), act: () => panel.save() }]
    implicitWidth: 280
    implicitHeight: col.implicitHeight

    function value(k) { return Tools.value(bar + ":" + k, def[k]) }
    function set(k, v) { Tools.setValue(bar + ":" + k, v) }
    function pickTip(i) {  // a tip brings its hardness, angle and roundness
        Tools.setValues({ [bar + ":tip"]: i, [bar + ":hard"]: Presets.TIPS[i][1], [bar + ":angle"]: null, [bar + ":roundness"]: null })
    }
    function toggleOpen(k) { Tools.setValue("bset:open", Object.assign({}, opened, { [k]: !opened[k] })) }
    function save() {  // as Brushes' Brush preset mới: at the end of the last group, which opens
        const groups = Tools.value("brushes:groups", Presets.BR_GROUPS), open = Tools.value("brushes:open", [true, true, false])
        const gs = groups.map(g => [g[0], g[1].slice()]), o = open.slice()
        gs[gs.length - 1][1].push([tipRow[0] + " " + Fmt.vn(value("size")), value("size"), value("hard"), ""])
        o[gs.length - 1] = true
        Tools.setValues({ "brushes:groups": gs, "brushes:open": o })
    }

    Column {
        id: col
        width: parent.width
        Rectangle {  // Brush Tip Shape
            width: parent.width
            height: 28
            color: Theme.sel
            Text {
                x: 31
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Brush Tip Shape")
                color: Theme.t1
                font.pointSize: Theme.pt(12)
                font.weight: Font.Medium
            }
        }
        Column {
            id: shape
            x: 12
            width: parent.width - 24
            topPadding: 8
            bottomPadding: 10
            spacing: 8
            Row {
                spacing: 4
                Repeater {
                    model: Presets.TIPS
                    AbstractButton {
                        id: tb
                        required property var modelData
                        required property int index
                        readonly property bool on: index === panel.tip
                        width: (shape.width - 20) / 6
                        height: 40
                        text: modelData[0]
                        hoverEnabled: true
                        ToolTip.visible: hovered
                        ToolTip.delay: 600
                        ToolTip.text: text
                        Accessible.name: qsTr("Đầu cọ %1").arg(modelData[0])
                        Accessible.checkable: true
                        Accessible.checked: on
                        onClicked: panel.pickTip(index)
                        background: Rectangle {
                            radius: 4
                            color: Theme.bg
                            Rectangle {  // the tip in use: a 2 px ring outside
                                visible: tb.on
                                anchors.fill: parent
                                anchors.margins: -2
                                radius: 6
                                color: "transparent"
                                border.width: 2
                                border.color: Theme.accent
                            }
                        }
                        contentItem: Item {
                            TipShape.Glyph {
                                anchors.centerIn: parent
                                width: 24
                                height: 24
                                hard: tb.modelData[1]
                                square: tb.modelData[2] !== "50%"
                                angle: -tb.modelData[3]
                                roundness: tb.modelData[4]
                                dots: tb.index === 5
                            }
                        }
                    }
                }
            }
            SliderField {
                width: parent.width
                label: qsTr("Size")
                min: 1
                max: 5000
                unit: "px"
                value: panel.value("size")
                onMoved: v => panel.set("size", v)
            }
            Row {
                spacing: 14
                CheckRow { text: qsTr("Flip X"); checked: panel.value("flipX"); onClicked: panel.set("flipX", !checked) }
                CheckRow { text: qsTr("Flip Y"); checked: panel.value("flipY"); onClicked: panel.set("flipY", !checked) }
            }
            Row {
                spacing: 12
                TipShape {
                    angle: panel.value("angle")
                    roundness: panel.value("roundness")
                    onMoved: (a, r) => Tools.setValues({ [panel.bar + ":angle"]: a, [panel.bar + ":roundness"]: r })
                }
                Column {
                    width: shape.width - 84 - 12
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6
                    NumberField {
                        width: parent.width
                        label: qsTr("Angle")
                        labelColor: Theme.t3
                        unit: "°"
                        min: -180
                        max: 180
                        value: panel.value("angle")
                        defaultValue: panel.def.angle
                        onMoved: v => panel.set("angle", v)
                    }
                    NumberField {
                        width: parent.width
                        label: qsTr("Roundness")
                        labelColor: Theme.t3
                        unit: "%"
                        min: 0
                        max: 100
                        value: panel.value("roundness")
                        defaultValue: panel.def.roundness
                        onMoved: v => panel.set("roundness", v)
                    }
                }
            }
            SliderField {
                width: parent.width
                label: qsTr("Hardness")
                unit: "%"
                value: panel.value("hard")
                onMoved: v => panel.set("hard", v)
            }
            Item {  // Spacing, with its box: off, the dabs follow the speed of the stroke
                width: parent.width
                height: 25
                CheckRow {
                    height: 16
                    text: qsTr("Spacing")
                    checked: panel.value("spacingOn")
                    onClicked: panel.set("spacingOn", !checked)
                }
                Text {
                    anchors.right: parent.right
                    text: Fmt.vn(spacing.value) + "%"
                    color: spacing.enabled ? Theme.t1 : Theme.t4
                    font.family: Theme.mono
                    font.pointSize: Theme.pt(11.5)
                }
                SliderField {
                    id: spacing
                    y: 16
                    width: parent.width
                    enabled: panel.value("spacingOn")
                    min: 1
                    max: 1000
                    unit: "%"
                    value: panel.value("spacing")
                    Accessible.name: qsTr("Spacing")
                    onMoved: v => panel.set("spacing", v)
                }
            }
        }
        Repeater {
            model: Presets.BS_ROWS
            Column {
                id: sec
                required property var modelData
                readonly property string key: modelData[0]
                readonly property var rows: panel.more[key] || []
                readonly property bool open: rows.length > 0 && !!panel.opened[key]
                width: col.width
                Item {
                    width: parent.width
                    height: 30
                    Rectangle { width: parent.width; height: 1; color: Theme.p2 }
                    CheckRow {
                        x: 12
                        anchors.verticalCenter: parent.verticalCenter
                        text: sec.modelData[1]
                        font.weight: sec.rows.length > 0 ? Font.Medium : Font.Normal  // the sections that open, as the artboard
                        checked: panel.value(sec.key)
                        Accessible.name: qsTr("Bật %1").arg(sec.modelData[1])
                        onClicked: panel.set(sec.key, !checked)
                    }
                    ToolToggle {
                        visible: sec.rows.length > 0
                        x: parent.width - 8 - width
                        anchors.verticalCenter: parent.verticalCenter
                        icon.name: sec.open ? "expand_more" : "chevron_right"
                        icon.width: 14
                        Accessible.name: qsTr("Mở %1").arg(sec.modelData[1])
                        onClicked: panel.toggleOpen(sec.key)
                    }
                }
                Column {
                    visible: sec.open
                    x: 12
                    width: parent.width - 24
                    topPadding: 8
                    bottomPadding: 10
                    spacing: 8
                    Repeater {
                        model: sec.open ? sec.rows : []
                        Loader {
                            id: cell
                            required property var modelData
                            width: parent.width
                            sourceComponent: modelData.length > 1 ? sliderC : controlC
                            Component {
                                id: sliderC
                                SliderField {
                                    label: cell.modelData[1]
                                    unit: "%"
                                    value: panel.value(cell.modelData[0])
                                    onMoved: v => panel.set(cell.modelData[0], v)
                                }
                            }
                            Component {
                                id: controlC
                                Item {
                                    implicitHeight: 24
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: qsTr("Control")
                                        color: Theme.t3
                                        font.pointSize: Theme.pt(12)
                                    }
                                    Dropdown {
                                        x: 72
                                        width: parent.width - x
                                        options: ToolSpec.LISTS.bsCtl
                                        value: panel.value(cell.modelData[0])
                                        Accessible.name: qsTr("Control")
                                        onPicked: v => panel.set(cell.modelData[0], v)
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        Item {  // the stroke the brush draws
            width: parent.width
            height: 80
            Rectangle {
                x: 12
                y: 8
                width: parent.width - 24
                height: 64
                radius: 6
                color: Theme.bg
                StrokePreview {
                    anchors.centerIn: parent
                    width: Math.min(260, parent.width - 16)
                    height: 40
                    size: panel.value("size")
                    hardness: panel.value("hard")
                    spacing: panel.value("spacingOn") ? panel.value("spacing") : 25
                    angle: panel.value("flipX") !== panel.value("flipY") ? -panel.value("angle") : panel.value("angle")
                    roundness: panel.value("roundness")
                    square: panel.tipRow[2] !== "50%"
                    minSize: panel.value("shape") && panel.value("sizeControl") === "Pen Pressure" ? panel.value("minDiameter") / 100 : 1
                    minOpacity: panel.value("transfer") && panel.value("opacityControl") === "Pen Pressure" ? panel.value("opacityMin") / 100 : 1
                    Accessible.role: Accessible.Graphic
                    Accessible.name: qsTr("Nét xem trước")
                }
            }
        }
        Item {  // .foot
            width: parent.width
            height: 32
            Rectangle { width: parent.width; height: 1; color: Theme.ln }
            Text {
                x: 12
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Lưu thành Brush preset mới")
                color: Theme.t3
                font.pointSize: Theme.pt(11)
            }
            ToolToggle {
                x: parent.width - 8 - width
                anchors.verticalCenter: parent.verticalCenter
                icon.name: "add_box"
                ToolTip.text: qsTr("Lưu thành Brush preset mới · Create new brush")
                Accessible.name: qsTr("Lưu thành Brush preset mới")
                onClicked: panel.save()
            }
        }
    }
}
