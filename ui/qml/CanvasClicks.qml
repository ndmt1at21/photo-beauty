import QtQuick
import "ToolSpec.js" as ToolSpec

// A click on the mock canvas with a tool (docs/ui-tasks.md, U7b; the prototype's onCanvas): the selection tools select
// their shape, the painting tools, Gradient and Paint Bucket make a step of their name and icon, Type a text layer, the
// shape tools a shape layer; nothing without a document or during Free Transform. The click is pointerArea's, the
// canvas's MouseArea (Viewport's), as the prototype's button over the canvas: a left press and release on it, moved or
// not, so not one on what an overlay takes first (a handle, the text). It counts with the tool it was pressed with and
// without Alt: not the release ending Space's Hand, Alt's Eyedropper, Ctrl's Move (U5) held, nor the clone source's Alt
// click (U7). Eyedropper, the clone source (U7) and Zoom (U6) are theirs. The real canvas (V1, N3) paints what these
// steps only name.
Connections {
    id: cc
    property Doc doc: null
    property MouseArea pointerArea: null
    property string tool: ""  // what a plain left press on it found in Tools.tool, else ""
    target: pointerArea

    function onPressed(m) { tool = m.button === Qt.LeftButton && !(m.modifiers & Qt.AltModifier) ? Tools.tool : "" }
    function onClicked() {
        if (tool === Tools.tool)
            run(tool)
    }

    // The prototype's onCanvas, copied from build/proto/Main.dc.html, its calls made Doc's commands; true for a step.
    function run(t) {
        const T = ToolSpec.TOOL[t]
        if (!doc) return false
        if (Tools.xf) return false
        if (t === "selbrush") return doc.run({ op: "select", shape: "skin", name: T.n })
        if (ToolSpec.PAINT_T[t]) return doc.run({ op: "step", name: T.n, icon: T.icn })
        switch (t) {
        case "rect": return doc.run({ op: "select", shape: "rect", name: T.n })
        case "ellip": return doc.run({ op: "select", shape: "ellip", name: T.n })
        case "lasso": case "plasso": return doc.run({ op: "select", shape: "lasso", name: T.n })
        case "wand": return doc.run({ op: "select", shape: "subject", name: T.n })
        case "gradient": case "bucket": return doc.run({ op: "step", name: T.n, icon: T.icn })
        case "type": return doc.run({ op: "newTypeLayer" })  // "Chữ mẫu", step Horizontal Type
        case "rectS": case "ellipS": case "tri": case "poly": case "line": return doc.run({ op: "newShapeLayer", shape: t })  // nextName(T.n)
        default: return false
        }
    }
}
