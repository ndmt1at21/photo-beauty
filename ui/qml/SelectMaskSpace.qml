pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import "View.js" as View
import "Mock.js" as Mock
import "Fmt.js" as Fmt

// Select and Mask (docs/ui-tasks.md, U15; artboard SelectMask; Select › Select and Mask…, Ctrl Alt R, the selection
// tools' bars, a mask's Properties): Refine Edge Brush, Brush, Lasso, Hand, Zoom; the 7 views (O M V A T K Y) drawn
// over the photo by the mock selection's shape (Mock.SHAPES; decision 3: no pixels change), Show Edge (J), Show
// Original (P); Refine Mode, Edge Detection, Global Refinements, Clear Selection, Invert, Output To, Remember Settings.
// It opens on the document's selection (the subject without one, as the artboard), the settings Remember Settings kept
// (Tools.values.SelectMask) or the prototype's. OK makes the output one step of the document's History, "Select and
// Mask" (New Document: a copy of it); Hủy and Esc leave everything as it was.
Workspace {
    id: sm
    // The prototype's tables. Tools: id, icon, name, key, its +/− modes ([label, English]), a brush size, Sample All Layers.
    readonly property var toolSpec: [
        ["smRefine", "refine_edge", qsTr("Refine Edge Brush"), "R", [[qsTr("Mở rộng vùng dò cạnh"), "Expand Detection Area"], [qsTr("Thu lại về cạnh gốc"), "Restore Original Edge"]], true, true],
        ["smBrush", "brush", qsTr("Brush"), "B", [[qsTr("Cộng vào vùng chọn"), "Add to Selection"], [qsTr("Trừ khỏi vùng chọn"), "Subtract from Selection"]], true, false],
        ["smLasso", "lasso_select", qsTr("Lasso, Polygonal Lasso"), "L", [[qsTr("Cộng vào vùng chọn"), "Add to Selection"], [qsTr("Trừ khỏi vùng chọn"), "Subtract from Selection"]], false, false],
        ["hand", "pan_tool", qsTr("Hand"), "H", null, false, false],
        ["zoom", "zoom_in", qsTr("Zoom"), "Z", [[qsTr("Phóng to"), "Zoom In"], [qsTr("Thu nhỏ"), "Zoom Out"]], false, false]
    ]
    readonly property var views: [["onion", qsTr("Onion Skin"), "O"], ["ants", qsTr("Marching Ants"), "M"], ["overlay", qsTr("Overlay"), "V"], ["black", qsTr("On Black"), "A"],
        ["white", qsTr("On White"), "T"], ["bw", qsTr("Black & White"), "K"], ["layers", qsTr("On Layers"), "Y"]]
    readonly property var colors: [["red", qsTr("Đỏ"), "#ff0000"], ["green", qsTr("Xanh lá"), "#00ff00"], ["blue", qsTr("Xanh dương"), "#0050ff"], ["yellow", qsTr("Vàng"), "#ffd800"]]
    readonly property var outs: [["Selection", qsTr("Selection")], ["Layer Mask", qsTr("Layer Mask")], ["New Layer", qsTr("New Layer")],
        ["New Layer with Layer Mask", qsTr("New Layer with Layer Mask")], ["New Document", qsTr("New Document")]]
    // Global Refinements: key, name, min, max, step, default, unit.
    readonly property var refinements: [["smooth", qsTr("Smooth"), 0, 100, 1, 0, ""], ["feather", qsTr("Feather"), 0, 250, 0.1, 0.8, "px"],
        ["contrast", qsTr("Contrast"), 0, 100, 1, 12, "%"], ["shift", qsTr("Shift Edge"), -100, 100, 1, -8, "%"]]

    property var v: ({})  // the settings by the prototype's keys, get() the default of one not set
    property var sel: ({ shape: null, inverse: false })  // what is selected: a Mock.SHAPES key (null: nothing), inverted
    readonly property var tool: toolSpec.find(t => t[0] === Tools.tool) || toolSpec[0]
    readonly property string mode: get("view", "overlay")
    readonly property bool original: get("orig", false)
    readonly property real opacityPct: get("op", 50)
    readonly property var colour: colors.find(c => c[0] === get("oc", "red")) || colors[0]
    readonly property bool tinted: ["onion", "overlay", "black", "white"].indexOf(mode) >= 0  // Opacity applies
    // What is not selected is drawn with: the checker of transparency, the overlay's colour, black, white; "" nothing.
    readonly property string ground: ({ onion: "checker", overlay: colour[2], black: "#000000", white: "#ffffff", bw: "#000000", layers: "checker" })[mode] || ""
    readonly property string label: original ? qsTr("Show Original · ảnh gốc") : [views.find(x => x[0] === mode)[1]].concat(tinted ? [Math.round(opacityPct) + "%"] : [], [({
        onion: qsTr("phần trong suốt là phần không được chọn"), ants: qsTr("đường nét đứt là cạnh vùng chọn"),
        overlay: qsTr("phần %1 là phần không được chọn").arg(colour[1].toLowerCase()), black: qsTr("phần đen là phần không được chọn"),
        white: qsTr("phần trắng là phần không được chọn"), bw: qsTr("trắng là phần được chọn, đen là phần không được chọn"),
        layers: qsTr("phần trong suốt là phần không được chọn")
    })[mode]]).join(" · ")
    readonly property int brushSize: get("size_" + tool[0], 30)

    function get(k, d) {
        const x = v[k]
        return x === undefined || x === null ? d : x
    }
    function put(o) { v = Object.assign({}, v, o) }
    function setMode(i) {  // the tool's + or −; Zoom's is the Zoom tool's own
        if (tool[0] === "zoom")
            Tools.setValue("zoom:2", i)
        else
            put({ ["mode_" + tool[0]]: i })
    }
    function selectSubject() { sel = { shape: "subject", inverse: false } }
    function clearSelection() { sel = { shape: null, inverse: false } }
    function invert() { sel = sel.shape ? { shape: sel.shape, inverse: !sel.inverse } : { shape: "all", inverse: false } }

    title: qsTr("Select and Mask")
    tools: toolSpec.map(t => [t[0], t[1], t[2] + " (" + t[3] + ")"])
    checkText: qsTr("Remember Settings")
    checked: get("remember", false)
    onCheckToggled: put({ remember: !checked })
    startTool: "smRefine"
    Component.onCompleted: {
        v = Tools.value("SelectMask", null) || {}
        const d = Docs.current
        sel = d && d.selection ? { shape: d.selection, inverse: d.inverse } : { shape: "subject", inverse: false }
    }
    onAccepted: {
        apply()
        Tools.setValue("SelectMask", get("remember", false) ? v : null)
        finished(true)
    }
    onRejected: finished(false)

    // The output, one step "Select and Mask": the selection; a mask on the active layer, on a copy of it; the copy
    // alone. New Document opens a copy of the document. The mock's masks are its own pictures (Mock.js, mask).
    function apply() {
        const d = doc
        if (!d)
            return
        const out = get("out", "Layer Mask"), s = sel.shape
        if (out === "New Document") {
            Docs.duplicate(d)
            return
        }
        const mask = !s || (s === "all" && sel.inverse) ? "hide" : s === "all" ? "reveal" : sel.inverse ? "selInv" : "sel"
        d.beginGesture()
        d.run({ op: "step", name: qsTr("Select and Mask"), icon: "select" })
        if (out === "Selection") {
            if (mask === "hide") {
                d.run({ op: "deselect" })
            } else {
                d.run({ op: "select", shape: s })
                if (sel.inverse)
                    d.run({ op: "inverse" })
            }
        } else {
            if (out !== "Layer Mask")
                d.run({ op: "duplicateLayers" })
            if (out !== "New Layer")
                d.run({ op: "mask", action: mask })
        }
        d.endGesture()
    }

    // ---- keys ---------------------------------------------------------------------------------------------------------
    Instantiator {
        model: sm.views.map(x => [x[2], () => sm.put({ view: x[0] })])
            .concat([["J", () => sm.put({ edge: !sm.get("edge", false) })], ["P", () => sm.put({ orig: !sm.original })]])
            .concat(sm.toolSpec.map(t => [t[3], () => Tools.tool = t[0]]))
        delegate: Shortcut {
            required property var modelData
            sequence: modelData[0]
            onActivated: modelData[1]()
        }
    }

    // ---- the options bar ----------------------------------------------------------------------------------------------
    options: [
        Icon {
            name: sm.tool[1]
            color: Theme.t1
            Layout.preferredWidth: 28
            HoverHandler { id: toolHover }
            ToolTip.visible: toolHover.hovered
            ToolTip.delay: 600
            ToolTip.text: sm.tool[2] + " (" + sm.tool[3] + ")"
        },
        Rectangle { visible: !!sm.tool[4] || sm.tool[5]; implicitWidth: 1; implicitHeight: 20; color: Theme.ln },
        Segmented {
            visible: !!sm.tool[4]
            implicitHeight: 24
            options: sm.tool[4] ? sm.tool[4].map((m, i) => ({ t: i ? "−" : "+", tip: m[0] + " · " + m[1] })) : []
            current: sm.tool[0] === "zoom" ? Tools.value("zoom:2", 0) : sm.get("mode_" + sm.tool[0], 0)
            onPicked: i => sm.setMode(i)
        },
        AbstractButton {  // the brush's size, its slider under it
            id: sizeBox
            visible: sm.tool[5]
            implicitWidth: implicitContentWidth + leftPadding + rightPadding
            implicitHeight: 24
            leftPadding: 4
            rightPadding: 2
            text: qsTr("Cỡ brush: %1 px").arg(Fmt.vn(sm.brushSize))
            ToolTip.visible: hovered && !sizePop.visible
            ToolTip.delay: 600
            ToolTip.text: qsTr("Cỡ brush")
            hoverEnabled: true
            Accessible.name: text
            onClicked: sizePop.visible ? sizePop.close() : sizePop.open()
            background: Rectangle { radius: 4; color: Theme.p2 }
            contentItem: Row {
                spacing: 8
                Rectangle { anchors.verticalCenter: parent.verticalCenter; width: 18; height: 18; radius: 9; color: "transparent"; border.width: 1.5; border.color: Theme.t1 }
                Text { anchors.verticalCenter: parent.verticalCenter; text: Fmt.vn(sm.brushSize); color: Theme.t1; font.family: Theme.mono; font.pointSize: Theme.pt(11.5) }
                Icon { anchors.verticalCenter: parent.verticalCenter; name: "expand_more"; size: 16; color: Theme.t3 }
            }
            Popup {
                id: sizePop
                y: 28
                width: 240
                padding: 12
                topPadding: 10
                background: Rectangle { radius: 8; color: Theme.p2; border.color: Theme.ln }
                contentItem: SliderField {
                    label: qsTr("Size")
                    min: 1
                    max: 5000
                    unit: "px"
                    value: sm.brushSize
                    onMoved: x => sm.put({ ["size_" + sm.tool[0]]: x })
                }
            }
        },
        CheckRow {
            visible: sm.tool[6]
            text: qsTr("Sample All Layers")
            checked: sm.get("sample", false)
            onClicked: sm.put({ sample: !checked })
        },
        Rectangle { implicitWidth: 1; implicitHeight: 20; color: Theme.ln },
        Btn {
            implicitHeight: 24
            leftPadding: 12
            rightPadding: 12
            icon.name: "frame_person"
            text: qsTr("Select Subject")
            onClicked: sm.selectSubject()
        },
        Btn {
            readonly property bool hair: sm.get("hair", false)
            implicitHeight: 24
            leftPadding: 12
            rightPadding: 12
            on: hair
            icon.name: hair ? "check" : ""
            text: qsTr("Refine Hair")
            onClicked: sm.put({ hair: !hair })
        },
        Item { Layout.fillWidth: true }  // the rest of the bar: the items keep to the left
    ]

    // ---- the canvas -----------------------------------------------------------------------------------------------
    overlay: [
        Canvas {  // the view: what is not selected under its colour, the photo where it is
            anchors.fill: parent
            readonly property var key: [sm.view, sm.size, sm.sel, sm.v, sm.doc ? sm.doc.w : 0, sm.doc ? sm.doc.h : 0]
            onKeyChanged: requestPaint()
            onPaint: sm.shade(getContext("2d"), isImageLoaded(sm.checkerTile))
            onImageLoaded: requestPaint()
            Component.onCompleted: loadImage(sm.checkerTile)
        },
        Ants {  // Marching Ants: the edge of what is selected (U7)
            anchors.fill: parent
            visible: sm.mode === "ants" && !sm.original
            view: sm.view
            size: sm.size
            docW: sm.doc ? sm.doc.w : 0
            docH: sm.doc ? sm.doc.h : 0
            shape: sm.sel.shape === "all" && sm.sel.inverse ? null : sm.sel.shape  // everything inverted: nothing
            inverse: sm.sel.inverse
        },
        Rectangle {
            x: 12
            y: parent.height - height - 12
            width: hint.implicitWidth + 20
            height: hint.implicitHeight + 10
            radius: 6
            color: Theme.onPhoto
            Text { id: hint; x: 10; y: 5; text: sm.label; color: "#ffffff"; font.pointSize: Theme.pt(11) }
        },
        BrushCursor {  // Refine Edge Brush, Brush at their size (U7): its handlers on the canvas's MouseArea
            anchors.fill: parent
            visible: !!sm.tool[5]
            view: sm.view
            size: sm.size
            diameter: sm.brushSize
            tip: Prefs.value("paintCursor") ?? 3
            crosshair: Prefs.value("crosshair") ?? true
            crosshairWhilePainting: !!Prefs.value("crosshairWhilePainting")
            previewColor: Prefs.value("brushPreviewColor") ?? "#ff0000"
            pointerArea: sm.pointerArea
            onAdjusted: (d, h) => sm.put({ ["size_" + sm.tool[0]]: d })
        }
    ]
    // On a frame-sized Context2D: the page (the document at the view's scale, turned with it), white for Black & White,
    // then the ground over what is not selected: the page but the shape (even-odd), or the shape when inverted.
    function shade(ctx, tiled) {
        ctx.reset()
        const d = doc, vw = view, sz = size
        if (!d || !vw || !sz || original)
            return
        const s = View.scale(vw, sz), W = d.w * s, H = d.h * s
        ctx.translate(sz.w / 2, sz.h / 2)
        ctx.rotate(vw.angle * Math.PI / 180)
        ctx.translate(-vw.x * s, -vw.y * s)
        if (mode === "bw") {
            ctx.fillStyle = "#ffffff"
            ctx.fillRect(0, 0, W, H)
        }
        if (!ground || (sel.shape === "all" && !sel.inverse) || (ground === "checker" && !tiled))
            return
        ctx.globalAlpha = tinted ? opacityPct / 100 : 1
        ctx.fillStyle = ground === "checker" ? ctx.createPattern(checkerTile, "repeat") : ground
        ctx.fillRule = Qt.OddEvenFill
        ctx.beginPath()
        if (!sel.inverse)
            ctx.rect(0, 0, W, H)
        for (const p of sel.shape ? Mock.SHAPES[sel.shape] : []) {
            if (p[0] === "r") {
                ctx.rect(p[1] * W, p[2] * H, p[3] * W, p[4] * H)
            } else if (p[0] === "e") {
                ctx.ellipse((p[1] - p[3]) * W, (p[2] - p[4]) * H, 2 * p[3] * W, 2 * p[4] * H)
            } else {
                ctx.moveTo(p[1][0][0] * W, p[1][0][1] * H)
                for (const q of p[1].slice(1))
                    ctx.lineTo(q[0] * W, q[1] * H)
                ctx.closePath()
            }
        }
        ctx.fill()
    }
    // The prototype's checker, an 8 px tile with #c9c9c9 at the top right and bottom left, as a PPM the canvas loads
    // for its pattern: Qt 6.11's createImageData() upsets the count of the JS heap (U11's trap), loadImage() does not.
    readonly property string checkerTile: {
        const px = []
        for (let i = 0; i < 64; i++) {
            const g = (i % 8 < 4) !== (i < 32) ? 201 : 255
            px.push(g + " " + g + " " + g)
        }
        return "data:image/x-portable-pixmap," + encodeURIComponent("P3 8 8 255 " + px.join(" "))
    }

    // ---- Properties -------------------------------------------------------------------------------------------------
    Grp {
        title: qsTr("View Mode")
        rule: false
        RowLayout {
            spacing: 10
            Rectangle {  // the view over a thumbnail of the photo
                implicitWidth: 44
                implicitHeight: 44
                radius: 6
                color: Theme.p2
                clip: true
                Image {
                    anchors.fill: parent
                    visible: !!sm.doc && sm.doc.photo !== null && sm.doc.photo !== undefined
                    source: visible ? Mock.photoUrl(sm.doc.photo) : ""
                    sourceSize.width: 88
                    fillMode: Image.PreserveAspectCrop
                }
                Rectangle {
                    anchors.fill: parent
                    color: sm.ground === "checker" ? "#e4e4e4" : sm.ground || "transparent"
                    opacity: (sm.tinted ? sm.opacityPct : 60) / 100
                }
            }
            Dropdown {
                Layout.fillWidth: true
                options: sm.views.map(x => [x[0], x[1] + " (" + x[2] + ")"])
                value: sm.mode
                onPicked: x => sm.put({ view: x })
            }
        }
        ColumnLayout {
            spacing: 2  // 22 apart, as the artboard's lines
            CheckRow { text: qsTr("Show Edge (J)"); checked: sm.get("edge", false); onClicked: sm.put({ edge: !checked }) }
            CheckRow { text: qsTr("Show Original (P)"); checked: sm.original; onClicked: sm.put({ orig: !checked }) }
            CheckRow { text: qsTr("High Quality Preview"); checked: sm.get("hq", true); onClicked: sm.put({ hq: !checked }) }
        }
        RowLayout {
            spacing: 10
            SliderField {
                Layout.fillWidth: true
                label: qsTr("Opacity")
                unit: "%"
                value: sm.opacityPct
                enabled: sm.tinted
                onMoved: x => sm.put({ op: x })
            }
            Dropdown {  // the overlay's colour: a swatch, its list on the right
                implicitWidth: 28
                implicitHeight: 22
                enabled: sm.mode === "overlay"
                opacity: enabled ? 1 : 0.4
                options: sm.colors.map(c => [c[0], c[1], c[2], false, c[0] === sm.colour[0]])
                value: sm.colour[0]
                onPicked: x => sm.put({ oc: x })
                ToolTip.visible: hovered && !popup.visible
                ToolTip.delay: 600
                ToolTip.text: qsTr("Màu lớp phủ")
                background: Rectangle { radius: 4; color: sm.colour[2]; border.color: Theme.t4 }
                contentItem: Item {}
                indicator: null
                delegate: sm.swatchRow
                popup.x: width - popup.width
                popup.width: 150
            }
        }
    }
    Grp {
        title: qsTr("Refine Mode")
        Segmented {
            Layout.fillWidth: true
            implicitHeight: 26
            fill: true
            options: [qsTr("Color Aware"), qsTr("Object Aware")]
            current: sm.get("rm", "object") === "color" ? 0 : 1
            onPicked: i => sm.put({ rm: i ? "object" : "color" })
        }
    }
    Grp {
        title: qsTr("Edge Detection")
        SliderField {
            Layout.fillWidth: true
            label: qsTr("Radius")
            max: 250
            unit: "px"
            value: sm.get("radius", 6)
            onMoved: x => sm.put({ radius: x })
        }
        CheckRow { text: qsTr("Smart Radius"); checked: sm.get("smart", true); onClicked: sm.put({ smart: !checked }) }
    }
    Grp {
        title: qsTr("Global Refinements")
        Repeater {
            model: sm.refinements
            SliderField {
                required property var modelData
                Layout.fillWidth: true
                label: modelData[1]
                min: modelData[2]
                max: modelData[3]
                step: modelData[4]
                decimals: modelData[4] < 1 ? 1 : 0
                unit: modelData[6]
                value: sm.get(modelData[0], modelData[5])
                valueText: Fmt.vn(value, decimals) + Fmt.suffix(unit)  // −8%, no + (the prototype's sg)
                onMoved: x => sm.put({ [modelData[0]]: x })
            }
        }
        RowLayout {
            spacing: 6
            Btn { Layout.fillWidth: true; Layout.preferredWidth: 1; implicitHeight: 26; text: qsTr("Clear Selection"); onClicked: sm.clearSelection() }  // equal halves
            Btn { Layout.fillWidth: true; Layout.preferredWidth: 1; implicitHeight: 26; text: qsTr("Invert"); onClicked: sm.invert() }
        }
    }
    Grp {
        title: qsTr("Output Settings")
        Item { implicitHeight: 24 }  // Decontaminate Colors (P2)
        RowLayout {
            spacing: 8
            Text { Layout.preferredWidth: 64; text: qsTr("Output To"); color: Theme.t2; font.pointSize: Theme.pt(12) }
            Dropdown {
                Layout.fillWidth: true
                options: sm.outs
                value: sm.get("out", "Layer Mask")
                onPicked: x => sm.put({ out: x })
            }
        }
    }

    // A group of Properties (.grp): a rule but over the first, its caps title, its rows 9 apart.
    component Grp: ColumnLayout {
        id: grp
        property string title
        property bool rule: true
        default property alias rows: body.data
        Layout.fillWidth: true
        spacing: 0
        Rectangle { visible: grp.rule; Layout.fillWidth: true; implicitHeight: 1; color: Theme.ln }
        ColumnLayout {
            id: body
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Layout.topMargin: 10
            Layout.bottomMargin: 12
            spacing: 9
            Text {
                text: grp.title
                color: Theme.t3
                font.pointSize: Theme.pt(10.5)
                font.weight: Font.Medium
                font.letterSpacing: 0.63  // .06em
                font.capitalization: Font.AllUppercase
            }
        }
    }
}
