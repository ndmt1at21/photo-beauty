import QtQuick

// Help › Kiểm tra cập nhật… (docs/ui-tasks.md, U25; build/design2/project/HelpDialogs.dc.html, the last two boxes): the
// mock checks and finds this version the latest; params { state: "available", version, notes } is the other state (a
// newer version, its release notes, [Để sau][Tải về]; --state Update:available). Tải về only closes it: the mock has
// nothing to fetch.
DialogBox {
    id: dlg
    property var params: ({})
    readonly property bool available: params.state === "available"

    title: qsTr("Kiểm tra cập nhật")
    boxWidth: 400
    closable: true
    buttons: available ? [{ text: qsTr("Để sau"), act: () => dlg.close() }, { text: qsTr("Tải về"), primary: true, act: () => dlg.close() }]
                       : [{ text: qsTr("Đóng"), primary: true, act: () => dlg.close() }]

    Row {
        spacing: 12
        Icon {
            width: 28
            height: 28
            name: dlg.available ? "system_update" : "check"
            size: 28
            color: Theme.accent
        }
        Column {
            width: dlg.width - 76
            spacing: dlg.available ? 6 : 4
            Text {
                width: parent.width
                height: Math.round(16.2 * Theme.fontScale)  // 12 px × 1.35
                verticalAlignment: Text.AlignVCenter
                text: dlg.available ? qsTr("Có bản mới: %1").arg(dlg.params.version || qsTr("[phiên bản mới]")) : qsTr("Bạn đang dùng bản mới nhất")
                color: Theme.t1
                font.pointSize: Theme.pt(12)
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
            Text {  // this version, when it was checked
                visible: !dlg.available
                height: Math.round(15.5 * Theme.fontScale)  // 11.5 px × 1.35
                verticalAlignment: Text.AlignVCenter
                text: [Qt.application.version, qsTr("kiểm tra lúc %1").arg(Qt.formatTime(new Date(), "HH:mm"))].filter(s => s !== "").join(" · ")
                color: Theme.t3
                font.family: Theme.mono
                font.pointSize: Theme.pt(11.5)
            }
            Text {  // the release notes
                visible: dlg.available
                width: parent.width
                text: dlg.params.notes || qsTr("[Ghi chú phát hành: hai, ba dòng về điều mới.]")
                color: Theme.t2
                font.pointSize: Theme.pt(12)
                lineHeightMode: Text.FixedHeight  // 12 px × 1.35, whole pixels
                lineHeight: Math.round(16.2 * Theme.fontScale)
                wrapMode: Text.Wrap
            }
            Text {
                visible: dlg.available
                width: parent.width
                height: Math.round(14.9 * Theme.fontScale)  // 11 px × 1.35
                verticalAlignment: Text.AlignVCenter
                text: qsTr("Tải ở nền; cài khi bạn đóng app.")
                color: Theme.t3
                font.pointSize: Theme.pt(11)
            }
        }
    }
}
