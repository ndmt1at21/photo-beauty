import QtQuick
import QtQuick.Controls.Basic
import "Fmt.js" as Fmt

// Help › Thông tin hệ thống… (docs/ui-tasks.md, U25; build/design2/project/HelpDialogs.dc.html, the second box): the app,
// the machine and the settings a bug report needs, in a read-only block that scrolls; Sao chép puts it on the clipboard.
// What QML knows comes from Qt, Screen, Theme and Prefs (the RAM budget, scratch disks, the mock machine of
// Prefs.system); the rest is the artboard's. params { title, lead, info }: another title, lines above the system's, and
// info false leaves those out (Gửi báo lỗi's Xem nội dung sẽ gửi shows its report here).
DialogBox {
    id: dlg
    property var params: ({})
    property bool copied: false  // the block is on the clipboard
    readonly property string machine: {
        const p = Prefs.values, s = Prefs.system, r = log.Screen.devicePixelRatio
        const row = (label, v) => (label + "           ").slice(0, 11) + v  // the label in 11 columns
        const gb = x => x >= 1000 ? Fmt.vn(x / 1000, 1) + " TB" : Fmt.vn(x, 0) + " GB"
        const os = ({ windows: "Windows", osx: "macOS", linux: "Linux" })[Qt.platform.os] || Qt.platform.os
        return ["photo-beauty " + Qt.application.version + ", Qt 6.11.3",
            os + qsTr(" [phiên bản]"),
            row("CPU", qsTr("[tên CPU], [số luồng] luồng")),
            row("RAM", qsTr("%1 GB; ngân sách %2 GB (%3%)").arg(s.ramGB).arg(Fmt.vn(s.ramGB * p.ramBudget / 100, 1).replace(/,0$/, "")).arg(p.ramBudget)),
            row("GPU", s.gpuName),
            row("", "driver 101.7084, " + s.gpuApi),
            row("", "present: FIFO, IMMEDIATE"),
            row(qsTr("Màn hình"), qsTr("%1 × %2, 60 Hz, tỉ lệ %3%").arg(Math.round(log.Screen.width * r)).arg(Math.round(log.Screen.height * r)).arg(Math.round(r * 100))),
            row("Scratch", p.scratchDisks.filter(d => d.on).map(d => qsTr("%1 còn %2 trống").arg(d.name).arg(gb(s.disks[d.name].free))).join(", ")),
            row("Font", [Theme.sans, Theme.mono, Theme.logo].join(", ")),
            row(qsTr("Nhật ký"), "%LOCALAPPDATA%\\photo-beauty\\logs")].join("\n")
    }

    title: params.title || qsTr("Thông tin hệ thống")
    boxWidth: 520
    closable: true
    buttons: [{ text: qsTr("Đóng"), primary: true, act: () => dlg.close() }]

    function copy() {
        clip.text = log.text
        clip.selectAll()
        clip.copy()
        copied = true
    }

    TextEdit { id: clip; visible: false }
    Rectangle {  // .log: 280 high, scrolling both ways
        width: parent.width
        height: 280
        radius: 6
        color: Theme.p2
        Flickable {
            anchors.fill: parent
            contentWidth: log.implicitWidth + 24
            contentHeight: log.implicitHeight + 20
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            ScrollBar.horizontal: ScrollBar {}
            Text {
                id: log
                x: 12
                y: 10
                text: [dlg.params.lead || "", dlg.params.info === false ? "" : dlg.machine].filter(t => t !== "").join("\n\n")
                textFormat: Text.PlainText
                color: Theme.t2
                font.family: Theme.mono
                font.pointSize: Theme.pt(11.5)
                lineHeightMode: Text.FixedHeight  // 11.5 px × 1.55
                lineHeight: Math.round(17.8 * Theme.fontScale)
            }
        }
    }
    Btn {  // on the footer's left, as the artboard's margin-right:auto
        parent: dlg.footer
        x: 16
        y: 13
        icon.name: "content_copy"
        text: dlg.copied ? qsTr("Đã chép") : qsTr("Sao chép")
        onClicked: dlg.copy()
    }
}
