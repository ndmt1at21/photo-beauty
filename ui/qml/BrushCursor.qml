import QtQuick
import QtQuick.Shapes
import "View.js" as View
import "Fmt.js" as Fmt

// The painting tools' pointer (docs/ui-tasks.md, U7; artboard Overlays, the prototype's cur; Thiết lập › Con trỏ): the
// brush tip's outline at its size on the canvas, white between two dark lines, a crosshair in it; Caps Lock leaves the
// crosshair alone. Alt dragged with the right button (Ctrl Option with the left on macOS) changes the size across and
// the hardness up (softer) and down, over a disc of the preview colour; the owner keeps the values (adjusted()). It
// hides the system pointer while it draws one and takes no other press: clicks, drags and the wheel go to the canvas.
// The owner lays it over the frame; its pointer handlers go on pointerArea, the canvas's MouseArea under it, when given:
// a HoverHandler above that area would take its hover (Viewport's rulers mark the pointer by it).
Item {
    id: bc
    property var view: null  // Viewport.view
    property var size: null  // Viewport.size
    property real diameter: 400  // document px
    property real hardness: 0  // %
    property real roundness: 1  // the tip's height over its width
    property real tipAngle: 0  // °
    property bool square: false
    property int tip: 3  // Thiết lập › Khi vẽ: 0 Standard (the system's pointer), 1 Precise, 2 Normal Brush Tip, 3 Full Size Brush Tip
    property bool crosshair: true  // in the tip
    property bool crosshairWhilePainting: false
    property bool precise: capsLock  // the crosshair alone
    property color previewColor: "#ff0000"
    property real minDiameter: 1
    property real maxDiameter: 5000
    property Item pointerArea: null  // the canvas's MouseArea under it (Viewport's), else itself
    signal adjusted(real diameter, real hardness)

    // Under the pointer, canvas px.
    readonly property var point: !ok ? null : press.active ? mapFromItem(press.parent, press.point.position) : hover.hovered ? mapFromItem(hover.parent, hover.point.position) : null
    readonly property bool painting: press.active
    readonly property bool adjusting: drag.start !== null
    // ponytail: Caps Lock as its presses toggle it, so wrong when the app starts with it on; the platform's state is C++.
    property bool capsLock: false

    readonly property bool ok: !!view && !!size
    readonly property real s: ok ? View.scale(view, size) : 1
    readonly property bool cross: tip === 1 || precise || (crosshairWhilePainting && painting)  // only the crosshair
    readonly property bool drawn: ok && tip > 0  // else Standard: the canvas's own pointer
    // The outline's width on the canvas: Normal Brush Tip rings the part painted at 50 % or more.
    // Even, so the 1 px ring falls on whole pixels round a whole-pixel centre, as the prototype's 1 px border.
    readonly property real d: 2 * Math.max(2, Math.round(diameter * s * (tip === 2 ? (1 + hardness / 100) / 2 : 1) / 2))
    readonly property bool mac: Qt.platform.os === "osx"

    Connections {
        target: bc.Window.contentItem ? bc.Window.contentItem.Keys : null
        function onPressed(e) {
            if (e.key === Qt.Key_CapsLock && !e.isAutoRepeat)
                bc.capsLock = !bc.capsLock
        }
    }
    HoverHandler {
        id: hover
        parent: bc.pointerArea ?? bc
        enabled: bc.drawn && bc.visible
        cursorShape: Qt.BlankCursor
    }
    PointHandler {  // passive: the press still reaches the canvas
        id: press
        parent: bc.pointerArea ?? bc
        enabled: bc.drawn && bc.visible
        acceptedButtons: Qt.LeftButton
    }
    MouseArea {  // Alt and the right button (macOS: Ctrl Option and the left one) dragged: size across, hardness up and down
        id: drag
        property var start: null  // { x, y, d, h }
        anchors.fill: parent
        enabled: bc.ok  // Standard's pointer too
        acceptedButtons: bc.mac ? Qt.LeftButton : Qt.RightButton
        onPressed: m => {
            m.accepted = !!(m.modifiers & Qt.AltModifier) && (!bc.mac || !!(m.modifiers & Qt.MetaModifier))
            if (m.accepted)
                start = { x: m.x, y: m.y, d: bc.diameter, h: bc.hardness }
        }
        onPositionChanged: m => {
            if (start)
                bc.adjusted(Math.round(Math.max(bc.minDiameter, Math.min(bc.maxDiameter, start.d + 2 * (m.x - start.x) / bc.s))), Math.round(Math.max(0, Math.min(100, start.h + m.y - start.y))))
        }
        onReleased: start = null
        onCanceled: start = null
    }

    Shape {  // the tip's outline: dark, white, dark
        id: outline
        readonly property real w: bc.d + 4
        visible: bc.drawn && !!bc.point && !bc.cross && !bc.adjusting
        x: bc.point ? Math.round(bc.point.x) - w / 2 : 0
        y: bc.point ? Math.round(bc.point.y) - w / 2 : 0
        width: w
        height: w
        rotation: bc.tipAngle
        readonly property string path: {
            const c = w / 2, rx = bc.d / 2 - 0.5, ry = rx * (bc.square ? 1 : bc.roundness)
            return bc.square ? "M" + (c - rx) + " " + (c - ry) + "H" + (c + rx) + "V" + (c + ry) + "H" + (c - rx) + "Z"
                             : "M" + (c - rx) + " " + c + "A" + rx + " " + ry + " 0 1 0 " + (c + rx) + " " + c + "A" + rx + " " + ry + " 0 1 0 " + (c - rx) + " " + c + "Z"
        }
        ShapePath {
            strokeColor: Qt.rgba(0, 0, 0, 0.6)
            strokeWidth: 3
            fillColor: "transparent"
            PathSvg { path: outline.path }
        }
        ShapePath {
            strokeColor: Qt.rgba(1, 1, 1, 0.95)
            strokeWidth: 1
            fillColor: "transparent"
            PathSvg { path: outline.path }
        }
    }
    Item {  // the crosshair: white, a dark line round it
        visible: bc.drawn && !!bc.point && (bc.cross || bc.crosshair) && !bc.adjusting
        x: bc.point ? Math.round(bc.point.x) : 0
        y: bc.point ? Math.round(bc.point.y) : 0
        Rectangle { x: -5.5; y: -1.5; width: 11; height: 3; color: Qt.rgba(0, 0, 0, 0.45) }
        Rectangle { x: -1.5; y: -5.5; width: 3; height: 11; color: Qt.rgba(0, 0, 0, 0.45) }
        Rectangle { x: -4.5; y: -0.5; width: 9; height: 1; color: "#ffffff" }
        Rectangle { x: -0.5; y: -4.5; width: 1; height: 9; color: "#ffffff" }
    }
    Shape {  // adjusting: the tip as a disc of the preview colour, hard to its hardness, ringed white
        readonly property real r: bc.diameter * bc.s / 2
        visible: bc.adjusting
        x: drag.start ? drag.start.x - r : 0
        y: drag.start ? drag.start.y - r : 0
        width: 2 * r
        height: 2 * r
        ShapePath {
            strokeColor: Qt.rgba(1, 1, 1, 0.9)
            strokeWidth: 1
            fillGradient: RadialGradient {
                centerX: bc.diameter * bc.s / 2
                centerY: centerX
                centerRadius: centerX
                focalX: centerX
                focalY: centerX
                GradientStop { position: 0; color: Qt.alpha(bc.previewColor, 0.55) }
                GradientStop { position: Math.min(0.999, bc.hardness / 100); color: Qt.alpha(bc.previewColor, 0.55) }
                GradientStop { position: 1; color: Qt.alpha(bc.previewColor, 0) }
            }
            PathAngleArc {
                centerX: bc.diameter * bc.s / 2
                centerY: centerX
                radiusX: Math.max(0, centerX - 0.5)
                radiusY: radiusX
                startAngle: 0
                sweepAngle: 360
            }
        }
    }
    CanvasBar.Hud {
        visible: bc.adjusting
        x: drag.start ? Math.round(Math.max(4, Math.min(bc.width - width - 4, drag.start.x - width / 2))) : 0  // kept in the frame
        y: drag.start ? Math.round(drag.start.y + bc.diameter * bc.s / 2) + 10 : 0
        text: qsTr("Cỡ %1 px · Độ cứng %2%").arg(Fmt.vn(bc.diameter)).arg(Math.round(bc.hardness))
    }
}
