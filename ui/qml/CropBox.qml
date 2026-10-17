pragma ComponentBehavior: Bound
import QtQuick
import "View.js" as View
import "Fmt.js" as Fmt

// The Crop tool's box (docs/ui-tasks.md, U7; artboard Overlays, the prototype's ov.crop): the part cut away shaded,
// the rule of thirds, corner and side handles, the ratio and size below it, the turning arrow as the pointer outside it.
// The owner lays it over the frame.
Item {
    id: cr
    property var view: null  // Viewport.view
    property var size: null  // Viewport.size
    property real docW: 0
    property real docH: 0
    property var box: null  // { x, y, w, h }, document px
    property string ratio  // the HUD's first part, "4:5"
    property Item pointerArea: null  // the canvas's MouseArea under it (Viewport's): the hover handler goes on it, else on this

    readonly property bool ok: !!view && !!size && docW > 0 && docH > 0 && !!box
    readonly property real s: ok ? View.scale(view, size) : 1
    readonly property var origin: ok ? View.toCanvas(view, size, { x: 0, y: 0 }) : ({ x: 0, y: 0 })
    // The box on the canvas, whole px of the document's box as the prototype's cr.
    readonly property rect r: ok ? Qt.rect(Math.round(box.x * s), Math.round(box.y * s), Math.round(box.w * s), Math.round(box.h * s)) : Qt.rect(0, 0, 0, 0)
    readonly property var corner: ok ? View.toCanvas(view, size, { x: box.x + box.w, y: box.y + box.h }) : ({ x: 0, y: 0 })  // bottom right
    readonly property string hud: ok ? (ratio ? ratio + " · " : "") + Fmt.vn(Math.round(box.w)) + " × " + Fmt.vn(Math.round(box.h)) + " px" : ""
    // The pointer outside the box: it turns the crop there, and shows so.
    readonly property bool turning: ok && hover.hovered && !inside(View.toDoc(view, size, mapFromItem(hover.parent, hover.point.position)))
    function inside(p) { return p.x >= box.x && p.x <= box.x + box.w && p.y >= box.y && p.y <= box.y + box.h }

    HoverHandler {
        id: hover
        parent: cr.pointerArea ?? cr
        enabled: cr.ok && cr.visible
        cursorShape: cr.turning ? Qt.BlankCursor : undefined  // else the canvas's
    }
    Item {  // the document's box, turned with the view
        id: page
        x: Math.round(cr.origin.x)
        y: Math.round(cr.origin.y)
        width: Math.round(cr.docW * cr.s)
        height: Math.round(cr.docH * cr.s)
        rotation: cr.ok ? cr.view.angle : 0
        transformOrigin: Item.TopLeft
        visible: cr.ok
        // The shade: above, below, left, right of the box.
        Repeater {
            model: [[0, 0, page.width, cr.r.y], [0, cr.r.y + cr.r.height, page.width, page.height - cr.r.y - cr.r.height], [0, cr.r.y, cr.r.x, cr.r.height],
                [cr.r.x + cr.r.width, cr.r.y, page.width - cr.r.x - cr.r.width, cr.r.height]]
            Rectangle {
                required property var modelData
                x: modelData[0]
                y: modelData[1]
                width: Math.max(0, modelData[2])
                height: Math.max(0, modelData[3])
                color: Qt.rgba(0, 0, 0, 0.62)
            }
        }
        Item {
            id: crop
            x: cr.r.x
            y: cr.r.y
            width: cr.r.width
            height: cr.r.height
            Rectangle {  // its edge, just outside it
                x: -1
                y: -1
                width: parent.width + 2
                height: parent.height + 2
                color: "transparent"
                border.color: Qt.rgba(1, 1, 1, 0.9)
            }
            Repeater {  // thirds
                model: [[1, 0], [2, 0], [0, 1], [0, 2]]
                Rectangle {
                    required property var modelData
                    x: modelData[0] ? Math.round(crop.width * modelData[0] / 3) : 0
                    y: modelData[1] ? Math.round(crop.height * modelData[1] / 3) : 0
                    width: modelData[0] ? 1 : crop.width
                    height: modelData[0] ? crop.height : 1
                    color: Qt.rgba(1, 1, 1, 0.5)
                }
            }
            // Handles, 3 px thick, 3 px outside the box: the corners' arms, then the sides' middles.
            Repeater {
                model: {
                    const w = cr.r.width, h = cr.r.height, mx = Math.round(w / 2) - 9, my = Math.round(h / 2) - 9
                    return [[-3, -3, 18, 3], [-3, -3, 3, 18], [w - 15, -3, 18, 3], [w, -3, 3, 18], [-3, h, 18, 3], [-3, h - 15, 3, 18], [w - 15, h, 18, 3], [w, h - 15, 3, 18],
                        [mx, -3, 18, 3], [mx, h, 18, 3], [-3, my, 3, 18], [w, my, 3, 18]]
                }
                Rectangle {
                    required property var modelData
                    x: modelData[0] - 1
                    y: modelData[1] - 1
                    width: modelData[2] + 2
                    height: modelData[3] + 2
                    color: Qt.rgba(0, 0, 0, 0.6)
                    Rectangle { x: 1; y: 1; width: parent.width - 2; height: parent.height - 2; color: "#ffffff" }
                }
            }
        }
    }
    CanvasBar.Hud {
        visible: cr.ok
        x: Math.round(cr.corner.x)
        y: Math.round(cr.corner.y) + 8
        text: cr.hud
    }
    Loader {  // the pointer outside the box (made when shown: Viewport's test finds the canvas's own Icon first)
        active: cr.turning
        readonly property point at: cr.turning ? cr.mapFromItem(hover.parent, hover.point.position) : Qt.point(0, 0)
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
