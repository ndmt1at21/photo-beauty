pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts

// Tùy chọn Auto of Levels (docs/ui-tasks.md, U25; build/design2/project/AutoColor.dc.html): how Auto sets the tones, one
// of four algorithms, Snap Neutral Midtones, the target colours of shadows, midtones and highlights and how much of the
// dark and light ends it clips (0–9,99 %). params { options, picked(options) }: the adjustment's own options (none:
// Prefs' autoColor, which Lưu làm mặc định keeps, else the defaults) and where OK sends them. Hủy, Esc, × leave both.
DialogBox {
    id: dlg
    property var params: ({})
    readonly property var algorithms: ["Enhance Monochromatic Contrast", "Enhance Per Channel Contrast", "Find Dark & Light Colors",
        "Enhance Brightness and Contrast"]
    readonly property var defaults: ({ algo: 2, snap: true, shadow: "#000000", mid: "#808080", high: "#ffffff", clipShadow: 0.1, clipHigh: 0.1 })
    property var opts: Object.assign({}, defaults, Prefs.value("autoColor"), params.options)
    property bool keep: false  // Lưu làm mặc định

    title: qsTr("Auto Color Correction Options")
    boxWidth: 460
    closable: true
    buttons: [{ text: qsTr("Hủy"), act: () => dlg.close() }, { text: qsTr("OK"), primary: true, act: () => dlg.apply() }]

    function set(k, v) { opts = Object.assign({}, opts, { [k]: v }) }
    function apply() {
        const o = opts
        if (keep)
            Prefs.set("autoColor", o)
        if (params.picked)
            params.picked(o)
        close()
    }

    component Cap: Text {  // .cap
        color: Theme.t3
        font.pointSize: Theme.pt(10.5)
        font.weight: Font.Medium
        font.letterSpacing: 0.63  // .06em
        font.capitalization: Font.AllUppercase
        Layout.preferredHeight: Math.round(14.2 * Theme.fontScale)  // 10.5 px × 1.35
    }
    component Label: Text {  // .rl
        color: Theme.t2
        font.pointSize: Theme.pt(12)
        Layout.preferredHeight: Math.round(16.2 * Theme.fontScale)  // 12 px × 1.35
    }

    ColumnLayout {
        width: parent.width
        spacing: 12
        Cap { text: qsTr("Thuật toán") }
        ColumnLayout {
            spacing: 8
            Repeater {
                model: dlg.algorithms
                Radio {
                    required property string modelData
                    required property int index
                    spacing: 8  // .chk
                    text: modelData
                    checked: dlg.opts.algo === index
                    onClicked: dlg.set("algo", index)
                }
            }
        }
        CheckRow {
            spacing: 8
            text: qsTr("Snap Neutral Midtones")
            checked: !!dlg.opts.snap
            onClicked: dlg.set("snap", !dlg.opts.snap)
        }
        Cap {
            Layout.topMargin: 4
            text: qsTr("Màu đích và cắt")
        }
        Repeater {
            // [name, colour key, clip key]: the artboard's grid of 90, 40, the rest, 40 and 96 px, 10 apart
            model: [["Shadows", "shadow", "clipShadow"], ["Midtones", "mid", ""], ["Highlights", "high", "clipHigh"]]
            RowLayout {
                id: target
                required property var modelData
                readonly property string clipKey: modelData[2]
                spacing: 10
                Label {
                    Layout.preferredWidth: 90
                    text: target.modelData[0]
                }
                ColorButton {
                    implicitWidth: 40
                    implicitHeight: 24
                    color: dlg.opts[target.modelData[1]]
                    title: qsTr("Màu đích %1").arg(target.modelData[0])
                    onChosen: c => dlg.set(target.modelData[1], String(c))
                }
                Item { Layout.fillWidth: true }
                Label {
                    visible: target.clipKey !== ""
                    Layout.preferredWidth: 40
                    horizontalAlignment: Text.AlignRight
                    text: qsTr("Clip")
                }
                NumberField {
                    visible: target.clipKey !== ""
                    Layout.preferredWidth: 96
                    boxHeight: 28
                    value: dlg.opts[target.clipKey] ?? 0
                    unit: "%"
                    decimals: 2
                    step: 0.01
                    min: 0
                    max: 9.99
                    Accessible.name: qsTr("Clip %1, phần trăm").arg(target.modelData[0])
                    onMoved: v => dlg.set(target.clipKey, v)
                }
            }
        }
        CheckRow {
            Layout.topMargin: 4
            spacing: 8
            text: qsTr("Lưu làm mặc định")
            checked: dlg.keep
            onClicked: dlg.keep = !dlg.keep
        }
    }
}
