import QtQuick
import QtQuick.Shapes
import "View.js" as View

// Clone Stamp and Healing Brush's source (docs/ui-tasks.md, U7; artboard Overlays): a crosshair where the brush takes
// from, and in the brush's ring the photo as it is there; a hint bottom left. Before the first stroke after Alt sets it,
// the crosshair stays on the source; from then on it goes along with the pointer, `offset` away. The owner lays it over
// the frame.
Item {
    id: cs
    property var view: null  // Viewport.view
    property var size: null  // Viewport.size
    property real docW: 0
    property real docH: 0
    property var source: null  // { x, y }, document px, where Alt clicked; null for none
    property var offset: null  // from the pointer to where it takes from, document px; null before the first stroke
    property var point: null  // the pointer, canvas px
    property real diameter: 100  // the brush's, document px
    property url photo  // the mock photo the document shows, covering it
    property size photoSize  // its pixels
    property string hint  // bottom left, "" for none

    readonly property bool ok: !!view && !!size && docW > 0 && docH > 0
    readonly property var at: !ok || !source ? null : offset && point ? View.toDoc(view, size, point) : null  // the pointer, document px
    // The crosshair: on the source, or the pointer's offset away once it is set.
    readonly property var cross: !ok || !source ? null : View.toCanvas(view, size, at ? { x: at.x + offset.x, y: at.y + offset.y } : source)
    // ponytail: no preview past 1.000 px across, the Canvas's size; a GPU canvas (V1) can do any.
    readonly property bool previewing: !!at && diameter * View.scale(view, size) >= 4 && diameter * View.scale(view, size) <= 1000 && photo.toString() !== ""

    Item {
        visible: !!cs.cross
        x: cs.cross ? Math.round(cs.cross.x) : 0
        y: cs.cross ? Math.round(cs.cross.y) : 0
        Rectangle { x: -9.5; y: -1.5; width: 19; height: 3; color: Qt.rgba(0, 0, 0, 0.5) }
        Rectangle { x: -1.5; y: -9.5; width: 3; height: 19; color: Qt.rgba(0, 0, 0, 0.5) }
        Rectangle { x: -8.5; y: -0.5; width: 17; height: 1; color: "#ffffff" }
        Rectangle { x: -0.5; y: -8.5; width: 1; height: 17; color: "#ffffff" }
    }
    Item {  // the ring: the photo where the brush takes from, faded a little, white and dark round it
        id: ring
        readonly property real s: cs.ok ? View.scale(cs.view, cs.size) : 1
        readonly property real r: cs.previewing ? cs.diameter * s / 2 : 0
        visible: cs.previewing
        x: cs.point ? cs.point.x - r - 2 : 0
        y: cs.point ? cs.point.y - r - 2 : 0
        width: 2 * r + 4
        height: 2 * r + 4
        // A Canvas, as Shape's fillItem draws nothing on the software renderer. ponytail: the photo as the document
        // covers it, not turned or flipped by Image Rotation.
        Canvas {
            id: lens
            anchors.fill: parent
            opacity: 0.85
            readonly property var src: cs.previewing ? { x: cs.at.x + cs.offset.x, y: cs.at.y + cs.offset.y, a: cs.view.angle, r: ring.r, s: ring.s } : null
            onSrcChanged: requestPaint()
            onImageLoaded: requestPaint()
            Component.onCompleted: loadImage(String(cs.photo))
            Connections {
                target: cs
                function onPhotoChanged() {
                    lens.loadImage(String(cs.photo))
                    lens.requestPaint()
                }
            }
            onPaint: {
                const ctx = getContext("2d"), q = src
                ctx.reset()
                if (!q || !isImageLoaded(String(cs.photo)) || cs.photoSize.width <= 0)
                    return
                // The photo covers the document, centred, in document px; the source's point at the ring's centre.
                const k = Math.max(cs.docW / cs.photoSize.width, cs.docH / cs.photoSize.height), w = cs.photoSize.width * k, h = cs.photoSize.height * k
                ctx.beginPath()
                ctx.arc(q.r + 2, q.r + 2, q.r, 0, 2 * Math.PI)
                ctx.clip()
                ctx.translate(q.r + 2, q.r + 2)
                ctx.rotate(q.a * Math.PI / 180)
                ctx.scale(q.s, q.s)
                ctx.drawImage(String(cs.photo), (cs.docW - w) / 2 - q.x, (cs.docH - h) / 2 - q.y, w, h)
            }
        }
        Shape {
            anchors.fill: parent
            ShapePath {
                strokeColor: Qt.rgba(0, 0, 0, 0.55)
                strokeWidth: 3
                fillColor: "transparent"
                PathAngleArc { centerX: ring.r + 2; centerY: ring.r + 2; radiusX: ring.r + 0.5; radiusY: ring.r + 0.5; startAngle: 0; sweepAngle: 360 }
            }
            ShapePath {
                strokeColor: "#ffffff"
                strokeWidth: 1
                fillColor: "transparent"
                PathAngleArc { centerX: ring.r + 2; centerY: ring.r + 2; radiusX: ring.r + 0.5; radiusY: ring.r + 0.5; startAngle: 0; sweepAngle: 360 }
            }
        }
    }
    CanvasBar.Hud {
        visible: cs.hint !== ""
        x: 12
        y: cs.height - 12 - height
        text: cs.hint
    }
}
