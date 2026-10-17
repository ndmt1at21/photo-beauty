pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import "Fmt.js" as Fmt

// The options a format asks for once Lưu thành, Lưu bản sao has picked it (build/proto/SaveOptions.dc.html, plan.md's
// Hộp thoại): params.format "jpeg" (quality 0–12 and its four levels, Baseline, Baseline Optimized, Progressive, the
// size it comes to), "psd" (Maximize Compatibility, what is kept, not asking again), "tiff" (compression, layers), "png"
// (compression, interlace); embedding the profile in all four. Without params.format, the document's own. OK saves
// Docs.current as that format in the background (a copy with params.copy: its unsaved dot stays); Hủy, Esc leave it.
DialogBox {
    id: dlg
    property var params: ({})
    readonly property Doc doc: Docs.current
    readonly property string format: {
        const f = String(params.format || "").toLowerCase()
        if (["jpeg", "psd", "tiff", "png"].indexOf(f) >= 0)
            return f
        const ext = doc ? doc.file.replace(/^.*\./, "").toLowerCase() : ""
        return ({ jpg: "jpeg", jpeg: "jpeg", tif: "tiff", tiff: "tiff", png: "png" })[ext] || "psd"
    }
    property int quality: 10  // JPEG, 0–12
    property string kind: "optimized"  // JPEG: baseline, optimized, progressive
    property bool maximize: Prefs.value("psdCompatibility") !== 2  // PSD: Thiết lập › File và xuất says Không
    property bool dontAsk: false
    property string compression: "lzw"  // TIFF: none, lzw, zip, jpeg (8 bits only)
    property string layers: "rle"  // TIFF: rle, zip, flat
    property string pngCompression: "balanced"  // fast, balanced, smallest
    property string interlace: "none"  // PNG: none, interlaced
    property bool embed: true
    readonly property var levels: [[3, qsTr("Thấp")], [5, qsTr("Vừa")], [8, qsTr("Cao")], [10, qsTr("Tối đa")]]  // as Photoshop: 0–4, 5–7, 8–9, 10–12
    readonly property int level: quality <= 4 ? 3 : quality <= 7 ? 5 : quality <= 9 ? 8 : 10
    // The prototype's sizes of chân dung.psd (4.400 × 5.600) as JPEG, MB at each quality; other sizes by their pixels.
    readonly property var estimates: [0.9, 1.0, 1.1, 1.3, 1.5, 1.8, 2.1, 2.6, 3.2, 4.1, 5.6, 7.4, 10.2]
    readonly property string estimate: Fmt.vn(estimates[quality] * (doc ? doc.w * doc.h : 0) / (4400 * 5600), 1) + " MB"
    readonly property string file: {  // what it saves to: PSB past 30.000 px
        const base = doc ? doc.file.replace(/\.[^.]+$/, "") : ""
        return base + (format === "psd" && doc && Math.max(doc.w, doc.h) > 30000 ? ".psb" : ({ jpeg: ".jpg", psd: ".psd", tiff: ".tif", png: ".png" })[format])
    }

    title: qsTr("Tùy chọn %1").arg(({ jpeg: "JPEG", psd: "PSD", tiff: "TIFF", png: "PNG" })[format])
    boxWidth: 340
    buttons: [{ text: qsTr("Hủy"), act: () => dlg.close() }, { text: qsTr("OK"), primary: true, disabled: !dlg.doc, act: () => dlg.apply() }]

    function apply() {
        if (format === "psd" && dontAsk)
            Prefs.set("psdCompatibility", maximize ? 0 : 2)  // Luôn, Không
        if (doc)  // ponytail: Docs has no saveAs(); N2's Session takes the options when it saves for real
            Docs.startJob("save", file, params.copy ? null : doc, 2)
        close()
    }

    component Cap: Text {
        Layout.topMargin: 4
        color: Theme.t3
        font.pointSize: Theme.pt(10.5)
        font.weight: Font.Medium
        font.letterSpacing: 0.63  // .06em
        font.capitalization: Font.AllUppercase
    }
    component Note: Text {
        Layout.fillWidth: true
        color: Theme.t3
        font.pointSize: Theme.pt(11)
        wrapMode: Text.Wrap
    }
    // Radio buttons for one value, in equal columns: options [value, label]; `disabled` lists values not to pick now.
    component Radios: GridLayout {
        id: radios
        property var options: []
        property var value
        property var disabled: []
        signal picked(var value)
        Layout.fillWidth: true
        columns: 1
        columnSpacing: 6
        rowSpacing: 10
        Repeater {
            model: radios.options
            AssignProfileDialog.Radio {
                id: radio
                required property var modelData
                Layout.fillWidth: true
                Layout.preferredWidth: 1  // the columns share the width alike
                text: radio.modelData[1]
                checked: radios.value === radio.modelData[0]
                enabled: radios.disabled.indexOf(radio.modelData[0]) < 0
                onClicked: radios.picked(radio.modelData[0])
            }
        }
    }
    component Embed: CheckRow {
        Layout.topMargin: 4
        text: qsTr("Nhúng hồ sơ màu")
        checked: dlg.embed
        onClicked: dlg.embed = !dlg.embed
    }

    ColumnLayout {
        objectName: "jpeg"
        visible: dlg.format === "jpeg"
        width: parent.width
        spacing: 10
        Cap {
            Layout.topMargin: 0
            text: qsTr("Chất lượng")
        }
        RowLayout {
            spacing: 10
            SliderField {
                Layout.fillWidth: true
                value: dlg.quality
                min: 0
                max: 12
                onMoved: v => dlg.quality = v
            }
            NumberField {
                Layout.preferredWidth: 54
                value: dlg.quality
                min: 0
                max: 12
                onMoved: v => dlg.quality = v
            }
            Dropdown {
                Layout.preferredWidth: 84
                implicitHeight: 26
                options: dlg.levels
                value: dlg.level
                onPicked: v => dlg.quality = v
            }
        }
        Text {
            text: qsTr("0–12 như Photoshop · nhỏ ← → lớn")
            color: Theme.t3
            font.family: Theme.mono
            font.pointSize: Theme.pt(10.5)
        }
        Cap { text: qsTr("Kiểu") }
        Radios {
            options: [["baseline", "Baseline"], ["optimized", "Baseline Optimized"], ["progressive", "Progressive"]]
            value: dlg.kind
            onPicked: v => dlg.kind = v
        }
        Embed {}
        Row {
            Note {
                width: implicitWidth
                text: qsTr("Ước lượng: ")
            }
            Note {
                width: implicitWidth
                text: dlg.estimate
                font.family: Theme.mono
            }
        }
    }
    ColumnLayout {
        objectName: "psd"
        visible: dlg.format === "psd"
        width: parent.width
        spacing: 10
        CheckRow {
            text: qsTr("Maximize Compatibility")
            checked: dlg.maximize
            onClicked: dlg.maximize = !dlg.maximize
        }
        Text {
            Layout.fillWidth: true
            text: qsTr("Kèm ảnh ghép trong file để Lightroom, SnapOne và app khác đọc được. File lớn hơn một chút.")
            color: Theme.t2
            font.pointSize: Theme.pt(12)
            wrapMode: Text.Wrap
        }
        Cap { text: qsTr("Phần chưa hỗ trợ") }
        Note {
            text: qsTr("Giữ nguyên khi lưu lại, không mất dữ liệu của Photoshop.")
            font.pointSize: Theme.pt(11.5)
        }
        Embed {}
        CheckRow {
            Layout.topMargin: 6
            text: qsTr("Không hỏi lại (đổi ở Thiết lập › File và xuất)")
            checked: dlg.dontAsk
            onClicked: dlg.dontAsk = !dlg.dontAsk
        }
        Row {
            Note {
                width: implicitWidth
                text: qsTr("Ảnh có cạnh dài hơn ")
            }
            Note {
                width: implicitWidth
                text: Fmt.vn(30000) + " px"
                font.family: Theme.mono
            }
            Note {
                width: implicitWidth
                text: qsTr(" thì lưu thành PSB.")
            }
        }
    }
    ColumnLayout {
        objectName: "tiff"
        visible: dlg.format === "tiff"
        width: parent.width
        spacing: 10
        Cap {
            Layout.topMargin: 0
            text: qsTr("Nén ảnh")
        }
        Radios {
            columns: 2
            rowSpacing: 6
            options: [["none", "None"], ["lzw", "LZW"], ["zip", "ZIP"], ["jpeg", "JPEG"]]
            disabled: dlg.doc && dlg.doc.bits === 8 ? [] : ["jpeg"]  // JPEG in a TIFF takes 8 bits only
            value: dlg.compression
            onPicked: v => dlg.compression = v
        }
        Cap { text: qsTr("Layer") }
        Radios {
            options: [["rle", qsTr("Giữ layer, nén RLE")], ["zip", qsTr("Giữ layer, nén ZIP")], ["flat", qsTr("Bỏ layer, lưu bản phẳng")]]
            value: dlg.layers
            onPicked: v => dlg.layers = v
        }
        Embed {}
    }
    ColumnLayout {
        objectName: "png"
        visible: dlg.format === "png"
        width: parent.width
        spacing: 10
        Cap {
            Layout.topMargin: 0
            text: qsTr("Nén")
        }
        Radios {
            options: [["fast", qsTr("Nhanh, file lớn")], ["balanced", qsTr("Cân bằng")], ["smallest", qsTr("Nhỏ nhất, chậm")]]
            value: dlg.pngCompression
            onPicked: v => dlg.pngCompression = v
        }
        Cap { text: qsTr("Interlace") }
        Radios {
            options: [["none", "None"], ["interlaced", "Interlaced"]]
            value: dlg.interlace
            onPicked: v => dlg.interlace = v
        }
        Embed {}
    }
}
