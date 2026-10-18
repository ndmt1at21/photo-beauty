import QtQuick
import QtQuick.Shapes
import "View.js" as View

// The grid, the guides and, from 600 %, the pixel grid over the document (docs/ui-tasks.md, U7; artboard Overlays,
// the prototype's ov.grid, ov.pix, ov.guides): lines in the colours and styles of Thiết lập › Guide và lưới, inside the
// document's box like the prototype's. The owner lays it over the frame and gives it the view, the document's size and
// what to show.
Item {
    id: gg
    property var view: null  // Viewport.view
    property var size: null  // Viewport.size
    property real docW: 0
    property real docH: 0
    property bool grid: false
    property bool guides: false
    property bool pixelGrid: false  // drawn from a zoom of 6 (600 %)
    property real spacing: 100  // document px between the grid's lines
    property int subdivisions: 4
    property color gridColor: "#8a8a8a"
    property int gridStyle: 0  // 0 lines, 1 dashed, 2 dots
    property color guideColor: "#29d3f5"
    property int guideStyle: 0  // 0 lines, 1 dashed
    property var guidesX: []  // document x of each vertical guide
    property var guidesY: []

    readonly property bool ok: !!view && !!size && docW > 0 && docH > 0
    readonly property real s: ok ? View.scale(view, size) : 1  // logical px per document px
    readonly property var origin: ok ? View.toCanvas(view, size, { x: 0, y: 0 }) : ({ x: 0, y: 0 })
    readonly property bool pixels: ok && pixelGrid && view.zoom >= 6
    // The document pixels in the frame, whole: where the pixel grid goes.
    readonly property rect seen: {
        if (!pixels)
            return Qt.rect(0, 0, 0, 0)
        const xs = [], ys = []
        for (const p of [{ x: 0, y: 0 }, { x: size.w, y: 0 }, { x: 0, y: size.h }, { x: size.w, y: size.h }]) {
            const d = View.toDoc(view, size, p)
            xs.push(d.x)
            ys.push(d.y)
        }
        const x0 = Math.max(0, Math.floor(Math.min.apply(null, xs))), y0 = Math.max(0, Math.floor(Math.min.apply(null, ys)))
        return Qt.rect(x0, y0, Math.min(docW, Math.ceil(Math.max.apply(null, xs))) - x0, Math.min(docH, Math.ceil(Math.max.apply(null, ys))) - y0)
    }

    function at(v) { return Math.round(v * s) + 0.5 }  // a 1 px line on whole pixels of the box
    // Vertical lines at document x = a, a + d, … ≤ b and horizontal ones likewise, spanning the box (or [lo, hi]); keep(k)
    // says which k-th lines to draw.
    function lines(d, keep, x0, x1, y0, y1) {
        let p = ""
        if (!(d * s >= 1))
            return p
        for (let k = Math.ceil(x0 / d); k * d <= x1; k++)
            if (keep(k))
                p += "M" + at(k * d) + " " + y0 * s + "V" + y1 * s
        for (let k = Math.ceil(y0 / d); k * d <= y1; k++)
            if (keep(k))
                p += "M" + x0 * s + " " + at(k * d) + "H" + x1 * s
        return p
    }
    // Thiết lập's lines, each 1st, 2nd, 5th, 10th… while closer than 50 px: 62,5 px at 12,5 % as the prototype's ov.grid
    // (a line each 500 px). The subdivisions show from 8 px apart, the lines left out among them.
    readonly property real every: [1, 2, 5, 10, 20, 50, 100, 200, 500, 1000].find(m => spacing * s * m >= 50) || 1000
    readonly property real minor: spacing / Math.max(1, subdivisions)
    readonly property string majorPath: ok && grid ? lines(spacing * every, () => true, 0, docW, 0, docH) : ""
    readonly property string minorPath: ok && grid && subdivisions > 1 && minor * s >= 8 ? lines(minor, k => k % (subdivisions * every) !== 0, 0, docW, 0, docH) : ""
    readonly property string pixelPath: pixels ? lines(1, () => true, seen.x, seen.x + seen.width, seen.y, seen.y + seen.height) : ""
    readonly property string guidePath: {
        if (!ok || !guides)
            return ""
        let p = ""
        for (const x of guidesX)
            p += "M" + at(x) + " 0V" + docH * s
        for (const y of guidesY)
            p += "M0 " + at(y) + "H" + docW * s
        return p
    }

    Item {  // the document's box, turned with the view: one document pixel is s logical px
        x: Math.round(gg.origin.x)
        y: Math.round(gg.origin.y)
        width: gg.docW * gg.s
        height: gg.docH * gg.s
        rotation: gg.ok ? gg.view.angle : 0
        transformOrigin: Item.TopLeft
        Shape {
            anchors.fill: parent
            ShapePath {  // pixels first: the grid and the guides go over them
                strokeColor: Qt.rgba(0.5, 0.5, 0.5, 0.35)
                strokeWidth: 1
                fillColor: "transparent"
                PathSvg { path: gg.pixelPath }
            }
            ShapePath {
                strokeColor: Qt.alpha(gg.gridColor, 0.25)
                strokeWidth: 1
                fillColor: "transparent"
                strokeStyle: gg.gridStyle ? ShapePath.DashLine : ShapePath.SolidLine
                dashPattern: gg.gridStyle === 2 ? [1, 2] : [4, 4]
                PathSvg { path: gg.minorPath }
            }
            ShapePath {
                strokeColor: Qt.alpha(gg.gridColor, 0.5)
                strokeWidth: 1
                fillColor: "transparent"
                strokeStyle: gg.gridStyle ? ShapePath.DashLine : ShapePath.SolidLine
                dashPattern: gg.gridStyle === 2 ? [1, 2] : [4, 4]
                PathSvg { path: gg.majorPath }
            }
            ShapePath {
                strokeColor: gg.guideColor
                strokeWidth: 1
                fillColor: "transparent"
                strokeStyle: gg.guideStyle ? ShapePath.DashLine : ShapePath.SolidLine
                dashPattern: [4, 4]
                PathSvg { path: gg.guidePath }
            }
        }
    }
}
