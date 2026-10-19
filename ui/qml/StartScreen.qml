pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Shapes
import QtQuick.Controls.Basic
import "NewDocSpec.js" as Spec
import "Mock.js" as Mock
import "Fmt.js" as Fmt
import "MenuSpec.js" as MenuSpec

// Màn hình bắt đầu (build/proto/Start.dc.html), under the top bar while no document is open: Tạo mới…, Mở…, where to
// drop files; the files to recover; a row of presets that each make a document at once (Tùy chỉnh… opens New); Gần
// đây, found without diacritics, sorted by date, name or size, as a grid or a list, each file with a menu (also on a
// right click). Enter opens the file Tab reached, Delete takes it off the list.
Rectangle {
    id: screen
    property alias query: search.text
    property int sort: 0  // Ngày sửa, Tên, Dung lượng
    property bool list: false  // the list instead of the grid
    property string menuFile  // whose menu is open
    property string copied  // whose path was copied last
    readonly property var shown: Spec.recentShown(Docs.recent, query, sort)
    color: Theme.bg

    function newDoc() {  // as File › New; a test has no Main and opens it here
        const main = Window.window as Main
        if (main) {
            main.openDialog("NewDoc", {})
            return
        }
        const dlg = newDocC.createObject(screen) as NewDocDialog
        dlg.closed.connect(dlg.destroy)
        dlg.open()
    }
    function openFile() { (Window.window as Main)?.act(MenuSpec.MENUS[0].items.find((i) => i.act === "@openFile")) }  // File › Mở…
    function create(p) { Docs.create({ name: Spec.untitled(Docs.docs), w: p.w, h: p.h, ppi: p.ppi }) }
    function dims(r) { return Fmt.vn(r.w) + " × " + Fmt.vn(r.h) + " px · " + r.size }
    // The list changes once the menu has closed: a card may go with it (Docs.open hides this screen).
    function showMenu(r, item, x, y) {
        menuFile = r.file
        menu.show([{ l: qsTr("Mở"), k: "Enter", act: () => Qt.callLater(Docs.open, r.file) },
            { l: qsTr("Mở thư mục chứa file") },  // ponytail: a mock file has no folder; N1 opens the real one
            { l: copied === r.file ? qsTr("Đã sao chép đường dẫn") : qsTr("Sao chép đường dẫn"), act: () => screen.copyPath(r.file) },
            { t: "sep" },
            { l: qsTr("Bỏ khỏi danh sách"), k: "Del", act: () => Qt.callLater(Docs.forgetRecent, r.file) }], item, x, y)
    }
    function copyPath(file) {
        clip.text = file
        clip.selectAll()
        clip.copy()
        copied = file
    }

    Component { id: newDocC; NewDocDialog {} }
    PopupMenu { id: menu; plain: true; onClosed: screen.menuFile = "" }
    TextEdit { id: clip; visible: false }

    // Tạo mới…, Mở… of the left column.
    component Big: AbstractButton {
        id: big
        property bool primary
        property string keys
        Layout.fillWidth: true
        implicitHeight: 36
        leftPadding: 12
        rightPadding: 12
        font.pointSize: Theme.pt(12)
        font.weight: primary ? Font.DemiBold : Font.Normal
        background: Rectangle { radius: 6; color: big.primary ? Theme.accent : Theme.p2 }
        contentItem: RowLayout {
            spacing: 8
            Icon { name: big.icon.name; size: 18; color: big.primary ? Theme.acct : Theme.t2 }
            Text { Layout.fillWidth: true; text: big.text; font: big.font; color: big.primary ? Theme.acct : Theme.t1 }
            Text { text: big.keys; font.family: Theme.mono; font.pointSize: Theme.pt(11); color: big.primary ? Theme.acct : Theme.t3 }
        }
    }
    component Heading: Text {
        color: Theme.t1
        font.pointSize: Theme.pt(15)
        font.weight: Font.DemiBold
    }
    component Small: Text {
        color: Theme.t3
        font.pointSize: Theme.pt(11)
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: Math.min(260, 240 + Math.max(0, screen.width - 800) / 1000)  // flex 1 1 240px beside the page's 999 1 560px
            color: Theme.p1
            Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.ln }
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                anchors.rightMargin: 17
                anchors.topMargin: 20
                anchors.bottomMargin: 20
                spacing: 8
                Big { primary: true; icon.name: "add"; text: qsTr("Tạo mới…"); keys: "Ctrl N"; onClicked: screen.newDoc() }
                Big { icon.name: "folder_open"; text: qsTr("Mở…"); keys: "Ctrl O"; onClicked: screen.openFile() }
                Item { Layout.fillHeight: true; Layout.minimumHeight: 24 }
                Item {  // a drop anywhere on the window opens (Main)
                    Layout.fillWidth: true
                    implicitHeight: drop.implicitHeight + 46
                    Shape {
                        anchors.fill: parent
                        ShapePath {  // the prototype's 1.5px dashed as Chrome draws it at DPR 1: 1 px, dashes 3, gaps 2
                            strokeColor: Theme.t4
                            strokeWidth: 1
                            strokeStyle: ShapePath.DashLine
                            dashPattern: [3, 2]
                            fillColor: "transparent"
                            PathRectangle { x: 0.5; y: 0.5; width: drop.width + 33; height: drop.height + 45; radius: 10 }
                        }
                    }
                    ColumnLayout {
                        id: drop
                        x: 17
                        y: 23
                        width: parent.width - 34
                        spacing: 8
                        Icon { Layout.alignment: Qt.AlignHCenter; Layout.preferredHeight: 28; name: "upload_file"; size: 28; color: Theme.t2 }
                        Text {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                            text: qsTr("Thả file vào đây để mở")
                            color: Theme.t2
                            font.pointSize: Theme.pt(12.5)
                        }
                        Small {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                            text: qsTr("Thả nhiều file thì mỗi file mở một tab. JPEG, PNG, TIFF, WebP, HEIC, RAW, PSD, PSB, .pbd")
                        }
                    }
                }
            }
        }
        Flickable {
            id: page
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: body.height + 76
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            acceptedButtons: Qt.NoButton  // the wheel and the scroll bar; a drag is the cards'
            ScrollBar.vertical: ScrollBar {}
            Column {
                id: body
                x: Math.max(0, (page.width - 1140) / 2) + 40
                y: 28
                width: Math.min(page.width, 1140) - 80
                spacing: 28

                Rectangle {  // Khôi phục
                    visible: Docs.recovery.length > 0
                    width: body.width
                    height: rec.implicitHeight + 30
                    radius: 10
                    color: Theme.p1
                    border.color: Theme.ln
                    RowLayout {  // 14 px and 16 px inside the 1 px border
                        id: rec
                        x: 17
                        y: 15
                        width: parent.width - 34
                        spacing: 14
                        Icon { Layout.alignment: Qt.AlignTop; name: "history"; size: 22; color: Theme.warn }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Flow {
                                Layout.fillWidth: true
                                spacing: 10
                                Text {
                                    text: qsTr("Khôi phục tài liệu chưa lưu")
                                    color: Theme.t1
                                    font.pointSize: Theme.pt(13)
                                    font.weight: Font.DemiBold
                                }
                                Text {  // ponytail: the crash time is the mock's; N2 knows the real one
                                    text: qsTr("photo-beauty đã tắt đột ngột lúc %1. Bản tự lưu còn nguyên.").arg("14:41")
                                    color: Theme.t3
                                    font.pointSize: Theme.pt(12)
                                }
                            }
                            Repeater {
                                model: Docs.recovery
                                Item {
                                    id: lost
                                    required property var modelData
                                    required property int index
                                    Layout.fillWidth: true
                                    implicitHeight: 39
                                    Rectangle { width: parent.width; height: 1; color: Theme.ln }
                                    RowLayout {
                                        y: 7
                                        width: parent.width
                                        spacing: 10
                                        Text {
                                            Layout.fillWidth: true
                                            text: lost.modelData.file
                                            color: Theme.t1
                                            font.pointSize: Theme.pt(12)
                                            elide: Text.ElideRight
                                        }
                                        Small { text: lost.modelData.time; font.family: Theme.mono }
                                        Btn {
                                            primary: true
                                            implicitHeight: 26
                                            leftPadding: 12
                                            rightPadding: 12
                                            text: qsTr("Mở")
                                            Accessible.name: qsTr("Mở %1").arg(lost.modelData.file)
                                            onClicked: Qt.callLater(Docs.recover, lost.index)
                                        }
                                        Btn {
                                            id: dropBtn
                                            implicitHeight: 26
                                            leftPadding: 12
                                            rightPadding: 12
                                            text: qsTr("Bỏ")
                                            Accessible.name: qsTr("Bỏ %1").arg(lost.modelData.file)
                                            onClicked: Qt.callLater(Docs.dropRecovery, lost.index)
                                            background: Rectangle { radius: 6; color: dropBtn.hovered ? Theme.sel : "transparent"; border.color: Theme.ln }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                Column {  // Tạo mới
                    width: body.width
                    spacing: 12
                    RowLayout {
                        width: parent.width
                        Heading { Layout.fillWidth: true; Layout.alignment: Qt.AlignBaseline; text: qsTr("Tạo mới") }
                        Link {  // the prototype's a: 26 tall, its text in the middle, on the heading's baseline
                            Layout.alignment: Qt.AlignBaseline
                            implicitHeight: 26
                            topPadding: (26 - implicitContentHeight) / 2
                            leftPadding: 8
                            rightPadding: 8
                            text: qsTr("Mọi preset…")
                            ink: Theme.t2
                            onClicked: screen.newDoc()
                        }
                    }
                    GridLayout {  // a CSS grid: auto-fill columns of 150 px or more, cards of a row as tall as its tallest
                        id: presets
                        readonly property int cols: Math.max(1, Math.floor((width + 10) / 160))
                        width: parent.width
                        columns: cols
                        uniformCellWidths: true
                        rowSpacing: 10
                        columnSpacing: 10
                        Repeater {
                            model: Spec.START.concat([{ name: qsTr("Tùy chỉnh…"), tip: qsTr("Tạo mới (Ctrl N)"), custom: true }])
                            AbstractButton {
                                id: preset
                                required property var modelData
                                readonly property bool custom: !!modelData.custom
                                readonly property var shape: custom ? ({ w: 0, h: 0 }) : Spec.box(modelData.w, modelData.h, 64, 52)
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1  // the columns share the width
                                Layout.fillHeight: true
                                padding: 10
                                hoverEnabled: true
                                text: modelData.name
                                font.pointSize: Theme.pt(12.5)
                                ToolTip.visible: hovered
                                ToolTip.delay: 600
                                ToolTip.text: modelData.tip
                                onClicked: custom ? screen.newDoc() : screen.create(modelData)
                                background: Rectangle { radius: 8; color: Theme.p1; border.color: Theme.ln }
                                contentItem: Column {
                                    spacing: 4
                                    Item {
                                        width: parent.width
                                        height: 74
                                        Rectangle {
                                            width: parent.width
                                            height: 70
                                            radius: 6
                                            color: Theme.p2
                                            Icon { visible: preset.custom; anchors.centerIn: parent; name: "add"; size: 24; color: Theme.t2 }
                                            Rectangle {
                                                visible: !preset.custom
                                                anchors.centerIn: parent
                                                width: preset.shape.w + 2
                                                height: preset.shape.h + 2
                                                radius: 3
                                                color: Theme.bg
                                                border.color: Theme.t3
                                            }
                                        }
                                    }
                                    Text {
                                        width: parent.width
                                        text: preset.text
                                        font: preset.font
                                        color: Theme.t1
                                        wrapMode: Text.Wrap
                                    }
                                    Small {
                                        width: parent.width
                                        text: preset.custom ? qsTr("Mở hộp thoại New") : Fmt.vn(preset.modelData.w) + " × " + Fmt.vn(preset.modelData.h) + " px"
                                        font.family: Theme.mono
                                        wrapMode: Text.Wrap
                                    }
                                }
                            }
                        }
                    }
                }

                Column {  // Gần đây
                    width: body.width
                    spacing: 12
                    RowLayout {
                        width: parent.width
                        spacing: 10
                        Heading { Layout.fillWidth: true; text: qsTr("Gần đây") }
                        TextBox {
                            id: search
                            Layout.preferredWidth: 240
                            leftPadding: 30
                            placeholderText: qsTr("Tìm tài liệu gần đây")
                            Accessible.name: placeholderText
                            Icon { x: 8; anchors.verticalCenter: parent.verticalCenter; name: "search"; size: 16; color: Theme.t3 }
                        }
                        Dropdown {
                            Layout.preferredWidth: 170 * Theme.fontScale
                            implicitHeight: 28
                            plain: true
                            prefix: qsTr("Sắp xếp: ")
                            options: [[0, qsTr("Ngày sửa")], [1, qsTr("Tên")], [2, qsTr("Dung lượng")]]
                            value: screen.sort
                            onPicked: v => screen.sort = v
                        }
                        Rectangle {  // Kiểu xem
                            implicitWidth: 58
                            implicitHeight: 28
                            radius: 6
                            color: Theme.p2
                            Row {
                                x: 2
                                y: 2
                                spacing: 2
                                ToolToggle {
                                    icon.name: "grid_view"
                                    icon.width: 16
                                    checked: !screen.list
                                    ToolTip.text: qsTr("Xem dạng lưới")
                                    onClicked: screen.list = false
                                }
                                ToolToggle {
                                    icon.name: "view_list"
                                    icon.width: 16
                                    checked: screen.list
                                    ToolTip.text: qsTr("Xem dạng danh sách")
                                    onClicked: screen.list = true
                                }
                            }
                        }
                    }
                    Text {
                        visible: screen.shown.length === 0
                        width: parent.width
                        topPadding: 24
                        bottomPadding: 24
                        horizontalAlignment: Text.AlignHCenter
                        text: screen.query.trim() ? qsTr("Không có tài liệu nào khớp “%1”.").arg(screen.query) : qsTr("Chưa có tài liệu nào mở gần đây.")
                        color: Theme.t3
                        font.pointSize: Theme.pt(12)
                    }
                    GridLayout {  // auto-fill columns of 196 px or more
                        id: cards
                        readonly property int cols: Math.max(1, Math.floor((width + 12) / 208))
                        visible: !screen.list
                        width: parent.width
                        columns: cols
                        uniformCellWidths: true
                        rowSpacing: 12
                        columnSpacing: 12
                        Repeater {
                            model: screen.list ? [] : screen.shown
                            AbstractButton {
                                id: card
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1
                                Layout.fillHeight: true
                                padding: 8
                                hoverEnabled: true
                                Accessible.name: qsTr("Mở %1").arg(modelData.file)
                                onClicked: Qt.callLater(Docs.open, modelData.file)
                                Keys.onDeletePressed: Qt.callLater(Docs.forgetRecent, modelData.file)
                                TapHandler { acceptedButtons: Qt.RightButton; onTapped: screen.showMenu(card.modelData) }
                                background: Rectangle {
                                    radius: 10
                                    color: card.hovered ? Theme.p1 : "transparent"
                                    border.color: screen.menuFile === card.modelData.file ? Theme.accent : "transparent"
                                }
                                contentItem: Column {
                                    spacing: 8
                                    Rectangle {
                                        width: parent.width
                                        height: 148
                                        radius: 6
                                        color: Theme.p2
                                        Image {
                                            anchors.fill: parent
                                            fillMode: Image.PreserveAspectFit
                                            source: Mock.photoUrl(card.modelData.photo)
                                            sourceSize.height: 296
                                        }
                                    }
                                    RowLayout {
                                        x: 2
                                        width: parent.width - 4
                                        spacing: 4
                                        Column {
                                            Layout.fillWidth: true
                                            Layout.bottomMargin: 2
                                            spacing: 2
                                            Text {
                                                width: parent.width
                                                text: card.modelData.file
                                                color: Theme.t1
                                                font.pointSize: Theme.pt(12.5)
                                                elide: Text.ElideRight
                                            }
                                            Small { width: parent.width; text: screen.dims(card.modelData); font.family: Theme.mono; wrapMode: Text.Wrap }
                                            Small { width: parent.width; text: card.modelData.when; wrapMode: Text.Wrap }
                                        }
                                        IconButton {
                                            Layout.alignment: Qt.AlignTop
                                            implicitWidth: 24
                                            implicitHeight: 24
                                            icon.name: "more_horiz"
                                            icon.width: 16
                                            ToolTip.text: qsTr("Tùy chọn (hay chuột phải)")
                                            onClicked: screen.showMenu(card.modelData, card, card.width * 0.4, 96)
                                        }
                                    }
                                }
                            }
                        }
                    }
                    Rectangle {
                        visible: screen.list && screen.shown.length > 0
                        width: parent.width
                        height: rows.height
                        radius: 10
                        color: Theme.p1
                        border.color: Theme.ln
                        Column {
                            id: rows
                            width: parent.width
                            Repeater {
                                model: screen.list ? screen.shown : []
                                AbstractButton {
                                    id: row
                                    required property var modelData
                                    required property int index
                                    width: rows.width
                                    implicitHeight: 45
                                    leftPadding: 10
                                    rightPadding: 10
                                    Accessible.name: qsTr("Mở %1").arg(modelData.file)
                                    onClicked: Qt.callLater(Docs.open, modelData.file)
                                    Keys.onDeletePressed: Qt.callLater(Docs.forgetRecent, modelData.file)
                                    TapHandler { acceptedButtons: Qt.RightButton; onTapped: screen.showMenu(row.modelData) }
                                    background: Item {
                                        Rectangle { visible: row.index < screen.shown.length - 1; anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.ln }
                                    }
                                    contentItem: RowLayout {
                                        spacing: 12
                                        Image {
                                            Layout.preferredWidth: 44
                                            Layout.preferredHeight: 32
                                            fillMode: Image.PreserveAspectCrop
                                            source: Mock.photoUrl(row.modelData.photo)
                                            sourceSize.height: 64
                                            Rectangle {  // the corners rounded to 4 in the row's colour: no shader clips offscreen
                                                anchors.fill: parent
                                                anchors.margins: -2
                                                radius: 6
                                                color: "transparent"
                                                border.width: 2
                                                border.color: Theme.p1
                                            }
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            text: row.modelData.file
                                            color: Theme.t1
                                            font.pointSize: Theme.pt(12.5)
                                            elide: Text.ElideRight
                                        }
                                        Small { Layout.preferredWidth: 240; text: screen.dims(row.modelData); font.family: Theme.mono; elide: Text.ElideRight }
                                        Small { Layout.preferredWidth: 120; text: row.modelData.when; elide: Text.ElideRight }
                                        IconButton {
                                            implicitWidth: 24
                                            implicitHeight: 24
                                            icon.name: "more_horiz"
                                            icon.width: 16
                                            ToolTip.text: qsTr("Tùy chọn (hay chuột phải)")
                                            onClicked: screen.showMenu(row.modelData, row, row.width - 246, 40)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
