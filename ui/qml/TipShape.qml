import QtQuick
import QtQuick.Shapes

// The angle and roundness of a brush tip (docs/ui-tasks.md, U13; TipShape of Controls.dc.html, 84 px in Brush Settings
// of PanelsB.dc.html): the tip's ellipse in a ring, turned to `angle` (°, anticlockwise from the right as Photoshop's,
// −180–180) and flattened to `roundness` (%, 0–100). Drag the arrow, or anywhere off the two dots, to turn it (Shift:
// steps of 15°); drag a dot to flatten it. Value protocol of the shared controls: started(), moved(angle, roundness) at
// each change, committed(angle, roundness) when the mouse lets go. Glyph draws a tip as the brush pickers show one.
Item {
    id: ts
    property real angle
    property real roundness: 100
    signal started()
    signal moved(real angle, real roundness)
    signal committed(real angle, real roundness)

    implicitWidth: 84
    implicitHeight: 84

    readonly property real k: Math.min(width, height) / 120  // the artboard's box is 120
    readonly property real rad: angle * Math.PI / 180
    readonly property real half: 46 * k  // the ellipse's long half axis
    // The dot at one end of the short axis, from the centre; the other is opposite.
    readonly property point dot: Qt.point(Math.sin(rad) * half * roundness / 100, Math.cos(rad) * half * roundness / 100)
    readonly property bool dragging: area.active

    onAngleChanged: canvas.requestPaint()
    onRoundnessChanged: canvas.requestPaint()
    Connections {
        target: Theme
        function onDarkChanged() { canvas.requestPaint() }
    }

    Canvas {
        id: canvas
        anchors.fill: parent
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            const c = getContext("2d"), k = ts.k
            c.reset()
            c.translate(width / 2, height / 2)
            c.lineWidth = 1
            c.strokeStyle = Theme.t4
            c.beginPath()
            c.arc(0, 0, 52 * k, 0, 2 * Math.PI)
            c.moveTo(-52 * k, 0)
            c.lineTo(52 * k, 0)
            c.moveTo(0, -52 * k)
            c.lineTo(0, 52 * k)
            c.stroke()
            c.rotate(-ts.rad)
            c.save()
            c.scale(1, Math.max(ts.roundness / 100, 0.005))
            c.beginPath()
            c.arc(0, 0, ts.half, 0, 2 * Math.PI)
            c.restore()
            c.fillStyle = Theme.faint
            c.fill()
            c.lineWidth = 1.5
            c.strokeStyle = Theme.t1
            c.stroke()
            c.beginPath()  // the arrow, its head past the ring as the artboard's
            c.moveTo(0, 0)
            c.lineTo(50 * k, 0)
            c.stroke()
            c.fillStyle = Theme.t1
            c.beginPath()
            c.moveTo(58 * k, 0)
            c.lineTo(47.5 * k, -5.5 * k)
            c.lineTo(47.5 * k, 5.5 * k)
            c.closePath()
            c.fill()
            const r = ts.half * ts.roundness / 100, dot = Math.max(3, 4 * k)
            c.beginPath()
            c.arc(0, -r, dot, 0, 2 * Math.PI)
            c.arc(0, r, dot, 0, 2 * Math.PI)
            c.fill()
        }
    }
    MouseArea {
        id: area
        property int mode  // 1 turns, 2 flattens
        property bool active  // a drag that changed something
        property var last: []
        anchors.fill: parent
        preventStealing: true
        function go(e) {
            const dx = e.x - width / 2, dy = e.y - height / 2
            let a = ts.angle, r = ts.roundness
            if (mode === 1) {
                if (Math.hypot(dx, dy) < 4)
                    return
                a = Math.atan2(-dy, dx) * 180 / Math.PI
                a = e.modifiers & Qt.ShiftModifier ? Math.round(a / 15) * 15 : Math.round(a)
            } else {
                r = Math.round(Math.min(100, Math.abs(dx * Math.sin(ts.rad) + dy * Math.cos(ts.rad)) / ts.half * 100))
            }
            const was = active ? last : [ts.angle, ts.roundness]
            if (a === was[0] && r === was[1])
                return
            if (!active) {
                active = true
                ts.started()
            }
            last = [a, r]
            ts.moved(a, r)
        }
        function end() {
            if (!active)
                return
            active = false
            ts.committed(last[0], last[1])
        }
        onPressed: e => {
            const dx = e.x - width / 2, dy = e.y - height / 2, grab = Math.max(7, 8 * ts.k)
            mode = Math.hypot(dx - ts.dot.x, dy - ts.dot.y) <= grab || Math.hypot(dx + ts.dot.x, dy + ts.dot.y) <= grab ? 2 : 1
            go(e)
        }
        onPositionChanged: e => go(e)
        onReleased: end()
        onCanceled: end()
    }

    // A brush tip as the prototype's CSS draws one in the pickers (BrushPicker's Tip, U5): under 34 % hardness soft (t1
    // to 12 %, clear at 70 % of the half diagonal), under 67 % mid (45 %, 72 %), else solid; a circle, or a square
    // (corners 3); turned to `angle` (°, anticlockwise) and flattened to `roundness` (0–1); dots: the grain tip's three.
    component Glyph: Item {
        id: g
        property real hard
        property bool square
        property real angle
        property real roundness: 1
        property bool dots
        readonly property color clear: Qt.rgba(Theme.t1.r, Theme.t1.g, Theme.t1.b, 0)
        transform: [
            Scale { origin.x: g.width / 2; origin.y: g.height / 2; yScale: g.roundness },
            Rotation { origin.x: g.width / 2; origin.y: g.height / 2; angle: -g.angle }
        ]
        Rectangle {
            visible: !g.dots && g.hard >= 67
            anchors.fill: parent
            radius: g.square ? 3 : width / 2
            color: Theme.t1
        }
        Shape {
            visible: !g.dots && g.hard < 67
            anchors.fill: parent
            ShapePath {
                strokeColor: "transparent"
                fillGradient: RadialGradient {
                    centerX: g.width / 2
                    centerY: g.height / 2
                    centerRadius: g.width / Math.SQRT2
                    focalX: g.width / 2
                    focalY: g.height / 2
                    GradientStop { position: 0; color: Theme.t1 }
                    GradientStop { position: g.hard >= 34 ? 0.45 : 0.12; color: Theme.t1 }
                    GradientStop { position: g.hard >= 34 ? 0.72 : 0.7; color: g.clear }
                }
                PathAngleArc {
                    centerX: g.width / 2
                    centerY: g.height / 2
                    radiusX: g.width / 2
                    radiusY: g.height / 2
                    sweepAngle: 360
                }
            }
        }
        Rectangle { visible: g.dots; x: 0.3 * g.width - 2.5; y: 0.3 * g.height - 2.5; width: 5; height: 5; radius: 2.5; color: Theme.t1 }
        Rectangle { visible: g.dots; x: 0.62 * g.width - 2.5; y: 0.55 * g.height - 2.5; width: 5; height: 5; radius: 2.5; color: Theme.t1 }
        Rectangle { visible: g.dots; x: 0.4 * g.width - 2.5; y: 0.75 * g.height - 2.5; width: 5; height: 5; radius: 2.5; color: Theme.t1 }
    }
}
