pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes
import "View.js" as View

// A gradient edited on the canvas (docs/ui-tasks.md, U7; artboard Overlays): its line white on dark, colour stops in
// their colours (the one picked square, its place and colour beside it), a diamond midway between two. A stop drags
// along the line (stopMoved()), away from it to go (stopRemoved()), a double click asks for its colour (stopPicked());
// the owner keeps the stops. The owner lays it over the frame.
Item {
    id: gl
    property var view: null  // Viewport.view
    property var size: null  // Viewport.size
    property var from: null  // document px
    property var to: null
    property var stops: []  // [{ pos: 0 to 1, color: CSS colour }]
    property int current: -1  // the stop picked
    signal stopMoved(int index, real pos)
    signal stopRemoved(int index)
    signal stopPicked(int index)

    readonly property bool ok: !!view && !!size && !!from && !!to
    readonly property var a: ok ? View.toCanvas(view, size, from) : ({ x: 0, y: 0 })
    readonly property var b: ok ? View.toCanvas(view, size, to) : ({ x: 0, y: 0 })
    function along(t) { return { x: a.x + t * (b.x - a.x), y: a.y + t * (b.y - a.y) } }
    // Where canvas point p falls on the line (0 to 1), and how far from it.
    function project(p) {
        const dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy
        const t = l2 ? Math.max(0, Math.min(1, ((p.x - a.x) * dx + (p.y - a.y) * dy) / l2)) : 0, q = along(t)
        return { t: t, off: Math.hypot(p.x - q.x, p.y - q.y) }
    }
    function label(c) { return c === "transparent" ? qsTr("trong suốt") : String(c).toUpperCase() }

    Shape {
        anchors.fill: parent
        visible: gl.ok
        ShapePath {
            strokeColor: Qt.rgba(0, 0, 0, 0.55)
            strokeWidth: 3
            fillColor: "transparent"
            PathSvg { path: "M" + gl.a.x + " " + gl.a.y + "L" + gl.b.x + " " + gl.b.y }
        }
        ShapePath {
            strokeColor: "#ffffff"
            strokeWidth: 1
            fillColor: "transparent"
            PathSvg { path: "M" + gl.a.x + " " + gl.a.y + "L" + gl.b.x + " " + gl.b.y }
        }
    }
    Repeater {  // midpoints
        model: gl.ok ? Math.max(0, gl.stops.length - 1) : 0
        Rectangle {
            required property int index
            readonly property var p: gl.along((gl.stops[index].pos + gl.stops[index + 1].pos) / 2)
            x: p.x - 4.5
            y: p.y - 4.5
            width: 9
            height: 9
            rotation: 45
            color: "#ffffff"
            border.color: "#1c1c1e"
        }
    }
    Repeater {  // by count: a stop dragged changes the stops, and a new model would remake the one under the pointer
        model: gl.ok ? gl.stops.length : 0
        Rectangle {
            id: stop
            required property int index
            readonly property var modelData: gl.stops[index] ?? { pos: 0, color: "transparent" }
            readonly property bool picked: index === gl.current
            readonly property var p: gl.along(modelData.pos)
            x: p.x - width / 2
            y: p.y - height / 2
            width: picked ? 21 : 18
            height: width
            radius: picked ? 4.5 : 9
            color: modelData.color
            border.color: "#ffffff"
            border.width: picked ? 3 : 2
            MouseArea {
                property bool away: false
                anchors.fill: parent
                anchors.margins: -3
                cursorShape: Qt.PointingHandCursor
                onPressed: gl.current = stop.index
                onPositionChanged: m => {
                    const q = gl.project(mapToItem(gl, m.x, m.y))
                    away = q.off > 40 && gl.stops.length > 2
                    if (!away)
                        gl.stopMoved(stop.index, q.t)
                }
                onReleased: {
                    if (away)
                        gl.stopRemoved(stop.index)
                    away = false
                }
                onDoubleClicked: gl.stopPicked(stop.index)
            }
        }
    }
    CanvasBar.Hud {
        readonly property var stop: gl.ok ? gl.stops[gl.current] : undefined
        readonly property var p: stop ? gl.along(stop.pos) : ({ x: 0, y: 0 })
        visible: !!stop
        x: Math.round(p.x) + 20
        y: Math.round(p.y) + 5
        text: stop ? Math.round(stop.pos * 100) + "% · " + gl.label(stop.color) : ""
    }
}
