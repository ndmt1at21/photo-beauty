pragma ComponentBehavior: Bound
import QtQuick

// Hồ sơ màu khác (docs/ui-tasks.md, U20; build/proto/Notices.dc.html, the first box): a file opened whose embedded
// profile is not the working space's: keep it, convert to the working space, or drop it. Mock data (decision 3 of
// docs/ui-tasks.md): params { file, profile, working, picked(choice) }, the artboard's by default; OK gives picked()
// "keep", "convert" or "discard"; Hủy, Esc leave it.
DialogBox {
    id: dlg
    property var params: ({})
    readonly property string file: params.file || "cuoi-trang-an.psd"
    property string choice: "keep"
    readonly property var choices: [["keep", qsTr("Dùng hồ sơ nhúng")], ["convert", qsTr("Chuyển màu sang không gian làm việc")],
        ["discard", qsTr("Bỏ hồ sơ, không quản lý màu")]]

    title: qsTr("Hồ sơ màu khác không gian làm việc")
    icon: "warning"
    iconColor: Theme.warn
    buttons: [{ text: qsTr("Hủy"), act: () => dlg.close() }, { text: qsTr("OK"), primary: true, act: () => dlg.accept() }]
    onAccepted: {
        if (params.picked)
            params.picked(choice)
    }

    Column {
        width: parent.width
        spacing: 10
        Text {
            width: parent.width
            text: qsTr("%1 có hồ sơ nhúng khác với không gian làm việc.").arg("<font color=\"" + Theme.t1 + "\">" + dlg.file.replace(/&/g, "&amp;").replace(/</g, "&lt;") + "</font>")
            textFormat: Text.StyledText
            color: Theme.t2
            font.pointSize: Theme.pt(12)
            wrapMode: Text.Wrap
        }
        Grid {
            columns: 2
            columnSpacing: 10
            rowSpacing: 4
            Repeater {
                model: [qsTr("Hồ sơ nhúng"), dlg.params.profile || "Adobe RGB (1998)", qsTr("Không gian làm việc"), dlg.params.working || "sRGB IEC61966-2.1"]
                delegate: Text {
                    required property string modelData
                    required property int index
                    width: index % 2 ? dlg.width - 36 - 160 : 150
                    text: modelData
                    color: index % 2 ? Theme.t1 : Theme.t3
                    font.pointSize: Theme.pt(11.5)
                    elide: Text.ElideRight
                }
            }
        }
        Column {
            topPadding: 4  // the first radio's margin-top
            spacing: 10
            Repeater {
                model: dlg.choices
                delegate: Radio {
                    required property var modelData
                    implicitHeight: 17  // a line of the artboard's 12 px text
                    text: modelData[1]
                    checked: dlg.choice === modelData[0]
                    onClicked: dlg.choice = modelData[0]
                }
            }
        }
    }
}
