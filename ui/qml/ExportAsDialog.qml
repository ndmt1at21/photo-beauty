pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import "Fmt.js" as Fmt
import "Mock.js" as Mock

// File › Xuất › Xuất thành (build/proto/ExportAs.dc.html, plan.md's Hộp thoại): a preview to zoom (Vừa, 12,5–400 %)
// that shows the original while held; the format (JPEG, PNG, WebP, TIFF) and its options; the image size (W, H chained,
// scale, Resample), the canvas, several scales with their file name suffixes, metadata, colour. Xuất is one background
// job of Docs, a file for each scale (progress and cancel on the top bar); Hủy, Esc, × leave it. As decision 3 of
// docs/ui-tasks.md has it, the preview does not follow the quality, and the estimated size is the prototype's formula.
DialogBox {
    id: dlg
    property var params: ({})
    readonly property Doc doc: Docs.current
    readonly property int srcW: doc ? doc.w : 1
    readonly property int srcH: doc ? doc.h : 1
    property string format: "JPEG"
    property int quality: 82
    property bool transparent: true  // PNG, WebP, TIFF
    property bool small: false  // PNG at 8 bits
    property string compression: "lzw"  // TIFF
    // The image in px, unrounded so chained W and H keep the ratio: 2.400 px wide as the prototype opens, no wider than
    // the document.
    property real iw: Math.min(srcW, 2400)
    property real ih: Math.min(srcW, 2400) * srcH / srcW
    property bool link: true
    property string resample: "Bicubic Sharper"
    property real cw: NaN  // the canvas, NaN: the image's
    property real ch: NaN
    readonly property int canvasW: isNaN(cw) ? Math.round(iw) : cw
    readonly property int canvasH: isNaN(ch) ? Math.round(ih) : ch
    property var scales: [{ s: 1, suf: "" }, { s: 0.5, suf: "@0,5x" }]  // s: the factor, suf: added to the file name
    property int meta: 0  // Không, Bản quyền và liên hệ
    property bool srgb: true
    property bool embed: true
    property int zoom: -1  // the preview's: -1 Vừa, else an index of zooms
    readonly property var zooms: [12.5, 25, 33.3, 50, 66.7, 100, 200, 400]
    readonly property bool comparing: imageArea.pressed || compareButton.pressed
    readonly property real fit: Math.max(1e-3, Math.min((stage.width - 70) / canvasW, (stage.height - 70) / canvasH))  // preview px a pixel at Vừa
    readonly property real zoomPct: zoom < 0 ? fit * 100 : zooms[zoom]
    readonly property int zoomOut: zooms.filter(z => z < zoomPct).length - 1  // -1: none
    readonly property int zoomIn: zooms.length - zooms.filter(z => z > zoomPct).length  // zooms.length: none
    // Bits a pixel of each format takes, roughly (the prototype's): JPEG and WebP by quality, PNG, TIFF by compression.
    readonly property real bpp: format === "PNG" ? (small ? 5 : 12) : format === "TIFF" ? 24 * ({ none: 1, lzw: 0.75, zip: 0.7 })[compression]
                              : (format === "WebP" ? 0.7 : 1) * (0.25 + 0.018 * quality + 6 * Math.pow(quality / 100, 16))
    readonly property string info: qsTr("%1 × %2 px · ước %3").arg(Fmt.vn(canvasW)).arg(Fmt.vn(canvasH)).arg(bytes(canvasW * canvasH * bpp / 8))
    readonly property var files: {  // the file of each scale
        const base = doc ? doc.file.replace(/\.[^.]+$/, "") : "", ext = ({ JPEG: ".jpg", PNG: ".png", WebP: ".webp", TIFF: ".tif" })[format]
        return scales.map(x => base + x.suf + ext)
    }

    title: qsTr("Xuất thành")
    boxWidth: 1100
    closable: true
    buttons: [{ text: qsTr("Hủy"), act: () => dlg.close() }, { text: qsTr("Xuất"), primary: true, disabled: !dlg.doc, act: () => dlg.run() }]

    function bytes(b) {  // "1,4 MB", "820 KB": whole MB from 100, no ",0"
        return b >= 1e6 ? Fmt.vn(b / 1e6, b >= 1e8 ? 0 : 1).replace(/,0$/, "") + " MB" : Fmt.vn(b / 1e3) + " KB"
    }
    function setW(x) {
        const k = ih / iw
        iw = x
        if (link)
            ih = x * k
    }
    function setH(x) {
        const k = iw / ih
        ih = x
        if (link)
            iw = x * k
    }
    function setScale(pct) {  // of the document
        iw = srcW * pct / 100
        ih = srcH * pct / 100
    }
    function scaleText(s) { return String(s).replace(".", ",") + "×" }  // 0.5 → "0,5×"
    function suffix(s) { return s === 1 ? "" : "@" + String(s).replace(".", ",") + "x" }  // 0.5 → "@0,5x"
    function setScaleAt(i, s) {  // a suffix still the factor's own follows it
        scales = scales.map((x, j) => j === i ? { s: s, suf: x.suf === suffix(x.s) ? suffix(s) : x.suf } : x)
    }
    function setSuffix(i, t) { scales = scales.map((x, j) => j === i ? { s: x.s, suf: t } : x) }
    function removeScale(i) {
        if (scales.length > 1)
            scales = scales.filter((x, j) => j !== i)
    }
    readonly property var nextScale: [2, 3, 1.5, 4, 0.75, 0.5, 1].find(s => !scales.some(x => x.s === s))  // undefined: all there
    function addScale() {
        if (nextScale !== undefined)
            scales = scales.concat([{ s: nextScale, suf: suffix(nextScale) }])
    }
    function run() {
        if (doc)
            Docs.exportAs(doc, files.length === 1 ? files[0] : qsTr("%1 file", "", files.length).arg(files.length))
        close()
    }

    component Cap: Text {
        color: Theme.t3
        font.pointSize: Theme.pt(10.5)
        font.weight: Font.Medium
        font.letterSpacing: 0.63  // .06em
        font.capitalization: Font.AllUppercase
        Layout.preferredHeight: Math.round(14.2 * Theme.fontScale)  // 10.5 px × 1.35, 15 of its own (capitals: no descender cut)
    }
    component Plain: Text {
        color: Theme.t2
        font.pointSize: Theme.pt(12)
    }
    // A section of the settings column (.sec): its rows 8 apart, 12 × 16 padding, a rule above but on the first.
    component Sec: Item {
        id: sec
        property bool first: false
        default property alias rows: col.data
        Layout.fillWidth: true
        implicitHeight: col.implicitHeight + 24
        Rectangle { visible: !sec.first; width: parent.width; height: 1; color: Theme.ln }
        ColumnLayout {
            id: col
            x: 16
            y: 12
            width: parent.width - 32
            spacing: 8
        }
    }
    component Tb: ToolToggle {  // .tb of this dialog: 28 × 26
        implicitWidth: 28
        implicitHeight: 26
        icon.width: 16
    }

    contentItem: Item {
        implicitWidth: 1100
        implicitHeight: 582  // 680 with the title and the buttons, as the artboard
        RowLayout {
            anchors.fill: parent
            spacing: 0
            ColumnLayout {  // the preview and its bar
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0
                Rectangle {
                    id: stage
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: Theme.view
                    clip: true
                    Item {  // the exported canvas, fitted at Vừa, scaled about its centre
                        anchors.centerIn: parent
                        width: dlg.canvasW * dlg.fit
                        height: dlg.canvasH * dlg.fit
                        scale: dlg.zoomPct / 100 / dlg.fit
                        Rectangle {  // the photo's outline
                            anchors.fill: parent
                            anchors.margins: -1
                            color: "#ffffff"  // a new document's white
                            border.color: Theme.shadow
                        }
                        Image {
                            anchors.fill: parent
                            source: Mock.photoUrl(dlg.doc ? dlg.doc.photo : null)
                            fillMode: Image.PreserveAspectCrop
                        }
                        MouseArea {
                            id: imageArea
                            anchors.fill: parent
                        }
                    }
                    Rectangle {
                        visible: dlg.comparing
                        x: 12
                        y: 12
                        width: original.implicitWidth + 16
                        height: original.implicitHeight + 6
                        radius: 4
                        color: Theme.onPhoto
                        Text {
                            id: original
                            anchors.centerIn: parent
                            text: qsTr("Ảnh gốc")
                            color: "#ffffff"
                            font.pointSize: Theme.pt(11)
                        }
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 40
                    color: Theme.p1
                    Rectangle { width: parent.width; height: 1; color: Theme.ln }
                    RowLayout {
                        x: 12
                        width: parent.width - 24
                        height: parent.height
                        spacing: 8
                        Tb {
                            icon.name: "zoom_out"
                            icon.width: 17
                            enabled: dlg.zoomOut >= 0
                            ToolTip.text: qsTr("Thu nhỏ · Zoom Out")
                            Accessible.name: qsTr("Thu nhỏ")
                            onClicked: dlg.zoom = dlg.zoomOut
                        }
                        Dropdown {
                            Layout.preferredWidth: 84
                            implicitHeight: 26
                            options: [[-1, qsTr("Vừa")]].concat(dlg.zooms.map((z, i) => [i, Fmt.zt(z)]))
                            value: dlg.zoom
                            onPicked: v => dlg.zoom = v
                        }
                        Tb {
                            icon.name: "zoom_in"
                            icon.width: 17
                            enabled: dlg.zoomIn < dlg.zooms.length
                            ToolTip.text: qsTr("Phóng to · Zoom In")
                            Accessible.name: qsTr("Phóng to")
                            onClicked: dlg.zoom = dlg.zoomIn
                        }
                        Rectangle { implicitWidth: 1; implicitHeight: 18; color: Theme.ln }
                        Tb {
                            id: compareButton
                            icon.name: "compare"
                            icon.width: 17
                            checked: dlg.comparing
                            ToolTip.text: qsTr("Nhấp giữ để so với gốc")
                            Accessible.name: qsTr("So với gốc")
                        }
                        Text {
                            text: qsTr("Nhấp giữ trên ảnh để so với gốc")
                            color: Theme.t3
                            font.pointSize: Theme.pt(11)
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            text: dlg.info
                            color: Theme.t2
                            font.family: Theme.mono
                            font.pointSize: Theme.pt(11)
                        }
                    }
                }
            }
            Rectangle { Layout.fillHeight: true; implicitWidth: 1; color: Theme.ln }
            Flickable {  // the settings
                Layout.preferredWidth: Math.round(339 * Theme.fontScale)  // Lớn: "Resample Bicubic Sharper" whole
                Layout.fillHeight: true
                contentHeight: side.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                ColumnLayout {
                    id: side
                    width: parent.width
                    spacing: 0
                    Sec {
                        first: true
                        Cap { text: qsTr("Định dạng") }
                        Dropdown {
                            Layout.preferredWidth: 150
                            implicitHeight: 26
                            options: ["JPEG", "PNG", "WebP", "TIFF"]
                            value: dlg.format
                            onPicked: v => dlg.format = v
                        }
                        ColumnLayout {
                            objectName: "quality"
                            visible: dlg.format === "JPEG" || dlg.format === "WebP"
                            spacing: 7
                            RowLayout {
                                Plain {
                                    Layout.fillWidth: true
                                    text: qsTr("Chất lượng")
                                }
                                NumberField {
                                    boxHeight: 26
                                    Layout.preferredWidth: 56
                                    horizontalAlignment: TextInput.AlignRight
                                    value: dlg.quality
                                    min: 1
                                    max: 100
                                    onMoved: v => dlg.quality = v
                                }
                            }
                            SliderField {
                                Layout.fillWidth: true
                                value: dlg.quality
                                min: 1
                                max: 100
                                onMoved: v => dlg.quality = v
                            }
                        }
                        RowLayout {
                            objectName: "compression"
                            visible: dlg.format === "TIFF"
                            spacing: 8
                            Plain {
                                Layout.preferredWidth: 72
                                text: qsTr("Nén")
                            }
                            Dropdown {
                                Layout.fillWidth: true
                                implicitHeight: 26
                                options: [["none", qsTr("Không")], ["lzw", "LZW"], ["zip", "ZIP"]]
                                value: dlg.compression
                                onPicked: v => dlg.compression = v
                            }
                        }
                        CheckRow {
                            objectName: "transparent"
                            visible: dlg.format !== "JPEG"
                            text: qsTr("Trong suốt")
                            checked: dlg.transparent
                            onClicked: dlg.transparent = !dlg.transparent
                        }
                        CheckRow {
                            objectName: "small"
                            visible: dlg.format === "PNG"
                            text: qsTr("File nhỏ hơn (8 bit)")
                            checked: dlg.small
                            onClicked: dlg.small = !dlg.small
                        }
                    }
                    Sec {
                        Cap { text: qsTr("Cỡ ảnh") }
                        RowLayout {
                            spacing: 6
                            NumberField {
                                boxHeight: 26
                                Layout.fillWidth: true
                                label: "W"
                                labelColor: Theme.t3
                                unit: "px"
                                value: Math.round(dlg.iw)  // as shown: a field left untouched applies its text
                                min: 1
                                max: 30000
                                onMoved: v => dlg.setW(v)
                            }
                            Tb {
                                icon.name: "link"
                                checked: dlg.link
                                ToolTip.text: qsTr("Giữ tỉ lệ W và H")
                                Accessible.name: ToolTip.text
                                onClicked: dlg.link = !dlg.link
                            }
                            NumberField {
                                boxHeight: 26
                                Layout.fillWidth: true
                                label: "H"
                                labelColor: Theme.t3
                                unit: "px"
                                value: Math.round(dlg.ih)
                                min: 1
                                max: 30000
                                onMoved: v => dlg.setH(v)
                            }
                        }
                        RowLayout {
                            spacing: 6
                            NumberField {
                                boxHeight: 26
                                Layout.preferredWidth: 110
                                label: qsTr("Tỉ lệ")
                                labelColor: Theme.t3
                                unit: "%"
                                decimals: 1
                                value: Math.round(dlg.iw / dlg.srcW * 1000) / 10
                                min: 0.1
                                max: 1000
                                onMoved: v => dlg.setScale(v)
                            }
                            Dropdown {
                                Layout.fillWidth: true
                                implicitHeight: 26
                                prefix: "Resample\u00a0"
                                options: ["Automatic", "Bicubic", "Bicubic Smoother", "Bicubic Sharper", "Bilinear", "Nearest Neighbor", "Lanczos"]
                                value: dlg.resample
                                onPicked: v => dlg.resample = v
                            }
                        }
                    }
                    Sec {
                        RowLayout {
                            Cap {
                                Layout.fillWidth: true
                                text: qsTr("Cỡ canvas")
                            }
                            Btn {
                                implicitHeight: 22
                                leftPadding: 8
                                rightPadding: 8
                                font.pointSize: Theme.pt(11)
                                text: qsTr("Đặt lại")
                                onClicked: {
                                    dlg.cw = NaN
                                    dlg.ch = NaN
                                }
                            }
                        }
                        RowLayout {
                            spacing: 6
                            NumberField {
                                boxHeight: 26
                                Layout.fillWidth: true
                                label: "W"
                                labelColor: Theme.t3
                                unit: "px"
                                value: dlg.canvasW
                                min: 1
                                max: 30000
                                onMoved: v => dlg.cw = v
                            }
                            NumberField {
                                boxHeight: 26
                                Layout.fillWidth: true
                                label: "H"
                                labelColor: Theme.t3
                                unit: "px"
                                value: dlg.canvasH
                                min: 1
                                max: 30000
                                onMoved: v => dlg.ch = v
                            }
                        }
                    }
                    Sec {
                        Cap { text: qsTr("Nhiều tỉ lệ") }
                        Repeater {
                            model: dlg.scales.length  // by count: typing a suffix keeps its field
                            RowLayout {
                                id: scaleRow
                                required property int index
                                readonly property var entry: dlg.scales[index] ?? ({ s: 1, suf: "" })
                                objectName: "scale"
                                Layout.fillWidth: true
                                spacing: 6
                                Dropdown {
                                    Layout.preferredWidth: 78
                                    implicitHeight: 26
                                    options: [0.5, 0.75, 1, 1.5, 2, 3, 4].map(s => [s, dlg.scaleText(s)])
                                    value: scaleRow.entry.s
                                    onPicked: v => dlg.setScaleAt(scaleRow.index, v)
                                }
                                TextBox {
                                    Layout.fillWidth: true
                                    implicitHeight: 26
                                    leftPadding: suffixLabel.implicitWidth + 14
                                    font.family: Theme.mono
                                    font.pointSize: Theme.pt(11.5)
                                    text: scaleRow.entry.suf
                                    Accessible.name: qsTr("Hậu tố tên file cho %1").arg(dlg.scaleText(scaleRow.entry.s))
                                    onTextEdited: dlg.setSuffix(scaleRow.index, text)
                                    Text {
                                        id: suffixLabel
                                        x: 8
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: qsTr("Hậu tố")
                                        color: Theme.t3
                                        font.pointSize: Theme.pt(12)
                                    }
                                }
                                Tb {
                                    icon.name: "delete"
                                    enabled: dlg.scales.length > 1
                                    ToolTip.text: qsTr("Bỏ tỉ lệ này")
                                    Accessible.name: qsTr("Bỏ tỉ lệ %1").arg(dlg.scaleText(scaleRow.entry.s))
                                    onClicked: dlg.removeScale(scaleRow.index)
                                }
                            }
                        }
                        Btn {
                            implicitHeight: 26
                            leftPadding: 8
                            icon.name: "add"
                            text: qsTr("Thêm tỉ lệ")
                            enabled: dlg.nextScale !== undefined
                            onClicked: dlg.addScale()
                        }
                    }
                    Sec {
                        Cap { text: qsTr("Metadata") }
                        Segmented {
                            Layout.fillWidth: true
                            implicitHeight: 26
                            fill: true
                            options: [qsTr("Không"), qsTr("Bản quyền và liên hệ")]
                            current: dlg.meta
                            onPicked: i => dlg.meta = i
                        }
                    }
                    Sec {
                        Cap { text: qsTr("Màu") }
                        CheckRow {
                            text: qsTr("Đổi sang sRGB")
                            checked: dlg.srgb
                            onClicked: dlg.srgb = !dlg.srgb
                        }
                        CheckRow {
                            text: qsTr("Nhúng hồ sơ màu")
                            checked: dlg.embed
                            onClicked: dlg.embed = !dlg.embed
                        }
                    }
                }
            }
        }
        Text {  // on the footer's left
            parent: dlg.footer
            x: 16
            width: parent.width - 220
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Xuất chạy nền: tiến độ ở thanh trên, vẫn chỉnh tiếp được.")
            color: Theme.t3
            font.pointSize: Theme.pt(11)
            elide: Text.ElideRight
        }
    }
}
