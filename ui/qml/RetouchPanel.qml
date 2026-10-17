pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import "Mock.js" as Mock

// The Retouch panel (docs/ui-tasks.md, U14; the Retouch artboard, the rtd part of Main.dc.html; plan.md's Retouch chân
// dung), a tab of the dock's group Properties, Adjustments, Retouch: the faces found, then five folding sections. Each
// button is one History step of Docs.current (Mock.js's Retouch commands): Tách tần số, Mask tự động, Làm mịn da,
// Dodge & Burn (then the Brush at Opacity 6%), Mắt, răng. The sections open, the overlay chips and every value are
// Tools.retouch's (the panel goes when its tab does), which FaceOverlay (U7) draws from; the badges say what the
// document has already. The commands carry the panel's values for the core; the mock has no pixels to use them on.
Item {
    id: panel
    readonly property Doc doc: Docs.current
    readonly property var rt: Tools.retouch
    readonly property bool faces: !!doc && doc.faces
    // The prototype's done: a group Tách tần số; a layer whose name starts with Dodge & Burn.
    readonly property bool fsDone: !!doc && doc.layers.some(l => l.kind === "group" && l.props.name === qsTr("Tách tần số"))
    readonly property bool dbDone: !!doc && doc.layers.some(l => l.props.name.indexOf(qsTr("Dodge & Burn")) === 0)
    property var menu: []
    implicitWidth: 308
    implicitHeight: col.implicitHeight

    function set(patch) { Tools.setRetouch(patch) }
    function flip(key, k) { set({ [key]: Object.assign({}, rt[key], { [k]: !rt[key][k] }) }) }  // a section open, a chip on
    function run(cmd) { return !!doc && doc.run(cmd) }
    function dodgeBurn() {
        if (!run({ op: "dodgeBurn", method: rt.dbm }))
            return
        Tools.setTool("brush", "brush")
        Tools.setValue("brush:7", 6)  // the Brush bar's Opacity, %
    }

    component SmallBtn: Btn {  // .btn: 24 high
        implicitHeight: 24
        leftPadding: 10
        rightPadding: 10
    }
    component Note: Text {  // 11 px, t3
        color: Theme.t3
        font.pointSize: Theme.pt(11)
        wrapMode: Text.Wrap
    }
    component RowLabel: Text {
        color: Theme.t2
        font.pointSize: Theme.pt(12)
    }
    component Radius: NumberField {  // a Gaussian radius, as a field of the prototype's 120 px
        Layout.preferredWidth: 120
        label: qsTr("Radius")
        labelColor: Theme.t3
        unit: "px"
        min: 1
        max: 1000
    }

    ColumnLayout {
        id: col
        width: parent.width
        spacing: 0

        RowLayout {  // the faces: the one of a mock portrait, picked
            Layout.margins: 12
            Layout.topMargin: 8
            Layout.bottomMargin: 8
            spacing: 10
            AbstractButton {
                objectName: "face"
                visible: panel.faces
                implicitWidth: 44
                implicitHeight: 44
                ToolTip.visible: hovered
                ToolTip.delay: 600
                ToolTip.text: qsTr("Mặt 1")
                Accessible.name: qsTr("Chọn Mặt 1")
                Accessible.checkable: true
                Accessible.checked: true
                contentItem: Item {
                    Item {
                        anchors.fill: parent
                        clip: true
                        Image {
                            x: -51
                            y: -47
                            width: 127
                            height: 159
                            fillMode: Image.PreserveAspectCrop
                            source: Mock.photoUrl(panel.doc ? panel.doc.photo : null)
                            sourceSize.width: 254
                        }
                    }
                    Rectangle {  // the accent ring 2 px out, which also rounds the photo's corners
                        anchors.fill: parent
                        anchors.margins: -2
                        radius: 8
                        color: "transparent"
                        border.width: 2
                        border.color: Theme.accent
                    }
                }
            }
            Column {
                visible: panel.faces
                Layout.fillWidth: true
                Text { text: qsTr("1 khuôn mặt"); color: Theme.t1; font.pointSize: Theme.pt(12) }
                Note { text: qsTr("Nhấp để chọn mặt cần chỉnh") }
            }
            Text {
                visible: !panel.faces
                Layout.fillWidth: true
                text: qsTr("Không thấy khuôn mặt nào trong ảnh này.")
                color: Theme.t3
                font.pointSize: Theme.pt(12)
                wrapMode: Text.Wrap
            }
            SmallBtn { text: qsTr("Tìm lại") }  // the mock's faces are its document's: nothing more to find
        }

        Section {
            Layout.fillWidth: true
            title: qsTr("Tách tần số")
            open: !!panel.rt.open.fs
            badge: panel.fsDone ? qsTr("Đã có nhóm") : ""
            onToggled: panel.flip("open", "fs")
            RowLayout {
                spacing: 8
                Radius {
                    value: panel.rt.fsRadius ?? 8
                    Accessible.name: qsTr("Radius tách tần số")
                    onMoved: v => panel.set({ fsRadius: v })
                }
                Note { Layout.fillWidth: true; text: qsTr("gợi ý theo cỡ mặt") }
            }
            RowLayout {
                spacing: 8
                RowLabel { text: qsTr("Xem") }
                Segmented {
                    Layout.fillWidth: true
                    fill: true
                    options: [qsTr("Cả hai"), qsTr("Thấp"), qsTr("Cao")]
                    current: panel.rt.fsView
                    Accessible.name: qsTr("Xem lớp")
                    onPicked: i => panel.set({ fsView: i })
                }
            }
            SmallBtn {
                Layout.alignment: Qt.AlignRight
                primary: true
                text: panel.fsDone ? qsTr("Tạo lại") : qsTr("Tạo")
                onClicked: panel.run({ op: "frequencySeparation", radius: panel.rt.fsRadius ?? 8 })
            }
        }

        Section {
            Layout.fillWidth: true
            title: qsTr("Mask tự động")
            open: !!panel.rt.open.mask
            onToggled: panel.flip("open", "mask")
            ColumnLayout {
                spacing: 9
                Chips {
                    Layout.fillWidth: true
                    options: [["skin", qsTr("Da"), Qt.rgba(1, 45 / 255, 85 / 255, 0.7)], ["eyes", qsTr("Mắt"), Qt.rgba(1, 214 / 255, 0, 0.8)],
                        ["lips", qsTr("Môi"), Qt.rgba(170 / 255, 60 / 255, 1, 0.75)], ["teeth", qsTr("Răng"), Qt.rgba(0, 200 / 255, 1, 0.75)]]
                    value: panel.rt.chips
                    onPicked: k => panel.flip("chips", k)
                }
                Toggle {
                    Layout.fillWidth: true
                    topPadding: 0
                    bottomPadding: 0
                    text: qsTr("Xem lớp phủ")
                    checked: panel.rt.overlay
                    onClicked: panel.set({ overlay: !panel.rt.overlay })
                }
                SliderField {
                    Layout.fillWidth: true
                    label: qsTr("Feather")
                    max: 50
                    unit: "px"
                    value: panel.rt.feather
                    onMoved: v => panel.set({ feather: v })
                }
                RowLayout {
                    spacing: 8
                    RowLabel { text: qsTr("Ra") }
                    Segmented {
                        Layout.fillWidth: true
                        fill: true
                        options: [qsTr("Vùng chọn"), qsTr("Mask")]
                        current: panel.rt.out
                        Accessible.name: qsTr("Ra")
                        onPicked: i => panel.set({ out: i })
                    }
                    SmallBtn {
                        primary: true
                        text: qsTr("Tạo")
                        onClicked: panel.run({ op: "autoMask", parts: ["skin", "eyes", "lips", "teeth"].filter(k => panel.rt.chips[k]), output: panel.rt.out,
                            feather: panel.rt.feather })
                    }
                }
            }
        }

        Section {
            Layout.fillWidth: true
            title: qsTr("Làm mịn da")
            open: !!panel.rt.open.smooth
            onToggled: panel.flip("open", "smooth")
            ColumnLayout {
                spacing: 9
                SliderField {
                    Layout.fillWidth: true
                    label: qsTr("Amount")
                    value: panel.rt.amount
                    onMoved: v => panel.set({ amount: v })
                }
                SliderField {
                    Layout.fillWidth: true
                    label: qsTr("Texture")
                    value: panel.rt.texture
                    onMoved: v => panel.set({ texture: v })
                }
                RowLayout {
                    spacing: 8
                    Radius {
                        value: panel.rt.radius ?? 12
                        Accessible.name: qsTr("Radius làm mịn da")
                        onMoved: v => panel.set({ radius: v })
                    }
                    Note { Layout.fillWidth: true; text: qsTr("gợi ý theo cỡ mặt") }
                    SmallBtn {
                        text: qsTr("Áp dụng")
                        onClicked: panel.run({ op: "smoothSkin", amount: panel.rt.amount, texture: panel.rt.texture, radius: panel.rt.radius ?? 12 })
                    }
                }
                Note { Layout.fillWidth: true; text: qsTr("Chỉ trong Mask da; ra layer riêng có Layer Mask, sửa lại được.") }
            }
        }

        Section {
            Layout.fillWidth: true
            title: qsTr("Dodge & Burn")
            open: !!panel.rt.open.db
            badge: panel.dbDone ? qsTr("Đã có layer") : ""
            onToggled: panel.flip("open", "db")
            ColumnLayout {
                spacing: 9
                ColumnLayout {
                    spacing: 6
                    Accessible.name: qsTr("Cách dựng Dodge & Burn")
                    Repeater {
                        model: [qsTr("Cặp Curves sáng, tối, Mask đen"), qsTr("Layer xám 50% Soft Light")]
                        AssignProfileDialog.Radio {
                            required property string modelData
                            required property int index
                            implicitHeight: 16  // a line of the prototype's label
                            text: modelData
                            checked: panel.rt.dbm === index
                            onClicked: panel.set({ dbm: index })
                        }
                    }
                }
                RowLayout {
                    spacing: 8
                    Note { Layout.fillWidth: true; text: qsTr("Sau đó chuyển sang Brush, Opacity 6%.") }
                    SmallBtn { primary: true; text: qsTr("Tạo"); onClicked: panel.dodgeBurn() }
                }
            }
        }

        Section {
            Layout.fillWidth: true
            title: qsTr("Mắt, răng")
            open: !!panel.rt.open.eyes
            onToggled: panel.flip("open", "eyes")
            ColumnLayout {
                spacing: 9
                Repeater {
                    model: [["eyeW", qsTr("Làm trắng mắt")], ["eyeB", qsTr("Làm sáng mắt")], ["teeth", qsTr("Làm trắng răng")]]
                    SliderField {
                        required property var modelData
                        Layout.fillWidth: true
                        label: modelData[1]
                        value: panel.rt[modelData[0]]
                        onMoved: v => panel.set({ [modelData[0]]: v })
                    }
                }
                SmallBtn {
                    Layout.alignment: Qt.AlignRight
                    primary: true
                    text: qsTr("Tạo")
                    onClicked: panel.run({ op: "eyesTeeth", eyeW: panel.rt.eyeW, eyeB: panel.rt.eyeB, teeth: panel.rt.teeth })
                }
            }
        }
    }
}
