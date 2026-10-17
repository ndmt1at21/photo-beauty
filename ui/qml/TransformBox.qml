pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes
import "View.js" as View
import "Fmt.js" as Fmt

// Free Transform and Warp's box (docs/ui-tasks.md, U7; artboard Overlays, the prototype's ov.xf, ov.warp): the layer's
// box scaled and turned about its reference point as the options bar has it, 8 handles, the reference point, W, H and
// the angle beside it, the turning arrow as the pointer outside it; Warp: its grid, the corners' points and Bezier
// handles. The owner lays it over the frame.
Item {
    id: tb
    property var view: null  // Viewport.view
    property var size: null  // Viewport.size
    property real docW: 0
    property real docH: 0
    property var box: ({ x: 0, y: 0, w: docW, h: docH })  // the layer's bounds, document px
    property real scaleX: 1  // 1 is 100 %
    property real scaleY: 1
    property real angle: 0  // °, clockwise, about the reference point
    property int pivot: 4  // the reference point, 0–8 as AnchorGrid's: rows from the top left
    property bool warp: false
    property int cells: 3  // Warp's grid, n × n
    property Item pointerArea: null  // the canvas's MouseArea under it (Viewport's): the hover handler goes on it, else on this

    readonly property bool ok: !!view && !!size && docW > 0 && docH > 0 && !!box && box.w > 0 && box.h > 0
    readonly property real s: ok ? View.scale(view, size) : 1
    readonly property var origin: ok ? View.toCanvas(view, size, { x: 0, y: 0 }) : ({ x: 0, y: 0 })
    readonly property var p: ok ? { x: box.x + pivot % 3 / 2 * box.w, y: box.y + Math.floor(pivot / 3) / 2 * box.h } : ({ x: 0, y: 0 })  // document px
    // A point of the box (fx, fy: 0 to 1 across it) transformed: scaled and turned about the reference point.
    function at(fx, fy) {
        const a = angle * Math.PI / 180, dx = (box.x + fx * box.w - p.x) * scaleX, dy = (box.y + fy * box.h - p.y) * scaleY
        return { x: p.x + dx * Math.cos(a) - dy * Math.sin(a), y: p.y + dx * Math.sin(a) + dy * Math.cos(a) }
    }
    readonly property var topLeft: ok ? at(0, 0) : ({ x: 0, y: 0 })
    readonly property var topRight: ok ? View.toCanvas(view, size, at(1, 0)) : ({ x: 0, y: 0 })
    readonly property bool turning: ok && hover.hovered && !inside(View.toDoc(view, size, mapFromItem(hover.parent, hover.point.position)))
    function inside(q) {  // q, a document point, in the transformed box
        const a = -angle * Math.PI / 180, dx = q.x - p.x, dy = q.y - p.y
        const fx = (p.x + (dx * Math.cos(a) - dy * Math.sin(a)) / scaleX - box.x) / box.w, fy = (p.y + (dx * Math.sin(a) + dy * Math.cos(a)) / scaleY - box.y) / box.h
        return fx >= 0 && fx <= 1 && fy >= 0 && fy <= 1
    }

    HoverHandler {
        id: hover
        parent: tb.pointerArea ?? tb
        enabled: tb.ok && tb.visible && !tb.warp
        cursorShape: tb.turning ? Qt.BlankCursor : undefined  // else the canvas's
    }
    Item {  // the document's box, turned with the view
        x: Math.round(tb.origin.x)
        y: Math.round(tb.origin.y)
        rotation: tb.ok ? tb.view.angle : 0
        transformOrigin: Item.TopLeft
        visible: tb.ok
        Item {  // the layer's box as transformed
            id: lb
            x: tb.topLeft.x * tb.s
            y: tb.topLeft.y * tb.s
            width: tb.ok ? tb.box.w * Math.abs(tb.scaleX) * tb.s : 0
            height: tb.ok ? tb.box.h * Math.abs(tb.scaleY) * tb.s : 0
            rotation: tb.angle
            transformOrigin: Item.TopLeft
            Rectangle {  // its edge: white, then dark, outside it
                x: -2
                y: -2
                width: parent.width + 4
                height: parent.height + 4
                color: "transparent"
                border.color: Qt.rgba(0, 0, 0, 0.45)
                visible: !tb.warp
                Rectangle {
                    x: 1
                    y: 1
                    width: parent.width - 2
                    height: parent.height - 2
                    color: "transparent"
                    border.color: "#ffffff"
                }
            }
            Shape {  // Warp: its grid over the edge, white on dark, and the corners' Bezier handles
                id: mesh
                anchors.fill: parent
                visible: tb.warp
                readonly property string grid: {
                    let d = "M0 0H" + lb.width + "V" + lb.height + "H0Z"
                    for (let k = 1; k < tb.cells; k++)
                        d += "M" + lb.width * k / tb.cells + " 0V" + lb.height + "M0 " + lb.height * k / tb.cells + "H" + lb.width
                    return d
                }
                readonly property real hx: lb.width * 0.13  // a corner's Bezier handle along the edge, where the artboard has it
                ShapePath {
                    strokeColor: Qt.rgba(0, 0, 0, 0.55)
                    strokeWidth: 3
                    fillColor: "transparent"
                    PathSvg { path: mesh.grid }
                }
                ShapePath {
                    strokeColor: "#ffffff"
                    strokeWidth: 1
                    fillColor: "transparent"
                    PathSvg { path: mesh.grid + "M0 0h" + mesh.hx + "M" + lb.width + " 0h" + -mesh.hx + "M0 " + lb.height + "h" + mesh.hx + "M" + lb.width + " " + lb.height + "h" + -mesh.hx }
                }
            }
            Repeater {  // Warp's Bezier handles
                model: tb.warp ? [[0, 0], [1, 0], [0, 1], [1, 1]] : []
                Rectangle {
                    required property var modelData
                    x: (modelData[0] ? lb.width - mesh.hx : mesh.hx) - 3.5
                    y: lb.height * modelData[1] - 3.5
                    width: 7
                    height: 7
                    radius: 3.5
                    color: "#ffffff"
                    border.color: "#1c1c1e"
                }
            }
            Repeater {  // handles: 8, Warp's 4 corners
                model: tb.warp ? [0, 2, 6, 8] : [0, 1, 2, 3, 5, 6, 7, 8]
                Rectangle {
                    required property int modelData
                    x: Math.round(lb.width * (modelData % 3) / 2) - 5
                    y: Math.round(lb.height * Math.floor(modelData / 3) / 2) - 5
                    width: 10
                    height: 10
                    color: "#1c1c1e"
                    Rectangle { x: 1; y: 1; width: 8; height: 8; color: "#ffffff" }
                }
            }
            Rectangle {  // the reference point
                visible: !tb.warp
                x: Math.round(lb.width * (tb.pivot % 3) / 2) - 8
                y: Math.round(lb.height * Math.floor(tb.pivot / 3) / 2) - 8
                width: 16
                height: 16
                radius: 8
                color: "transparent"
                border.color: Qt.rgba(0, 0, 0, 0.5)
                Rectangle {
                    x: 1
                    y: 1
                    width: 14
                    height: 14
                    radius: 7
                    color: "transparent"
                    border.color: "#ffffff"
                }
            }
        }
    }
    CanvasBar.Hud {
        visible: tb.ok && !tb.warp
        x: Math.round(tb.topRight.x) + 36
        y: Math.round(tb.topRight.y) - 21
        text: "W " + Fmt.vn(tb.scaleX * 100, 1) + "% H " + Fmt.vn(tb.scaleY * 100, 1) + "%\n∠ " + Fmt.vn(tb.angle, 1) + "°"
    }
    Loader {  // the pointer outside the box (made when shown: Viewport's test finds the canvas's own Icon first)
        active: tb.turning
        readonly property point at: tb.turning ? tb.mapFromItem(hover.parent, hover.point.position) : Qt.point(0, 0)
        x: at.x - 11
        y: at.y - 11
        sourceComponent: Icon {
            name: "rotate_right"
            size: 22
            color: "#ffffff"
            style: Text.Outline
            styleColor: Qt.rgba(0, 0, 0, 0.9)
        }
    }
}
