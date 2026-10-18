pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import "NewDocSpec.js" as Spec
import "Fmt.js" as Fmt

// Tạo tài liệu mới (build/proto/NewDoc.dc.html), from File › New and the Start screen: the preset groups and their
// cards on the left, Chi tiết on the right; Tạo (or Enter in a field) makes the document with Docs.create(). Sizes are
// kept in px whatever the fields show, so a new unit or orientation keeps the size, and a new resolution keeps the
// printed size unless the unit is Pixels. The fields take sums and units ("1240*2", "5cm") as they are typed. Đã lưu
// is kept in Prefs ("newDocPresets").
DialogBox {
    id: dlg
    property var params: ({})
    // The document to make: name; w, h in px; unit (Spec.UNITS), ppi (px/inch), ru (Spec.RES_UNITS); mode, bits, bg
    // as Docs.create() takes them; bgColor, the colour of Tùy chọn; profile.
    property var d: Spec.DEFAULT
    property string cat: "print"  // the group shown
    property string sel: "print 0"  // the card on: "<group> <index>"
    readonly property var saved: Spec.SAVED.concat(Prefs.value("newDocPresets"))  // the mock's, then the user's
    readonly property var cards: cat === "saved" ? saved : Spec.presets(cat)
    readonly property bool portrait: d.h >= d.w
    readonly property bool perCm: d.ru === "Pixels/Centimeter"

    title: qsTr("Tạo tài liệu mới")
    closable: true
    boxWidth: 920
    buttons: [{ text: qsTr("Hủy"), act: () => dlg.close() }, { text: qsTr("Tạo"), primary: true, act: () => dlg.create() }]
    Component.onCompleted: d = Object.assign({}, Spec.DEFAULT, { name: Spec.untitled(Docs.docs) })
    onOpened: {
        nameField.forceActiveFocus()
        nameField.selectAll()
    }

    function set(o) { d = Object.assign({}, d, o) }
    function clamp(x, a, b) { return Math.min(b, Math.max(a, x)) }
    function profileFor(mode) { return Spec.PROFILES[mode].indexOf(d.profile) < 0 ? Spec.PROFILES[mode][0] : d.profile }
    function apply(p, id) {
        sel = id
        set({ name: p.n, w: Spec.toPx(p.w, p.u, p.ppi), h: Spec.toPx(p.h, p.u, p.ppi), unit: p.u, ppi: p.ppi, mode: p.mode, bits: p.bits, bg: p.bg, profile: profileFor(p.mode) })
    }
    function setSize(key, x) { set({ [key]: clamp(Spec.toPx(x, d.unit, d.ppi), 1, 300000) }) }  // x in the unit shown
    function setRes(x) {
        const p = clamp(perCm ? x * 2.54 : x, 1, 30000), f = d.unit === "Pixels" ? 1 : p / d.ppi
        set({ ppi: p, w: clamp(d.w * f, 1, 300000), h: clamp(d.h * f, 1, 300000) })
    }
    function orient(toPortrait) {
        if (toPortrait !== portrait && d.w !== d.h)
            set({ w: d.h, h: d.w })
    }
    function savePreset() {
        const p = Spec.P(d.name, Spec.fromPx(d.w, d.unit, d.ppi), Spec.fromPx(d.h, d.unit, d.ppi), d.unit, d.ppi, { mode: d.mode, bits: d.bits, bg: d.bg })
        Prefs.set("newDocPresets", Prefs.value("newDocPresets").concat([p]))
        cat = "saved"
        sel = "saved " + (saved.length - 1)
    }
    function create() {
        if (Docs.create({ name: d.name, w: Math.round(d.w), h: Math.round(d.h), ppi: d.ppi, mode: d.mode, bits: d.bits, background: d.bg, color: d.bgColor, profile: d.profile }))
            close()
    }

    // A label over a row of Chi tiết (.lb).
    component Cap: Text {
        color: Theme.t3
        font.pointSize: Theme.pt(11)
        Layout.bottomMargin: -2
    }
    // A number field (.fld): `value` in the unit it shows, applied as it is typed (sums and units read by Fmt.parse in
    // `unit`), ↑ ↓ ±1 (Shift ±10). It shows the model's value again once it loses focus.
    component Num: TextBox {
        property real value
        property int decimals: 2
        property string unit
        property real ppi: 72
        readonly property string shown: Spec.num(value, decimals)
        signal typed(real v)
        Layout.fillWidth: true
        implicitWidth: 40
        font.family: Theme.mono
        Component.onCompleted: text = shown
        onShownChanged: if (!activeFocus) text = shown
        onActiveFocusChanged: if (!activeFocus) text = shown
        onTextEdited: {
            const v = Fmt.parse(text, unit, ppi, value)
            if (!isNaN(v))
                typed(v)
        }
        Keys.onUpPressed: e => nudge(e.modifiers & Qt.ShiftModifier ? 10 : 1)
        Keys.onDownPressed: e => nudge(e.modifiers & Qt.ShiftModifier ? -10 : -1)
        function nudge(k) {
            typed(value + k)
            text = shown
            selectAll()
        }
    }

    Text {  // the footer's note at its left, as the artboard's .df (its "P2: Artboards." a design note)
        parent: dlg.footer
        x: 16
        anchors.verticalCenter: parent.verticalCenter
        text: qsTr("Nền: trắng, đen, màu sau, trong suốt hay màu tùy chọn.")
        color: Theme.t3
        font.pointSize: Theme.pt(11)
    }
    contentItem: Item {
        implicitHeight: 502
        RowLayout {
            anchors.fill: parent
            spacing: 0
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0
                Item {  // the groups
                    Layout.fillWidth: true
                    implicitHeight: 43
                    Row {
                        x: 14
                        y: 10
                        spacing: 2
                        Repeater {
                            model: Spec.CATS
                            AbstractButton {
                                id: tab
                                required property var modelData
                                readonly property bool on: dlg.cat === modelData[0]
                                height: 32
                                leftPadding: 10
                                rightPadding: 10
                                text: modelData[1]
                                font.pointSize: Theme.pt(12)
                                Accessible.checkable: true
                                Accessible.checked: on
                                onClicked: dlg.cat = modelData[0]
                                contentItem: Text {
                                    text: tab.text
                                    font: tab.font
                                    color: tab.on ? Theme.t1 : Theme.t3
                                    verticalAlignment: Text.AlignVCenter
                                }
                                background: Item {
                                    Rectangle { visible: tab.on; anchors.bottom: parent.bottom; width: parent.width; height: 2; color: Theme.accent }
                                }
                            }
                        }
                    }
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.ln }
                }
                Flickable {
                    id: page
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentWidth: width
                    contentHeight: grid.height + 32
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    acceptedButtons: Qt.NoButton
                    ScrollBar.vertical: ScrollBar {}
                    GridLayout {  // the cards of a row as tall as its tallest
                        id: grid
                        x: 16
                        y: 16
                        width: page.width - 32
                        columns: 4
                        uniformCellWidths: true
                        rowSpacing: 10
                        columnSpacing: 10
                        Repeater {
                            model: dlg.cards
                            AbstractButton {
                                id: card
                                required property var modelData
                                required property int index
                                readonly property string cardId: dlg.cat + " " + index
                                readonly property bool on: dlg.sel === cardId
                                readonly property var shape: Spec.box(modelData.w, modelData.h, 70, 62)
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1  // the columns share the width
                                Layout.fillHeight: true
                                padding: 10
                                text: modelData.n
                                font.pointSize: Theme.pt(12.5)
                                Accessible.checkable: true
                                Accessible.checked: on
                                onClicked: dlg.apply(modelData, cardId)
                                background: Rectangle {
                                    radius: 8
                                    color: card.on ? Theme.acc2 : Theme.p1
                                    border.width: card.on ? 1.5 : 1
                                    border.color: card.on ? Theme.accent : Theme.ln
                                }
                                contentItem: Column {
                                    spacing: 4
                                    Item {
                                        width: parent.width
                                        height: 90
                                        Rectangle {
                                            width: parent.width
                                            height: 86
                                            radius: 6
                                            color: Theme.p2
                                            Rectangle {
                                                anchors.centerIn: parent
                                                width: card.shape.w + 2
                                                height: card.shape.h + 2
                                                radius: 3
                                                color: Theme.bg
                                                border.color: Theme.t3
                                            }
                                        }
                                    }
                                    Text {
                                        width: parent.width
                                        text: card.text
                                        font: card.font
                                        color: Theme.t1
                                        wrapMode: Text.Wrap
                                    }
                                    Text {
                                        readonly property int line: Math.round(14.2 * Theme.fontScale)  // 10.5 px × 1.35 (15 of its own)
                                        width: parent.width
                                        height: lineCount * line
                                        text: Spec.sizeText(card.modelData)
                                        font.family: Theme.mono
                                        font.pointSize: Theme.pt(10.5)
                                        lineHeightMode: Text.FixedHeight
                                        lineHeight: line
                                        color: Theme.t3
                                        wrapMode: Text.Wrap
                                    }
                                }
                            }
                        }
                    }
                }
            }
            Rectangle {  // Chi tiết
                Layout.preferredWidth: 300
                Layout.fillHeight: true
                color: Theme.bg
                Rectangle { width: 1; height: parent.height; color: Theme.ln }
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    anchors.leftMargin: 17  // past the rule
                    spacing: 10
                    RowLayout {
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Chi tiết")
                            color: Theme.t1
                            font.pointSize: Theme.pt(12)
                            font.weight: Font.DemiBold
                        }
                        IconButton {
                            implicitWidth: 30
                            icon.name: "save"
                            ToolTip.text: qsTr("Lưu thành preset · Save Document Preset")
                            onClicked: dlg.savePreset()
                        }
                    }
                    TextBox {
                        id: nameField
                        Layout.fillWidth: true
                        text: dlg.d.name
                        Accessible.name: qsTr("Tên tài liệu")
                        onTextEdited: dlg.set({ name: text })
                        onAccepted: dlg.create()
                    }
                    Cap { text: qsTr("Rộng · Cao") }
                    RowLayout {
                        spacing: 6
                        Num {
                            value: Spec.fromPx(dlg.d.w, dlg.d.unit, dlg.d.ppi)
                            decimals: dlg.d.unit === "Pixels" ? 0 : 2
                            unit: Spec.ABBR[dlg.d.unit]
                            ppi: dlg.d.ppi
                            Accessible.name: qsTr("Rộng")
                            onTyped: v => dlg.setSize("w", v)
                            onAccepted: dlg.create()
                        }
                        Num {
                            value: Spec.fromPx(dlg.d.h, dlg.d.unit, dlg.d.ppi)
                            decimals: dlg.d.unit === "Pixels" ? 0 : 2
                            unit: Spec.ABBR[dlg.d.unit]
                            ppi: dlg.d.ppi
                            Accessible.name: qsTr("Cao")
                            onTyped: v => dlg.setSize("h", v)
                            onAccepted: dlg.create()
                        }
                        Select {
                            Layout.preferredWidth: 96
                            options: Spec.UNITS.map((u) => [u, u])
                            value: dlg.d.unit
                            Accessible.name: qsTr("Đơn vị")
                            onPicked: v => dlg.set({ unit: v })
                        }
                    }
                    RowLayout {
                        spacing: 6
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Hướng")
                            color: Theme.t2
                            font.pointSize: Theme.pt(12)
                        }
                        IconButton {
                            implicitWidth: 30
                            icon.name: "crop_portrait"
                            icon.width: 17
                            on: dlg.portrait
                            ToolTip.text: qsTr("Dọc · Portrait")
                            onClicked: dlg.orient(true)
                        }
                        IconButton {
                            implicitWidth: 30
                            icon.name: "crop_landscape"
                            icon.width: 17
                            on: !dlg.portrait
                            ToolTip.text: qsTr("Ngang · Landscape")
                            onClicked: dlg.orient(false)
                        }
                    }
                    Cap { text: qsTr("Độ phân giải") }
                    RowLayout {
                        spacing: 6
                        Num {
                            value: dlg.perCm ? dlg.d.ppi / 2.54 : dlg.d.ppi
                            Accessible.name: qsTr("Độ phân giải")
                            onTyped: v => dlg.setRes(v)
                            onAccepted: dlg.create()
                        }
                        Select {
                            Layout.preferredWidth: 132
                            options: Spec.RES_UNITS.map((u) => [u, u])
                            value: dlg.d.ru
                            Accessible.name: qsTr("Đơn vị độ phân giải")
                            onPicked: v => dlg.set({ ru: v })
                        }
                    }
                    Cap { text: qsTr("Mode màu") }
                    RowLayout {
                        spacing: 6
                        Select {
                            Layout.fillWidth: true
                            options: Spec.MODES
                            value: dlg.d.mode
                            Accessible.name: qsTr("Mode màu")
                            onPicked: v => dlg.set({ mode: v, profile: dlg.profileFor(v) })
                        }
                        Select {
                            Layout.preferredWidth: 96
                            options: Spec.BITS
                            value: dlg.d.bits
                            Accessible.name: qsTr("Độ sâu bit")
                            onPicked: v => dlg.set({ bits: v })
                        }
                    }
                    Cap { text: qsTr("Nền") }
                    RowLayout {
                        spacing: 6
                        Select {
                            Layout.fillWidth: true
                            options: Spec.BG.map((b) => [b[0], b[1]])
                            value: dlg.d.bg
                            Accessible.name: qsTr("Nền")
                            onPicked: v => dlg.set({ bg: v })
                        }
                        AbstractButton {  // the colour, Tùy chọn's from the Color Picker
                            id: swatch
                            readonly property string fill: dlg.d.bg === "background" ? Tools.bg : dlg.d.bg === "custom" ? dlg.d.bgColor : Spec.BG.find((b) => b[0] === dlg.d.bg)[2]
                            implicitWidth: 28
                            implicitHeight: 28
                            hoverEnabled: true
                            ToolTip.visible: hovered
                            ToolTip.delay: 600
                            ToolTip.text: qsTr("Màu nền")
                            Accessible.name: ToolTip.text
                            onClicked: {
                                const main = swatch.Window.window as Main
                                if (main && main.hasDialog("ColorPicker"))
                                    main.openDialog("ColorPicker", { color: fill, title: qsTr("màu nền"), picked: (c) => dlg.set({ bg: "custom", bgColor: String(c) }) })
                            }
                            background: Item {
                                Rectangle { anchors.fill: parent; radius: 5; color: swatch.fill }
                                Canvas {  // Trong suốt: the checkerboard of Thiết lập
                                    anchors.fill: parent
                                    visible: dlg.d.bg === "transparent"
                                    onVisibleChanged: requestPaint()
                                    onPaint: {
                                        const c = getContext("2d")
                                        c.reset()
                                        c.beginPath()
                                        c.roundedRect(0, 0, width, height, 5, 5)
                                        c.clip()
                                        for (let y = 0; y < height; y += 4)
                                            for (let x = 0; x < width; x += 4) {
                                                c.fillStyle = (x + y) / 4 % 2 ? Prefs.value("checkerColor2") : Prefs.value("checkerColor1")
                                                c.fillRect(x, y, 4, 4)
                                            }
                                    }
                                }
                                Rectangle { anchors.fill: parent; radius: 5; color: "transparent"; border.color: Theme.t4 }
                            }
                        }
                    }
                    Cap { text: qsTr("Hồ sơ màu") }
                    Select {
                        Layout.fillWidth: true
                        options: Spec.PROFILES[dlg.d.mode].map((p) => [p, p])
                        value: dlg.d.profile
                        Accessible.name: qsTr("Hồ sơ màu")
                        onPicked: v => dlg.set({ profile: v })
                    }
                    Item { Layout.fillHeight: true }
                    Text {
                        text: Spec.info(dlg.d.w, dlg.d.h, dlg.d.ppi, dlg.d.mode, dlg.d.bits)
                        color: Theme.t3
                        font.family: Theme.mono
                        font.pointSize: Theme.pt(11)
                    }
                }
            }
        }
    }
}
