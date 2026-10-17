import QtQuick
import QtQuick.Shapes
import "View.js" as View
import "Mock.js" as Mock

// The selection's marching ants and Quick Mask (docs/ui-tasks.md, U7; artboard Overlays, the prototype's ov.ants,
// ov.qm): a mock selection shape (Mock.SHAPES) drawn at the canvas's scale inside the document's box, white under black
// dashes that step 10 times a second, only this Shape redrawn; the part not selected tinted with `mask`. Nothing runs
// without a selection. The owner lays it over the frame.
Item {
    id: an
    property var view: null  // Viewport.view
    property var size: null  // Viewport.size
    property real docW: 0
    property real docH: 0
    property var shape: null  // a key of Mock.SHAPES, null for no selection
    property bool inverse: false
    property bool ants: true  // the marching ants
    property color mask: "transparent"  // the part not selected, Quick Mask's Qt.rgba(1, 0, 0, 0.5)
    property bool faint: false  // no pixel over 50 % selected: no ants (the owner says why)
    readonly property int phase: tick.n % 8  // where the dashes are, a step every 100 ms

    readonly property bool ok: !!view && !!size && docW > 0 && docH > 0 && !!shape
    readonly property real s: ok ? View.scale(view, size) : 1  // logical px per document px
    readonly property var origin: ok ? View.toCanvas(view, size, { x: 0, y: 0 }) : ({ x: 0, y: 0 })
    readonly property real cw: Math.round(docW * s)  // the document on the canvas, whole px as the prototype's W, H
    readonly property real ch: Math.round(docH * s)
    // The prototype's shapeD at the canvas's scale, and the document's edge half a pixel in.
    readonly property string outline: ok ? Mock.shapePath(shape, cw, ch) : ""
    readonly property string border: "M0.5 0.5h" + (cw - 1) + "v" + (ch - 1) + "h" + (1 - cw) + "Z"
    readonly property string antsPath: ok && ants && !faint ? (inverse ? border + (shape === "all" ? "" : outline) : outline) : ""
    readonly property string maskPath: ok && mask.a > 0 ? (inverse ? (shape === "all" ? border : outline) : border + outline) : ""

    Item {  // the document's box, turned with the view
        x: Math.round(an.origin.x)
        y: Math.round(an.origin.y)
        width: an.cw
        height: an.ch
        rotation: an.ok ? an.view.angle : 0
        transformOrigin: Item.TopLeft
        Shape {
            anchors.fill: parent
            ShapePath {
                strokeWidth: -1
                fillColor: an.mask
                fillRule: ShapePath.OddEvenFill
                PathSvg { path: an.maskPath }
            }
            ShapePath {
                strokeColor: "#ffffff"
                strokeWidth: 1
                fillColor: "transparent"
                PathSvg { path: an.antsPath }
            }
            ShapePath {
                strokeColor: "#000000"
                strokeWidth: 1
                fillColor: "transparent"
                strokeStyle: ShapePath.DashLine
                dashPattern: [4, 4]
                dashOffset: an.phase
                PathSvg { path: an.antsPath }
            }
        }
    }
    Timer {
        id: tick
        property int n: 0
        interval: 100
        repeat: true
        running: an.visible && an.antsPath !== ""
        onTriggered: n++
    }
}
