import QtQuick
import QtTest
import PhotoBeauty
import "../qml/ToolSpec.js" as ToolSpec

// U7b: a click on the mock canvas with each tool makes one History step, of the tool's name and icon (a selection, a
// layer), or none, and Undo gives the document back; none during Free Transform, with Alt, with another button, when
// the tool changed while pressed, on a handle, or with the overlays hidden. A stroke is a click too.
Item {
    id: root
    width: 1100
    height: 800
    Component { id: mainC; Main {} }

    function find(item, pred) {
        for (const c of item.children) {
            if (pred(c))
                return c
            const f = find(c, pred)
            if (f)
                return f
        }
        return null
    }

    TestCase {
        name: "U7b"
        when: windowShown
        property var w: null  // one Main for every case: init() gives it a new document

        function initTestCase() {
            w = mainC.createObject(root, { startState: "Main" })
            w.requestActivate()
            tryVerify(() => w.active)
        }
        function init() {
            Docs.viewport = { w: 1034, h: 750 }
            Docs.reset()
            Tools.reset()
            Prefs.reset()
            waitForRendering(w.contentItem)
        }
        function cleanupTestCase() {
            w.destroy()
            Docs.reset()
            Tools.reset()
            Prefs.reset()
        }
        function overlays() { return find(w.contentItem, o => o instanceof Overlays) }
        function area() { return overlays().pointerArea }  // Viewport's MouseArea: the canvas
        function steps(d) { return JSON.stringify([d.index, d.history.map(h => h.name)]) }

        // [step name, icon] or none, then the selection or the new layer [kind, name] it leaves.
        function test_tool_data() {
            return [
                { tag: "move" },
                { tag: "rect", step: ["Rectangular Marquee", "select"], sel: "rect" },
                { tag: "ellip", step: ["Elliptical Marquee", "select"], sel: "ellip" },
                { tag: "lasso", step: ["Lasso", "select"], sel: "lasso" },
                { tag: "plasso", step: ["Polygonal Lasso", "select"], sel: "lasso" },
                { tag: "wand", step: ["Magic Wand", "select"], sel: "subject" },
                { tag: "selbrush", step: ["Selection Brush", "select"], sel: "skin" },
                { tag: "crop" },
                { tag: "pcrop" },
                { tag: "eyedrop" },
                { tag: "spot", step: ["Spot Healing Brush", "spot_heal"] },
                { tag: "heal", step: ["Healing Brush", "healing"] },
                { tag: "patch" },
                { tag: "brush", step: ["Brush", "brush"] },
                { tag: "pencil", step: ["Pencil", "edit"] },
                { tag: "clone", step: ["Clone Stamp", "approval"] },
                { tag: "eraser", step: ["Eraser", "ink_eraser"] },
                { tag: "gradient", step: ["Gradient", "gradient"] },
                { tag: "bucket", step: ["Paint Bucket", "format_color_fill"] },
                { tag: "dodge", step: ["Dodge", "dodge"] },
                { tag: "burn", step: ["Burn", "local_fire_department"] },
                { tag: "sponge", step: ["Sponge", "sponge"] },
                { tag: "type", step: ["Horizontal Type", "title"], layer: ["text", "Chữ mẫu"] },
                { tag: "rectS", step: ["Rectangle", "rectangle"], layer: ["shape", "Rectangle 1"] },
                { tag: "ellipS", step: ["Ellipse", "circle"], layer: ["shape", "Ellipse 1"] },
                { tag: "tri", step: ["Triangle", "change_history"], layer: ["shape", "Triangle 1"] },
                { tag: "poly", step: ["Polygon", "pentagon"], layer: ["shape", "Polygon 1"] },
                { tag: "line", step: ["Line", "pen_size_2"], layer: ["shape", "Line 1"] },
                { tag: "hand" },
                { tag: "rot" },
                { tag: "zoom" }
            ]
        }
        function test_tool(data) {
            const d = Docs.current, a = area(), i = d.index, before = JSON.stringify(d.state), was = steps(d), n = d.layers.length
            compare(Object.keys(ToolSpec.TOOL).length, test_tool_data().length, "a row for every tool")
            Tools.setTool(data.tag, ToolSpec.TOOL[data.tag].g)
            mouseClick(a, a.width / 2, a.height / 2)
            if (!data.step) {
                compare(steps(d), was, "no step")
                compare(JSON.stringify(d.state), before)
                return
            }
            compare([d.index, d.history.length], [i + 1, i + 2], "one step")
            compare([d.history[i + 1].name, d.history[i + 1].icon], data.step)
            if (data.sel)
                compare([d.selection, d.inverse], [data.sel, false])
            if (data.layer)
                compare([d.layers.length, d.active.kind, d.active.props.name], [n + 1, data.layer[0], data.layer[1]])
            if (!data.sel && !data.layer)
                compare(JSON.stringify(d.state), before, "a step the mock paints nothing for")
            d.undo()
            compare(d.index, i)
            compare(JSON.stringify(d.state), before, "Undo gives it back")
        }

        function test_not_a_click() {
            const d = Docs.current, ov = overlays(), a = area(), x = a.width / 2, y = a.height / 2, was = steps(d)
            Tools.setTool("brush", "brush")
            Tools.xf = true
            mouseClick(a, x, y)
            compare(steps(d), was, "Free Transform")
            Tools.xf = false
            mouseClick(a, x, y, Qt.MiddleButton)
            compare(steps(d), was, "the middle button")
            mouseClick(a, x, y, Qt.LeftButton, Qt.AltModifier)
            compare(steps(d), was, "Alt")
            Tools.setTool("clone", "stamp")
            mouseClick(a, x, y, Qt.LeftButton, Qt.AltModifier)
            verify(!!ov.cloneSource, "the clone source")
            compare(steps(d), was, "the clone source is no step")
            Tools.tool = "hand"  // Space held (U5)
            mousePress(a, x, y)
            Tools.tool = "clone"  // let go before the button
            mouseRelease(a, x, y)
            compare(steps(d), was, "Space's Hand")
            mousePress(a, x, y)
            Tools.tool = "hand"  // held while pressed
            mouseRelease(a, x, y)
            Tools.tool = "clone"
            compare(steps(d), was, "Space held while pressed")
            ov.visible = false  // as a space hides them (U15)
            mouseClick(a, x, y)
            ov.visible = true
            compare(steps(d), was, "Overlays hidden")
            mouseClick(a, x, y)
            compare(d.history[d.index].name, "Clone Stamp", "a click at last")
        }
        function test_stroke_handle() {  // a stroke is one step; a press on a handle is the handle's
            const d = Docs.current, a = area(), i = d.index
            Tools.setTool("brush", "brush")
            mousePress(a, 300, 300)
            for (let k = 1; k <= 5; k++)
                mouseMove(a, 300 + 40 * k, 300 + 20 * k, -1, Qt.LeftButton)
            mouseRelease(a, 500, 400)
            compare([d.index, d.history[d.index].name], [i + 1, "Brush"])
            Tools.setTool("rectS", "shape")
            mouseClick(a, 300, 300)
            compare(d.active.props.name, "Rectangle 1")
            waitForRendering(w.contentItem)
            const handles = find(overlays(), o => o instanceof ShapeHandles), was = steps(d)
            verify(handles.visible)
            const dot = find(handles, o => o instanceof Rectangle && o.width === 9 && o.visible)
            mouseClick(dot, 4, 4)
            compare(steps(d), was, "the corner's dot is no click on the canvas")
        }
    }
}
