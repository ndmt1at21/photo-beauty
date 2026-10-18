pragma ComponentBehavior: Bound
import QtQuick

// Khôi phục (docs/ui-tasks.md, U20; build/proto/Notices.dc.html, the fifth box): the documents left unsaved when the app
// last stopped (Docs.recovery). Mở opens one (Docs.recover) and closes the box, Bỏ drops one (Docs.dropRecovery), Mở tất
// cả opens them all; Để sau, Esc keep them for later. params { time }: when the app stopped, the artboard's by default.
DialogBox {
    id: dlg
    property var params: ({})
    readonly property var files: Docs.recovery

    title: qsTr("Khôi phục tài liệu chưa lưu")
    icon: "history"
    iconColor: Theme.warn
    buttons: files.length ? [{ text: qsTr("Để sau"), act: () => dlg.close() }, { text: qsTr("Mở tất cả"), primary: true, act: () => dlg.recoverAll() }]
                          : [{ text: qsTr("Đóng"), act: () => dlg.close() }]
    // A row's buttons change Docs.recovery once their click is through: the rows are made again from it.
    function recoverAt(i) {
        Qt.callLater(Docs.recover, i)
        close()
    }
    function recoverAll() {
        while (Docs.recovery.length)
            Docs.recover(0)
        close()
    }

    Column {
        width: parent.width
        spacing: 10
        Text {
            width: parent.width
            text: qsTr("photo-beauty đã tắt đột ngột lúc %1. Bản tự lưu còn nguyên:").arg(dlg.params.time || "14:41")
            color: Theme.t2
            font.pointSize: Theme.pt(12)
            lineHeightMode: Text.FixedHeight  // Notices' .pb line-height:1.4, whole pixels
            lineHeight: Math.round(16.8 * Theme.fontScale)
            wrapMode: Text.Wrap
        }
        Column {
            id: rows
            width: parent.width
            Repeater {
                model: dlg.files
                delegate: Item {  // .lr: the file and when it was saved, Mở, Bỏ
                    id: rr
                    required property var modelData  // { file, time }
                    required property int index
                    width: rows.width
                    height: info.implicitHeight + 17
                    Rectangle { width: parent.width; height: 1; color: Theme.p2 }
                    Column {
                        id: info
                        y: 9
                        width: openBtn.x - 10
                        Text {
                            width: parent.width
                            text: rr.modelData.file
                            color: Theme.t1
                            font.pointSize: Theme.pt(12)
                            lineHeightMode: Text.FixedHeight  // Notices' .pb line-height:1.4, whole pixels
                            lineHeight: Math.round(16.8 * Theme.fontScale)
                            elide: Text.ElideRight
                        }
                        Text {
                            width: parent.width
                            text: rr.modelData.time
                            color: Theme.t3
                            font.family: Theme.mono
                            font.pointSize: Theme.pt(10.5)
                            lineHeightMode: Text.FixedHeight  // a line of the row's 12 px × 1.4, as its span in the artboard
                            lineHeight: Math.round(16.8 * Theme.fontScale)
                            elide: Text.ElideRight
                        }
                    }
                    Btn {
                        id: openBtn
                        x: dropBtn.x - 10 - width
                        y: (rr.height + 1 - height) / 2
                        implicitHeight: 26
                        primary: true
                        text: qsTr("Mở")
                        Accessible.name: qsTr("Mở %1").arg(rr.modelData.file)
                        onClicked: dlg.recoverAt(rr.index)
                    }
                    Btn {
                        id: dropBtn
                        x: parent.width - width
                        y: openBtn.y
                        implicitHeight: 26
                        text: qsTr("Bỏ")
                        Accessible.name: qsTr("Bỏ %1").arg(rr.modelData.file)
                        onClicked: Qt.callLater(Docs.dropRecovery, rr.index)
                    }
                }
            }
            Item {  // the rows dropped
                visible: !dlg.files.length
                width: parent.width
                height: 34
                Rectangle { width: parent.width; height: 1; color: Theme.p2 }
                Text {
                    y: 9
                    text: qsTr("Đã bỏ hết bản tự lưu.")
                    color: Theme.t3
                    font.pointSize: Theme.pt(12)
                    lineHeightMode: Text.FixedHeight  // Notices' .pb line-height:1.4, whole pixels
                    lineHeight: Math.round(16.8 * Theme.fontScale)
                }
            }
        }
    }
}
