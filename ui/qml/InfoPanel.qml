pragma ComponentBehavior: Bound
import QtQuick
import "Fmt.js" as Fmt
import "ToolSpec.js" as ToolSpec

// The Info panel (docs/ui-tasks.md, U12; PanelsB, the inf part of Main.dc.html), opened beside the icon column: two
// colour readouts, RGB and HSB, of the foreground colour as the prototype's; X, Y; W, H of the document while Free
// Transform is on; the document's memory line; the hint of the tool in use (ToolSpec.hint()). X, Y: the document pixel
// under the pointer on the canvas (Tools.pointer, U6), "—" off it.
Item {
    id: panel
    readonly property Doc doc: Docs.current
    readonly property var rgb: {
        const n = parseInt(Tools.fg.slice(1), 16)
        return [(n >> 16) & 255, (n >> 8) & 255, n & 255]
    }
    readonly property var hsb: {  // the prototype's rgb2hsb(): H °, S and B %
        const r = rgb[0] / 255, g = rgb[1] / 255, b = rgb[2] / 255, mx = Math.max(r, g, b), d = mx - Math.min(r, g, b)
        const h = !d ? 0 : ((mx === r ? (g - b) / d % 6 : mx === g ? (b - r) / d + 2 : (r - g) / d + 4) * 60 + 360) % 360
        return [h, mx ? d / mx * 100 : 0, mx * 100]
    }
    readonly property var reads: [
        { name: qsTr("Kiểu đọc màu 1: RGB"), rows: [["R", String(rgb[0])], ["G", String(rgb[1])], ["B", String(rgb[2])]] },
        { name: qsTr("Kiểu đọc màu 2: HSB"), rows: [["H", Math.round(hsb[0]) + "°"], ["S", Math.round(hsb[1]) + "%"], ["B", Math.round(hsb[2]) + "%"]] }
    ]
    readonly property string hint: ToolSpec.hint(Tools.tool)
    property var menu: []
    readonly property real line: 11.5 * 1.35 * Theme.fontScale  // CSS's line box of an 11,5 px row (line-height 1.35)
    implicitWidth: 280
    implicitHeight: col.height + 18

    Column {  // .sec
        id: col
        x: 12
        y: 8
        width: parent.width - 24
        spacing: 8
        Row {
            spacing: 6
            Repeater {
                model: panel.reads
                Row {
                    id: read
                    required property var modelData
                    width: (col.width - 6) / 2
                    spacing: 6
                    Icon {
                        width: 22
                        height: 22
                        name: "colorize"
                        size: 15
                        Accessible.name: read.modelData.name
                    }
                    Pairs { pairs: read.modelData.rows }
                }
            }
        }
        Rule {
            Row {
                y: 7
                spacing: 6
                Row {
                    width: (col.width - 6) / 2
                    spacing: 8
                    Icon { name: "plus"; size: 16; color: Theme.t3 }
                    Pairs { pairs: [["X", Tools.pointer ? Fmt.vn(Tools.pointer.x) : "—"], ["Y", Tools.pointer ? Fmt.vn(Tools.pointer.y) : "—"]] }
                }
                Row {
                    spacing: 8
                    Icon { name: "crop_free"; size: 16; color: Theme.t3 }
                    Pairs { pairs: [["W", panel.doc && Tools.xf ? Fmt.vn(panel.doc.w) : "—"], ["H", panel.doc && Tools.xf ? Fmt.vn(panel.doc.h) : "—"]] }
                }
            }
        }
        Rule {
            Row {
                y: 7
                Text {
                    height: panel.line
                    text: qsTr("Tài liệu: ")
                    color: Theme.t2
                    font.pointSize: Theme.pt(11.5)
                    verticalAlignment: Text.AlignVCenter
                }
                Text {
                    height: panel.line
                    text: panel.doc && panel.doc.mem || "—"
                    color: Theme.t2
                    font.family: Theme.mono
                    font.pointSize: Theme.pt(11.5)
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
        Text {
            width: col.width
            text: panel.hint
            color: Theme.t3
            font.pointSize: Theme.pt(11)
            wrapMode: Text.Wrap
            lineHeightMode: Text.FixedHeight
            lineHeight: 11 * 1.35 * Theme.fontScale
        }
    }

    // A readout's rows [label, value]: labels 14 wide in t3, values in mono.
    component Pairs: Grid {
        id: g
        required property var pairs
        columns: 2
        columnSpacing: 6
        rowSpacing: 2
        Repeater {
            model: g.pairs.length * 2
            Text {
                required property int index
                width: index % 2 ? implicitWidth : 14
                height: panel.line
                verticalAlignment: Text.AlignVCenter
                text: g.pairs[index >> 1][index % 2]
                color: index % 2 ? Theme.t1 : Theme.t3
                font.family: index % 2 ? Theme.mono : Theme.sans
                font.pointSize: Theme.pt(11.5)
            }
        }
    }
    // A part under a 1 px rule in p2, 6 below it: its content is placed from y 7.
    component Rule: Item {
        width: parent ? parent.width : 0
        height: childrenRect.y + childrenRect.height
        Rectangle { width: parent.width; height: 1; color: Theme.p2 }
    }
}
