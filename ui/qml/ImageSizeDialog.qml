pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import "FilterSpec.js" as Spec
import "Fmt.js" as Fmt
import "Mock.js" as Mock

// Image › Image Size (build/proto/ImageSize.dc.html, plan.md's Hộp thoại): the new size in pixels and the resolution;
// W and H in any unit, chained or not; Fit To presets; Resample and its method. With Resample off the pixels stay and a
// physical W or H changes the resolution. OK resizes Docs.current in one History step; Hủy, Esc, × leave it.
DialogBox {
    id: dlg
    property var params: ({})
    readonly property Doc doc: Docs.current
    readonly property var base: doc ? [doc.w, doc.h] : [1, 1]
    readonly property real basePpi: doc ? doc.ppi : 72
    readonly property real bpp: doc ? Spec.bytesPerPixel(doc.mode, doc.bits) : 3
    // What OK gives: W, H in px (rounded then), the resolution in px per inch.
    property var size: base.slice()
    property real ppi: basePpi
    property var units: ["px", "px"]  // of W and H, keys of Spec.UNITS
    property string resUnit: "in"  // the resolution's: "in" px per inch, "cm" px per centimetre
    property bool link: true
    property bool resample: true
    property string method: "automatic"
    property int fit: 0  // of Spec.FIT
    property bool scaleStyles: false
    readonly property bool linked: link || !resample
    readonly property int custom: Spec.FIT.length - 1
    readonly property bool changed: Math.round(size[0]) !== base[0] || Math.round(size[1]) !== base[1]

    title: qsTr("Image Size")
    boxWidth: 600
    closable: true
    buttons: [{ text: qsTr("Hủy"), act: () => dlg.close() }, { text: qsTr("OK"), primary: true, act: () => dlg.apply() }]

    function px(v) { return Math.max(1, Math.min(300000, v)) }
    // W (0) or H (1) as its field shows it.
    function shown(i) { return Spec.round(Spec.toUnit(size[i], units[i], ppi, base[i]), units[i] === "px" ? 0 : 2) }
    // A value typed into W or H, in its unit: the prototype's setDim.
    function setDim(i, x) {
        const k = Spec.PER_INCH[units[i]]
        if (!resample) {
            if (k && x > 0)
                setPpi(size[i] * k / x)
            return
        }
        const v = px(Spec.fromUnit(x, units[i], ppi, base[i])), o = link ? px(v * size[1 - i] / size[i]) : size[1 - i]
        size = i ? [o, v] : [v, o]
        fit = custom
    }
    function setPpi(p) {
        ppi = Math.max(1, Math.min(30000, p))
        fit = custom
    }
    function setRes(x) {  // the resolution typed, in its unit; with Resample on the physical size stays
        const old = ppi
        setPpi(resUnit === "cm" ? x * 2.54 : x)
        if (resample)
            size = [px(size[0] * ppi / old), px(size[1] * ppi / old)]
    }
    function setUnit(i, u) { units = i ? [units[0], u] : [u, units[1]] }
    function fitTo(i) {
        const p = Spec.FIT[i]
        if (i === 0) {
            size = base.slice()
            ppi = basePpi
            units = ["px", "px"]
        } else if (p.w !== undefined) {
            const bw = Spec.fromUnit(p.w, p.unit, p.ppi, 1), bh = Spec.fromUnit(p.h, p.unit, p.ppi, 1), k = Math.min(bw / size[0], bh / size[1])
            units = [p.unit, p.unit]
            if (!resample) {  // the print size follows the resolution
                setPpi(p.unit === "px" ? p.ppi : p.ppi * Math.max(size[0] / bw, size[1] / bh))
            } else {
                ppi = p.ppi
                size = link ? [px(size[0] * k), px(size[1] * k)] : [bw, bh]
            }
        }
        fit = i
    }
    function toggleResample() {
        resample = !resample
        if (!resample)
            size = base.slice()  // no pixels change without resampling
    }
    function apply() {
        const w = Math.round(size[0]), h = Math.round(size[1])
        if (doc && (w !== doc.w || h !== doc.h || ppi !== doc.ppi)) {
            const c = { op: "imageSize", w: w, h: h, ppi: ppi, scaleStyles: scaleStyles }
            if (resample)
                c.method = method
            doc.run(c)
        }
        close()
    }

    component RowLabel: Text {
        color: Theme.t2
        font.pointSize: Theme.pt(12)
        Layout.preferredWidth: 96
    }
    component Mono: Text {
        color: Theme.t1
        font.family: Theme.mono
        font.pointSize: Theme.pt(12)
    }

    RowLayout {
        width: parent.width
        spacing: 18
        ColumnLayout {
            Layout.alignment: Qt.AlignTop
            spacing: 6
            Rectangle {  // the photo at 100 %, as the prototype crops it
                id: thumb
                readonly property var photo: dlg.doc && dlg.doc.photo !== null ? Mock.PHOTOS[dlg.doc.photo] : null
                readonly property real k: photo ? Math.max(width / photo.w, height / photo.h) : 1
                implicitWidth: 200
                implicitHeight: 280
                radius: 6
                color: Theme.view
                clip: true
                Image {
                    visible: !!thumb.photo
                    width: thumb.photo ? thumb.photo.w * thumb.k : 0
                    height: thumb.photo ? thumb.photo.h * thumb.k : 0
                    x: (thumb.width - width) * 0.52
                    y: (thumb.height - height) * 0.38
                    source: thumb.photo ? Mock.photoUrl(dlg.doc.photo) : ""
                }
                Rectangle {  // rounds the photo's corners: clip only cuts square ones
                    anchors.fill: parent
                    anchors.margins: -4
                    radius: 10
                    color: "transparent"
                    border.width: 4
                    border.color: Theme.p1
                }
            }
            Text {
                Layout.alignment: Qt.AlignHCenter
                text: "100%"
                color: Theme.t3
                font.family: Theme.mono
                font.pointSize: Theme.pt(11)
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            spacing: 10
            Column {
                spacing: 2
                Row {
                    Text { text: qsTr("Cỡ ảnh: "); color: Theme.t1; font.pointSize: Theme.pt(12) }
                    Mono { text: Spec.megabytes(Math.round(dlg.size[0]) * Math.round(dlg.size[1]) * dlg.bpp) }
                    Text { visible: dlg.changed; text: qsTr(" (trước "); color: Theme.t3; font.pointSize: Theme.pt(12) }
                    Mono { visible: dlg.changed; text: Spec.megabytes(dlg.base[0] * dlg.base[1] * dlg.bpp); color: Theme.t3 }
                    Text { visible: dlg.changed; text: ")"; color: Theme.t3; font.pointSize: Theme.pt(12) }
                }
                Row {
                    Text { text: qsTr("Kích thước: "); color: Theme.t2; font.pointSize: Theme.pt(12) }
                    Mono { text: Fmt.vn(Math.round(dlg.size[0])) + " × " + Fmt.vn(Math.round(dlg.size[1])) + " px"; color: Theme.t2 }
                }
            }
            RowLayout {
                spacing: 8
                RowLabel { text: qsTr("Fit To") }
                Dropdown {
                    implicitHeight: 28
                    Layout.fillWidth: true
                    options: Spec.FIT.map((f, i) => [i, f.name])
                    value: dlg.fit
                    onPicked: v => dlg.fitTo(v)
                }
            }
            RowLayout {
                spacing: 6
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Repeater {
                        model: 2
                        RowLayout {
                            id: dim
                            required property int index
                            spacing: 8
                            RowLabel { text: dim.index ? qsTr("Cao") : qsTr("Rộng") }
                            NumberField {
                                boxHeight: 28
                                Layout.fillWidth: true
                                value: dlg.shown(dim.index)
                                decimals: dlg.units[dim.index] === "px" ? 0 : 2
                                enabled: dlg.resample || !!Spec.PER_INCH[dlg.units[dim.index]]  // pixels stay without Resample
                                onMoved: v => dlg.setDim(dim.index, v)
                            }
                            Dropdown {
                                implicitHeight: 28
                                Layout.preferredWidth: 128
                                options: Spec.UNITS
                                value: dlg.units[dim.index]
                                onPicked: v => dlg.setUnit(dim.index, v)
                            }
                        }
                    }
                }
                Column {  // the chain between W and H, bracketed while they keep their ratio
                    Layout.preferredWidth: 20
                    Layout.alignment: Qt.AlignVCenter
                    Item {
                        width: 20
                        height: 16
                        Rectangle { x: 6; width: 8; height: 1; color: dlg.linked ? Theme.t3 : "transparent" }
                        Rectangle { x: 13; width: 1; height: 16; color: dlg.linked ? Theme.t3 : "transparent" }
                    }
                    IconButton {
                        x: -1
                        width: 22
                        height: 22
                        rotation: 90
                        icon.name: "link"
                        icon.width: 14
                        on: dlg.linked
                        enabled: dlg.resample  // always kept without it
                        ToolTip.text: qsTr("Giữ tỉ lệ W và H · Constrain Proportions")
                        Accessible.name: qsTr("Giữ tỉ lệ W và H")
                        onClicked: dlg.link = !dlg.link
                    }
                    Item {
                        width: 20
                        height: 16
                        Rectangle { x: 13; width: 1; height: 16; color: dlg.linked ? Theme.t3 : "transparent" }
                        Rectangle { x: 6; y: 15; width: 8; height: 1; color: dlg.linked ? Theme.t3 : "transparent" }
                    }
                }
            }
            RowLayout {
                spacing: 8
                RowLabel { text: qsTr("Độ phân giải") }
                NumberField {
                    boxHeight: 28
                    Layout.fillWidth: true
                    value: Spec.round(dlg.resUnit === "cm" ? dlg.ppi / 2.54 : dlg.ppi, decimals)
                    decimals: dlg.resUnit === "cm" ? 2 : 0
                    onMoved: v => dlg.setRes(v)
                }
                Dropdown {
                    implicitHeight: 28
                    Layout.preferredWidth: 128
                    options: Spec.RESOLUTION_UNITS
                    value: dlg.resUnit
                    onPicked: v => dlg.resUnit = v
                }
            }
            RowLayout {
                spacing: 8
                CheckRow {
                    Layout.preferredWidth: 96
                    text: qsTr("Resample")
                    checked: dlg.resample
                    onClicked: dlg.toggleResample()
                }
                Dropdown {
                    implicitHeight: 28
                    Layout.fillWidth: true
                    options: Spec.RESAMPLE
                    value: dlg.method
                    enabled: dlg.resample
                    onPicked: v => dlg.method = v
                }
            }
            CheckRow {
                text: qsTr("Scale Styles")
                checked: dlg.scaleStyles
                enabled: dlg.resample && !!dlg.doc && dlg.doc.layers.some(l => !!l.props.fx)  // only for layer styles
                onClicked: dlg.scaleStyles = !dlg.scaleStyles
            }
        }
    }
}
