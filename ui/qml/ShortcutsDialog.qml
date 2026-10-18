pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import "MenuSpec.js" as MenuSpec
import "ToolSpec.js" as ToolSpec

// Phím tắt (docs/ui-tasks.md, U20; build/proto/Shortcuts.dc.html; Edit › Phím tắt…, Help, Thiết lập's link): three
// tabs read from where the keys run, not retyped: Menu ứng dụng, the menus' keys Main registers (MenuSpec.keyed());
// Menu panel, each panel's … menu (its `menu`, read from the panel made for a moment); Công cụ, ToolSpec.KEYS, the
// toolbar's. The search folds case and Vietnamese marks ("luu" finds Lưu) over a row's name, its English name and its
// keys (⇧ as "shift"); groups flow in two columns. Shows the keys only: changing them is P2.
DialogBox {
    id: dlg
    property var params: ({})
    property int tab: 0  // 0 Menu ứng dụng, 1 Menu panel, 2 Công cụ
    property alias query: search.text
    // Each tab's [[group, rows]], a row { n, en, keys (as shown, one per cap: "Ctrl", "⇧", "Z"), find (what the search
    // reads, folded) }: made once open, as the panels' menus are read from panels made for the moment.
    property var lists: [[], [], []]
    readonly property var shown: {
        const q = fold(query.trim())
        return lists[tab].map(g => [g[0], g[1].filter(r => !q || r.find.indexOf(q) >= 0)]).filter(g => g[1].length)
    }
    // The groups in two columns as CSS columns balance them: the split whose taller column is shortest, the first
    // column as full as that allows. A group is its caption, 28 high, and rows 30 high.
    readonly property var columns: {
        const h = shown.map(g => 28 + 30 * g[1].length), total = h.reduce((a, b) => a + b, 0)
        let at = 0, best = Infinity, sum = 0
        for (let k = 0; k <= h.length; k++) {
            if (Math.max(sum, total - sum) <= best) {
                best = Math.max(sum, total - sum)
                at = k
            }
            sum += h[k] || 0
        }
        return [shown.slice(0, at), shown.slice(at)]
    }
    onShownChanged: list.contentY = 0

    title: qsTr("Phím tắt")
    boxWidth: 860
    implicitHeight: 600
    closable: true
    Component.onCompleted: lists = [appKeys(), panelKeys(), toolKeys()]
    onOpened: search.forceActiveFocus()

    // Case, Vietnamese marks and đ folded, ⇧ as "shift", as the prototype's search.
    function fold(s) { return String(s).toLowerCase().normalize("NFD").replace(/[\u0300-\u036f]/g, "").replace(/đ/g, "d").replace(/⇧/g, "shift") }
    function row(n, label, en, k) {
        const e = en || english[String(label).replace(/…$/, "")] || ""
        const keys = k ? String(k).split(" ") : []
        return { n: n, en: e, keys: keys, find: fold(n + " " + e + " " + keys.join(" ")) }
    }
    // The menu bar's, by menu; an item in a submenu named after it ("Mới › Layer…").
    function appKeys() {
        const out = []
        for (const k of MenuSpec.keyed()) {
            let g = out.find(x => x[0] === k.path[0])
            if (!g)
                out.push(g = [k.path[0], []])
            g[1].push(row(k.path.slice(1).join(" › "), k.item.l, k.item.en, k.item.k))
        }
        return out
    }
    // The panels' menus, keys or none, as the artboard lists them: its Layers, History, Channels, Brushes first, then the
    // others in the Window menu's order (its "@win:<panel>"). A panel is made in a hidden item (a parent for its
    // bindings), read, and destroyed before any event reaches it.
    function panelKeys() {
        const out = []
        for (const m of MenuSpec.MENUS)
            for (const it of m.items) {
                if (MenuSpec.name(it.act) !== "win")
                    continue
                const c = Qt.createComponent("PhotoBeauty", MenuSpec.arg(it.act).replace(/ /g, "") + "Panel", Component.PreferSynchronous)
                const p = c.status === Component.Ready ? c.createObject(holder) : null
                if (!p)
                    continue
                const rows = menuOf(p).filter(x => x.l).map(x => row(x.l, x.l, x.en, x.k))
                p.destroy()
                if (rows.length)
                    out.push([it.l, rows])
            }
        const first = ["Layers", "History", "Channels", "Brushes"]
        return first.map(n => out.filter(g => g[0] === n)).reduce((a, b) => a.concat(b), []).concat(out.filter(g => first.indexOf(g[0]) < 0))
    }
    function menuOf(panel) { return panel.menu || [] }  // untyped: any panel's
    function toolKeys() { return ToolSpec.KEYS.map(g => [g[0], g[1].map(r => row(r.l, r.l, r.en, r.k))]) }
    // Photoshop's English names of the menus' Vietnamese labels (… dropped): the artboard's, then those it has no row for.
    readonly property var english: ({
        "Tạo mới": "New", "Mở": "Open", "Lưu": "Save", "Lưu thành": "Save As", "Xuất thành": "Export As", "Đóng": "Close",
        "Hoàn tác": "Undo", "Làm lại": "Redo", "Thiết lập": "Preferences", "Layer mới": "New Layer", "Nhóm layer": "Group",
        "Gộp xuống": "Merge Down", "Bỏ chọn": "Deselect", "Đảo vùng chọn": "Inverse", "Nhân bản layer": "Duplicate Layer",
        "Xóa layer": "Delete Layer", "Gộp layer đang hiện": "Merge Visible", "Làm phẳng ảnh": "Flatten Image",
        "Bước tới": "Step Forward", "Bước lùi": "Step Backward", "Snapshot mới": "New Snapshot", "Xóa lịch sử": "Clear History",
        "Kênh mới": "New Channel", "Nhân bản kênh": "Duplicate Channel", "Xóa kênh": "Delete Channel",
        "Brush preset mới": "New Brush Preset", "Đổi tên brush": "Rename Brush", "Xóa brush": "Delete Brush",
        "Đóng tất cả": "Close All", "Lưu bản sao": "Save a Copy", "Hoàn nguyên": "Revert", "Thoát": "Exit",
        "Đổi qua lại bước cuối": "Toggle Last State", "Cắt": "Cut", "Sao chép": "Copy", "Sao chép hợp nhất": "Copy Merged",
        "Dán": "Paste", "Dán tại chỗ": "Paste in Place", "Xóa": "Clear", "Lặp lại": "Again", "Phím tắt": "Keyboard Shortcuts",
        "Layer": "New Layer", "Sao chép sang layer mới": "Layer via Copy", "Cắt sang layer mới": "Layer via Cut",
        "Tạo Clipping Mask": "Create Clipping Mask", "Bỏ nhóm": "Ungroup Layers", "Ẩn layer": "Hide Layers",
        "Đưa lên trên cùng": "Bring to Front", "Đưa lên một bậc": "Bring Forward", "Đưa xuống một bậc": "Send Backward",
        "Đưa xuống dưới cùng": "Send to Back", "Khóa layer": "Lock Layers", "Chọn tất cả": "All", "Chọn lại": "Reselect",
        "Mọi layer": "All Layers", "Sửa trong Quick Mask": "Edit in Quick Mask Mode", "Phóng to": "Zoom In", "Thu nhỏ": "Zoom Out",
        "Vừa màn hình": "Fit on Screen", "Guide": "Guides", "Lưới": "Grid", "Thước": "Rulers", "Bắt dính": "Snap",
        "Khóa guide": "Lock Guides", "Hướng dẫn photo-beauty": "Help", "Lưu vùng chọn thành kênh": "Save Selection as Channel",
        "Nạp kênh làm vùng chọn": "Load Channel as Selection", "Nhóm mới": "New Group", "Xóa brush preset": "Delete Brush",
        "Lưu thành Brush preset mới": "New Brush Preset", "Đặt lại Character": "Reset Character", "Đặt lại Paragraph": "Reset Paragraph",
        "Thêm màu trước vào Swatches": "Add Foreground Color to Swatches", "Swatch mới": "New Swatch", "Xóa swatch": "Delete Swatch",
        "Khôi phục swatch mặc định": "Restore Default Swatches", "Gradient mới": "New Gradient", "Xóa gradient": "Delete Gradient",
        "Khôi phục gradient mặc định": "Restore Default Gradients"
    })

    contentItem: Item {
        Item { id: holder; visible: false; width: 308; height: 600 }  // the panels made for a moment, laid out as in the dock
        Item {  // the tabs and the search box, under the title
            id: bar
            width: parent.width
            height: 53
            Segmented {
                x: 18
                y: 12
                height: 28
                options: [qsTr("Menu ứng dụng"), qsTr("Menu panel"), qsTr("Công cụ")]
                current: dlg.tab
                onPicked: i => dlg.tab = i
            }
            TextBox {
                id: search
                x: parent.width - 18 - width
                y: 12
                width: 260
                leftPadding: 30
                placeholderText: qsTr("Tìm lệnh hay phím, tên tiếng Anh cũng được")
                Accessible.name: qsTr("Tìm phím tắt")
                Icon { x: 8; anchors.verticalCenter: parent.verticalCenter; name: "search"; size: 16; color: Theme.t3 }
            }
            Rectangle { y: 52; width: parent.width; height: 1; color: Theme.ln }
        }
        Flickable {
            id: list
            y: bar.height
            width: parent.width
            height: parent.height - y
            contentHeight: Math.max(left.height, right.height) + 20
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            acceptedButtons: Qt.NoButton  // wheel and scroll bar
            ScrollBar.vertical: ScrollBar {}
            Column {
                id: left
                x: 18
                y: 8
                width: (list.width - 36 - 28) / 2
                Repeater { model: dlg.columns[0]; delegate: groupC }
            }
            Column {
                id: right
                x: left.x + left.width + 28
                y: 8
                width: left.width
                Repeater { model: dlg.columns[1]; delegate: groupC }
            }
            Text {
                visible: dlg.shown.length === 0
                x: 18
                y: 22
                width: list.width - 36
                text: qsTr("Không có lệnh nào khớp “%1”.").arg(dlg.query)
                color: Theme.t3
                font.pointSize: Theme.pt(12)
                elide: Text.ElideRight
            }
        }
    }
    footer: Item {
        implicitHeight: 52
        Rectangle { width: parent.width; height: 1; color: Theme.ln }
        Text {
            x: 16
            anchors.verticalCenter: parent.verticalCenter
            width: done.x - x - 8
            text: qsTr("Phím mặc định như Photoshop.")
            color: Theme.t3
            font.pointSize: Theme.pt(11)
            elide: Text.ElideRight
        }
        Btn {
            id: done
            x: parent.width - 16 - width
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Đóng")
            onClicked: dlg.close()
        }
    }

    // A group: its caption, then its rows: the name, its English name after it in t3, the keys in caps at the right.
    Component {
        id: groupC
        Column {
            id: grp
            required property var modelData  // [name, rows]
            width: parent ? parent.width : 0
            Item {
                width: grp.width
                height: 28
                FillDialog.Cap { y: 10; text: grp.modelData[0] }
            }
            Repeater {
                model: grp.modelData[1]
                delegate: Item {
                    id: r
                    required property var modelData
                    width: grp.width
                    height: 30
                    Rectangle { y: 29; width: parent.width; height: 1; color: Theme.p2 }
                    Text {
                        id: name
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.min(implicitWidth, caps.x - 10)
                        text: r.modelData.n
                        color: Theme.t1
                        font.pointSize: Theme.pt(12)
                        elide: Text.ElideRight
                    }
                    Text {
                        x: name.width + 4
                        anchors.baseline: name.baseline
                        width: Math.max(0, caps.x - 10 - x)
                        visible: r.modelData.en !== "" && width > 0
                        text: r.modelData.en
                        color: Theme.t3
                        font.pointSize: Theme.pt(11)
                        elide: Text.ElideRight
                    }
                    Row {
                        id: caps
                        x: parent.width - width
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 3
                        Repeater {
                            model: r.modelData.keys
                            delegate: Rectangle {  // .kb: a key cap, its inset shadow a line at its foot
                                id: cap
                                required property string modelData
                                width: Math.max(22, label.implicitWidth + 12)
                                height: 20
                                radius: 4
                                color: Theme.p2
                                Rectangle { x: 3; y: 19; width: cap.width - 6; height: 1; color: Theme.ln }
                                Text {
                                    id: label
                                    anchors.centerIn: parent
                                    text: cap.modelData
                                    color: Theme.t1
                                    font.family: Theme.mono
                                    font.pointSize: Theme.pt(11)
                                    font.weight: Font.Medium
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
