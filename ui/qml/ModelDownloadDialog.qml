pragma ComponentBehavior: Bound
import QtQuick
import "Fmt.js" as Fmt

// Tải model AI (docs/ui-tasks.md, U20; build/proto/Notices.dc.html, the sixth box and the two after it): a feature
// that needs a model not downloaded yet, its name, size and licence; Tải fetches it, Hủy, Esc, × leave it. params
// { model (a Prefs key of Thiết lập › AI, "modelBiRefNet"), feature ("Select Subject"), license ("MIT"), fail }: the
// model's name and size come from Prefs.panes.ai. The mock downloads with a Timer, its progress in the box (Docs.job
// has saves and exports only); done, Prefs holds the model as downloaded and the box closes. fail ("network", "sha")
// makes the first try fail, at 3/8 or at the end: Thử lại tries again, Đóng leaves it.
DialogBox {
    id: dlg
    property var params: ({})
    readonly property string key: params.model || "modelBiRefNet"
    readonly property var model: Prefs.panes.ai.find(r => r.key === key) || ({ label: key, size: "" })
    readonly property real mb: Fmt.pnum(model.size) || 0
    readonly property string feature: params.feature || "Select Subject"
    property string phase: "ask"  // "load", "fail"
    property real got: 0  // MB downloaded
    property string failing: params.fail || ""  // the try that fails next: "network", "sha", or none
    property string failed: ""  // why the last try failed

    title: phase !== "fail" ? qsTr("%1 cần một model").arg(feature) : failed === "sha" ? qsTr("Tải không xong: sai SHA-256") : qsTr("Tải không xong: mất kết nối")
    icon: phase === "fail" ? "error" : "download"
    iconColor: phase === "fail" ? Theme.danger : Theme.t2
    closable: phase !== "fail"
    buttons: phase === "ask" ? [{ text: qsTr("Hủy"), act: () => dlg.close() }, { text: qsTr("Tải %1").arg(model.size), primary: true, act: () => dlg.start() }]
           : phase === "load" ? [{ text: qsTr("Hủy"), act: () => dlg.close() }]
           : [{ text: qsTr("Đóng"), act: () => dlg.close() }, { text: qsTr("Thử lại"), primary: true, act: () => dlg.start() }]

    function start() {
        got = 0
        phase = "load"
    }
    function step() {  // one tick of the mock download; a lost connection stops it at 3/8
        const end = failing === "network" ? mb * 3 / 8 : mb
        got = Math.min(end, got + mb / 17)
        if (got < end)
            return
        if (failing) {
            failed = failing
            failing = ""
            phase = "fail"
        } else {
            Prefs.set(key, true)
            close()
        }
    }
    function megabytes(x) { return Fmt.vn(x, mb < 10 ? 1 : 0) }

    Timer {
        interval: 100
        repeat: true
        running: dlg.phase === "load" && dlg.opened  // not while it fades out
        onTriggered: dlg.step()
    }
    Column {
        width: parent.width
        spacing: 10
        Grid {
            visible: dlg.phase !== "fail"
            columns: 2
            columnSpacing: 10
            rowSpacing: 4
            Repeater {
                model: [qsTr("Model"), dlg.model.label, qsTr("Dung lượng"), dlg.model.size, qsTr("Giấy phép"), dlg.params.license || "MIT"]
                delegate: Text {
                    required property string modelData
                    required property int index
                    width: index % 2 ? dlg.width - 36 - 120 : 110
                    text: modelData
                    color: index % 2 ? Theme.t1 : Theme.t3
                    font.family: index === 3 ? Theme.mono : Theme.sans
                    font.pointSize: Theme.pt(11.5)
                    lineHeightMode: Text.FixedHeight  // Notices' .pb line-height:1.4, whole pixels
                    lineHeight: Math.round(16.1 * Theme.fontScale)
                    elide: Text.ElideRight
                }
            }
        }
        Text {
            width: parent.width
            text: dlg.phase !== "fail" ? qsTr("Tải một lần, chạy trên máy: ảnh không rời máy. Tải chạy nền, tiến độ ở thanh trên. Xóa được ở Thiết lập › AI.")
                : dlg.failed === "sha" ? qsTr("Tệp tải về sai SHA-256, đã xóa. Thử lại sẽ tải lại %1 (%2) từ đầu.").arg(dlg.model.label).arg(dlg.model.size)
                : qsTr("%1 dừng ở %2 / %3. %4 dùng được khi tải xong; kiểm tra mạng rồi thử lại.").arg(dlg.model.label).arg(dlg.megabytes(dlg.got)).arg(dlg.model.size).arg(dlg.feature)
            color: Theme.t2
            font.pointSize: Theme.pt(12)
            lineHeightMode: Text.FixedHeight  // Notices' .pb line-height:1.4, whole pixels
            lineHeight: Math.round(16.8 * Theme.fontScale)
            wrapMode: Text.Wrap
        }
        Item {  // the download going: a bar and how far it is
            visible: dlg.phase === "load"
            width: parent.width
            height: 16
            Rectangle {
                id: track
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - done.width - 10
                height: 3
                radius: 2
                color: Theme.trk
                Rectangle { width: dlg.mb ? track.width * dlg.got / dlg.mb : 0; height: 3; radius: 2; color: Theme.accent }
            }
            Text {
                id: done
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: dlg.megabytes(dlg.got) + " / " + dlg.model.size
                color: Theme.t3
                font.family: Theme.mono
                font.pointSize: Theme.pt(11)
            }
        }
    }
}
