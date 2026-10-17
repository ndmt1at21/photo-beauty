import QtQuick
import QtQuick.Shapes
import "StrokeIcons.js" as StrokeIcons

// An icon the prototype draws itself (StrokeIcons.js), as its SVG: the 960 grid scaled to `size`,
// strokes 52/960 of it, centred in the item. Icon { name } shows these too, in the box of a glyph.
Item {
    id: si
    property string name
    property int size: 20
    property color color: Theme.t2
    readonly property var parts: StrokeIcons.K[name] ?? ({})
    implicitWidth: size
    implicitHeight: size

    Shape {
        x: (si.width - si.size) / 2
        y: (si.height - si.size) / 2
        width: 960
        height: 960
        scale: si.size / 960
        transformOrigin: Item.TopLeft
        transform: Translate { y: si.size }  // the viewBox starts at y = -960
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: si.color
            strokeWidth: 52
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: si.parts.s ?? "" }
        }
        ShapePath {
            strokeColor: si.color
            strokeWidth: 52
            fillColor: "transparent"
            strokeStyle: ShapePath.DashLine
            dashPattern: [64 / 52, 1]  // stroke-dasharray 64 52, in stroke widths
            capStyle: ShapePath.FlatCap
            joinStyle: ShapePath.MiterJoin
            miterLimit: 4
            PathSvg { path: si.parts.x ?? "" }
        }
        ShapePath {
            strokeColor: "transparent"
            fillColor: si.color
            fillRule: ShapePath.WindingFill
            PathSvg { path: si.parts.d ?? "" }
        }
    }
}
