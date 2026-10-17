pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes

// Levels' triangles (the prototype's HIST and OUT rows, Controls' LevelsBar) at `stops` (0..255), each filled with its
// of `fills`: under `histogram` (an SVG path in a 284 × 96 box, the image's, as the prototype draws it) for the input
// levels (black, grey, white) or Threshold's, under a black-to-white ramp (no histogram) for the output levels. Drag a
// triangle: started(), moved(index, value)…, committed(); the owner keeps them in order.
Item {
    id: lb
    property var stops: []
    property var fills: []
    property string histogram
    signal started()
    signal moved(int index, int value)
    signal committed()
    readonly property real band: histogram !== "" ? 96 : 12  // the histogram or the ramp, then the triangles

    implicitWidth: 284
    implicitHeight: band + 15

    function xOf(v) { return 4 + v * (width - 8) / 255 }

    Rectangle {
        visible: lb.histogram !== ""
        width: parent.width
        height: 96
        radius: 4
        color: Theme.bg
        Shape {
            width: 284
            height: 96
            transform: Scale { xScale: lb.width / 284 }
            ShapePath {
                strokeColor: "transparent"
                fillColor: Theme.t3
                PathSvg { path: lb.histogram }
            }
        }
    }
    Rectangle {
        visible: lb.histogram === ""
        width: parent.width
        height: 12
        radius: 2
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: "#000000" }
            GradientStop { position: 1; color: "#ffffff" }
        }
    }
    Repeater {
        model: lb.stops.length
        Shape {
            id: tri
            required property int index
            x: lb.xOf(lb.stops[index]) - 7
            y: lb.band + 1
            width: 14
            height: 12
            ShapePath {
                strokeWidth: 1
                strokeColor: lb.histogram === "" && lb.fills[tri.index] === "#ffffff" ? Theme.t3 : Theme.t2
                fillColor: lb.fills[tri.index] || Theme.t2
                startX: 7
                startY: 0
                PathLine { x: 14; y: 12 }
                PathLine { x: 0; y: 12 }
                PathLine { x: 7; y: 0 }
            }
        }
    }
    MouseArea {
        y: lb.band
        width: parent.width
        height: parent.height - lb.band
        preventStealing: true
        property int held: -1  // the triangle dragged
        property real ox  // where it was grabbed, from its tip
        property int v
        property bool active  // started() sent
        function end() {
            held = -1
            if (!active) return
            active = false
            lb.committed()
        }
        onPressed: e => {  // the nearest within 9 px; of two on one spot the later (white over black)
            let d = 9
            held = -1
            lb.stops.forEach((s, i) => {
                const k = Math.abs(lb.xOf(s) - e.x)
                if (k <= d) {
                    d = k
                    held = i
                }
            })
            if (held < 0) return
            ox = lb.xOf(lb.stops[held]) - e.x
            v = Math.round(lb.stops[held])
        }
        onPositionChanged: e => {
            if (held < 0) return
            const nv = Math.max(0, Math.min(255, Math.round((e.x + ox - 4) * 255 / (width - 8))))
            if (nv === v) return
            if (!active) {
                active = true
                lb.started()
            }
            v = nv
            lb.moved(held, nv)
        }
        onReleased: end()
        onCanceled: end()
    }
}
