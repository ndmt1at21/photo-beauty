import QtQuick

// Help › Về photo-beauty (docs/ui-tasks.md, U25; build/design2/project/HelpDialogs.dc.html, the first box): the app's icon,
// the wordmark, the version (main.cpp sets CMake's PB_VERSION), what it does, the copyright. Giấy phép… opens Thiết lập on
// Báo lỗi, cập nhật, giấy phép; Đóng, Esc, × close it.
DialogBox {
    id: dlg
    property var params: ({})

    title: qsTr("Về photo-beauty")
    boxWidth: 420
    closable: true
    buttons: [{ text: qsTr("Đóng"), primary: true, act: () => dlg.close() }]

    function licence() {
        const w = col.win
        close()
        w?.openDialog("Settings", { page: "about" })
    }

    Column {  // .body centred: 28 above, 24 below and at the sides
        id: col
        readonly property Main win: Window.window as Main  // the window: a Popup has none of its own
        width: parent.width
        topPadding: 12  // DialogBox's 16 and these
        bottomPadding: 8
        spacing: 10
        Image {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 72
            height: 72
            source: "qrc:/qt/qml/PhotoBeauty/brand/icon.png"  // its corners round already
            sourceSize: Qt.size(144, 144)
            smooth: true
            mipmap: true
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            height: 32  // 24 px × 1.35
            verticalAlignment: Text.AlignVCenter
            text: "photo-beauty"
            color: Theme.t1
            font.family: Theme.logo
            font.weight: Font.ExtraBold
            font.pointSize: Theme.pt(24)
            font.letterSpacing: -0.24  // -.01em
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            height: Math.round(15.5 * Theme.fontScale)  // 11.5 px × 1.35
            verticalAlignment: Text.AlignVCenter
            text: qsTr("Phiên bản %1").arg(Qt.application.version)
            color: Theme.t3
            font.family: Theme.mono
            font.pointSize: Theme.pt(11.5)
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            height: Math.round(16.2 * Theme.fontScale)  // 12 px × 1.35
            verticalAlignment: Text.AlignVCenter
            text: qsTr("Chỉnh ảnh, retouch chân dung trên GPU.")
            color: Theme.t2
            font.pointSize: Theme.pt(12)
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            width: dlg.width - 48
            text: qsTr("© 2026 photo-beauty. Dùng Qt, Vulkan, Slang và các thư viện mã nguồn mở khác.")
            color: Theme.t3
            font.pointSize: Theme.pt(11)
            lineHeightMode: Text.FixedHeight  // 11 px × 1.35, whole pixels
            lineHeight: Math.round(15 * Theme.fontScale)
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
        }
    }
    Btn {  // on the footer's left, as the artboard's margin-right:auto
        parent: dlg.footer
        x: 16
        y: 13
        text: qsTr("Giấy phép…")
        onClicked: dlg.licence()
    }
}
