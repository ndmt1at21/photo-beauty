import QtQuick
import QtQuick.Shapes

// The Eyedropper's sampling ring (docs/ui-tasks.md, U7; artboard Overlays; Show Sampling Ring of its bar): the new
// colour in the top half, the one it replaces below, in grey rings, a crosshair in the middle, both named beside it.
// The owner lays it over the frame and shows it while the button is down.
Item {
    id: sr
    property var point: null  // the canvas point sampled
    property color newColor: "#ffffff"
    property color oldColor: "#ffffff"

    function hex(c) { return "#" + [c.r, c.g, c.b].map(v => ("0" + Math.round(v * 255).toString(16)).slice(-2)).join("").toUpperCase() }

    Item {
        visible: !!sr.point
        x: sr.point ? Math.round(sr.point.x) : 0
        y: sr.point ? Math.round(sr.point.y) : 0
        Shape {
            x: -70
            y: -70
            width: 140
            height: 140
            ShapePath {  // grey around
                strokeColor: "#8f8f8f"
                strokeWidth: 6
                fillColor: "transparent"
                PathAngleArc { centerX: 70; centerY: 70; radiusX: 63; radiusY: 63; startAngle: 0; sweepAngle: 360 }
            }
            ShapePath {  // new above
                strokeColor: sr.newColor
                strokeWidth: 20
                fillColor: "transparent"
                capStyle: ShapePath.FlatCap
                PathAngleArc { centerX: 70; centerY: 70; radiusX: 50; radiusY: 50; startAngle: 180; sweepAngle: 180 }
            }
            ShapePath {  // old below
                strokeColor: sr.oldColor
                strokeWidth: 20
                fillColor: "transparent"
                capStyle: ShapePath.FlatCap
                PathAngleArc { centerX: 70; centerY: 70; radiusX: 50; radiusY: 50; startAngle: 0; sweepAngle: 180 }
            }
            ShapePath {  // grey within
                strokeColor: "#8f8f8f"
                strokeWidth: 2
                fillColor: "transparent"
                PathAngleArc { centerX: 70; centerY: 70; radiusX: 42; radiusY: 42; startAngle: 0; sweepAngle: 360 }
            }
        }
        Rectangle { x: -9.5; y: -1.5; width: 19; height: 3; color: Qt.rgba(0, 0, 0, 0.5) }
        Rectangle { x: -1.5; y: -9.5; width: 3; height: 19; color: Qt.rgba(0, 0, 0, 0.5) }
        Rectangle { x: -8.5; y: -0.5; width: 17; height: 1; color: "#ffffff" }
        Rectangle { x: -0.5; y: -8.5; width: 1; height: 17; color: "#ffffff" }
        CanvasBar.Hud { x: 70; y: -54; text: qsTr("mới %1").arg(sr.hex(sr.newColor)) }
        CanvasBar.Hud { x: 70; y: 34; text: qsTr("cũ %1").arg(sr.hex(sr.oldColor)) }
    }
}
