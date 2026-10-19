pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts

// Image › Trim… (docs/ui-tasks.md, U25; build/design2/project/ImageDialogs.dc.html, Trim): what is trimmed away
// (Transparent Pixels, greyed when the document has a Background layer and so nothing clear; the top left pixel's colour,
// the default; the bottom right's) and from which sides (all four on). OK is one History step "Trim" on Docs.current,
// which the mock does not crop ({ op: "step" } with the choices); Hủy, Esc, × leave it.
DialogBox {
    id: dlg
    property var params: ({})
    readonly property Doc doc: Docs.current
    readonly property bool clear: !!doc && !doc.layers.some(l => !!l.props.background)  // pixels that can be transparent
    property string basedOn: "topLeft"  // "transparent", "topLeft", "bottomRight"
    property var sides: ({ top: true, left: true, bottom: true, right: true })

    title: qsTr("Trim")
    boxWidth: 400
    closable: true
    buttons: [{ text: qsTr("Hủy"), act: () => dlg.close() }, { text: qsTr("OK"), primary: true, act: () => dlg.apply() }]

    function apply() {
        if (doc)
            doc.run({ op: "step", name: qsTr("Trim"), icon: "crop", basedOn: basedOn, top: sides.top, left: sides.left, bottom: sides.bottom, right: sides.right })
        close()
    }

    ColumnLayout {
        width: parent.width
        spacing: 12
        FillDialog.Cap { text: qsTr("Dựa trên") }
        Column {
            spacing: 8
            Repeater {
                model: [["transparent", qsTr("Điểm ảnh trong suốt")], ["topLeft", qsTr("Màu điểm ảnh góc trên trái")], ["bottomRight", qsTr("Màu điểm ảnh góc dưới phải")]]
                Radio {
                    required property var modelData
                    spacing: 8
                    enabled: modelData[0] !== "transparent" || dlg.clear
                    text: modelData[1]
                    checked: dlg.basedOn === modelData[0]
                    onClicked: dlg.basedOn = modelData[0]
                }
            }
        }
        FillDialog.Cap {
            Layout.topMargin: 4
            text: qsTr("Cắt bỏ")
        }
        GridLayout {
            columns: 2
            rowSpacing: 8
            columnSpacing: 18
            Repeater {
                model: [["top", qsTr("Trên")], ["left", qsTr("Trái")], ["bottom", qsTr("Dưới")], ["right", qsTr("Phải")]]
                CheckRow {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1  // equal halves
                    spacing: 8
                    text: modelData[1]
                    checked: dlg.sides[modelData[0]]
                    onClicked: dlg.sides = Object.assign({}, dlg.sides, { [modelData[0]]: !checked })
                }
            }
        }
    }
}
