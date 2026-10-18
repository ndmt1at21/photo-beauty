pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import "Color.js" as Color
import "Fmt.js" as Fmt

// Colour field (Controls: ColorField; docs/ui-tasks.md, U11): the Color Picker's square and strip, the strip one channel
// of the colour (`channel`, the dialog's radios: H S Bh, R G Bl, L a b) and the square the other two of its model, as
// Photoshop lays them out; `wheel` instead is the Color panel's wheel, hue round and saturation out. Drag them, or
// ← → ↑ ↓ when focused (Shift ten steps). The colour is the owner's: h 0–360 (kept for greys), s, v 0–1; value protocol
// of the shared controls: started(), moved({ h, s, v })… while the mouse drags, committed({ h, s, v }); a key, the three.
Item {
    id: f
    property real h
    property real s
    property real v
    property string channel: "H"
    property bool wheel: false
    property int size: 256
    signal started()
    signal moved(var c)
    signal committed(var c)

    implicitWidth: wheel ? size : size + 36
    implicitHeight: size

    // [model, the strip's axis, the square's x axis, its y axis]
    readonly property var spec: ({ H: ["hsb", 0, 1, 2], S: ["hsb", 1, 0, 2], Bh: ["hsb", 2, 0, 1], R: ["rgb", 0, 2, 1], G: ["rgb", 1, 2, 0],
                                   Bl: ["rgb", 2, 0, 1], L: ["lab", 0, 1, 2], a: ["lab", 1, 2, 0], b: ["lab", 2, 1, 0] })[channel] || ["hsb", 0, 1, 2]
    readonly property var range: ({ hsb: [[0, 360], [0, 1], [0, 1]], rgb: [[0, 255], [0, 255], [0, 255]], lab: [[0, 100], [-128, 127], [-128, 127]] })[spec[0]]
    readonly property var steps: spec[0] === "hsb" ? [1, 0.01, 0.01] : [1, 1, 1]
    readonly property var names: ({ hsb: [qsTr("Hue"), qsTr("Saturation"), qsTr("Brightness")], rgb: [qsTr("Red"), qsTr("Green"), qsTr("Blue")],
                                    lab: [qsTr("Lab L"), qsTr("Lab a"), qsTr("Lab b")] })[spec[0]]
    readonly property var t: inModel(h, s, v)  // the colour in the channel's model
    readonly property color hue: Color.css(Color.hsv2rgb(h, 1, 1))

    function inModel(h, s, v) { const c = Color.hsv2rgb(h, s, v); return spec[0] === "hsb" ? [h, s, v] : spec[0] === "rgb" ? c : Color.rgb2lab(c) }
    function rgbOf(t) { return spec[0] === "hsb" ? Color.hsv2rgb(t[0], t[1], t[2]) : spec[0] === "rgb" ? t : Color.lab2rgb(t) }
    function hsvOf(t) { return spec[0] === "hsb" ? { h: t[0], s: t[1], v: t[2] } : Color.rgb2hsv(rgbOf(t), h) }
    function norm(i, x) { return (x - range[i][0]) / (range[i][1] - range[i][0]) }
    function lerp(i, u) { return range[i][0] + (range[i][1] - range[i][0]) * Color.clamp(u, 0, 1) }
    function shown(i, x) { return names[i] + " " + (spec[0] !== "hsb" ? Fmt.vn(x) : i ? Math.round(x * 100) + "%" : Math.round(x) + "°") }

    QtObject {
        id: st
        property var t0  // the colour as the press found it, in the model: the axes not dragged stay so
        property var last
        property bool on  // a drag that changed the colour
    }
    function go(c) {  // a drag's step
        if (c.h === h && c.s === s && c.v === v) return
        if (!st.on) {
            st.on = true
            started()
        }
        st.last = c
        moved(c)
    }
    function end() {
        if (!st.on) return
        st.on = false
        committed(st.last)
    }
    function nudge(c) {  // a key's step
        if (c.h === h && c.s === s && c.v === v) return
        started()
        moved(c)
        committed(c)
    }
    function nudgeAxis(i, n) {
        const nt = t.slice()
        nt[i] = Color.clamp(nt[i] + n * steps[i], range[i][0], range[i][1])
        nudge(hsvOf(nt))
    }
    // the wheel: saturation full at 70% of the radius, where the white over the hues has faded out
    function wheelAt(x, y) {
        const r = size / 2, dx = x - r, dy = y - r
        return { h: (Math.atan2(dy, dx) * 180 / Math.PI + 360) % 360, s: Math.min(1, Math.hypot(dx, dy) / (0.7 * r)), v: v }
    }

    component Marker: Rectangle {  // 14 px, white ringed in dark
        width: 14
        height: 14
        radius: 7
        color: "transparent"
        border.width: 2
        border.color: "#ffffff"
        Rectangle { anchors.fill: parent; anchors.margins: -1; radius: width / 2; color: "transparent"; border.color: "#80000000" }
    }

    // The square (a Control, for FocusRing)
    T.Control {
        id: square
        visible: !f.wheel
        width: f.size
        height: f.size
        focusPolicy: f.wheel ? Qt.NoFocus : Qt.TabFocus
        Accessible.role: Accessible.Slider
        Accessible.name: qsTr("Ô màu: %1, %2").arg(f.shown(f.spec[2], f.t[f.spec[2]])).arg(f.shown(f.spec[3], f.t[f.spec[3]]))
        Keys.onLeftPressed: e => f.nudgeAxis(f.spec[2], e.modifiers & Qt.ShiftModifier ? -10 : -1)
        Keys.onRightPressed: e => f.nudgeAxis(f.spec[2], e.modifiers & Qt.ShiftModifier ? 10 : 1)
        Keys.onUpPressed: e => f.nudgeAxis(f.spec[3], e.modifiers & Qt.ShiftModifier ? 10 : 1)
        Keys.onDownPressed: e => f.nudgeAxis(f.spec[3], e.modifiers & Qt.ShiftModifier ? -10 : -1)
        Rectangle {  // H: white to the hue, then black from the bottom, as the prototype's CSS
            visible: f.channel === "H"
            anchors.fill: parent
            radius: 4
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: "#ffffff" }
                GradientStop { position: 1; color: f.hue }
            }
            Rectangle {
                anchors.fill: parent
                radius: 4
                gradient: Gradient {
                    GradientStop { position: 0; color: "#00000000" }
                    GradientStop { position: 1; color: "#000000" }
                }
            }
        }
        Field {  // the other channels: the colours worked out at 64 × 64, smoothed up
            visible: f.channel !== "H"
            n: 64
            key: [f.channel, f.t[f.spec[1]]]
            at: (x, y) => {
                const c = f.t.slice()
                c[f.spec[2]] = f.lerp(f.spec[2], x)
                c[f.spec[3]] = f.lerp(f.spec[3], 1 - y)
                return f.rgbOf(c)
            }
        }
        Marker {
            x: f.norm(f.spec[2], f.t[f.spec[2]]) * f.size - 7
            y: (1 - f.norm(f.spec[3], f.t[f.spec[3]])) * f.size - 7
        }
        MouseArea {
            anchors.fill: parent
            preventStealing: true
            cursorShape: Qt.CrossCursor
            function at(e) {
                const c = st.t0.slice()
                c[f.spec[2]] = f.lerp(f.spec[2], e.x / width)
                c[f.spec[3]] = f.lerp(f.spec[3], 1 - e.y / height)
                f.go(f.hsvOf(c))
            }
            onPressed: e => {
                square.forceActiveFocus(Qt.MouseFocusReason)
                st.t0 = f.t.slice()
                at(e)
            }
            onPositionChanged: e => at(e)
            onReleased: f.end()
            onCanceled: f.end()
        }
    }

    // The strip
    T.Control {
        id: strip
        visible: !f.wheel
        x: f.size + 16
        width: 20
        height: f.size
        focusPolicy: f.wheel ? Qt.NoFocus : Qt.TabFocus
        Accessible.role: Accessible.Slider
        Accessible.name: qsTr("Dải %1").arg(f.shown(f.spec[1], f.t[f.spec[1]]))
        Keys.onUpPressed: e => f.nudgeAxis(f.spec[1], e.modifiers & Qt.ShiftModifier ? 10 : 1)
        Keys.onDownPressed: e => f.nudgeAxis(f.spec[1], e.modifiers & Qt.ShiftModifier ? -10 : -1)
        Rectangle {  // the hues, as the prototype's
            visible: f.channel === "H"
            anchors.fill: parent
            radius: 3
            gradient: Gradient {
                GradientStop { position: 0; color: "#ff0000" }
                GradientStop { position: 1 / 6; color: "#ff00ff" }
                GradientStop { position: 2 / 6; color: "#0000ff" }
                GradientStop { position: 3 / 6; color: "#00ffff" }
                GradientStop { position: 4 / 6; color: "#00ff00" }
                GradientStop { position: 5 / 6; color: "#ffff00" }
                GradientStop { position: 1; color: "#ff0000" }
            }
        }
        Field {
            visible: f.channel !== "H"
            n: 1
            rows: 64
            key: [f.channel, f.t[f.spec[2]], f.t[f.spec[3]]]
            at: (x, y) => {
                const c = f.t.slice()
                c[f.spec[1]] = f.lerp(f.spec[1], 1 - y)
                return f.rgbOf(c)
            }
        }
        Rectangle {  // the handle: white, 4 px past the strip each side, in a faint dark ring 1 px outside it (box-shadow)
            x: -5
            y: (1 - f.norm(f.spec[1], f.t[f.spec[1]])) * f.size - 3
            width: parent.width + 10
            height: 6
            radius: 3
            color: "#80000000"
            Rectangle { anchors.fill: parent; anchors.margins: 1; radius: 2; color: "#ffffff" }
        }
        MouseArea {
            anchors.fill: parent
            anchors.leftMargin: -4
            anchors.rightMargin: -4
            preventStealing: true
            function at(e) {
                const c = st.t0.slice()
                c[f.spec[1]] = f.lerp(f.spec[1], 1 - e.y / height)
                f.go(f.hsvOf(c))
            }
            onPressed: e => {
                strip.forceActiveFocus(Qt.MouseFocusReason)
                st.t0 = f.t.slice()
                at(e)
            }
            onPositionChanged: e => at(e)
            onReleased: f.end()
            onCanceled: f.end()
        }
    }

    // The wheel
    T.Control {
        id: disc
        visible: f.wheel
        width: f.size
        height: f.size
        focusPolicy: f.wheel ? Qt.TabFocus : Qt.NoFocus
        Accessible.role: Accessible.Slider
        Accessible.name: qsTr("Vòng màu: Hue %1°, Saturation %2%").arg(Math.round(f.h)).arg(Math.round(f.s * 100))
        Keys.onLeftPressed: e => f.nudge({ h: (f.h + 360 - (e.modifiers & Qt.ShiftModifier ? 10 : 1)) % 360, s: f.s, v: f.v })
        Keys.onRightPressed: e => f.nudge({ h: (f.h + (e.modifiers & Qt.ShiftModifier ? 10 : 1)) % 360, s: f.s, v: f.v })
        Keys.onUpPressed: e => f.nudge({ h: f.h, s: Math.min(1, f.s + (e.modifiers & Qt.ShiftModifier ? 0.1 : 0.01)), v: f.v })
        Keys.onDownPressed: e => f.nudge({ h: f.h, s: Math.max(0, f.s - (e.modifiers & Qt.ShiftModifier ? 0.1 : 0.01)), v: f.v })
        Canvas {  // hues clockwise from the right, white from the middle out to 70%, as the prototype's conic and radial CSS
            anchors.fill: parent
            visible: f.wheel
            onPaint: {
                const ctx = getContext("2d"), r = width / 2
                ctx.reset()
                const g = ctx.createConicalGradient(r, r, 0), hues = ["#ff0000", "#ff00ff", "#0000ff", "#00ffff", "#00ff00", "#ffff00", "#ff0000"]
                hues.forEach((c, i) => g.addColorStop(i / 6, c))  // counter-clockwise from the right
                ctx.beginPath()
                ctx.arc(r, r, r, 0, 2 * Math.PI)
                ctx.fillStyle = g
                ctx.fill()
                const w = ctx.createRadialGradient(r, r, 0, r, r, r)
                w.addColorStop(0, "#ffffff")
                w.addColorStop(0.7, "#00ffffff")
                ctx.fillStyle = w
                ctx.fill()
            }
        }
        Marker {
            readonly property real k: 0.7 * f.s * f.size / 2
            width: 12
            height: 12
            radius: 6
            x: f.size / 2 + k * Math.cos(f.h * Math.PI / 180) - 6
            y: f.size / 2 + k * Math.sin(f.h * Math.PI / 180) - 6
        }
        MouseArea {
            anchors.fill: parent
            preventStealing: true
            cursorShape: Qt.CrossCursor
            onPressed: e => {
                disc.forceActiveFocus(Qt.MouseFocusReason)
                f.go(f.wheelAt(e.x, e.y))
            }
            onPositionChanged: e => f.go(f.wheelAt(e.x, e.y))
            onReleased: f.end()
            onCanceled: f.end()
        }
    }

    // n × rows colours of at(x, y) (0–1, at the middle of each cell) drawn small, then scaled over the parent, smoothed.
    // A fillRect a cell, not createImageData(): in Qt 6.11 each createImageData() leaves the JS heap's accounting so that,
    // after some panels made and destroyed, every allocation runs a whole GC (~2 ms; Main made 18 times was enough).
    component Field: Canvas {
        id: fc
        property int n
        property int rows: n
        property var key
        property var at
        width: n
        height: rows
        transform: Scale { xScale: fc.parent.width / fc.n; yScale: fc.parent.height / fc.rows }
        smooth: true
        onKeyChanged: requestPaint()
        onVisibleChanged: requestPaint()
        onPaint: {
            if (!visible)  // hidden (the H channel, the wheel): nothing to work out
                return
            const ctx = getContext("2d")
            for (let y = 0; y < rows; y++)
                for (let x = 0; x < n; x++) {
                    const c = at((x + 0.5) / n, (y + 0.5) / rows)
                    ctx.fillStyle = Qt.rgba(c[0] / 255, c[1] / 255, c[2] / 255, 1)
                    ctx.fillRect(x, y, 1, 1)
                }
        }
    }
}
