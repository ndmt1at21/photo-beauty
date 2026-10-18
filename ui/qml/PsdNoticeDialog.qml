pragma ComponentBehavior: Bound
import QtQuick

// Báo cáo PSD (docs/ui-tasks.md, U20; build/proto/Notices.dc.html, the third box): a PSD opened with parts photo-beauty
// cannot redo yet, by layer; they show as the pixels saved and are kept as they are when saved back to PSD. Mock data
// (decision 3): params { file, items: [{ layer, what }] }, the artboard's by default. Chép danh sách puts the list on the
// clipboard.
DialogBox {
    id: dlg
    property var params: ({})
    readonly property string file: params.file || "poster-tet.psd"
    readonly property var items: params.items || [
        { layer: "Tiêu đề (layer chữ)", what: "Bevel & Emboss: chưa vẽ lại được, dùng điểm ảnh đã lưu." },
        { layer: "Ưu đãi 30% (layer chữ)", what: "Bevel & Emboss: chưa vẽ lại được, dùng điểm ảnh đã lưu." },
        { layer: "Logo khách (smart object)", what: "Smart filter Gaussian Blur: chưa sửa được, giữ nguyên khi lưu." }]
    property bool copied: false  // the list is on the clipboard

    title: qsTr("PSD: %1 layer có phần chưa sửa được", "", items.length).arg(items.length)
    icon: "info"
    closable: true
    function copy() {
        clip.text = [file].concat(items.map(i => i.layer + ": " + i.what)).join("\n")
        clip.selectAll()
        clip.copy()
        copied = true
    }

    Column {
        width: parent.width
        spacing: 10
        TextEdit { id: clip; visible: false }
        Text {
            width: parent.width
            text: qsTr("%1 mở được đủ layer, mask, adjustment layer. Phần dưới hiện bằng điểm ảnh đã lưu và giữ nguyên khi lưu lại PSD.")
                  .arg("<font color=\"" + Theme.t1 + "\">" + dlg.file.replace(/&/g, "&amp;").replace(/</g, "&lt;") + "</font>")
            textFormat: Text.StyledText
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
                model: dlg.items
                delegate: Item {  // .lr: the layer, what of it
                    id: pr
                    required property var modelData
                    width: rows.width
                    height: info.implicitHeight + 17
                    Rectangle { width: parent.width; height: 1; color: Theme.p2 }
                    Column {
                        id: info
                        y: 9
                        width: parent.width
                        Text {
                            width: parent.width
                            text: pr.modelData.layer
                            color: Theme.t1
                            font.pointSize: Theme.pt(12)
                            lineHeightMode: Text.FixedHeight  // Notices' .pb line-height:1.4, whole pixels
                            lineHeight: Math.round(16.8 * Theme.fontScale)
                            wrapMode: Text.Wrap
                        }
                        Text {
                            width: parent.width
                            text: pr.modelData.what
                            color: Theme.t3
                            font.pointSize: Theme.pt(11)
                            lineHeightMode: Text.FixedHeight  // a line of the row's 12 px × 1.4, as its span in the artboard
                            lineHeight: Math.round(16.8 * Theme.fontScale)
                            wrapMode: Text.Wrap
                        }
                    }
                }
            }
        }
    }
    // Its own buttons, so the one pressed keeps the focus as its text turns to Đã chép.
    footer: Item {
        implicitHeight: 52
        Rectangle { width: parent.width; height: 1; color: Theme.ln }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            Btn {
                text: dlg.copied ? qsTr("Đã chép") : qsTr("Chép danh sách")
                onClicked: dlg.copy()
            }
            Btn {
                width: Math.max(84, implicitWidth)
                primary: true
                text: qsTr("Đóng")
                onClicked: dlg.close()
            }
        }
    }
}
