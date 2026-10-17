pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes
import "View.js" as View
import "Fmt.js" as Fmt

// A live shape's handles (docs/ui-tasks.md, U7; artboard Overlays): its outline white then dark, 8 size handles on its
// box, a dot in each corner that drags the corners' radius (radiusDragged()), its size beside it. The owner lays it over
// the frame.
Item {
    id: sh
    property var view: null  // Viewport.view
    property var size: null  // Viewport.size
    property var box: null  // { x, y, w, h }, document px
    property real radius: -1  // the corners', document px; below 0 no dots
    property bool ellipse: false
    signal radiusDragged(real radius)

    readonly property bool ok: !!view && !!size && !!box
    readonly property real s: ok ? View.scale(view, size) : 1
    readonly property var origin: ok ? View.toCanvas(view, size, { x: box.x, y: box.y }) : ({ x: 0, y: 0 })
    readonly property var corner: ok ? View.toCanvas(view, size, { x: box.x + box.w, y: box.y + box.h }) : ({ x: 0, y: 0 })  // bottom right
    readonly property real w: ok ? Math.round(box.w * s) : 0
    readonly property real h: ok ? Math.round(box.h * s) : 0
    readonly property real r: Math.min(Math.max(0, radius) * s, w / 2, h / 2)
    // The outline e px outside the shape's edge, 4 px in from the top left (where the Shape drawing it starts).
    function outline(e) {
        const x = 4 - e, y = 4 - e, W = w + 2 * e, H = h + 2 * e
        if (ellipse)
            return "M" + x + " " + (y + H / 2) + "A" + W / 2 + " " + H / 2 + " 0 1 0 " + (x + W) + " " + (y + H / 2) + "A" + W / 2 + " " + H / 2 + " 0 1 0 " + x + " " + (y + H / 2) + "Z"
        const q = r > 0 ? r + e : 0, arc = (tx, ty) => q > 0 ? "A" + q + " " + q + " 0 0 1 " + tx + " " + ty : "L" + tx + " " + ty
        return "M" + (x + q) + " " + y + "H" + (x + W - q) + arc(x + W, y + q) + "V" + (y + H - q) + arc(x + W - q, y + H) + "H" + (x + q) + arc(x, y + H - q) + "V" + (y + q) + arc(x + q, y) + "Z"
    }

    Item {  // the shape's box, turned with the view
        id: frame
        visible: sh.ok
        x: Math.round(sh.origin.x)
        y: Math.round(sh.origin.y)
        width: sh.w
        height: sh.h
        rotation: sh.ok ? sh.view.angle : 0
        transformOrigin: Item.TopLeft
        Shape {
            x: -4  // the software renderer drops what lies outside a Shape: it starts where the outline does
            y: -4
            width: frame.width + 8
            height: frame.height + 8
            ShapePath {
                strokeColor: Qt.rgba(0, 0, 0, 0.45)
                strokeWidth: 1
                fillColor: "transparent"
                PathSvg { path: sh.ok ? sh.outline(1.5) : "" }
            }
            ShapePath {
                strokeColor: "#ffffff"
                strokeWidth: 1
                fillColor: "transparent"
                PathSvg { path: sh.ok ? sh.outline(0.5) : "" }
            }
        }
        Repeater {
            model: [0, 1, 2, 3, 5, 6, 7, 8]
            Rectangle {
                required property int modelData
                x: Math.round(frame.width * (modelData % 3) / 2) - 5
                y: Math.round(frame.height * Math.floor(modelData / 3) / 2) - 5
                width: 10
                height: 10
                color: "#1c1c1e"
                Rectangle { x: 1; y: 1; width: 8; height: 8; color: "#ffffff" }
            }
        }
        Repeater {  // the corners' radius: a dot at the arc's centre, dragged along the diagonal
            model: sh.radius >= 0 && !sh.ellipse ? [[0, 0], [1, 0], [0, 1], [1, 1]] : []
            Rectangle {
                id: dot
                required property var modelData
                readonly property real at: Math.max(sh.r, 6)  // kept off the corner's handle
                x: (modelData[0] ? frame.width - at : at) - 4.5
                y: (modelData[1] ? frame.height - at : at) - 4.5
                width: 9
                height: 9
                radius: 4.5
                color: "#1c1c1e"
                Rectangle { x: 1; y: 1; width: 7; height: 7; radius: 3.5; color: "#ffffff" }
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -3
                    cursorShape: Qt.PointingHandCursor
                    onPositionChanged: m => {
                        if (!pressed)
                            return
                        const p = mapToItem(frame, m.x, m.y), dx = dot.modelData[0] ? frame.width - p.x : p.x, dy = dot.modelData[1] ? frame.height - p.y : p.y
                        sh.radiusDragged(Math.round(Math.max(0, Math.min((dx + dy) / 2, frame.width / 2, frame.height / 2)) / sh.s))
                    }
                }
            }
        }
    }
    CanvasBar.Hud {
        visible: sh.ok
        x: Math.round(sh.corner.x) + 12
        y: Math.round(sh.corner.y) - height
        text: qsTr("W %1 px H %2 px").arg(Fmt.vn(Math.round(sh.box ? sh.box.w : 0))).arg(Fmt.vn(Math.round(sh.box ? sh.box.h : 0)))
              + (sh.radius >= 0 && !sh.ellipse ? qsTr(" Bo góc %1 px").arg(Fmt.vn(Math.round(sh.radius))) : "")
    }
}
