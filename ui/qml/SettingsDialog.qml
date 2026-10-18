// The frame after snapone a3650de (ui/qml/SettingsDialog.qml); the pages are build/proto/Settings.dc.html's.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import "Fmt.js" as Fmt

// Thiết lập (docs/ui-tasks.md, U21): Prefs.sections on the left, the open one on the right. Hiệu năng is the
// artboard's own page; the other 15 are built from Prefs.panes, a row by kind. Values only go into Prefs: Main ties
// theme, accent and text size to Theme, the canvas and its overlays read the rest, so a change shows at once.
// params.page is the section to open on (Help › Giấy phép: "about"); Hiệu năng without it, as the prototype opens.
// Colour wells and Phím tắt's link open ColorPicker and Shortcuts through the window's openDialog() (Main), dimmed
// while the window has no such dialog.
DialogBox {
    id: st
    property var params: ({})
    property string page: params.page || "performance"
    property bool copied  // the install ID is on the clipboard

    function openOther(name, p) {
        const w = frame.win
        if (w && w.openDialog)
            w.openDialog(name, p)
    }
    function can(name) {
        const w = frame.win
        return !!(w && w.hasDialog && w.hasDialog(name))
    }
    function pickColor(key, title) {
        openOther("ColorPicker", { color: Prefs.values[key], title: title, picked: c => Prefs.set(key, String(c)) })
    }
    // An option of a select row. Nền quanh ảnh's last, Tùy chọn…, opens the Color Picker first, as Photoshop's Select
    // Custom Color…: OK keeps the colour in <key>Color and picks the option, Hủy leaves both.
    function pickOption(r, i) {
        const custom = r.key + "Color"
        if (i === r.options.length - 1 && custom in Prefs.defaults)
            openOther("ColorPicker", { color: Prefs.values[custom], title: r.label, picked: c => { Prefs.set(custom, String(c)); Prefs.set(r.key, i) } })
        else
            Prefs.set(r.key, i)
    }
    function act(r) {  // a button of a pane
        if (r.kind === "model") {
            Prefs.set(r.key, !Prefs.values[r.key])
        } else if (r.kind === "link") {  // the prototype's link leaves Thiết lập for the other
            close()
            openOther(r.target, {})
        } else if (r.action === "copyId") {
            clip.text = r.label.split(" ").pop()  // the label ends with the ID
            clip.selectAll()
            clip.copy()
            copied = true
        }
        // "license": the prototype's Nhập mã kích hoạt does nothing yet.
    }
    function toggleDisk(i) {
        Prefs.set("scratchDisks", Prefs.values.scratchDisks.map((d, j) => j === i ? { name: d.name, on: !d.on } : d))
    }
    function moveDisk(i, by) {
        const o = Prefs.values.scratchDisks.slice(), d = o[i]
        o[i] = o[i + by]
        o[i + by] = d
        Prefs.set("scratchDisks", o)
    }
    function dec(x) { return Fmt.vn(x, 2).replace(/,?0+$/, "") }  // 1,5 and 1,25 and 2, as the prototype's dec()
    function gbs(gb) { return gb >= 1000 ? Fmt.vn(gb / 1000, 1) + " TB" : Fmt.vn(gb, 0) + " GB" }

    title: qsTr("Thiết lập")
    boxWidth: 1000
    implicitHeight: 700
    padding: 1  // inside the border
    header: null
    footer: null
    onPageChanged: scroll.contentY = 0

    component Cap: Text {  // .cap
        Layout.fillWidth: true
        color: Theme.t3
        font.pointSize: Theme.pt(10.5)
        font.weight: Font.Medium
        font.letterSpacing: 0.63  // .06em
        font.capitalization: Font.AllUppercase
    }
    component Line: RowLayout {  // .rw: a label in 150 px, then what it sets
        property alias label: lbl.text
        Layout.fillWidth: true
        spacing: 14
        Text { id: lbl; Layout.preferredWidth: 150; color: Theme.t2; font.pointSize: Theme.pt(12) }
    }
    component Budget: Line {  // the value and a note over a slider of Prefs' key
        id: b
        property string key
        property alias shown: val.text
        property alias side: note.text
        property alias min: slider.min
        property alias max: slider.max
        property alias step: slider.step
        property alias decimals: slider.decimals
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 7
            RowLayout {
                Layout.fillWidth: true
                Text { id: val; color: Theme.t1; font.family: Theme.mono; font.pointSize: Theme.pt(12) }
                Item { Layout.fillWidth: true }
                Text { id: note; color: Theme.t3; font.pointSize: Theme.pt(12) }
            }
            SliderField {
                id: slider
                Layout.fillWidth: true
                value: Prefs.values[b.key]
                Accessible.name: b.label
                onMoved: v => Prefs.set(b.key, v)
            }
        }
    }
    component Meter: RowLayout {  // what is in use of a budget
        id: m
        property alias name: n.text
        property real percent
        property alias shown: v.text
        Layout.fillWidth: true
        spacing: 10
        Text { id: n; Layout.preferredWidth: 70; color: Theme.t3; font.pointSize: Theme.pt(12) }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 6
            radius: 3
            color: Theme.trk
            Rectangle { width: parent.width * m.percent / 100; height: 6; topLeftRadius: 3; bottomLeftRadius: 3; color: Theme.accent }
        }
        Text {
            id: v
            Layout.preferredWidth: 120
            horizontalAlignment: Text.AlignRight
            color: Theme.t1
            font.family: Theme.mono
            font.pointSize: Theme.pt(11)
        }
    }

    contentItem: Item {
        id: frame
        readonly property var win: Window.window  // Main: openDialog(), hasDialog()
        TextEdit { id: clip; visible: false }

        Column {  // nav
            id: nav
            width: 249
            topPadding: 14
            Text {
                leftPadding: 16
                topPadding: 2
                bottomPadding: 12
                text: qsTr("Thiết lập")
                color: Theme.t1
                font.pointSize: Theme.pt(15)
                font.weight: Font.DemiBold
            }
            Repeater {
                model: Prefs.sections
                AbstractButton {
                    id: sec
                    required property var modelData
                    readonly property bool on: st.page === modelData[0]
                    width: nav.width
                    height: 32
                    leftPadding: 16
                    rightPadding: 16
                    text: modelData[1]
                    Accessible.role: Accessible.PageTab
                    Accessible.selected: on
                    onClicked: st.page = modelData[0]
                    background: Rectangle { color: sec.on ? Theme.sel : "transparent" }
                    contentItem: Row {
                        spacing: 10
                        Icon { anchors.verticalCenter: parent.verticalCenter; name: sec.modelData[2]; size: 17; color: Theme.t3 }
                        Text { anchors.verticalCenter: parent.verticalCenter; text: sec.text; color: sec.on ? Theme.t1 : Theme.t2; font.pointSize: Theme.pt(12) }
                    }
                }
            }
        }
        Rectangle { x: nav.width; width: 1; height: parent.height; color: Theme.ln }

        Item {
            x: nav.width + 1
            width: parent.width - x
            height: parent.height
            Text {
                x: 22
                width: parent.width - 72
                height: 52
                verticalAlignment: Text.AlignVCenter
                text: (Prefs.sections.find(s => s[0] === st.page) ?? ["", ""])[1]
                color: Theme.t1
                font.pointSize: Theme.pt(15)
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            IconButton {
                x: parent.width - 10 - width
                y: 11
                implicitWidth: 30
                implicitHeight: 30
                icon.name: "close"
                icon.width: 18
                ToolTip.text: qsTr("Đóng")
                Accessible.name: qsTr("Đóng thiết lập")
                onClicked: st.close()
            }
            Flickable {
                id: scroll
                y: 52
                width: parent.width
                height: parent.height - y
                contentHeight: body.implicitHeight + 26
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                acceptedButtons: Qt.NoButton  // wheel and scroll bar; sliders keep their drags
                ScrollBar.vertical: ScrollBar {}
                Column {
                    id: body
                    x: 22
                    y: 4
                    width: scroll.width - 44

                    ColumnLayout {  // Hiệu năng
                        id: perf
                        readonly property var p: Prefs.values
                        readonly property var sys: Prefs.system
                        readonly property real ram: sys.ramGB * p.ramBudget / 100  // the budget in GB
                        readonly property real free: p.scratchDisks.reduce((a, d) => a + (d.on ? sys.disks[d.name].free : 0), 0)
                        visible: st.page === "performance"
                        width: parent.width
                        spacing: 14
                        Cap { text: qsTr("Bộ nhớ") }
                        Budget {
                            label: qsTr("Ngân sách RAM")
                            key: "ramBudget"
                            min: 10
                            max: 90
                            step: 5
                            shown: qsTr("%1% · %2 GB").arg(perf.p.ramBudget).arg(Fmt.vn(perf.ram, 1))
                            side: qsTr("RAM máy %1 GB").arg(perf.sys.ramGB)
                        }
                        Budget {
                            label: qsTr("Ngân sách VRAM")
                            key: "vramBudget"
                            min: 0.5
                            max: 3
                            step: 0.25
                            decimals: 2
                            shown: qsTr("%1 GB").arg(st.dec(perf.p.vramBudget))
                            side: qsTr("GPU tích hợp dùng chung RAM")
                        }
                        Line {
                            label: qsTr("Đang dùng")
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                Meter {
                                    name: qsTr("RAM")
                                    percent: Math.min(100, Math.round(perf.sys.ramUsed / perf.ram * 100))
                                    shown: qsTr("%1 / %2 GB").arg(Fmt.vn(perf.sys.ramUsed, 1)).arg(Fmt.vn(perf.ram, 1))
                                }
                                Meter {
                                    name: qsTr("VRAM")
                                    percent: Math.min(100, Math.round(perf.sys.vramUsed / perf.p.vramBudget * 100))
                                    shown: qsTr("%1 / %2 GB").arg(Fmt.vn(perf.sys.vramUsed, 1)).arg(st.dec(perf.p.vramBudget))
                                }
                                Meter {
                                    name: qsTr("Scratch")
                                    percent: Math.max(1, Math.round(perf.sys.scratchUsed / perf.p.scratchBudget * 100))
                                    shown: qsTr("%1 / %2 GB").arg(Fmt.vn(perf.sys.scratchUsed, 1)).arg(Fmt.vn(perf.p.scratchBudget, 0))
                                }
                            }
                        }
                        Cap { Layout.topMargin: 6; text: qsTr("GPU") }
                        Line {
                            label: qsTr("Dùng GPU")
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 12
                                Toggle { topPadding: 0; bottomPadding: 0; checked: perf.p.gpu; Accessible.name: qsTr("Dùng GPU"); onClicked: Prefs.set("gpu", !perf.p.gpu) }
                                Text { text: perf.sys.gpuName; color: Theme.t1; font.pointSize: Theme.pt(12) }
                                Text { text: perf.sys.gpuApi; color: Theme.t3; font.family: Theme.mono; font.pointSize: Theme.pt(11) }
                                Item { Layout.fillWidth: true }
                                Btn { implicitHeight: 26; text: qsTr("Kiểm tra lại") }  // nothing to look for on the mock (N2)
                            }
                        }
                        Cap { Layout.topMargin: 6; text: qsTr("Ổ scratch · dùng theo thứ tự") }
                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: disks.implicitHeight
                            radius: 6
                            color: "transparent"
                            border.color: Theme.ln
                            Column {
                                id: disks
                                width: parent.width
                                Repeater {
                                    model: perf.p.scratchDisks.length  // rows by place: a click keeps its row and focus
                                    Item {
                                        id: disk
                                        required property int index
                                        readonly property var d: perf.p.scratchDisks[index]
                                        readonly property var info: perf.sys.disks[d.name]
                                        width: disks.width
                                        height: 38
                                        RowLayout {
                                            x: 10
                                            width: parent.width - 16
                                            height: 37
                                            spacing: 10
                                            CheckRow {
                                                Layout.preferredWidth: 13
                                                checked: disk.d.on
                                                Accessible.name: qsTr("Dùng ổ %1").arg(disk.d.name)
                                                onClicked: st.toggleDisk(disk.index)
                                            }
                                            Text {  // its place among the disks in use
                                                Layout.preferredWidth: 22
                                                text: disk.d.on ? perf.p.scratchDisks.slice(0, disk.index + 1).filter(x => x.on).length : ""
                                                color: Theme.t3
                                                font.family: Theme.mono
                                                font.pointSize: Theme.pt(11)
                                            }
                                            Row {
                                                Layout.fillWidth: true
                                                Text { text: disk.d.name; color: Theme.t1; font.pointSize: Theme.pt(12) }
                                                Text { text: " · " + disk.info.kind; color: Theme.t3; font.pointSize: Theme.pt(12) }
                                            }
                                            Text {
                                                text: qsTr("trống %1").arg(st.gbs(disk.info.free))
                                                color: Theme.t2
                                                font.family: Theme.mono
                                                font.pointSize: Theme.pt(11)
                                            }
                                            Row {
                                                Layout.leftMargin: 6
                                                spacing: 2
                                                IconButton {
                                                    implicitWidth: 24
                                                    implicitHeight: 24
                                                    icon.name: "expand_less"
                                                    icon.width: 16
                                                    enabled: disk.index > 0
                                                    ToolTip.text: qsTr("Lên")
                                                    Accessible.name: qsTr("Đưa ổ %1 lên").arg(disk.d.name)
                                                    onClicked: st.moveDisk(disk.index, -1)
                                                }
                                                IconButton {
                                                    implicitWidth: 24
                                                    implicitHeight: 24
                                                    icon.name: "expand_more"
                                                    icon.width: 16
                                                    enabled: disk.index < perf.p.scratchDisks.length - 1
                                                    ToolTip.text: qsTr("Xuống")
                                                    Accessible.name: qsTr("Đưa ổ %1 xuống").arg(disk.d.name)
                                                    onClicked: st.moveDisk(disk.index, 1)
                                                }
                                            }
                                        }
                                        Rectangle { y: 37; width: parent.width; height: 1; color: Theme.p2 }
                                    }
                                }
                            }
                        }
                        Budget {
                            label: qsTr("Ngân sách scratch")
                            key: "scratchBudget"
                            min: 20
                            max: 500
                            step: 10
                            shown: qsTr("%1 GB").arg(Fmt.vn(perf.p.scratchBudget, 0))
                            side: perf.free ? qsTr("trống %1 trên ổ đã chọn").arg(st.gbs(perf.free)) : qsTr("chưa chọn ổ nào")
                        }
                        Cap { Layout.topMargin: 6; text: qsTr("Cache") }
                        Line {
                            label: qsTr("Cache Levels")
                            Segmented {
                                Layout.preferredWidth: 240
                                implicitHeight: 26
                                radius: 6
                                fill: true
                                options: ["1", "2", "3", "4", "5", "6", "7", "8"]
                                current: perf.p.cacheLevels - 1
                                onPicked: i => Prefs.set("cacheLevels", i + 1)
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Vượt ngân sách thì ô nguội được nén rồi đẩy ra ổ scratch; tài liệu lớn hơn RAM vẫn mở và vẽ được. Hệ điều hành báo thiếu bộ nhớ thì ngân sách tự co.")
                            color: Theme.t3
                            font.pointSize: Theme.pt(11.5)
                            wrapMode: Text.Wrap
                        }
                    }

                    Repeater {  // the other sections
                        model: Prefs.panes[st.page] ?? []
                        Item {  // .prw: a row of Prefs.panes, by its kind
                            id: row
                            required property var modelData
                            readonly property var r: modelData
                            readonly property var v: Prefs.values[r.key]
                            readonly property bool isNote: r.kind === "note"
                            width: body.width
                            height: isNote ? noteText.implicitHeight + 20 : Math.max(38, Math.max(label.implicitHeight, ctl.height) + 13)
                            Text {
                                id: label
                                visible: !row.isNote
                                y: (row.height - 1 - height) / 2
                                width: row.width - ctl.width - 16
                                text: row.r.kind === "model" ? qsTr("%1 · %2 · %3").arg(row.r.label).arg(row.r.size).arg(row.v ? qsTr("đã tải") : qsTr("chưa tải"))
                                                             : row.r.label
                                color: Theme.t2
                                font.pointSize: Theme.pt(12)
                                wrapMode: Text.Wrap
                            }
                            Text {
                                id: noteText
                                visible: row.isNote
                                y: 10
                                width: row.width
                                text: row.r.label
                                color: Theme.t3
                                font.pointSize: Theme.pt(11.5)
                                wrapMode: Text.Wrap
                            }
                            Loader {
                                id: ctl
                                anchors.right: parent.right
                                y: (row.height - 1 - height) / 2
                                sourceComponent: ({ toggle: toggleC, select: selectC, group: groupC, range: rangeC, colors: colorsC, accent: accentC,
                                                    button: buttonC, link: buttonC, model: buttonC })[row.r.kind] ?? null
                            }
                            Rectangle { visible: !row.isNote; y: row.height - 1; width: row.width; height: 1; color: Theme.p2 }

                            Component {
                                id: toggleC
                                Toggle { topPadding: 0; bottomPadding: 0; checked: !!row.v; Accessible.name: row.r.label; onClicked: Prefs.set(row.r.key, !row.v) }
                            }
                            Component {
                                id: selectC
                                Select {  // as wide as its value, 150 or more; the list as wide as its longest option, at its right
                                    id: sel
                                    implicitWidth: Math.max(150, Math.ceil(fm.advanceWidth(displayText)) + leftPadding + rightPadding)
                                    implicitHeight: 26
                                    popup.x: width - popup.width
                                    options: row.r.options.map((o, i) => [i, o])
                                    value: row.v
                                    Accessible.name: row.r.label
                                    onPicked: i => st.pickOption(row.r, i)
                                    FontMetrics { id: fm; font: sel.font }
                                }
                            }
                            Component {
                                id: groupC
                                Segmented {
                                    implicitHeight: 26
                                    radius: 6
                                    options: row.r.options
                                    current: row.v
                                    onPicked: i => Prefs.set(row.r.key, i)
                                }
                            }
                            Component {
                                id: rangeC
                                Row {
                                    spacing: 8
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: Fmt.vn(row.v, 0) + row.r.unit
                                        color: Theme.t1
                                        font.family: Theme.mono
                                        font.pointSize: Theme.pt(11.5)
                                    }
                                    SliderField {
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 170
                                        min: row.r.min
                                        max: row.r.max
                                        step: row.r.step
                                        value: row.v
                                        Accessible.name: row.r.label
                                        onMoved: v => Prefs.set(row.r.key, v)
                                    }
                                }
                            }
                            Component {
                                id: colorsC
                                Row {
                                    spacing: 8
                                    Repeater {
                                        model: row.r.keys
                                        Swatch {
                                            id: well
                                            required property string modelData
                                            required property int index
                                            readonly property string name: row.r.keys.length > 1 ? row.r.label + " " + (index + 1) : row.r.label
                                            implicitWidth: 30
                                            implicitHeight: 22
                                            radius: 4
                                            ring: Theme.t4
                                            color: Prefs.values[modelData]
                                            enabled: st.can("ColorPicker")
                                            opacity: enabled ? 1 : 0.35
                                            ToolTip.text: qsTr("Chọn màu")
                                            Accessible.name: name + ": " + Prefs.values[modelData]
                                            onClicked: st.pickColor(modelData, name)
                                        }
                                    }
                                }
                            }
                            Component {
                                id: accentC
                                Row {
                                    spacing: 8
                                    Repeater {
                                        model: row.r.options
                                        AbstractButton {
                                            id: acc
                                            required property var modelData
                                            readonly property bool on: String(row.v).toUpperCase() === modelData[0].toUpperCase()
                                            implicitWidth: 22
                                            implicitHeight: 22
                                            Accessible.role: Accessible.RadioButton
                                            Accessible.name: modelData[1] + " " + modelData[0]
                                            Accessible.checked: on
                                            onClicked: Prefs.set(row.r.key, modelData[0])
                                            background: Rectangle {
                                                radius: 11
                                                color: acc.modelData[0]
                                                Rectangle {  // 2 px of the dialog, then 2 of the colour
                                                    visible: acc.on
                                                    anchors.centerIn: parent
                                                    width: 30
                                                    height: 30
                                                    radius: 15
                                                    color: "transparent"
                                                    border.width: 2
                                                    border.color: acc.modelData[0]
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                            Component {
                                id: buttonC
                                Btn {
                                    implicitHeight: 26
                                    text: row.r.kind === "model" ? (row.v ? qsTr("Xóa") : qsTr("Tải")) : row.r.action === "copyId" && st.copied ? qsTr("Đã chép") : row.r.text
                                    enabled: row.r.kind !== "link" || st.can(row.r.target)
                                    onClicked: st.act(row.r)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
