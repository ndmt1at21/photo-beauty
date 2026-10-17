pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts

// Thiếu font (docs/ui-tasks.md, U20; build/proto/Notices.dc.html, the second box; Type › Thay font thiếu…): each font
// a type layer uses and the machine lacks, its layers, and the font to put in its place. Mock data (decision 3):
// params { file, fonts: [{ miss, layers, to }], options }, the artboard's by default. Thay font is one History step on
// Docs.current ({ op: "step", fonts: { missing: picked } }); Để sau, Esc leave it.
DialogBox {
    id: dlg
    property var params: ({})
    readonly property string file: params.file || "poster-tet.pbd"
    readonly property var fonts: params.fonts || [{ miss: "SVN-Gilroy Bold", layers: ["Tết Bính Ngọ 2026", "Ưu đãi 30%"], to: "Be Vietnam Pro Bold" },
        { miss: "UTM Avo", layers: ["Địa chỉ cửa hàng"], to: "Space Grotesk Regular" }]
    readonly property var options: params.options || ["Be Vietnam Pro Bold", "Be Vietnam Pro Regular", "Space Grotesk Bold", "Space Grotesk Regular", "Roboto Bold"]
    property var picks: ({})  // missing font → the one picked instead of its `to`
    readonly property int layerCount: fonts.reduce((n, f) => n + f.layers.length, 0)

    title: qsTr("Thiếu %1 font", "", fonts.length).arg(fonts.length)
    icon: "warning"
    iconColor: Theme.warn
    buttons: [{ text: qsTr("Để sau"), act: () => dlg.close() }, { text: qsTr("Thay font"), primary: true, act: () => dlg.apply() }]

    function fontFor(f) { return picks[f.miss] || f.to }
    function pick(f, to) {
        const p = Object.assign({}, picks)
        p[f.miss] = to
        picks = p
    }
    function apply() {
        const map = {}
        for (const f of fonts)
            map[f.miss] = fontFor(f)
        if (Docs.current)
            Docs.current.run({ op: "step", name: qsTr("Thay font"), icon: "title", fonts: map })
        close()
    }

    Column {
        width: parent.width
        spacing: 10
        Text {
            width: parent.width
            text: qsTr("%1 layer chữ của %2 dùng font không có trên máy. Chữ vẫn hiện bằng điểm ảnh đã lưu cho tới khi sửa.", "", dlg.layerCount)
                  .arg(dlg.layerCount).arg("<font color=\"" + Theme.t1 + "\">" + dlg.file.replace(/&/g, "&amp;").replace(/</g, "&lt;") + "</font>")
            textFormat: Text.StyledText
            color: Theme.t2
            font.pointSize: Theme.pt(12)
            wrapMode: Text.Wrap
        }
        Column {
            id: rows
            width: parent.width
            Repeater {
                model: dlg.fonts
                delegate: Item {  // .lr: the missing font and its layers → the one in its place
                    id: fr
                    required property var modelData
                    width: rows.width
                    height: line.implicitHeight + 17
                    Rectangle { width: parent.width; height: 1; color: Theme.p2 }
                    RowLayout {
                        id: line
                        y: 9
                        width: parent.width
                        spacing: 10
                        Column {
                            Layout.fillWidth: true
                            Text {
                                width: parent.width
                                text: fr.modelData.miss
                                color: Theme.t1
                                font.pointSize: Theme.pt(12)
                                elide: Text.ElideRight
                            }
                            Text {
                                width: parent.width
                                text: fr.modelData.layers.join(", ")
                                color: Theme.t3
                                font.pointSize: Theme.pt(11)
                                elide: Text.ElideRight
                            }
                        }
                        Text {
                            text: "→"
                            color: Theme.t3
                            font.pointSize: Theme.pt(12)
                        }
                        Select {
                            Layout.preferredWidth: 170
                            options: dlg.options.map(o => [o, o])
                            value: dlg.fontFor(fr.modelData)
                            Accessible.name: qsTr("Font thay cho %1: %2").arg(fr.modelData.miss).arg(displayText)
                            onPicked: v => dlg.pick(fr.modelData, v)
                        }
                    }
                }
            }
        }
    }
}
