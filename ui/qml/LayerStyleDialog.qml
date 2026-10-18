pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import "Fmt.js" as Fmt
import "Mock.js" as Mock
import "Presets.js" as Presets

// Layer Style (build/proto/LayerStyle.dc.html, LayerStyleBlend.dc.html; plan.md's Layer Style): Blending Options and
// the four effects of P1 in a list with their check boxes, a page each, OK, Hủy, Preview and the preview box at the
// side. params.page opens a page: "blendingOptions" (also the default), "dropShadow", "stroke", "colorOverlay",
// "gradientOverlay", an effect's turned on as when its name is clicked; params.layer a layer by id, else the active one
// of Docs.current. The values go to the layer's blend, opacity, fill and props.fx (fxOut() gives its shape). With
// Preview on the layer follows them at once, inside this dialog's History step "Layer Style" (made at the first change,
// one gesture): OK keeps it, Hủy, Esc, × take it away, and Preview off too until it is back on. As decision 3 of
// docs/ui-tasks.md has it, the preview box does not draw the effects.
DialogBox {
    id: dlg
    property var params: ({})
    property Doc doc: null
    readonly property bool gray: !!doc && doc.mode === "Gray"  // one channel, Gray: Kênh and Blend If have it alone
    property int layerId: 0
    readonly property var layer: doc && doc.rev >= 0 ? doc.layer(layerId) : null
    property string page: "blendingOptions"
    property var v: from(Mock.PROPS)  // the values shown, as from() makes them
    property var present: ({})  // the effects props.fx had when the dialog opened: they stay listed while off
    property var base: ({})  // the layer as out() writes it, when the dialog opened
    property var applied: ({})  // as it is in the document now: base, or what push() put there
    property bool made  // this dialog's History step is there
    property bool wasDirty  // the document's unsaved dot when the dialog opened
    property bool preview: true
    property bool done  // OK
    property string channel: "gray"  // the one Blend If shows
    readonly property var effects: Object.keys(factory())
    readonly property var names: ({ dropShadow: qsTr("Drop Shadow"), stroke: qsTr("Stroke"), colorOverlay: qsTr("Color Overlay"), gradientOverlay: qsTr("Gradient Overlay") })
    readonly property var gradients: Presets.GR_GROUPS.reduce((o, g, gi) => o.concat(g[1].map((x, i) => [gi + "," + i, x[0]])), []).filter(x => ((Prefs.values.gradients || {}).hidden || []).indexOf(x[0]) < 0)  // ["g,i", name], not those Gradients deleted

    title: qsTr("Layer Style")
    boxWidth: 920

    // Each effect as it starts (the prototype's DEF): on, blend modes of Mock.BLENDS, colours "#rrggbb", opacity, spread,
    // noise and scale in %, sizes and distance in px, angles in °, gradient [group, index] of Presets.GR_GROUPS.
    function factory() {
        return {
            dropShadow: { on: false, blend: "multiply", color: "#2b1d4e", opacity: 35, angle: 120, distance: 12, spread: 0, size: 24, noise: 0, knockOut: true },
            stroke: { on: false, size: 6, position: "outside", blend: "normal", opacity: 100, color: "#f7c873" },
            colorOverlay: { on: false, blend: "normal", color: "#ff0000", opacity: 100 },
            gradientOverlay: { on: false, blend: "normal", dither: false, opacity: 100, gradient: [0, 0], reverse: false, style: "linear", align: true, angle: 90,
                scale: 100 }
        }
    }
    // The values for layer props p: blend, opacity and fill in %, channels { r, g, b } ({ gray } in a Gray document),
    // Blend If by channel, and each effect from props.fx, else as Đặt làm mặc định kept it (Prefs "layerStyleDefaults"),
    // else as it starts.
    function from(p) {
        const fx = p.fx || {}, f = factory(), saved = Prefs.value("layerStyleDefaults") || {}
        const o = { blend: p.blend, opacity: Math.round(p.opacity * 100), fill: Math.round(p.fill * 100),
            channels: Object.assign(gray ? { gray: true } : { r: true, g: true, b: true }, fx.channels), blendIf: {} }
        for (const ch of ["gray", "red", "green", "blue"])
            o.blendIf[ch] = Object.assign({ layer: [0, 0, 255, 255], under: [0, 0, 255, 255] }, (fx.blendIf || {})[ch])
        for (const k in f)
            o[k] = Object.assign(f[k], saved[k], fx[k])
        return o
    }
    // props.fx for values o: null for no style, else { channels when one is off, blendIf when a triangle moved (by
    // channel: { layer, under: [black left, black right, white left, white right] }), and the effects on or there
    // before, each { on, …the keys of factory() } }.
    function fxOut(o) {
        const fx = {}, none = from(Mock.PROPS)
        if (JSON.stringify(o.channels) !== JSON.stringify(none.channels))
            fx.channels = o.channels
        if (JSON.stringify(o.blendIf) !== JSON.stringify(none.blendIf))
            fx.blendIf = o.blendIf
        for (const k of effects)
            if (o[k].on || present[k])
                fx[k] = o[k]
        return Object.keys(fx).length ? fx : null
    }
    function out(o) { return { blend: o.blend, opacity: o.opacity / 100, fill: o.fill / 100, fx: fxOut(o) } }

    onAboutToShow: {
        doc = Docs.current
        layerId = typeof params.layer === "number" ? params.layer : doc && doc.active ? doc.active.id : 0
        const p = layer ? layer.props : Mock.PROPS, had = {}
        for (const k of effects)
            had[k] = !!(p.fx && p.fx[k])
        present = had
        v = from(p)
        base = applied = out(v)
        made = done = false
        wasDirty = !!doc && doc.dirty
        preview = true
        channel = "gray"
        pick(effects.indexOf(params.page) >= 0 ? params.page : "blendingOptions")
    }
    onClosed: if (!done) rollback()

    // The layer as the values have it, in this dialog's step: with Preview on, or now.
    function push(now) {
        if (!(preview || now) || !layer)
            return
        const want = out(v), p = {}
        for (const k in want)
            if (JSON.stringify(want[k]) !== JSON.stringify(applied[k]))
                p[k] = want[k]
        if (!Object.keys(p).length)
            return
        if (!made) {
            doc.beginGesture()
            doc.run({ op: "step", name: qsTr("Layer Style"), icon: "shadow" })
            made = true
        }
        if (doc.run({ op: "setProps", layer: layerId, props: p }))
            applied = want
    }
    // The layer and History as the dialog found them.
    function rollback() {
        if (!made)
            return
        doc.endGesture()
        doc.deleteStep()
        if (!wasDirty)
            doc.markSaved()  // ponytail: the mock keeps no saved step; DocModel (N1) knows it from History
        made = false
        applied = base
    }
    function ok() {
        push(true)
        if (made && JSON.stringify(applied) === JSON.stringify(base))
            rollback()  // all as it was: no step
        else if (made)
            doc.endGesture()
        made = false
        done = true
        close()
    }
    function togglePreview() {
        preview = !preview
        if (preview)
            push()
        else
            rollback()
    }
    // A page; an effect's is turned on, as Photoshop does when its name is clicked.
    function pick(k) {
        page = k
        if (effects.indexOf(k) >= 0 && !v[k].on)
            set(k, "on", true)
    }
    function setTop(key, x) {  // blend, opacity, fill
        v = Object.assign({}, v, { [key]: x })
        push()
    }
    function set(k, key, x) {  // one value of an effect, of channels, of blendIf (a channel)
        v = Object.assign({}, v, { [k]: Object.assign({}, v[k], { [key]: x }) })
        push()
    }
    function setBlendIf(which, x) { set("blendIf", channel, Object.assign({}, v.blendIf[channel], { [which]: x })) }
    function makeDefault() {  // Đặt làm mặc định: the page's effect starts so from now on
        const o = Object.assign({}, v[page])
        delete o.on
        Prefs.set("layerStyleDefaults", Object.assign({}, Prefs.value("layerStyleDefaults"), { [page]: o }))
    }
    function resetDefault() {  // Về mặc định
        v = Object.assign({}, v, { [page]: Object.assign(factory()[page], (Prefs.value("layerStyleDefaults") || {})[page], { on: v[page].on }) })
        push()
    }
    function realign() {  // Đặt lại căn chỉnh: angle and scale as they start, as the prototype's
        const f = factory().gradientOverlay
        v = Object.assign({}, v, { gradientOverlay: Object.assign({}, v.gradientOverlay, { angle: f.angle, scale: f.scale }) })
        push()
    }
    function modes(all) { return Mock.BLENDS.map((b, i) => [b, Mock.BLEND_LABELS[i]]).slice(all ? 0 : 1) }  // Pass Through for groups
    function gradCss(key) {  // "g,i" of Presets.GR_GROUPS
        const p = String(key).split(","), g = Presets.GR_GROUPS[+p[0]], x = g && g[1][+p[1]]
        return x ? x[1] : ""
    }

    // ---- parts ------------------------------------------------------------------------------------------------------
    component RowName: Text {  // a row's name (.rl)
        Layout.preferredWidth: 110
        color: Theme.t2
        font.pointSize: Theme.pt(12)
        elide: Text.ElideRight
    }
    component Cap: Text {  // .cap
        color: Theme.t3
        font.pointSize: Theme.pt(10.5)
        font.weight: Font.Medium
        font.capitalization: Font.AllUppercase
        font.letterSpacing: 0.63
        Layout.preferredHeight: Math.round(14.2 * Theme.fontScale)  // 10.5 px × 1.35, 15 of its own (capitals: no descender cut)
    }
    component Line: RowLayout {  // .rw
        Layout.fillWidth: true
        Layout.preferredHeight: 26
        spacing: 12
    }
    // Name, slider and number field (.rw); dragging the name scrubs the field.
    component Slide: Item {
        id: r
        property string label
        property real value
        property real min: 0
        property real max: 100
        property string unit: "%"
        signal moved(real v)
        Layout.fillWidth: true
        implicitHeight: 26
        Text {
            width: 110
            anchors.verticalCenter: parent.verticalCenter
            text: r.label
            color: Theme.t2
            font.pointSize: Theme.pt(12)
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.SizeHorCursor
                preventStealing: true
                onPressed: e => fld.scrubStart(e.x)
                onPositionChanged: e => fld.scrubTo(e.x, e.modifiers & Qt.ShiftModifier)
                onReleased: fld.scrubEnd()
                onCanceled: fld.scrubEnd()
            }
        }
        SliderField {
            x: 122
            width: r.width - 226
            anchors.verticalCenter: parent.verticalCenter
            value: r.value
            min: r.min
            max: r.max
            Accessible.name: r.label
            onMoved: x => r.moved(x)
        }
        NumberField {
            id: fld
            boxHeight: 26
            unitApart: true  // .fld input{text-align:right}, the unit in t3 after it
            horizontalAlignment: TextInput.AlignRight
            x: r.width - 92
            width: 92
            anchors.verticalCenter: parent.verticalCenter
            value: r.value
            min: r.min
            max: r.max
            unit: r.unit
            Accessible.name: r.label
            onMoved: x => r.moved(x)
        }
    }
    component Modes: Dropdown {  // an effect's Blend Mode
        id: dd
        required property string fx
        implicitHeight: 26
        Layout.fillWidth: true
        options: dlg.modes(false)
        value: dlg.v[fx].blend
        onPicked: x => dlg.set(dd.fx, "blend", x)
    }
    component ColorCell: Swatch {
        id: cc
        required property string fx
        property string what
        Layout.preferredHeight: 26
        radius: 5
        ring: Theme.t4
        color: dlg.v[fx].color
        ToolTip.text: what
        Accessible.name: what
        onClicked: {  // the Color Picker (U11) on it, once Main has one
            const main = cc.Window.window as Main
            if (main && main.hasDialog("ColorPicker"))
                main.openDialog("ColorPicker", { color: dlg.v[cc.fx].color, title: cc.what, picked: c => dlg.set(cc.fx, "color", String(c)) })
        }
    }
    component Check: CheckRow {  // an effect's yes or no
        id: ck
        required property string fx
        required property string key
        checked: dlg.v[fx][key]
        onClicked: dlg.set(ck.fx, ck.key, !ck.checked)
    }
    // The angle as a dial, not dragged in the prototype.
    component AngleDial: Rectangle {
        id: dial
        property real angle
        implicitWidth: 44
        implicitHeight: 44
        radius: 22
        color: Theme.p2
        border.color: Theme.t4
        Accessible.role: Accessible.Graphic
        Accessible.name: qsTr("Angle: %1°").arg(Fmt.vn(angle))
        Rectangle {
            x: 21
            y: 6
            width: 2
            height: 17
            color: Theme.t1
            transform: Rotation { origin.x: 1; origin.y: 16; angle: 90 - dial.angle }
        }
        Rectangle { x: 19; y: 19; width: 6; height: 6; radius: 3; color: Theme.t1 }
    }
    component Angle: NumberField {
        id: an
        required property string fx
        boxHeight: 26
        unitApart: true
        horizontalAlignment: TextInput.AlignRight
        Layout.preferredWidth: 92
        value: dlg.v[fx].angle
        min: -180
        max: 180
        unit: "°"
        Accessible.name: qsTr("Angle")
        onMoved: x => dlg.set(an.fx, "angle", x)
    }
    // A gradient left to right over the transparency checker, as the prototype's gradCss(…, BAR); css one of
    // Presets.GR_GROUPS: its colours evenly spaced, "transparent" a neighbour's colour at alpha 0.
    component GradBar: Canvas {
        property string css
        readonly property var deps: [css, width, height]
        onDepsChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d"), W = width, H = height, c = css.match(/#[0-9a-f]{6}|transparent/gi) || []
            ctx.reset()
            ctx.beginPath()
            ctx.roundedRect(0, 0, W, H, 2, 2)
            ctx.clip()
            for (let y = 0; y < H; y += 3)
                for (let x = 0; x < W; x += 3) {
                    ctx.fillStyle = (x + y) / 3 % 2 ? "#c9c9c9" : "#ffffff"
                    ctx.fillRect(x, y, 3, 3)
                }
            const g = ctx.createLinearGradient(0, 0, W, 0)
            c.forEach((x, i) => {
                const k = Qt.color(x !== "transparent" ? x : c[i - 1] && c[i - 1] !== "transparent" ? c[i - 1] : c[i + 1] || "#000000")
                g.addColorStop(c.length > 1 ? i / (c.length - 1) : 0, Qt.rgba(k.r, k.g, k.b, x === "transparent" ? 0 : 1))
            })
            ctx.fillStyle = g
            ctx.fillRect(0, 0, W, H)
        }
    }

    header: Item {
        implicitHeight: 46
        Row {
            x: 18
            height: parent.height
            spacing: 4
            Text {
                id: ttl
                anchors.verticalCenter: parent.verticalCenter
                text: dlg.title
                color: Theme.t1
                font.pointSize: Theme.pt(14)
                font.weight: Font.DemiBold
            }
            Text {  // the layer's name
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(implicitWidth, dlg.width - 18 - 46 - ttl.width - 4)
                visible: !!dlg.layer
                text: "· " + (dlg.layer ? dlg.layer.props.name : "")
                color: Theme.t3
                font.pointSize: Theme.pt(14)
                elide: Text.ElideRight
            }
        }
        IconButton {
            x: dlg.width - 8 - width
            anchors.verticalCenter: parent.verticalCenter
            implicitWidth: 30
            implicitHeight: 30
            icon.name: "close"
            icon.width: 18
            ToolTip.text: qsTr("Đóng")
            onClicked: dlg.close()
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.ln }
    }

    contentItem: Item {
        implicitHeight: 574

        Rectangle {  // the list
            x: 1
            width: 219
            height: parent.height - 1
            bottomLeftRadius: 9
            color: Theme.bg
            Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.ln }
            Column {
                y: 8
                width: parent.width - 1
                AbstractButton {
                    id: blendRow
                    width: parent.width
                    height: 32
                    text: qsTr("Blending Options")
                    font.pointSize: Theme.pt(12)
                    font.weight: Font.Medium
                    background: Rectangle { color: dlg.page === "blendingOptions" ? Theme.acc2 : "transparent" }
                    contentItem: Text {
                        leftPadding: 12
                        text: blendRow.text
                        font: blendRow.font
                        color: dlg.page === "blendingOptions" ? Theme.t1 : Theme.t2
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: dlg.pick("blendingOptions")
                }
                Item {
                    width: parent.width
                    height: 13
                    Rectangle { x: 12; y: 6; width: parent.width - 24; height: 1; color: Theme.ln }
                }
                Repeater {
                    model: dlg.effects
                    Rectangle {
                        id: fxRow
                        required property string modelData
                        width: parent.width
                        height: 32
                        color: dlg.page === modelData ? Theme.acc2 : "transparent"
                        CheckRow {
                            x: 12
                            anchors.verticalCenter: parent.verticalCenter
                            checked: dlg.v[fxRow.modelData].on
                            Accessible.name: qsTr("Bật %1").arg(dlg.names[fxRow.modelData])
                            onClicked: dlg.set(fxRow.modelData, "on", !checked)
                        }
                        AbstractButton {
                            id: fxName
                            x: 33
                            width: parent.width - x - 12
                            height: parent.height
                            text: dlg.names[fxRow.modelData]
                            font.pointSize: Theme.pt(12)
                            contentItem: Text {
                                text: fxName.text
                                font: fxName.font
                                color: dlg.page === fxRow.modelData ? Theme.t1 : Theme.t2
                                verticalAlignment: Text.AlignVCenter
                            }
                            onClicked: dlg.pick(fxRow.modelData)
                        }
                    }
                }
            }
        }

        ColumnLayout {  // the page
            x: 240
            y: 16
            width: parent.width - 220 - 170 - 40
            height: parent.height - 32
            spacing: 14
            Row {
                spacing: 10
                Text {
                    id: pageTitle
                    text: dlg.page === "blendingOptions" ? blendRow.text : dlg.names[dlg.page]
                    color: Theme.t1
                    font.pointSize: Theme.pt(13)
                    font.weight: Font.DemiBold
                }
                Cap {
                    visible: dlg.page !== "blendingOptions"
                    anchors.baseline: pageTitle.baseline
                    text: ({ dropShadow: qsTr("Cấu trúc"), stroke: qsTr("Cấu trúc"), colorOverlay: qsTr("Màu"), gradientOverlay: qsTr("Gradient") })[dlg.page] || ""
                }
            }

            // Blending Options
            ColumnLayout {
                Layout.fillWidth: true
                visible: dlg.page === "blendingOptions"
                spacing: 14
                Cap { text: qsTr("Chung") }
                Line {
                    RowName { text: qsTr("Blend Mode") }
                    Dropdown {
                        implicitHeight: 26
                        Layout.fillWidth: true
                        options: dlg.modes(!!dlg.layer && dlg.layer.kind === "group")
                        value: dlg.v.blend
                        onPicked: x => dlg.setTop("blend", x)
                    }
                }
                Slide { label: qsTr("Opacity"); value: dlg.v.opacity; onMoved: x => dlg.setTop("opacity", x) }
                Cap { Layout.topMargin: 4; text: qsTr("Nâng cao") }
                Slide { label: qsTr("Fill Opacity"); value: dlg.v.fill; onMoved: x => dlg.setTop("fill", x) }
                RowLayout {
                    spacing: 16
                    RowName { text: qsTr("Kênh") }
                    Repeater {
                        model: dlg.gray ? ["gray"] : ["r", "g", "b"]
                        CheckRow {
                            required property string modelData
                            text: modelData === "gray" ? "Gray" : modelData.toUpperCase()
                            checked: dlg.v.channels[modelData]
                            onClicked: dlg.set("channels", modelData, !checked)
                        }
                    }
                }
                Cap { Layout.topMargin: 4; text: qsTr("Blend If") }
                Line {
                    RowName { text: qsTr("Kênh") }
                    Dropdown {
                        implicitHeight: 26
                        Layout.preferredWidth: 140
                        options: [["gray", "Gray"]].concat(dlg.gray ? [] : [["red", "Red"], ["green", "Green"], ["blue", "Blue"]])
                        value: dlg.channel
                        onPicked: x => dlg.channel = x
                    }
                    Item { Layout.fillWidth: true }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    SplitSlider {
                        Layout.fillWidth: true
                        label: qsTr("This Layer")
                        value: dlg.v.blendIf[dlg.channel].layer
                        onMoved: x => dlg.setBlendIf("layer", x)
                    }
                    SplitSlider {
                        Layout.fillWidth: true
                        Layout.topMargin: 6
                        label: qsTr("Underlying Layer")
                        value: dlg.v.blendIf[dlg.channel].under
                        onMoved: x => dlg.setBlendIf("under", x)
                    }
                    Text {
                        Layout.fillWidth: true
                        Layout.topMargin: 4
                        readonly property var b: dlg.v.blendIf[dlg.channel]
                        text: qsTr("Alt kéo một tam giác để tách đôi: vùng giữa hai nửa chuyển mềm. Ở đây điểm ảnh tối của layer này mờ dần từ %1 tới %2; vùng rất sáng của layer dưới lộ qua từ %3.")
                            .arg(b.layer[0]).arg(b.layer[1]).arg(b.under[2])
                        color: Theme.t3
                        font.pointSize: Theme.pt(11)
                        wrapMode: Text.Wrap
                    }
                }
            }

            // Drop Shadow
            ColumnLayout {
                Layout.fillWidth: true
                visible: dlg.page === "dropShadow"
                spacing: 14
                Line {
                    RowName { text: qsTr("Blend Mode") }
                    Modes { fx: "dropShadow" }
                    ColorCell { fx: "dropShadow"; what: qsTr("Màu bóng"); Layout.preferredWidth: 40 }
                }
                Slide { label: qsTr("Opacity"); value: dlg.v.dropShadow.opacity; onMoved: x => dlg.set("dropShadow", "opacity", x) }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    RowName { text: qsTr("Angle") }
                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 44
                        AngleDial { angle: dlg.v.dropShadow.angle }
                    }
                    Angle { fx: "dropShadow" }
                }
                Slide { label: qsTr("Distance"); unit: "px"; max: 30000; value: dlg.v.dropShadow.distance; onMoved: x => dlg.set("dropShadow", "distance", x) }
                Slide { label: qsTr("Spread"); value: dlg.v.dropShadow.spread; onMoved: x => dlg.set("dropShadow", "spread", x) }
                Slide { label: qsTr("Size"); unit: "px"; max: 250; value: dlg.v.dropShadow.size; onMoved: x => dlg.set("dropShadow", "size", x) }
                Cap { Layout.topMargin: 6; text: qsTr("Chất lượng") }
                Slide { label: qsTr("Noise"); value: dlg.v.dropShadow.noise; onMoved: x => dlg.set("dropShadow", "noise", x) }
                Check { fx: "dropShadow"; key: "knockOut"; text: qsTr("Layer Knocks Out Drop Shadow") }
            }

            // Stroke
            ColumnLayout {
                Layout.fillWidth: true
                visible: dlg.page === "stroke"
                spacing: 14
                Slide { label: qsTr("Size"); unit: "px"; min: 1; max: 250; value: dlg.v.stroke.size; onMoved: x => dlg.set("stroke", "size", x) }
                Line {
                    RowName { text: qsTr("Position") }
                    Dropdown {
                        implicitHeight: 26
                        Layout.fillWidth: true
                        options: [["outside", qsTr("Outside")], ["inside", qsTr("Inside")], ["center", qsTr("Center")]]
                        value: dlg.v.stroke.position
                        onPicked: x => dlg.set("stroke", "position", x)
                    }
                }
                Line {
                    RowName { text: qsTr("Blend Mode") }
                    Modes { fx: "stroke" }
                }
                Slide { label: qsTr("Opacity"); value: dlg.v.stroke.opacity; onMoved: x => dlg.set("stroke", "opacity", x) }
                Cap { Layout.topMargin: 6; text: qsTr("Màu") }
                Line {
                    RowName { text: qsTr("Color") }
                    ColorCell { fx: "stroke"; what: qsTr("Màu viền"); Layout.preferredWidth: 60 }
                    Item { Layout.fillWidth: true }
                }
            }

            // Color Overlay
            ColumnLayout {
                Layout.fillWidth: true
                visible: dlg.page === "colorOverlay"
                spacing: 14
                Line {
                    RowName { text: qsTr("Blend Mode") }
                    Modes { fx: "colorOverlay" }
                    ColorCell { fx: "colorOverlay"; what: qsTr("Màu phủ"); Layout.preferredWidth: 40 }
                }
                Slide { label: qsTr("Opacity"); value: dlg.v.colorOverlay.opacity; onMoved: x => dlg.set("colorOverlay", "opacity", x) }
            }

            // Gradient Overlay
            ColumnLayout {
                Layout.fillWidth: true
                visible: dlg.page === "gradientOverlay"
                spacing: 14
                Line {
                    RowName { text: qsTr("Blend Mode") }
                    Modes { fx: "gradientOverlay" }
                    Check { fx: "gradientOverlay"; key: "dither"; text: qsTr("Dither") }
                }
                Slide { label: qsTr("Opacity"); value: dlg.v.gradientOverlay.opacity; onMoved: x => dlg.set("gradientOverlay", "opacity", x) }
                Line {
                    RowName { text: qsTr("Gradient") }
                    Dropdown {
                        id: grad
                        implicitHeight: 26
                        Layout.fillWidth: true
                        options: dlg.gradients
                        value: String(dlg.v.gradientOverlay.gradient)
                        Accessible.name: qsTr("Gradient: %1").arg(displayText)
                        onPicked: x => dlg.set("gradientOverlay", "gradient", x.split(",").map(Number))
                        contentItem: Item {
                            GradBar {
                                x: 2
                                width: parent.width - 2
                                height: 14
                                anchors.verticalCenter: parent.verticalCenter
                                css: dlg.gradCss(grad.value)
                            }
                        }
                        delegate: ItemDelegate {
                            id: gi
                            required property var modelData
                            required property int index
                            width: ListView.view.width
                            height: 28
                            leftPadding: 10
                            rightPadding: 10
                            hoverEnabled: true
                            highlighted: grad.highlightedIndex === index
                            text: modelData[1]
                            font.pointSize: Theme.pt(12.5)
                            background: Rectangle { color: gi.highlighted || gi.index === grad.valueIndex ? Theme.sel : "transparent" }
                            contentItem: Row {
                                spacing: 8
                                GradBar { width: 40; height: 12; anchors.verticalCenter: parent.verticalCenter; css: dlg.gradCss(gi.modelData[0]) }
                                Text { anchors.verticalCenter: parent.verticalCenter; text: gi.text; font: gi.font; color: Theme.t1 }
                            }
                        }
                    }
                    Check { fx: "gradientOverlay"; key: "reverse"; text: qsTr("Reverse") }
                }
                Line {
                    RowName { text: qsTr("Style") }
                    Dropdown {
                        implicitHeight: 26
                        Layout.fillWidth: true
                        options: [["linear", qsTr("Linear")], ["radial", qsTr("Radial")], ["angle", qsTr("Angle")], ["reflected", qsTr("Reflected")], ["diamond", qsTr("Diamond")]]
                        value: dlg.v.gradientOverlay.style
                        onPicked: x => dlg.set("gradientOverlay", "style", x)
                    }
                    Check { fx: "gradientOverlay"; key: "align"; text: qsTr("Align with Layer") }
                }
                RowLayout {
                    id: angleRow
                    Layout.fillWidth: true
                    spacing: 12
                    RowName { text: qsTr("Angle") }
                    Item {  // the grid's two auto columns share what is left, as the prototype's
                        Layout.preferredWidth: 44 + Math.max(0, angleRow.width - 110 - 92 - 36 - 44 - realign.implicitWidth) / 2
                        Layout.preferredHeight: 44
                        AngleDial { angle: dlg.v.gradientOverlay.angle }
                    }
                    Angle { fx: "gradientOverlay" }
                    Btn {
                        id: realign
                        Layout.fillWidth: true
                        text: qsTr("Đặt lại căn chỉnh")
                        ToolTip.text: qsTr("Reset Alignment")
                        onClicked: dlg.realign()
                    }
                }
                Slide { label: qsTr("Scale"); min: 10; max: 150; value: dlg.v.gradientOverlay.scale; onMoved: x => dlg.set("gradientOverlay", "scale", x) }
            }

            Item { Layout.fillHeight: true }
            Row {
                visible: dlg.page !== "blendingOptions"
                spacing: 8
                Btn { text: qsTr("Đặt làm mặc định"); ToolTip.text: qsTr("Make Default"); onClicked: dlg.makeDefault() }
                Btn { text: qsTr("Về mặc định"); ToolTip.text: qsTr("Reset to Default"); onClicked: dlg.resetDefault() }
            }
        }

        Item {  // OK, Hủy, Preview
            x: parent.width - 170
            width: 170
            height: parent.height
            Rectangle { width: 1; height: parent.height; color: Theme.ln }
            ColumnLayout {
                x: 16
                y: 16
                width: 138
                spacing: 8
                Btn { Layout.fillWidth: true; text: qsTr("OK"); primary: true; onClicked: dlg.ok() }
                Btn { Layout.fillWidth: true; text: qsTr("Hủy"); onClicked: dlg.close() }
                CheckRow { Layout.topMargin: 6; text: qsTr("Preview"); checked: dlg.preview; onClicked: dlg.togglePreview() }
                Rectangle {  // the preview: a pink layer on white, as the prototype's, without its effects (decision 3)
                    Layout.topMargin: 8
                    Layout.fillWidth: true
                    Layout.preferredHeight: 96
                    radius: 6
                    color: "#ffffff"
                    Rectangle { anchors.centerIn: parent; width: 44; height: 44; radius: 4; color: "#c0587e" }
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Xem trước hiệu ứng")
                    color: Theme.t3
                    font.pointSize: Theme.pt(11)
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }
    }
}
