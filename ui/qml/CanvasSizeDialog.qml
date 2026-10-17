pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import "FilterSpec.js" as Spec
import "Fmt.js" as Fmt

// Image › Canvas Size (build/proto/ImageSize.dc.html, plan.md's Hộp thoại): the new size, or with Relative the change,
// in any unit; the anchor the image keeps its place by; the colour of what is added (only with a Background layer, as
// Photoshop). OK is one History step on Docs.current (canvasSize, anchor and colour kept with it); Hủy, Esc, × leave it.
DialogBox {
    id: dlg
    property var params: ({})
    readonly property Doc doc: Docs.current
    readonly property var base: doc ? [doc.w, doc.h] : [1, 1]
    readonly property real ppi: doc ? doc.ppi : 72
    readonly property real bpp: doc ? Spec.bytesPerPixel(doc.mode, doc.bits) : 3
    property var size: base.slice()  // px, rounded on OK
    property var units: ["px", "px"]  // of W and H, keys of Spec.UNITS
    property bool relative: false
    property int anchor: 4  // 0–8 by rows from the top left, as AnchorGrid's
    property string extension: "bg"  // of Spec.EXTENSION
    property color other: "#e9c7b3"  // Khác…
    readonly property bool hasBackground: !!doc && doc.layers.some(l => !!l.props.background)
    readonly property string extColor: extension === "fg" ? Tools.fg : extension === "bg" ? Tools.bg : extension === "other" ? other.toString() : Spec.GREYS[extension]

    title: qsTr("Canvas Size")
    boxWidth: 440
    closable: true
    buttons: [{ text: qsTr("Hủy"), act: () => dlg.close() }, { text: qsTr("OK"), primary: true, act: () => dlg.apply() }]

    // W (0) or H (1) as its field shows it: the new size, or the change with Relative.
    function shown(i) { return Spec.round(Spec.toUnit(size[i] - (relative ? base[i] : 0), units[i], ppi, base[i]), units[i] === "px" ? 0 : 2) }
    function setDim(i, x) {
        const v = Math.max(1, Math.min(300000, Spec.fromUnit(x, units[i], ppi, base[i]) + (relative ? base[i] : 0)))
        size = i ? [size[0], v] : [v, size[1]]
    }
    function apply() {
        const w = Math.round(size[0]), h = Math.round(size[1])
        if (doc && (w !== doc.w || h !== doc.h))
            doc.run({ op: "canvasSize", w: w, h: h, anchor: anchor, color: hasBackground ? extColor : null })
        close()
    }

    component Mono: Text {
        color: Theme.t1
        font.family: Theme.mono
        font.pointSize: Theme.pt(12)
    }
    component Plain: Text {
        color: Theme.t2
        font.pointSize: Theme.pt(12)
    }

    ColumnLayout {
        width: parent.width
        spacing: 12
        Column {
            spacing: 2
            Row {
                Plain { text: qsTr("Cỡ hiện tại: "); color: Theme.t1 }
                Mono { text: Spec.megabytes(dlg.base[0] * dlg.base[1] * dlg.bpp) }
            }
            Row {
                Plain { text: qsTr("Rộng ") }
                Mono { text: Fmt.vn(dlg.base[0]) + " px"; color: Theme.t2 }
                Plain { text: qsTr(" · Cao ") }
                Mono { text: Fmt.vn(dlg.base[1]) + " px"; color: Theme.t2 }
            }
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: Theme.ln
        }
        Row {
            Plain { text: qsTr("Cỡ mới: "); color: Theme.t1 }
            Mono { text: Spec.megabytes(Math.round(dlg.size[0]) * Math.round(dlg.size[1]) * dlg.bpp) }
        }
        Repeater {
            model: 2
            RowLayout {
                id: dim
                required property int index
                spacing: 8
                Plain {
                    Layout.preferredWidth: 72
                    text: dim.index ? qsTr("Cao") : qsTr("Rộng")
                }
                NumberField {
                    Layout.fillWidth: true
                    value: dlg.shown(dim.index)
                    decimals: dlg.units[dim.index] === "px" ? 0 : 2
                    onMoved: v => dlg.setDim(dim.index, v)
                }
                Dropdown {
                    Layout.preferredWidth: 120
                    options: Spec.UNITS
                    value: dlg.units[dim.index]
                    onPicked: v => dlg.units = dim.index ? [dlg.units[0], v] : [v, dlg.units[1]]
                }
            }
        }
        CheckRow {
            Layout.leftMargin: 80
            text: qsTr("Relative")
            checked: dlg.relative
            onClicked: dlg.relative = !dlg.relative
        }
        RowLayout {
            spacing: 16
            Plain {
                Layout.preferredWidth: 64
                text: qsTr("Điểm neo")
            }
            AnchorGrid {
                cell: 26
                anchor: dlg.anchor
                onPicked: i => dlg.anchor = i
            }
        }
        RowLayout {
            spacing: 8
            Plain {
                Layout.preferredWidth: 150
                text: qsTr("Màu phần mở rộng")
            }
            Dropdown {
                Layout.fillWidth: true
                options: Spec.EXTENSION
                value: dlg.extension
                enabled: dlg.hasBackground
                onPicked: v => dlg.extension = v
            }
            FillDialog.ColorButton {
                Layout.preferredWidth: 28
                enabled: dlg.hasBackground
                color: dlg.extColor
                title: qsTr("Màu phần mở rộng")
                onChosen: c => {
                    dlg.other = c
                    dlg.extension = "other"
                }
            }
        }
    }
}
