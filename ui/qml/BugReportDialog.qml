import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic

// Help › Gửi báo lỗi… (docs/ui-tasks.md, U25; build/design2/project/HelpDialogs.dc.html, the third box): what happened,
// an email to ask back on (not required), what goes with it (the log of the last 7 days and the system's information on,
// a shot of the window off). Xem nội dung sẽ gửi shows the report in Thông tin hệ thống's box; Gửi, off while nothing is
// described, closes it with a toast that it went (the mock sends nothing). Hủy, Esc, × close it.
DialogBox {
    id: dlg
    property var params: ({})
    property bool attachLog: true
    property bool attachInfo: true
    property bool attachShot: false
    readonly property bool described: what.text.trim() !== ""  // a bool of its own: the buttons' model changes as it flips only

    title: qsTr("Gửi báo lỗi")
    boxWidth: 480
    closable: true
    buttons: [{ text: qsTr("Hủy"), act: () => dlg.close() },
        { text: qsTr("Gửi"), primary: true, disabled: !dlg.described, act: () => dlg.send() }]

    // The report as it would go: what happened, the email, what goes with it.
    function report() {
        const with_ = [[attachLog, qsTr("nhật ký 7 ngày gần nhất")], [attachInfo, qsTr("thông tin hệ thống")],
            [attachShot, qsTr("ảnh chụp cửa sổ")]].filter(a => a[0]).map(a => a[1])
        return [qsTr("Chuyện gì đã xảy ra?"), what.text.trim() || "—", "", qsTr("Email: %1").arg(mail.text.trim() || "—"),
            qsTr("Kèm theo: %1").arg(with_.join(", ") || "—")].join("\n")
    }
    function preview() {
        form.win?.openDialog("SystemInfo", { title: qsTr("Nội dung sẽ gửi"), lead: report(), info: attachInfo })
    }
    // The toast is made from Main's component, in its context: one declared here would die with the dialog.
    function send() {
        const w = form.win
        const t = w?.type("Toast")?.createObject(w.contentItem, { text: qsTr("Đã gửi báo lỗi, cảm ơn bạn"), ok: true })
        if (t) {
            t.done.connect(() => t.destroy())
            t.restart()
        }
        close()
    }

    component Label: Text {  // .rl
        color: Theme.t2
        font.pointSize: Theme.pt(12)
        Layout.preferredHeight: Math.round(16.2 * Theme.fontScale)  // 12 px × 1.35
    }

    ColumnLayout {
        id: form
        readonly property Main win: Window.window as Main  // the window: a Popup has none of its own
        width: parent.width
        spacing: 12
        Label { text: qsTr("Chuyện gì đã xảy ra?") }
        ScrollView {  // .ta: 110 high, the text scrolls inside
            Layout.fillWidth: true
            Layout.preferredHeight: 110
            TextArea {
                id: what
                padding: 8
                wrapMode: TextEdit.Wrap
                placeholderText: qsTr("Bạn đang làm gì, mong đợi gì, thấy gì…")
                placeholderTextColor: Theme.t4
                color: Theme.t1
                selectionColor: Theme.accent
                selectedTextColor: Theme.acct
                selectByMouse: true
                font.pointSize: Theme.pt(12)
                Accessible.name: qsTr("Chuyện gì đã xảy ra?")
                background: Rectangle {
                    radius: 5
                    color: Theme.p2
                    border.color: what.activeFocus ? Theme.t3 : "transparent"
                }
                // Tab goes on to the next control, as in the browser, instead of typing a tab.
                Keys.onTabPressed: what.nextItemInFocusChain(true)?.forceActiveFocus(Qt.TabFocusReason)
                Keys.onBacktabPressed: what.nextItemInFocusChain(false)?.forceActiveFocus(Qt.BacktabFocusReason)
            }
        }
        Label { text: qsTr("Email để chúng tôi hỏi lại (không bắt buộc)") }
        TextBox {
            id: mail
            Layout.fillWidth: true
            placeholderText: "ban@vidu.com"
            inputMethodHints: Qt.ImhEmailCharactersOnly
            Accessible.name: qsTr("Email")
        }
        ColumnLayout {
            Layout.topMargin: 2
            spacing: 8
            CheckRow {
                spacing: 8  // .chk
                text: qsTr("Kèm nhật ký 7 ngày gần nhất")
                checked: dlg.attachLog
                onClicked: dlg.attachLog = !dlg.attachLog
            }
            CheckRow {
                spacing: 8
                text: qsTr("Kèm thông tin hệ thống")
                checked: dlg.attachInfo
                onClicked: dlg.attachInfo = !dlg.attachInfo
            }
            CheckRow {
                spacing: 8
                text: qsTr("Kèm ảnh chụp cửa sổ")
                checked: dlg.attachShot
                onClicked: dlg.attachShot = !dlg.attachShot
            }
        }
        Text {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.round(14.9 * Theme.fontScale)  // 11 px × 1.35
            text: qsTr("Không gửi ảnh hay tài liệu bạn đang mở.")
            color: Theme.t3
            font.pointSize: Theme.pt(11)
            wrapMode: Text.Wrap
        }
    }
    Link {  // on the footer's left, as the artboard's margin-right:auto
        parent: dlg.footer
        x: 16
        y: 19
        text: qsTr("Xem nội dung sẽ gửi")
        onClicked: dlg.preview()
    }
}
