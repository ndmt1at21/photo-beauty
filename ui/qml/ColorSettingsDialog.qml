pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts

// Edit › Color Settings (build/proto/ColorSettings.dc.html, plan.md's Hộp thoại): the working spaces, what opening a file
// with another profile or none does, how conversions run. A preset sets them all; changing one makes it Tùy chỉnh. The
// box at the foot explains the row under the mouse. OK writes them to Prefs; Hủy, Esc, × leave them.
// ponytail: Prefs has no keys for these (U3 copied the Settings artboard's PANES, which has none), so their defaults live
// here and Prefs keeps them for the session only (restore() drops keys without a default); presets[0] into Prefs'
// defaults keeps them between starts.
DialogBox {
    id: dlg
    property var params: ({})
    readonly property var rgbSpaces: ["sRGB IEC61966-2.1", "Display P3", "Adobe RGB (1998)", "ProPhoto RGB"]
    readonly property var graySpaces: ["Gray Gamma 2.2", "Gray Gamma 1.8", "sGray", "Dot Gain 20%"]
    readonly property var intents: ["Perceptual", "Saturation", "Relative Colorimetric", "Absolute Colorimetric"]
    // The prototype's defaults (photo-beauty mặc định), then Monitor Color: the display's space, no policies, no asking.
    // A policy is an index: Tắt, Giữ hồ sơ nhúng, Chuyển sang không gian làm việc.
    readonly property var presets: [
        { colorRgb: "sRGB IEC61966-2.1", colorGray: "Gray Gamma 2.2", colorPolicyRgb: 1, colorPolicyGray: 1, colorWarnOpen: true, colorWarnPaste: false,
          colorWarnMissing: true, colorIntent: "Relative Colorimetric", colorBpc: true, colorDither: true },
        { colorRgb: "Display P3", colorGray: "Gray Gamma 2.2", colorPolicyRgb: 0, colorPolicyGray: 0, colorWarnOpen: false, colorWarnPaste: false,
          colorWarnMissing: false, colorIntent: "Relative Colorimetric", colorBpc: true, colorDither: false }
    ]
    property var vals: {  // what the dialog shows: Prefs' value of each key, the default where it has none
        const o = {}
        for (const k in presets[0])
            o[k] = Prefs.value(k) ?? presets[0][k]
        return o
    }
    readonly property int preset: {  // the preset vals are, 2 (Tùy chỉnh) for none
        const i = presets.findIndex(p => Object.keys(p).every(k => p[k] === vals[k]))
        return i < 0 ? 2 : i
    }
    property string hint  // the explanation of the row under the mouse

    title: qsTr("Color Settings")
    boxWidth: 600
    closable: true
    buttons: [{ text: qsTr("Hủy"), act: () => dlg.close() }, { text: qsTr("OK"), primary: true, act: () => dlg.apply() }]

    function set(key, v) { vals = Object.assign({}, vals, { [key]: v }) }
    function pickPreset(i) {
        if (presets[i])
            vals = Object.assign({}, presets[i])
    }
    function apply() {
        const v = vals
        for (const k in v)
            if (Prefs.value(k) !== v[k])
                Prefs.set(k, v[k])
        close()
    }

    component Cap: Text {
        Layout.topMargin: 6
        color: Theme.t3
        font.pointSize: Theme.pt(10.5)
        font.weight: Font.Medium
        font.letterSpacing: 0.63  // .06em
        font.capitalization: Font.AllUppercase
    }
    // A row of the prototype's .rw: the label in a 130 px column, its controls after it; `tip` explains it under the mouse.
    component Line: RowLayout {
        id: line
        property string label
        property string tip
        spacing: 10
        Text {
            Layout.preferredWidth: 130
            text: line.label
            color: Theme.t2
            font.pointSize: Theme.pt(12)
        }
        HoverHandler { onHoveredChanged: dlg.hint = hovered ? line.tip : dlg.hint === line.tip ? "" : dlg.hint }
    }
    component Choice: Select {
        property var items: []  // the values, shown as they are, or [value, label]
        Layout.fillWidth: true
        options: items.map(o => typeof o === "object" ? o : [o, o])
    }

    ColumnLayout {
        width: parent.width
        spacing: 10
        Line {
            label: qsTr("Bộ thiết lập")
            tip: qsTr("Một bộ giá trị cho mọi mục bên dưới; đổi một mục thì bộ thành Tùy chỉnh.")
            Choice {
                items: [[0, qsTr("photo-beauty mặc định")], [1, qsTr("Monitor Color")], [2, qsTr("Tùy chỉnh")]]
                value: dlg.preset
                onPicked: v => dlg.pickPreset(v)
            }
        }
        Cap { text: qsTr("Không gian làm việc") }
        Line {
            label: "RGB"
            tip: qsTr("Hồ sơ của tài liệu RGB mới và của file không có hồ sơ. sRGB cho web; Display P3, Adobe RGB, ProPhoto RGB có gam rộng hơn.")
            Choice {
                items: dlg.rgbSpaces
                value: dlg.vals.colorRgb
                onPicked: v => dlg.set("colorRgb", v)
            }
        }
        Line {
            label: "Gray"
            tip: qsTr("Hồ sơ của tài liệu Grayscale mới và của file xám không có hồ sơ.")
            Choice {
                items: dlg.graySpaces
                value: dlg.vals.colorGray
                onPicked: v => dlg.set("colorGray", v)
            }
        }
        Cap { text: qsTr("Chính sách khi mở file") }
        Line {
            label: "RGB"
            tip: qsTr("Khi mở file RGB có hồ sơ: giữ hồ sơ nhúng, chuyển màu sang không gian làm việc, hay tắt quản lý màu cho file đó.")
            Choice {
                items: [[0, qsTr("Tắt")], [1, qsTr("Giữ hồ sơ nhúng")], [2, qsTr("Chuyển sang không gian làm việc")]]
                value: dlg.vals.colorPolicyRgb
                onPicked: v => dlg.set("colorPolicyRgb", v)
            }
        }
        Line {
            label: "Gray"
            tip: qsTr("Như dòng RGB, cho file Grayscale.")
            Choice {
                items: [[0, qsTr("Tắt")], [1, qsTr("Giữ hồ sơ nhúng")], [2, qsTr("Chuyển sang không gian làm việc")]]
                value: dlg.vals.colorPolicyGray
                onPicked: v => dlg.set("colorPolicyGray", v)
            }
        }
        Line {
            label: qsTr("Hồ sơ khác")
            tip: qsTr("Hỏi khi file mở ra, hay ảnh dán vào, có hồ sơ khác không gian làm việc.")
            RowLayout {
                spacing: 18
                CheckRow {
                    text: qsTr("Báo khi mở")
                    checked: dlg.vals.colorWarnOpen
                    onClicked: dlg.set("colorWarnOpen", !dlg.vals.colorWarnOpen)
                }
                CheckRow {
                    text: qsTr("Hỏi khi dán")
                    checked: dlg.vals.colorWarnPaste
                    onClicked: dlg.set("colorWarnPaste", !dlg.vals.colorWarnPaste)
                }
            }
        }
        Line {
            label: qsTr("Thiếu hồ sơ")
            tip: qsTr("Hỏi khi file mở ra không có hồ sơ màu nào.")
            CheckRow {
                text: qsTr("Báo khi mở")
                checked: dlg.vals.colorWarnMissing
                onClicked: dlg.set("colorWarnMissing", !dlg.vals.colorWarnMissing)
            }
        }
        Cap { text: qsTr("Chuyển đổi") }
        Line {
            label: qsTr("Engine")
            tip: qsTr("Thư viện đổi màu theo hồ sơ ICC.")
            Text {
                text: "lcms2"
                color: Theme.t2
                font.family: Theme.mono
                font.pointSize: Theme.pt(12)
            }
        }
        Line {
            label: qsTr("Intent")
            tip: qsTr("Cách đưa màu ngoài gam vào gam đích: Relative Colorimetric giữ đúng màu trong gam, Perceptual co cả dải màu cho mượt.")
            Choice {
                items: dlg.intents
                value: dlg.vals.colorIntent
                onPicked: v => dlg.set("colorIntent", v)
            }
        }
        Line {
            tip: qsTr("Black Point Compensation giữ chi tiết vùng tối khi điểm đen hai hồ sơ khác nhau; Dither thêm nhiễu rất nhẹ để ảnh 8 bit không thành dải.")
            ColumnLayout {
                spacing: 6
                CheckRow {
                    text: qsTr("Black Point Compensation")
                    checked: dlg.vals.colorBpc
                    onClicked: dlg.set("colorBpc", !dlg.vals.colorBpc)
                }
                CheckRow {
                    text: qsTr("Dither (ảnh 8 bit)")
                    checked: dlg.vals.colorDither
                    onClicked: dlg.set("colorDither", !dlg.vals.colorDither)
                }
            }
        }
        Rectangle {  // the explanation; two lines high so it does not move the rows
            Layout.fillWidth: true
            implicitHeight: 2 * metrics.lineSpacing + 20
            radius: 6
            color: Theme.bg
            Text {
                id: explanation
                x: 12
                y: 10
                width: parent.width - 24
                text: dlg.hint || qsTr("Rê chuột lên một mục để xem giải thích.")
                color: Theme.t3
                font.pointSize: Theme.pt(11.5)
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
            FontMetrics { id: metrics; font: explanation.font }
        }
    }
}
