pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import QtQuick.Shapes
import "View.js" as View
import "Fmt.js" as Fmt

// Liquify (docs/ui-tasks.md, U15; artboard Liquify; Filter › Liquify…, Ctrl ⇧ X): the 12 tools (W R E C S B O F D A H
// Z), Brush Tool Options, Face-Aware Liquify (a face; Eyes, each side or linked, Eye Distance; Nose; Mouth; Face Shape;
// Đặt lại), Mask Options, View Options, Reconstruct… and Restore All, Preview. Decision 3: the photo does not change,
// the values live in the space for as long as it is open. It opens on Face with a face to work on, else Forward Warp.
// OK is one step "Liquify" of the document's History; Hủy and Esc leave everything as it was.
Workspace {
    id: lq
    // The prototype's tables. Tools: id, icon, name, key, its brush ("b", "r" with Rate, "" none); null a rule.
    readonly property var toolSpec: [
        ["lqWarp", "gesture", qsTr("Forward Warp"), "W", "b"], ["lqRecon", "history", qsTr("Reconstruct"), "R", "r"], ["lqSmooth", "water", qsTr("Smooth"), "E", "r"],
        ["lqTwirl", "cyclone", qsTr("Twirl Clockwise"), "C", "r"], ["lqPucker", "compress", qsTr("Pucker"), "S", "r"], ["lqBloat", "expand", qsTr("Bloat"), "B", "r"],
        ["lqPush", "swipe_left", qsTr("Push Left"), "O", "b"], null,
        ["lqFreeze", "ac_unit", qsTr("Freeze Mask"), "F", "b"], ["lqThaw", "water_drop", qsTr("Thaw Mask"), "D", "b"], null,
        ["lqFace", "face", qsTr("Face"), "A", ""], null,
        ["hand", "pan_tool", qsTr("Hand"), "H", ""], ["zoom", "zoom_in", qsTr("Zoom"), "Z", ""]
    ]
    // Brush Tool Options: key, name, min, max, default.
    readonly property var brushSpec: [["size", qsTr("Size"), 1, 15000, 300], ["density", qsTr("Density"), 0, 100, 50], ["pressure", qsTr("Pressure"), 1, 100, 100],
        ["rate", qsTr("Rate"), 0, 100, 0]]
    // Face-Aware groups: key, name, open, rows [key, name, a pair of eyes].
    readonly property var faceGroups: [
        ["eyes", qsTr("Eyes"), true, [["eyeSize", qsTr("Eye Size"), true], ["eyeHeight", qsTr("Eye Height"), true], ["eyeWidth", qsTr("Eye Width"), true],
            ["eyeTilt", qsTr("Eye Tilt"), true], ["eyeDist", qsTr("Eye Distance"), false]]],
        ["nose", qsTr("Nose"), false, [["noseHeight", qsTr("Nose Height"), false], ["noseWidth", qsTr("Nose Width"), false]]],
        ["mouth", qsTr("Mouth"), false, [["smile", qsTr("Smile"), false], ["upperLip", qsTr("Upper Lip"), false], ["lowerLip", qsTr("Lower Lip"), false],
            ["mouthWidth", qsTr("Mouth Width"), false], ["mouthHeight", qsTr("Mouth Height"), false]]],
        ["shape", qsTr("Face Shape"), false, [["forehead", qsTr("Forehead"), false], ["chinHeight", qsTr("Chin Height"), false], ["jawline", qsTr("Jawline"), false],
            ["faceWidth", qsTr("Face Width"), false]]]
    ]
    readonly property var faceDefaults: ({ eyeSizeL: 12, eyeSizeR: 12, eyeWidthL: 6, eyeWidthR: 6, eyeDist: -4 })
    readonly property var faceKeys: faceGroups.reduce((a, g) => a.concat(g[3].reduce((b, r) => b.concat(r[2] ? [r[0] + "L", r[0] + "R"] : [r[0]]), [])), [])
    readonly property var colorList: [["Red", qsTr("Red"), "#ff0000"], ["Yellow", qsTr("Yellow"), "#ffff00"], ["Green", qsTr("Green"), "#00ff00"], ["Cyan", qsTr("Cyan"), "#00ffff"],
        ["Blue", qsTr("Blue"), "#0000ff"], ["Magenta", qsTr("Magenta"), "#ff00ff"], ["Gray", qsTr("Gray"), "#808080"]].map(c => [c[0], c[1], c[2], false])
    readonly property var masks: [["rep", qsTr("Replace Selection"), "mode_new"], ["add", qsTr("Add to Selection"), "mode_add"], ["sub", qsTr("Subtract from Selection"), "mode_sub"],
        ["int", qsTr("Intersect with Selection"), "mode_int"], ["inv", qsTr("Invert Selection"), "invert_colors"]]

    property var v: ({})  // the values by the prototype's keys, get() the default of one not set
    readonly property var tool: toolSpec.find(t => !!t && t[0] === Tools.tool) || toolSpec[0]
    readonly property string kind: tool[4]
    readonly property bool faces: !!doc && doc.faces
    readonly property bool faceOn: faces && Tools.tool === "lqFace" && get("vFace", true)

    function get(k, d) {
        const x = v[k]
        return x === undefined || x === null ? d : x
    }
    function put(o) { v = Object.assign({}, v, o) }
    function face(k) { return get(k, faceDefaults[k] || 0) }
    // A face slider moved: one eye, or both while they are linked.
    function setFace(k, pair, side, x) { put(!pair ? { [k]: x } : get("lk_" + k, true) ? { [k + "L"]: x, [k + "R"]: x } : { [k + side]: x }) }
    function restoreAll() {  // Đặt lại, Restore All: every face slider at 0
        const o = {}
        for (const k of faceKeys)
            o[k] = 0
        put(o)
    }
    function reconstruct() {  // Revert Reconstruction's OK: the face sliders towards 0 by Amount
        const a = get("recon", 100), o = { recon: 100 }
        for (const k of faceKeys)
            o[k] = Math.round(face(k) * a / 100)
        put(o)
    }

    title: qsTr("Liquify")
    tools: toolSpec.map(t => t ? [t[0], t[1], t[2] + " (" + t[3] + ")"] : null)
    startTool: Docs.current && Docs.current.faces ? "lqFace" : "lqWarp"
    checkText: qsTr("Preview")
    checked: get("preview", true)
    onCheckToggled: put({ preview: !checked })
    onAccepted: {
        if (doc)
            doc.run({ op: "step", name: qsTr("Liquify"), icon: "filter" })
        finished(true)
    }
    onRejected: finished(false)

    Instantiator {
        model: lq.toolSpec.filter(t => !!t)
        delegate: Shortcut {
            required property var modelData
            sequence: modelData[3]
            onActivated: Tools.tool = modelData[0]
        }
    }

    // ---- the canvas -----------------------------------------------------------------------------------------------
    // The face as the artboard draws it on its 550 × 700 portrait (the jaw, the eyes, the mouth, the nose; the
    // handles), stretched to the document, turned with the view: a path in page pixels.
    readonly property var handles: [[245, 276], [305, 276], [275, 262], [340, 276], [400, 276], [370, 262], [278, 381], [366, 381], [322, 361], [322, 401],
        [222, 250], [412, 250], [322, 444], [245, 380], [399, 380]]
    function facePath(sx, sy) {
        const p = (x, y) => x * sx + " " + y * sy
        const ell = (cx, cy, rx, ry) => "M" + p(cx - rx, cy) + "A" + rx * sx + " " + ry * sy + " 0 1 0 " + p(cx + rx, cy) + "A" + rx * sx + " " + ry * sy + " 0 1 0 " + p(cx - rx, cy)
        return "M" + p(222, 250) + "C" + p(225, 330) + " " + p(255, 410) + " " + p(322, 444) + "C" + p(385, 412) + " " + p(410, 330) + " " + p(412, 250)
            + ell(275, 276, 30, 14) + ell(370, 276, 30, 14) + ell(322, 381, 44, 20) + "M" + p(300, 300) + "L" + p(292, 336) + "L" + p(322, 350) + "L" + p(352, 336) + "L" + p(344, 300)
    }
    overlay: [
        Item {  // the face (Face, Show Face Overlay): the document's page at the view's scale and turn
            id: page
            readonly property real s: lq.view ? View.scale(lq.view, lq.size) : 1
            readonly property var centre: lq.view ? View.toCanvas(lq.view, lq.size, { x: lq.doc.w / 2, y: lq.doc.h / 2 }) : ({ x: 0, y: 0 })
            visible: lq.faceOn && !!lq.view
            width: lq.doc ? lq.doc.w * s : 0
            height: lq.doc ? lq.doc.h * s : 0
            x: centre.x - width / 2
            y: centre.y - height / 2
            rotation: lq.view ? lq.view.angle : 0
            Shape {
                id: outline
                anchors.fill: parent
                readonly property string d: page.visible ? lq.facePath(page.width / 550, page.height / 700) : ""
                ShapePath {
                    strokeColor: Qt.rgba(0, 0, 0, 0.45)
                    strokeWidth: 3
                    fillColor: "transparent"
                    PathSvg { path: outline.d }
                }
                ShapePath {
                    strokeColor: "#ffffff"
                    strokeWidth: 1
                    fillColor: "transparent"
                    PathSvg { path: outline.d }
                }
            }
            Repeater {
                model: lq.handles
                Rectangle {
                    required property var modelData
                    x: modelData[0] * page.width / 550 - 4.5
                    y: modelData[1] * page.height / 700 - 4.5
                    width: 9
                    height: 9
                    radius: 4.5
                    color: "#1c1c1e"
                    Rectangle { x: 1; y: 1; width: 7; height: 7; radius: 3.5; color: "#ffffff" }
                }
            }
        },
        Rectangle {  // the eye handles' hint, by the face (Face, Show Face Overlay)
            readonly property var at: lq.view && lq.size ? View.toCanvas(lq.view, lq.size, { x: lq.doc.w * 348 / 550, y: lq.doc.h * 232 / 700 }) : ({ x: 0, y: 0 })
            visible: lq.faceOn
            x: at.x
            y: at.y
            width: eyeHint.implicitWidth + 14
            height: eyeHint.implicitHeight + 6
            radius: 4
            color: Theme.onPhoto
            Text { id: eyeHint; x: 7; y: 3; text: qsTr("Kéo để đổi cỡ mắt · Alt: chỉ một mắt"); color: "#ffffff"; font.pointSize: Theme.pt(11) }
        },
        Rectangle {
            x: 12
            y: parent.height - height - 12
            width: note.implicitWidth + 20
            height: note.implicitHeight + 10
            radius: 6
            color: Theme.onPhoto
            Row {
                id: note
                x: 10
                y: 5
                spacing: 8
                Text { text: qsTr("Xem trước ở mức zoom đang xem; OK thì áp full size ở nền"); color: "#ffffff"; font.pointSize: Theme.pt(11) }
                Text { text: lq.view ? Fmt.zt(lq.view.zoom * 100) : ""; color: "#ffffff"; font.family: Theme.mono; font.pointSize: Theme.pt(11) }
            }
        },
        BrushCursor {  // the brush tools at Size (U7): its handlers on the canvas's MouseArea
            anchors.fill: parent
            visible: !!lq.kind
            view: lq.view
            size: lq.size
            diameter: lq.get("size", 300)
            maxDiameter: 15000
            tip: Prefs.value("paintCursor") ?? 3
            crosshair: Prefs.value("crosshair") ?? true
            crosshairWhilePainting: !!Prefs.value("crosshairWhilePainting")
            previewColor: Prefs.value("brushPreviewColor") ?? "#ff0000"
            pointerArea: lq.pointerArea
            onAdjusted: (d, h) => lq.put({ size: d })
        }
    ]

    // ---- Properties -------------------------------------------------------------------------------------------------
    Fold { key: "brush"; text: qsTr("Brush Tool Options"); rule: false }
    Grid {
        id: brush
        visible: lq.get("sec_brush", true)
        Layout.fillWidth: true
        Layout.leftMargin: 12
        Layout.rightMargin: 12
        Layout.topMargin: 4
        Layout.bottomMargin: 12
        columns: 2
        columnSpacing: 14
        rowSpacing: 10
        Repeater {
            model: lq.brushSpec
            SliderField {
                required property var modelData
                width: (brush.width - 14) / 2
                gap: 7  // the artboard's: a column of gap 7
                label: modelData[1]
                min: modelData[2]
                max: modelData[3]
                value: lq.get(modelData[0], modelData[4])
                enabled: !!lq.kind && (modelData[0] !== "rate" || lq.kind === "r")
                onMoved: x => lq.put({ [modelData[0]]: x })
            }
        }
        CheckRow { enabled: !!lq.kind; text: qsTr("Stylus Pressure"); checked: lq.get("stylus", true); onClicked: lq.put({ stylus: !checked }) }
        CheckRow { enabled: !!lq.kind; text: qsTr("Pin Edges"); checked: lq.get("pin", false); onClicked: lq.put({ pin: !checked }) }
    }

    Fold { key: "face"; text: qsTr("Face-Aware Liquify") }
    ColumnLayout {
        visible: lq.get("sec_face", true)
        enabled: lq.faces
        Layout.fillWidth: true
        spacing: 0
        RowLayout {
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Layout.topMargin: 2
            Layout.bottomMargin: 8
            spacing: 8
            Text { text: qsTr("Chọn mặt"); color: Theme.t2; font.pointSize: Theme.pt(12) }
            Pick {
                Layout.fillWidth: true
                list: lq.faces ? [["1", qsTr("Mặt 1"), "", false]] : [["", qsTr("Không có mặt"), "", false]]
                value: lq.faces ? "1" : ""
            }
            Btn { implicitHeight: 24; leftPadding: 10; rightPadding: 10; text: qsTr("Đặt lại"); onClicked: lq.restoreAll() }
        }
        Repeater {
            model: lq.faceGroups
            ColumnLayout {
                id: group
                required property var modelData
                Layout.fillWidth: true
                spacing: 0
                Fold { key: group.modelData[0]; open: group.modelData[2]; text: group.modelData[1]; sub: true }
                ColumnLayout {
                    visible: lq.get("sec_" + group.modelData[0], group.modelData[2])
                    Layout.fillWidth: true
                    Layout.leftMargin: 22
                    Layout.rightMargin: 12
                    Layout.topMargin: 2
                    Layout.bottomMargin: 10
                    spacing: 10
                    Repeater {
                        model: group.modelData[3]
                        FaceRow {}
                    }
                }
            }
        }
    }

    Fold { key: "mask"; text: qsTr("Mask Options") }
    ColumnLayout {
        visible: lq.get("sec_mask", true)
        Layout.fillWidth: true
        Layout.leftMargin: 12
        Layout.rightMargin: 12
        Layout.topMargin: 4
        Layout.bottomMargin: 12
        spacing: 8
        Row {
            spacing: 6
            PopupMenu { id: maskMenu; width: 132 }
            Repeater {
                model: lq.masks
                AbstractButton {
                    id: mb
                    required property var modelData
                    implicitWidth: 48
                    implicitHeight: 28
                    hoverEnabled: true
                    text: modelData[1]
                    ToolTip.visible: hovered && !maskMenu.visible
                    ToolTip.delay: 600
                    ToolTip.text: text
                    onClicked: maskMenu.show([qsTr("Selection"), qsTr("Transparency"), qsTr("Layer Mask")].map(t => ({ l: t, act: () => {} })), mb, mb.modelData[0] === "int" || mb.modelData[0] === "inv" ? mb.width - 132 : 0, 32)
                    background: Rectangle { radius: 4; color: Theme.p2 }
                    contentItem: Row {
                        leftPadding: 7
                        spacing: 1
                        Icon { anchors.verticalCenter: parent.verticalCenter; name: mb.modelData[2]; size: 18; color: Theme.t2 }
                        Icon { anchors.verticalCenter: parent.verticalCenter; name: "arrow_drop_down"; size: 14; color: Theme.t3 }
                    }
                }
            }
        }
        RowLayout {
            spacing: 6
            Btn { Layout.fillWidth: true; Layout.preferredWidth: 1; implicitHeight: 26; text: qsTr("None") }  // equal thirds
            Btn { Layout.fillWidth: true; Layout.preferredWidth: 1; implicitHeight: 26; text: qsTr("Mask All") }
            Btn { Layout.fillWidth: true; Layout.preferredWidth: 1; implicitHeight: 26; text: qsTr("Invert All") }
        }
    }

    Fold { key: "view"; text: qsTr("View Options") }
    ColumnLayout {
        visible: lq.get("sec_view", true)
        Layout.fillWidth: true
        Layout.leftMargin: 12
        Layout.rightMargin: 12
        Layout.topMargin: 4
        Layout.bottomMargin: 12
        spacing: 8
        CheckRow { spacing: 8; text: qsTr("Show Guides"); checked: lq.get("vGuides", true); onClicked: lq.put({ vGuides: !checked }) }
        CheckRow { spacing: 8; text: qsTr("Show Face Overlay"); checked: lq.get("vFace", true); onClicked: lq.put({ vFace: !checked }) }
        CheckRow { spacing: 8; text: qsTr("Show Image"); checked: lq.get("vImage", true); onClicked: lq.put({ vImage: !checked }) }
        CheckRow { spacing: 8; text: qsTr("Show Mesh"); checked: lq.get("vMesh", false); onClicked: lq.put({ vMesh: !checked }) }
        ViewRow {
            text: qsTr("Mesh Size")
            Pick { Layout.fillWidth: true; key: "meshSize"; list: [["Small", qsTr("Small"), "", false], ["Medium", qsTr("Medium"), "", false], ["Large", qsTr("Large"), "", false]]; value: lq.get(key, "Medium") }
        }
        ViewRow {
            text: qsTr("Mesh Color")
            Pick { Layout.fillWidth: true; key: "meshColor"; list: lq.colorList; value: lq.get(key, "Gray") }
        }
        CheckRow { spacing: 8; text: qsTr("Show Mask"); checked: lq.get("vMask", true); onClicked: lq.put({ vMask: !checked }) }
        ViewRow {
            text: qsTr("Mask Color")
            Pick { Layout.fillWidth: true; key: "maskColor"; list: lq.colorList; value: lq.get(key, "Red") }
        }
        CheckRow { spacing: 8; text: qsTr("Show Backdrop"); checked: lq.get("vBack", false); onClicked: lq.put({ vBack: !checked }) }
        ViewRow {
            text: qsTr("Use")
            Pick {  // the document's other pixel layers
                Layout.fillWidth: true
                key: "bUse"
                list: [["all", qsTr("All Layers"), "", false]].concat(lq.doc ? lq.doc.layers.filter(l => l.kind === "pixel" && l !== lq.doc.active)
                    .map(l => [String(l.id), l.props.name, "", !!l.props.background]) : [])
                value: lq.get(key, "all")
            }
        }
        ViewRow {
            text: qsTr("Mode")
            Pick { Layout.fillWidth: true; key: "bMode"; list: [["front", qsTr("In Front"), "", false], ["behind", qsTr("Behind"), "", false], ["blend", qsTr("Blend"), "", false]]; value: lq.get(key, "front") }
        }
        ViewRow {
            text: qsTr("Opacity")
            SliderField { Layout.fillWidth: true; value: lq.get("bOp", 50); onMoved: x => lq.put({ bOp: x }) }
            Text {
                Layout.preferredWidth: 34
                horizontalAlignment: Text.AlignRight
                text: Fmt.vn(lq.get("bOp", 50)) + "%"
                color: Theme.t1
                font.family: Theme.mono
                font.pointSize: Theme.pt(11.5)
            }
        }
    }

    Fold { key: "recon"; text: qsTr("Brush Reconstruct Options") }
    RowLayout {
        visible: lq.get("sec_recon", true)
        Layout.fillWidth: true
        Layout.leftMargin: 12
        Layout.rightMargin: 12
        Layout.topMargin: 2
        Layout.bottomMargin: 12
        spacing: 6
        Btn {
            Layout.fillWidth: true
            Layout.preferredWidth: 1  // halves with Restore All
            implicitHeight: 26
            text: qsTr("Reconstruct…")
            onClicked: recon.visible ? recon.close() : recon.open()
            Popup {  // Revert Reconstruction, above the button
                id: recon
                y: -height - 4
                width: 250
                padding: 12
                topPadding: 10
                onClosed: lq.put({ recon: 100 })  // Hủy, Esc, a press outside
                background: Rectangle { radius: 8; color: Theme.p2; border.color: Theme.ln }
                contentItem: ColumnLayout {
                    spacing: 10
                    Text {
                        text: qsTr("Revert Reconstruction")
                        color: Theme.t3
                        font.pointSize: Theme.pt(10.5)
                        font.weight: Font.Medium
                        font.letterSpacing: 0.63
                        font.capitalization: Font.AllUppercase
                    }
                    SliderField { Layout.fillWidth: true; label: qsTr("Amount"); value: lq.get("recon", 100); onMoved: x => lq.put({ recon: x }) }
                    RowLayout {
                        spacing: 6
                        Item { Layout.fillWidth: true }
                        Btn { implicitHeight: 26; leftPadding: 12; rightPadding: 12; text: qsTr("Hủy"); onClicked: recon.close() }
                        Btn {
                            implicitWidth: Math.max(64, implicitContentWidth + leftPadding + rightPadding)
                            implicitHeight: 26
                            leftPadding: 12
                            rightPadding: 12
                            primary: true
                            text: qsTr("OK")
                            onClicked: {
                                lq.reconstruct()
                                recon.close()
                            }
                        }
                    }
                }
            }
        }
        Btn { Layout.fillWidth: true; Layout.preferredWidth: 1; implicitHeight: 26; text: qsTr("Restore All"); onClicked: lq.restoreAll() }
    }

    // A section's head (.ph, 30, a rule over it) or a group's in Face-Aware (.sub, 26): its chevron folds what follows,
    // open by v["sec_" + key].
    component Fold: AbstractButton {
        id: f
        property string key
        property bool open: true  // at first
        property bool sub: false
        property bool rule: !sub
        readonly property bool shown: lq.get("sec_" + key, open)
        Layout.fillWidth: true
        implicitHeight: sub ? 26 : 30
        leftPadding: sub ? 22 : 10
        rightPadding: sub ? 12 : 10
        hoverEnabled: true
        font.pointSize: Theme.pt(12)
        font.weight: sub ? Font.Normal : Font.Medium
        Accessible.role: Accessible.Button
        onClicked: lq.put({ ["sec_" + key]: !shown })
        background: Rectangle {
            color: f.hovered ? Theme.sel : "transparent"
            Rectangle { visible: f.rule; width: parent.width; height: 1; color: Theme.ln }
        }
        contentItem: Row {
            spacing: 6
            Icon { anchors.verticalCenter: parent.verticalCenter; name: f.shown ? "expand_more" : "chevron_right"; size: 14; color: Theme.t3 }
            Text { anchors.verticalCenter: parent.verticalCenter; text: f.text; font: f.font; color: f.sub ? Theme.t2 : Theme.t1 }
        }
    }
    // A face slider (.lbl over the value and the track), or the two eyes' with the link between them.
    component FaceRow: ColumnLayout {
        id: fr
        required property var modelData  // [key, name, a pair]
        readonly property string k: modelData[0]
        readonly property bool pair: modelData[2]
        readonly property bool linked: lq.get("lk_" + k, true)
        readonly property real valueL: lq.face(pair ? k + "L" : k)
        readonly property real valueR: pair ? lq.face(k + "R") : 0
        Layout.fillWidth: true
        spacing: 7
        Text { text: fr.modelData[1]; color: Theme.t2; font.pointSize: Theme.pt(11.5) }
        RowLayout {
            spacing: 8
            Text {
                Layout.preferredWidth: 26
                horizontalAlignment: Text.AlignRight
                text: Fmt.vn(fr.valueL)
                color: Theme.t2
                font.family: Theme.mono
                font.pointSize: Theme.pt(11)
            }
            SliderField {
                Layout.fillWidth: true
                min: -100
                max: 100
                value: fr.valueL
                Accessible.name: fr.pair ? qsTr("%1, mắt trái").arg(fr.modelData[1]) : fr.modelData[1]
                onMoved: x => lq.setFace(fr.k, fr.pair, "L", x)
            }
            IconButton {
                visible: fr.pair
                implicitWidth: 20
                implicitHeight: 20
                icon.name: fr.linked ? "link" : "link_off"
                icon.width: 14
                ink: fr.linked ? Theme.accent : Theme.t4
                ToolTip.text: qsTr("Liên kết hai mắt")
                onClicked: lq.put({ ["lk_" + fr.k]: !fr.linked })
            }
            SliderField {
                visible: fr.pair
                Layout.fillWidth: true
                min: -100
                max: 100
                value: fr.valueR
                Accessible.name: qsTr("%1, mắt phải").arg(fr.modelData[1])
                onMoved: x => lq.setFace(fr.k, true, "R", x)
            }
            Text {
                visible: fr.pair
                Layout.preferredWidth: 26
                text: Fmt.vn(fr.valueR)
                color: Theme.t2
                font.family: Theme.mono
                font.pointSize: Theme.pt(11)
            }
        }
    }
    // A row of View Options: its name, 72 wide, then its control, under its box.
    component ViewRow: RowLayout {
        property alias text: name.text
        Layout.leftMargin: 22
        spacing: 8
        Text { id: name; Layout.preferredWidth: 72; color: Theme.t2; font.pointSize: Theme.pt(12) }
    }
    // A drop-down of the prototype's lists: [value, label, colour or "", italics]; the value in v[key].
    component Pick: Dropdown {
        id: pk
        property string key
        property var list: []
        readonly property var cur: list.find(o => o[0] === value) || ["", "", "", false]
        options: list.map(o => o.concat([o[0] === value]))
        delegate: lq.swatchRow
        onPicked: x => { if (key) lq.put({ [key]: x }) }
        contentItem: Row {
            spacing: 6
            Rectangle {
                visible: !!pk.cur[2]
                anchors.verticalCenter: parent.verticalCenter
                width: 12
                height: 12
                radius: 4
                color: pk.cur[2] || "transparent"
            }
            Text {
                height: parent.height
                text: pk.cur[1]
                color: pk.enabled ? Theme.t1 : Theme.t4
                font.pointSize: Theme.pt(12)
                font.italic: !!pk.cur[3]
                verticalAlignment: Text.AlignVCenter
            }
        }
    }
}
