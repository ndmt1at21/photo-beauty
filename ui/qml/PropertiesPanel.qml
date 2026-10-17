pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import "AdjustSpec.js" as Spec
import "Mock.js" as Mock

// The Properties panel (docs/ui-tasks.md, U10; PanelsA, Adjustments, the pp, ad, mk parts of Main.dc.html), a tab of
// the dock's group Properties, Adjustments, Retouch: what the active layer is, the document without one. `page` names
// it: doc (canvas size, resolution, orientation, mode, profile; Select Subject, Remove Background); px (a pixel layer or
// a group: size, place, angle, flips, alignment); so (a smart object: its file, size, Sửa nội dung, Rasterize); text
// (Character, Paragraph in short); shape (size, Fill, Stroke, corners, path operation); fill (its colour); adj (an
// adjustment layer: its rows, AdjustRows; at the foot clip, view the previous state, reset, eye, delete); mask (the
// layer's mask while it takes the brush, Doc.editMask: Density, Feather, Select and Mask…, Color Range…, Invert; at the
// foot load as a selection, apply, disable, delete). What changes the document is one History step on Docs.current, a
// drag one too; a layer's size, place, angle and the type's fields are the prototype's own fields in Tools.values (its
// v: "L:<id>:w", "f:ch_size", "ch:font"…), no step, as the mock transforms nothing.
Item {
    id: panel
    property var menu: []  // the items of the … of its tab strip (PopupMenu's): none, as the prototype's
    readonly property Doc doc: Docs.current
    readonly property string page: !doc ? "none" : !doc.active ? "doc" : doc.editMask && active.props.mask ? "mask"
                                 : ["adj", "text", "shape", "fill", "so"].indexOf(active.kind) >= 0 ? active.kind : "px"
    // The active layer and the document, stand-ins without them: a page's bindings may run once more as it goes.
    readonly property var active: doc && doc.active ? doc.active : ({ id: -1, kind: "", props: Mock.PROPS })
    readonly property var cur: doc || ({ w: 1, h: 1, ppi: 72, mode: "RGB", bits: 8, profile: "" })
    property bool peeking  // Giữ để xem trạng thái trước held: the mock shows the same image
    readonly property real half: (width - 60) / 2  // one field of a pair (.g2: 12 in, 6 + 24 + 6 between)
    readonly property Main win: Window.window as Main  // openDialog(), openSpace(); null outside Main
    // W, H, X, Y as the prototype starts a layer's (PanelsA; a pixel layer, a group: the document's size at 0, 0)
    readonly property var sizes: ({ so: [2100, 1312, 190, 2040], text: [1840, 236, 320, 410], shape: [1200, 800, 640, 1900] })
    implicitWidth: 308
    implicitHeight: body.implicitHeight

    function run(cmd) { return !!doc && doc.run(cmd) }
    function lf(k, def) { return Tools.value("L:" + active.id + ":" + k, def) }
    function setLf(k, v) { Tools.setValue("L:" + active.id + ":" + k, v) }
    // The Color Picker (U11) on a colour; nothing while it is not in the module.
    function pickColor(color, title, picked) {
        if (win && win.hasDialog("ColorPicker"))
            win.openDialog("ColorPicker", { color: String(color), title: title, picked: picked })
    }

    component Cap: Text {  // .cap
        color: Theme.t3
        font.pointSize: Theme.pt(10.5)
        font.weight: Font.Medium
        font.letterSpacing: 0.63
        font.capitalization: Font.AllUppercase
    }
    component Dim: Text {
        color: Theme.t3
        font.pointSize: Theme.pt(12)
    }
    component Hd: RowLayout {  // .hd: an icon, what is shown
        property alias icon: hdIcon.name
        property alias text: hdText.text
        spacing: 8
        Layout.preferredHeight: 20
        Icon {
            id: hdIcon
            size: 16
            color: Theme.t3
        }
        Text {
            id: hdText
            Layout.fillWidth: true
            color: Theme.t2
            elide: Text.ElideRight
            font.pointSize: Theme.pt(12)
        }
    }
    component Foot: Item {  // .foot: 32 high, a rule over its buttons at the right
        default property alias buttons: footRow.data
        anchors.bottom: parent.bottom
        width: parent.width
        height: 32
        Rectangle {
            width: parent.width
            height: 1
            color: Theme.ln
        }
        Row {
            id: footRow
            anchors.right: parent.right
            anchors.rightMargin: 8
            y: 4
            spacing: 2
        }
    }
    component TextFld: Rectangle {  // a field the prototype keeps as text ("48 pt", "Auto"), an icon before it
        id: tf
        property string icon
        property real turn  // the icon's rotation
        property string value
        signal edited(string text)
        implicitWidth: 64
        implicitHeight: 24
        radius: 4
        color: Theme.p2
        onValueChanged: if (!input.activeFocus) input.text = value
        Icon {
            visible: tf.icon !== ""
            x: 6
            anchors.verticalCenter: parent.verticalCenter
            name: tf.icon
            size: 15
            color: Theme.t3
            rotation: tf.turn
        }
        TextInput {
            id: input
            x: tf.icon !== "" ? 27 : 6
            width: parent.width - x - 6
            anchors.verticalCenter: parent.verticalCenter
            clip: true
            color: Theme.t1
            selectByMouse: true
            font.family: Theme.mono
            font.pointSize: Theme.pt(11.5)
            Component.onCompleted: text = tf.value
            onEditingFinished: tf.edited(text)
            Keys.onEscapePressed: {
                text = tf.value
                focus = false
            }
        }
    }

    Loader {
        id: body
        width: parent.width
        sourceComponent: ({ none: noDoc, doc: docPage, px: pxPage, so: soPage, text: textPage, shape: shapePage, fill: fillPage, adj: adjPage, mask: maskPage })[panel.page]
    }

    Component {
        id: noDoc
        Item {
            objectName: "page:none"
            implicitHeight: 44
            Dim {
                x: 12
                y: 14
                text: qsTr("Chưa mở tài liệu nào.")
            }
        }
    }

    Component {  // no layer picked: the document
        id: docPage
        Item {
            objectName: "page:doc"
            implicitHeight: dc.implicitHeight + 18
            ColumnLayout {
                id: dc
                x: 12
                y: 8
                width: parent.width - 24
                spacing: 8
                Hd {
                    icon: "description"
                    text: qsTr("Tài liệu")
                }
                Cap { text: qsTr("Canvas") }
                Row {
                    spacing: 6
                    NumberField {
                        width: panel.half
                        label: "W"
                        labelColor: Theme.t3
                        unit: "px"
                        value: panel.cur.w
                        min: 1
                        max: 300000
                        ppi: panel.cur.ppi
                        Accessible.name: qsTr("Chiều rộng canvas")
                        onStarted: panel.doc.beginGesture()
                        onMoved: v => panel.run({ op: "canvasSize", w: v, h: panel.doc.h })
                        onCommitted: panel.doc.endGesture()
                    }
                    Item {
                        width: 24
                        height: 24
                    }
                    NumberField {
                        width: panel.half
                        label: "H"
                        labelColor: Theme.t3
                        unit: "px"
                        value: panel.cur.h
                        min: 1
                        max: 300000
                        ppi: panel.cur.ppi
                        Accessible.name: qsTr("Chiều cao canvas")
                        onStarted: panel.doc.beginGesture()
                        onMoved: v => panel.run({ op: "canvasSize", w: panel.doc.w, h: v })
                        onCommitted: panel.doc.endGesture()
                    }
                }
                RowLayout {
                    spacing: 6
                    ToolToggle {
                        icon.name: "crop_portrait"
                        icon.width: 16
                        checked: panel.cur.h >= panel.cur.w
                        ToolTip.text: qsTr("Dọc · Portrait")
                        Accessible.name: qsTr("Dọc")
                        onClicked: if (!checked) panel.run({ op: "swapOrientation" })
                    }
                    ToolToggle {
                        icon.name: "crop_landscape"
                        icon.width: 16
                        checked: panel.cur.w > panel.cur.h
                        ToolTip.text: qsTr("Ngang · Landscape")
                        Accessible.name: qsTr("Ngang")
                        onClicked: if (!checked) panel.run({ op: "swapOrientation" })
                    }
                    Item { Layout.fillWidth: true }
                    NumberField {
                        Layout.preferredWidth: 150  // the artboard's 132 cuts "300 ppi"
                        label: qsTr("Độ phân giải")
                        labelColor: Theme.t3
                        unit: "ppi"
                        value: panel.cur.ppi
                        min: 1
                        max: 10000
                        Accessible.name: qsTr("Độ phân giải")
                        onStarted: panel.doc.beginGesture()
                        onMoved: v => panel.run({ op: "imageSize", w: panel.doc.w, h: panel.doc.h, ppi: v })
                        onCommitted: panel.doc.endGesture()
                    }
                }
                Row {
                    spacing: 6
                    Dropdown {
                        width: (dc.width - 6) * 0.4
                        options: Spec.LISTS.docMode
                        value: (panel.cur.mode === "Gray" ? "Grayscale" : "RGB") + ", " + panel.cur.bits + " bit"
                        Accessible.name: qsTr("Mode")
                        onPicked: v => panel.run({ op: "setMode", mode: v.indexOf("Gray") === 0 ? "Gray" : "RGB", bits: v.indexOf("16") > 0 ? 16 : 8 })
                    }
                    Dropdown {
                        width: (dc.width - 6) * 0.6
                        options: Spec.LISTS.profile
                        value: panel.cur.profile
                        Accessible.name: qsTr("Hồ sơ màu")
                        onPicked: v => panel.run({ op: "assignProfile", profile: v })
                    }
                }
                Cap {
                    Layout.topMargin: 4
                    text: qsTr("Thao tác nhanh")
                }
                Row {
                    spacing: 6
                    Btn {
                        width: (dc.width - 6) / 2
                        implicitHeight: 26
                        leftPadding: 10
                        rightPadding: 10
                        icon.name: "frame_person"
                        text: qsTr("Select Subject")
                        onClicked: panel.run({ op: "select", shape: "subject" })
                    }
                    Btn {
                        width: (dc.width - 6) / 2
                        implicitHeight: 26
                        leftPadding: 10
                        rightPadding: 10
                        icon.name: "person"
                        text: qsTr("Remove Background")
                        onClicked: panel.run({ op: "removeBackground" })
                    }
                }
            }
        }
    }

    Component {  // W (link) H over X Y, the prototype's .g2 of a layer: fields kept out of the document
        id: sizeFields
        Column {
            id: sf
            readonly property var def: panel.sizes[panel.active.kind] || [panel.cur.w, panel.cur.h, 0, 0]
            spacing: 6
            Row {
                spacing: 6
                NumberField {
                    width: panel.half
                    label: "W"
                    labelColor: Theme.t3
                    unit: "px"
                    min: 1
                    value: panel.lf("w", sf.def[0])
                    Accessible.name: qsTr("Chiều rộng")
                    onMoved: v => panel.setLf("w", v)
                }
                Item {
                    width: 24
                    height: 24
                    ToolToggle {
                        visible: panel.active.kind !== "text"
                        width: 24
                        icon.name: "link"
                        icon.width: 16
                        checked: panel.lf("link", true)
                        ToolTip.text: qsTr("Giữ tỉ lệ · Link width and height")
                        Accessible.name: qsTr("Giữ tỉ lệ W và H")
                        onClicked: panel.setLf("link", !checked)
                    }
                }
                NumberField {
                    width: panel.half
                    label: "H"
                    labelColor: Theme.t3
                    unit: "px"
                    min: 1
                    value: panel.lf("h", sf.def[1])
                    Accessible.name: qsTr("Chiều cao")
                    onMoved: v => panel.setLf("h", v)
                }
            }
            Row {
                spacing: 6
                NumberField {
                    width: panel.half
                    label: "X"
                    labelColor: Theme.t3
                    unit: "px"
                    value: panel.lf("x", sf.def[2])
                    Accessible.name: qsTr("Vị trí X")
                    onMoved: v => panel.setLf("x", v)
                }
                Item {
                    width: 24
                    height: 24
                }
                NumberField {
                    width: panel.half
                    label: "Y"
                    labelColor: Theme.t3
                    unit: "px"
                    value: panel.lf("y", sf.def[3])
                    Accessible.name: qsTr("Vị trí Y")
                    onMoved: v => panel.setLf("y", v)
                }
            }
        }
    }

    Component {  // a pixel layer, a group
        id: pxPage
        Item {
            objectName: "page:px"
            implicitHeight: pc.implicitHeight + 18
            ColumnLayout {
                id: pc
                x: 12
                y: 8
                width: parent.width - 24
                spacing: 8
                RowLayout {
                    spacing: 8
                    Hd {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 18  // Main.dc.html's: the page 162 high, as the dock was measured
                        icon: panel.active.kind === "group" ? "folder" : "image"
                        text: panel.active.kind === "group" ? qsTr("Nhóm layer") : qsTr("Layer điểm ảnh")
                    }
                    Cap { text: qsTr("Biến đổi") }
                }
                Loader { sourceComponent: sizeFields }
                RowLayout {
                    spacing: 4
                    NumberField {
                        Layout.preferredWidth: 104
                        label: qsTr("Góc")
                        labelColor: Theme.t3
                        unit: "°"
                        decimals: 2
                        min: -180
                        max: 180
                        value: panel.lf("a", 0)
                        Accessible.name: qsTr("Góc xoay")
                        onMoved: v => panel.setLf("a", v)
                    }
                    ToolToggle {
                        icon.name: "flip"
                        icon.width: 16
                        ToolTip.text: qsTr("Lật ngang · Flip Horizontal")
                        Accessible.name: qsTr("Lật ngang")
                        onClicked: panel.run({ op: "flipLayer", axis: "h" })
                    }
                    ToolToggle {
                        icon.name: "flip"
                        icon.width: 16
                        contentItem.rotation: 90
                        ToolTip.text: qsTr("Lật dọc · Flip Vertical")
                        Accessible.name: qsTr("Lật dọc")
                        onClicked: panel.run({ op: "flipLayer", axis: "v" })
                    }
                    Item { Layout.fillWidth: true }
                }
                RowLayout {
                    spacing: 2
                    Cap {
                        Layout.preferredWidth: 42
                        text: qsTr("Căn")
                    }
                    Repeater {
                        model: Spec.ALIGN
                        ToolToggle {
                            id: al
                            required property var modelData
                            icon.name: al.modelData[0]
                            icon.width: 16
                            ToolTip.text: al.modelData[1]
                            Accessible.name: al.modelData[1]
                            onClicked: panel.run({ op: "step", name: al.modelData[1], icon: al.modelData[0] })
                        }
                    }
                    Item { Layout.fillWidth: true }
                }
            }
        }
    }

    Component {  // a smart object: the file it holds
        id: soPage
        Item {
            objectName: "page:so"
            implicitHeight: oc.implicitHeight + 18
            ColumnLayout {
                id: oc
                x: 12
                y: 8
                width: parent.width - 24
                spacing: 8
                Hd {
                    icon: "deployed_code"
                    text: qsTr("Smart object nhúng")
                }
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 40
                    radius: 6
                    color: Theme.p2
                    Row {
                        x: 6
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 10
                        Image {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 44
                            height: 28
                            fillMode: Image.PreserveAspectCrop
                            sourceSize: Qt.size(88, 56)
                            source: Mock.photoUrl(panel.active.props.photo)
                        }
                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            Text {
                                text: panel.active.props.name + ".jpg"
                                color: Theme.t1
                                font.pointSize: Theme.pt(12)
                            }
                            Text {
                                text: "1.800 × 1.125 px · RGB/8"
                                color: Theme.t3
                                font.family: Theme.mono
                                font.pointSize: Theme.pt(10.5)
                            }
                        }
                    }
                }
                Loader { sourceComponent: sizeFields }
                Row {
                    spacing: 6
                    Btn {
                        width: (oc.width - 6) / 2
                        implicitHeight: 26
                        leftPadding: 10
                        rightPadding: 10
                        icon.name: "open_in_new"
                        text: qsTr("Sửa nội dung")
                        onClicked: Docs.open(panel.active.props.name + ".psb")
                    }
                    Btn {
                        width: (oc.width - 6) / 2
                        implicitHeight: 26
                        text: qsTr("Rasterize")
                        onClicked: panel.run({ op: "rasterize" })
                    }
                }
            }
        }
    }

    Component {  // a type layer: Character and Paragraph in short
        id: textPage
        Item {
            objectName: "page:text"
            implicitHeight: tc.implicitHeight + 18
            ColumnLayout {
                id: tc
                x: 12
                y: 8
                width: parent.width - 24
                spacing: 8
                RowLayout {
                    spacing: 8
                    Layout.preferredHeight: 20
                    Text {
                        Layout.preferredWidth: 16
                        horizontalAlignment: Text.AlignHCenter
                        text: "T"
                        color: Theme.t3
                        font.weight: Font.DemiBold
                        font.pointSize: Theme.pt(12)
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Layer chữ")
                        color: Theme.t2
                        font.pointSize: Theme.pt(12)
                    }
                }
                Loader { sourceComponent: sizeFields }
                Cap {
                    Layout.topMargin: 2
                    text: qsTr("Character")
                }
                Dropdown {
                    Layout.fillWidth: true
                    implicitHeight: 26
                    font.weight: Font.DemiBold
                    options: Spec.LISTS.font
                    value: Tools.value("ch:font", "Be Vietnam Pro")
                    Accessible.name: qsTr("Font")
                    onPicked: v => Tools.setValue("ch:font", v)
                }
                GridLayout {
                    columns: 2
                    columnSpacing: 6
                    rowSpacing: 6
                    Dropdown {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 100
                        options: Spec.LISTS.fstyle
                        value: Tools.value("ch:style", "SemiBold")
                        Accessible.name: qsTr("Kiểu chữ")
                        onPicked: v => Tools.setValue("ch:style", v)
                    }
                    TextFld {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 100
                        icon: "format_size"
                        value: Tools.value("f:ch_size", "48 pt")
                        ToolTip.text: qsTr("Cỡ chữ · Set the font size")
                        Accessible.name: qsTr("Cỡ chữ")
                        onEdited: t => Tools.setValue("f:ch_size", t)
                    }
                    TextFld {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 100
                        icon: "format_line_spacing"
                        value: Tools.value("f:ch_lead", "Auto")
                        Accessible.name: qsTr("Leading")
                        onEdited: t => Tools.setValue("f:ch_lead", t)
                    }
                    TextFld {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 100
                        icon: "format_letter_spacing"
                        value: Tools.value("f:ch_track", "0")
                        Accessible.name: qsTr("Tracking")
                        onEdited: t => Tools.setValue("f:ch_track", t)
                    }
                }
                RowLayout {
                    spacing: 8
                    Dim { text: qsTr("Màu") }
                    Swatch {
                        Layout.preferredWidth: 34
                        Layout.preferredHeight: 22
                        radius: 4
                        ring: Theme.t4
                        color: Tools.value("ch:color", "#f4ede4")
                        ToolTip.text: qsTr("Màu chữ · Set the text color")
                        Accessible.name: qsTr("Màu chữ")
                        onClicked: panel.pickColor(color, qsTr("Màu chữ"), c => Tools.setValue("ch:color", String(c)))
                    }
                    Item { Layout.fillWidth: true }
                    Dropdown {
                        Layout.preferredWidth: 110
                        options: Spec.LISTS.aa
                        value: "aA " + Tools.antiAlias
                        Accessible.name: qsTr("Khử răng cưa")
                        onPicked: v => Tools.antiAlias = v.replace("aA ", "")
                    }
                }
                Cap {
                    Layout.topMargin: 2
                    text: qsTr("Paragraph")
                }
                RowLayout {
                    spacing: 2
                    Repeater {
                        model: [0, 1, 2, 6]
                        ToolToggle {
                            id: pa
                            required property int modelData
                            icon.name: Spec.PARA[pa.modelData][0]
                            icon.width: 16
                            checked: Tools.value("pa:align", 0) === pa.modelData
                            ToolTip.text: Spec.PARA[pa.modelData][1]
                            Accessible.name: Spec.PARA[pa.modelData][1].split(" · ")[0]
                            onClicked: Tools.setValue("pa:align", pa.modelData)
                        }
                    }
                    Item { Layout.fillWidth: true }
                    Btn {
                        implicitHeight: 24
                        leftPadding: 8
                        rightPadding: 8
                        text: qsTr("Mở Character")
                        onClicked: Tools.showPanel("Character")
                    }
                }
            }
        }
    }

    Component {  // a shape layer
        id: shapePage
        Item {
            id: spg
            objectName: "page:shape"
            implicitHeight: hc.implicitHeight + 18
            readonly property var tool: Mock.SHAPE_TOOLS[panel.active.props.shape] || Mock.SHAPE_TOOLS.rectS  // [name, icon]
            ColumnLayout {
                id: hc
                x: 12
                y: 8
                width: parent.width - 24
                spacing: 8
                Hd {
                    icon: spg.tool[1]
                    text: ({ ellipS: qsTr("Hình elip"), tri: qsTr("Hình tam giác"), poly: qsTr("Hình đa giác"), line: qsTr("Đường thẳng") })[panel.active.props.shape]
                          || qsTr("Hình chữ nhật")
                }
                Loader { sourceComponent: sizeFields }
                Cap {
                    Layout.topMargin: 2
                    text: qsTr("Diện mạo")
                }
                RowLayout {
                    spacing: 8
                    Dim { text: qsTr("Fill") }
                    Swatch {
                        Layout.preferredWidth: 34
                        Layout.preferredHeight: 22
                        radius: 4
                        ring: Theme.t4
                        color: panel.active.props.color || "#c0587e"
                        ToolTip.text: qsTr("Fill · Set shape fill type")
                        Accessible.name: qsTr("Màu Fill")
                        onClicked: panel.pickColor(color, qsTr("Màu Fill"), c => panel.run({ op: "setProps", props: { color: String(c) } }))
                    }
                    Dim {
                        Layout.leftMargin: 6
                        text: qsTr("Stroke")
                    }
                    Swatch {
                        Layout.preferredWidth: 34
                        Layout.preferredHeight: 22
                        radius: 4
                        ring: Theme.t4
                        none: !panel.lf("stroke", "")
                        color: panel.lf("stroke", "") || "transparent"
                        ToolTip.text: qsTr("Stroke · Set shape stroke type")
                        Accessible.name: qsTr("Màu Stroke")
                        onClicked: panel.pickColor(panel.lf("stroke", "#ffffff"), qsTr("Màu Stroke"), c => panel.setLf("stroke", String(c)))
                    }
                    TextFld {
                        Layout.preferredWidth: 58
                        value: Tools.value("f:sh_sw", "3 px")
                        Accessible.name: qsTr("Độ dày viền")
                        onEdited: t => Tools.setValue("f:sh_sw", t)
                    }
                    Item { Layout.fillWidth: true }
                }
                Cap {
                    Layout.topMargin: 2
                    text: qsTr("Bo góc")
                }
                Grid {  // top left, (link), top right; bottom left, bottom right
                    columns: 3
                    spacing: 6
                    Repeater {
                        model: [["r1", -90, qsTr("Bo góc trên trái")], null, ["r2", 0, qsTr("Bo góc trên phải")], ["r3", 180, qsTr("Bo góc dưới trái")], null,
                            ["r4", 90, qsTr("Bo góc dưới phải")]]
                        Item {
                            id: corner
                            required property var modelData
                            required property int index
                            width: corner.modelData ? panel.half : 24
                            height: 24
                            TextFld {
                                visible: !!corner.modelData
                                anchors.fill: parent
                                icon: "rounded_corner"
                                turn: corner.modelData ? corner.modelData[1] : 0
                                value: corner.modelData ? Tools.value("f:sh_" + corner.modelData[0], "24 px") : ""
                                Accessible.name: corner.modelData ? corner.modelData[2] : ""
                                onEdited: t => Tools.setValue("f:sh_" + corner.modelData[0], t)
                            }
                            ToolToggle {
                                visible: corner.index === 1
                                width: 24
                                icon.name: "link"
                                icon.width: 16
                                checked: Tools.value("sh:rlink", true)
                                ToolTip.text: qsTr("Bo đều bốn góc · Link together corner radius values")
                                Accessible.name: qsTr("Bo đều bốn góc")
                                onClicked: Tools.setValue("sh:rlink", !checked)
                            }
                        }
                    }
                }
                Cap {
                    Layout.topMargin: 2
                    text: qsTr("Thao tác path")
                }
                Row {
                    spacing: 2
                    Repeater {
                        model: Spec.PATH_OPS
                        ToolToggle {
                            id: po
                            required property var modelData
                            required property int index
                            icon.name: po.modelData[0]
                            icon.width: 16
                            checked: Tools.value("sh:op", 0) === po.index
                            ToolTip.text: po.modelData[1]
                            Accessible.name: po.modelData[1].split(" · ")[0]
                            onClicked: Tools.setValue("sh:op", po.index)
                        }
                    }
                }
            }
        }
    }

    Component {  // a fill layer: its colour (a gradient fill's gradient)
        id: fillPage
        Item {
            id: fpg
            objectName: "page:fill"
            implicitHeight: fc.implicitHeight + 18
            readonly property string fill: panel.active.props.color || "#808080"
            readonly property var stops: Spec.colors(fill)
            ColumnLayout {
                id: fc
                x: 12
                y: 8
                width: parent.width - 24
                spacing: 8
                Hd {
                    icon: "format_color_fill"
                    text: panel.active.props.name
                }
                RowLayout {
                    spacing: 8
                    Dim { text: qsTr("Màu") }
                    Swatch {
                        id: fs
                        readonly property bool solid: fpg.fill.charAt(0) === "#"
                        Layout.preferredWidth: 60
                        Layout.preferredHeight: 22
                        radius: 4
                        ring: Theme.t4
                        color: fs.solid ? fpg.fill : "transparent"
                        ToolTip.text: qsTr("Màu của fill layer · Mở Color Picker")
                        Accessible.name: qsTr("Màu của fill layer")
                        onClicked: {
                            if (fs.solid)
                                panel.pickColor(fs.color, qsTr("Màu của fill layer"), c => panel.run({ op: "setProps", props: { color: String(c) } }))
                            else
                                Tools.showPanel("Gradients")
                        }
                        Row {  // a gradient fill's: a two-colour piece between each pair of its stops
                            visible: !fs.solid
                            x: 1
                            y: 1
                            Repeater {
                                model: Math.max(0, fpg.stops.length - 1)
                                Rectangle {
                                    id: fp
                                    required property int index
                                    width: 58 / (fpg.stops.length - 1)
                                    height: 20
                                    gradient: Gradient {
                                        orientation: Gradient.Horizontal
                                        GradientStop { position: 0; color: fpg.stops[fp.index] }
                                        GradientStop { position: 1; color: fpg.stops[fp.index + 1] }
                                    }
                                }
                            }
                        }
                    }
                    Item { Layout.fillWidth: true }
                }
            }
        }
    }

    Component {  // an adjustment layer: its rows
        id: adjPage
        Item {
            id: apg
            objectName: "page:adj"
            implicitHeight: ac.implicitHeight + 20 + 32
            readonly property string adj: panel.active.props.adj || ""
            ColumnLayout {
                id: ac
                x: 12
                y: 8
                width: parent.width - 24
                spacing: 9
                RowLayout {
                    spacing: 8
                    Layout.preferredHeight: 24
                    Icon {
                        name: Spec.ADJ[apg.adj] ? Spec.ADJ[apg.adj].ic : "tune"
                        size: 17
                        color: Theme.t2
                    }
                    Text {
                        Layout.fillWidth: true
                        text: panel.active.props.name
                        color: Theme.t1
                        elide: Text.ElideRight
                        font.weight: Font.DemiBold
                        font.pointSize: Theme.pt(12.5)
                    }
                    ToolToggle {
                        icon.name: Spec.ADJ[apg.adj] ? Spec.ADJ[apg.adj].ic : "tune"
                        icon.width: 15
                        checked: true
                        ToolTip.text: qsTr("Thông số adjustment")
                        Accessible.name: qsTr("Thông số adjustment")
                    }
                    ToolToggle {
                        enabled: !!panel.active.props.mask
                        icon.name: "mask"
                        icon.width: 15
                        ToolTip.text: qsTr("Layer Mask")
                        Accessible.name: qsTr("Layer Mask")
                        onClicked: panel.doc.setEditMask(true)
                    }
                }
                AdjustRows {
                    Layout.fillWidth: true
                    name: apg.adj
                    values: Spec.full(apg.adj, panel.active.props.vals)
                    onStarted: panel.doc.beginGesture()
                    onMoved: v => panel.run({ op: "setProps", layer: panel.active.id, props: { vals: Spec.changed(apg.adj, v) } })
                    onCommitted: panel.doc.endGesture()
                }
            }
            Foot {
                ToolToggle {
                    icon.name: "subdirectory_arrow_right"
                    checked: !!panel.active.props.clip
                    ToolTip.text: qsTr("Clip vào layer dưới · Clip to Layer (Ctrl Alt G)")
                    Accessible.name: qsTr("Clip vào layer dưới")
                    onClicked: panel.run({ op: "clippingMask" })
                }
                ToolToggle {
                    icon.name: "preview"
                    ToolTip.text: qsTr("Giữ để xem trạng thái trước · View Previous State (\\)")
                    Accessible.name: qsTr("Giữ để xem trạng thái trước")
                    onPressedChanged: panel.peeking = pressed
                }
                ToolToggle {
                    icon.name: "restart_alt"
                    ToolTip.text: qsTr("Đặt lại mặc định · Reset to Adjustment Defaults")
                    Accessible.name: qsTr("Đặt lại mặc định")
                    onClicked: panel.run({ op: "resetAdjustment" })
                }
                ToolToggle {
                    icon.name: panel.active.props.visible ? "visibility" : "visibility_off"
                    ToolTip.text: qsTr("Ẩn hiện layer · Toggle Layer Visibility")
                    Accessible.name: qsTr("Ẩn hiện layer")
                    onClicked: panel.run({ op: "setProps", props: { visible: !panel.active.props.visible } })
                }
                ToolToggle {
                    icon.name: "delete"
                    ToolTip.text: qsTr("Xóa · Delete This Layer")
                    Accessible.name: qsTr("Xóa adjustment layer")
                    onClicked: panel.run({ op: "deleteLayers", layers: [panel.active.id] })
                }
            }
        }
    }

    Component {  // the layer's mask
        id: maskPage
        Item {
            objectName: "page:mask"
            implicitHeight: mc.implicitHeight + 18 + 32
            ColumnLayout {
                id: mc
                x: 12
                y: 8
                width: parent.width - 24
                spacing: 8
                RowLayout {  // .hd, the layer's name at its right
                    spacing: 8
                    Layout.preferredHeight: 20
                    Icon {
                        name: "mask"
                        size: 16
                        color: Theme.t3
                    }
                    Text {
                        text: qsTr("Layer Mask")
                        color: Theme.t2
                        font.pointSize: Theme.pt(12)
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        Layout.maximumWidth: 140
                        text: panel.active.props.name
                        color: Theme.t3
                        elide: Text.ElideRight
                        font.family: Theme.mono
                        font.pointSize: Theme.pt(11)
                    }
                    ToolToggle {  // back to an adjustment layer's rows
                        visible: panel.active.kind === "adj"
                        icon.name: Spec.ADJ[panel.active.props.adj] ? Spec.ADJ[panel.active.props.adj].ic : "tune"
                        icon.width: 15
                        ToolTip.text: qsTr("Thông số adjustment")
                        Accessible.name: qsTr("Thông số adjustment")
                        onClicked: panel.doc.setEditMask(false)
                    }
                }
                SliderField {
                    Layout.fillWidth: true
                    label: qsTr("Density")
                    unit: "%"
                    value: Math.round(panel.active.props.maskDensity * 100)
                    onStarted: panel.doc.beginGesture()
                    onMoved: v => panel.run({ op: "setProps", props: { maskDensity: v / 100 } })
                    onCommitted: panel.doc.endGesture()
                }
                SliderField {
                    Layout.fillWidth: true
                    Layout.topMargin: 2
                    label: qsTr("Feather")
                    unit: "px"
                    max: 250
                    decimals: 1
                    step: 0.1
                    value: panel.active.props.maskFeather
                    onStarted: panel.doc.beginGesture()
                    onMoved: v => panel.run({ op: "setProps", props: { maskFeather: v } })
                    onCommitted: panel.doc.endGesture()
                }
                Cap {
                    Layout.topMargin: 4
                    text: qsTr("Tinh chỉnh")
                }
                Grid {
                    columns: 2
                    spacing: 6
                    Btn {
                        width: (mc.width - 6) / 2
                        implicitHeight: 26
                        text: qsTr("Select and Mask…")
                        onClicked: if (panel.win && panel.win.hasSpace("SelectMask")) panel.win.openSpace("SelectMask")
                    }
                    Btn {
                        width: (mc.width - 6) / 2
                        implicitHeight: 26
                        enabled: !!panel.win && panel.win.hasDialog("ColorRange")
                        text: qsTr("Color Range…")
                        onClicked: panel.win.openDialog("ColorRange")
                    }
                    Btn {
                        width: (mc.width - 6) / 2
                        implicitHeight: 26
                        text: qsTr("Invert")
                        onClicked: panel.run({ op: "mask", action: "inv" })
                    }
                }
            }
            Foot {
                ToolToggle {
                    icon.name: "load_sel"
                    ToolTip.text: qsTr("Nạp làm vùng chọn · Load Selection from Mask")
                    Accessible.name: qsTr("Nạp Mask làm vùng chọn")
                    onClicked: panel.run({ op: "select", shape: "skin", name: qsTr("Nạp vùng chọn từ Mask") })
                }
                ToolToggle {
                    icon.name: "check"
                    ToolTip.text: qsTr("Áp Mask vào layer · Apply Mask")
                    Accessible.name: qsTr("Áp Mask vào layer")
                    onClicked: panel.run({ op: "mask", action: "apply" })
                }
                ToolToggle {
                    icon.name: "visibility"
                    checked: !!panel.active.props.maskOff
                    ToolTip.text: qsTr("Tắt Mask · Disable/Enable Mask")
                    Accessible.name: qsTr("Tắt Mask")
                    onClicked: panel.run({ op: "mask", action: "off" })
                }
                ToolToggle {
                    icon.name: "delete"
                    ToolTip.text: qsTr("Xóa Mask · Delete Mask")
                    Accessible.name: qsTr("Xóa Mask")
                    onClicked: panel.run({ op: "mask", action: "del" })
                }
            }
        }
    }
}
