import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Templates as T
import QtTest
import PhotoBeauty
import "../qml/Color.js" as Color

// U11: Color.js, the Color, Swatches and Gradients panels, ColorField, GradientBar and the Color Picker (docs/ui-tasks.md),
// the picker opened from the panel, Fill (U17) and Thiết lập (U21) on Main.
Item {
    id: root
    width: 1280
    height: 900

    Component { id: mainC; Main {} }
    Component { id: pickerC; ColorPickerDialog {} }
    Component { id: gradientsC; GradientsPanel {} }
    Component {  // a window that records what its openDialog() is asked, around a GradientBar
        id: hostC
        Window {
            property var calls: []
            function openDialog(name, params) {
                calls = calls.concat([[name, params]])
                return null
            }
            width: 400
            height: 160
            visible: true
            GradientBar {
                x: 20
                y: 20
                width: 300
                onMoved: v => value = v
                onCommitted: v => value = v
            }
        }
    }

    function find(item, pred) {
        for (const c of item.children) {
            if (pred(c)) return c
            const f = find(c, pred)
            if (f) return f
        }
        return null
    }
    function all(item, pred, out) {
        out = out || []
        for (const c of item.children) {
            if (pred(c)) out.push(c)
            all(c, pred, out)
        }
        return out
    }

    TestCase {
        name: "U11"
        when: windowShown

        function init() {
            Docs.reset()
            Tools.reset()
            Prefs.reset()
            createTemporaryObject(gradientsC, root).restore()  // the gradients a case added go
        }
        function cleanupTestCase() { init() }

        function open() {
            const w = createTemporaryObject(mainC, root, { startState: "Main" })
            w.requestActivate()
            tryVerify(() => w.active)
            waitForRendering(w.contentItem)
            return w
        }
        function shown(w, type) {
            waitForRendering(w.contentItem)
            return root.find(w.contentItem, o => o instanceof type && o.visible)
        }
        function topDialog(w) { return w.dialogs[w.dialogs.length - 1] || null }
        function show(params) {
            const d = createTemporaryObject(pickerC, root, { params: params })
            d.open()
            tryVerify(() => d.opened)
            return d
        }
        function pick(dd, value) {  // an option of a Dropdown, as its list gives it
            const i = dd.pairs.findIndex(o => o[0] === value)
            verify(i >= 0, value)
            dd.activated(i)
            compare(dd.value, value)
        }
        function type(f, text) {  // into a text field: click, the text, Enter
            mouseClick(f)
            tryVerify(() => f.activeFocus)
            f.text = text
            keyClick(Qt.Key_Return)
        }
        function typeNumber(f, text) { type(root.find(f, o => o instanceof T.TextField), text) }
        function click(w, item) {  // after a grid made again is laid out
            waitForRendering(w.contentItem)
            mouseClick(item)
        }
        function act(p, label) { p.menu.find(m => m.l === label).act() }  // an item of the panel's … menu
        function toggle(item, icon) { return root.find(item, o => o instanceof ToolToggle && o.icon.name === icon) }
        function button(item, text) { return root.find(item, o => o instanceof T.AbstractButton && o.text === text && o.visible) }
        function hsb(d) { return [Math.round(d.h), Math.round(d.s * 100), Math.round(d.v * 100)] }

        // ---- Color.js ------------------------------------------------------------------------------------------
        function test_convert() {  // sRGB → HSB, Lab, across, and back: one 8-bit level at most
            let worst = 0
            for (let r = 0; r < 256; r += 15)
                for (let g = 0; g < 256; g += 15)
                    for (let b = 0; b < 256; b += 15) {
                        const c = [r, g, b], h = Color.rgb2hsv(c, 0), lab = Color.rgb2lab(Color.hsv2rgb(h.h, h.s, h.v))
                        const via = Color.lab2rgb(lab), h2 = Color.rgb2hsv(via, h.h), back = Color.hsv2rgb(h2.h, h2.s, h2.v)
                        for (let i = 0; i < 3; i++)
                            worst = Math.max(worst, Math.abs(Math.round(via[i]) - c[i]), Math.abs(Math.round(back[i]) - c[i]))
                    }
            verify(worst <= 1, "worst " + worst)
            const h = Color.rgb2hsv([233, 199, 179], 0)
            compare([Math.round(h.h), Math.round(h.s * 100), Math.round(h.v * 100)], [22, 23, 91])
            compare(Color.rgb2lab([233, 199, 179]).map(Math.round), [83, 9, 14])
            compare(Color.rgb2lab([255, 0, 0]).map(Math.round), [53, 80, 67])
            compare(Color.rgb2hsv([128, 128, 128], 200).h, 200)  // a grey keeps the hue it had
            compare([Color.hex([233, 199, 179]), Color.css([233, 199, 179]), Color.parseHex("E9C7B3"), Color.parseHex("#abc"), Color.parseHex("12345g")],
                    ["E9C7B3", "#e9c7b3", [233, 199, 179], [170, 187, 204], null])
            compare([Color.parse("#80ff0000"), Color.parse("rgba(247,200,115,.55)"), Color.parse("transparent"), Color.parse("none")],
                    [[255, 0, 0, 128 / 255], [247, 200, 115, 0.55], [0, 0, 0, 0], null])
        }
        function test_gradient_css() {  // a preset's CSS as stops, then as the pieces Qt Quick draws
            let g = Color.gradient("linear-gradient(135deg, #e9c7b3, transparent)")
            compare(g.colors.map(s => [s.p, s.c]), [[0, [233, 199, 179]], [1, [233, 199, 179]]])
            compare(g.alphas.map(s => [s.p, s.a]), [[0, 1], [1, 0]])
            verify(Color.seeThrough(g))
            compare(Color.pieces(g).map(x => [x.x, x.w, x.a, x.b]), [[0, 1, "#ffe9c7b3", "#00e9c7b3"]])
            g = Color.gradient("linear-gradient(90deg, #2b1d4e 0%, #c0587e 48%, rgba(247,200,115,.55) 100%)")
            compare(g.colors.map(s => s.p), [0, 0.48, 1])
            compare(g.alphas.map(s => [s.p, s.a]), [[0, 1], [0.48, 1], [1, 0.55]])
            g = Color.gradient("linear-gradient(90deg, transparent, #ffffff, transparent)")  // Vệt sáng: white, its opacity up then down
            compare(g.colors.map(s => [s.p, Color.hex(s.c)]), [[0, "FFFFFF"], [1, "FFFFFF"]])
            compare(g.alphas.map(s => [s.p, s.a]), [[0, 0], [0.5, 1], [1, 0]])
            compare(Color.pieces(g).length, 2)
            g = Color.gradient("linear-gradient(135deg, #2b1d4e, #c0587e, #f7c873)")  // evenly spaced
            compare(g.colors.map(s => s.p), [0, 0.5, 1])
            verify(!Color.seeThrough(g))
            compare(Color.argb(g, 0.25), "#ff" + Color.hex([(0x2b + 0xc0) / 2, (0x1d + 0x58) / 2, (0x4e + 0x7e) / 2]).toLowerCase())
            g.colors[0].m = 0.25  // a midpoint off the middle: halfway at a quarter of the way
            compare(Color.argb(g, 0.125), Color.argb(Color.gradient("linear-gradient(90deg, #2b1d4e, #c0587e)"), 0.5))
            compare(Color.pieces(g).map(x => x.x), [0, 0.125, 0.5])
        }

        // ---- Color panel ---------------------------------------------------------------------------------------
        function test_color_modes() {  // the bars of each mode; the colour stays through them
            const w = open()
            Tools.setFg("#e9c7b3")
            const p = shown(w, ColorPanel), dd = root.find(p, o => o instanceof Dropdown)
            const vals = () => root.all(p, o => o instanceof T.TextField && o.width === 50 && o.visible).map(o => o.text)
            compare(vals(), ["22°", "23%", "91%"])
            compare(p.implicitHeight, 124)
            pick(dd, "RGB")
            compare(vals(), ["233", "199", "179"])
            pick(dd, "Lab")
            compare(vals(), ["83", "9", "14"])
            pick(dd, "Wheel")
            compare(p.implicitHeight, 204)
            const wheel = root.find(p, o => o instanceof ColorField && o.wheel)
            verify(wheel.visible)
            compare(dd.width, 100)
            pick(dd, "HSB")
            verify(!wheel.visible)
            compare(Tools.fg, "#e9c7b3")
            compare(Tools.colorMode, "HSB")
            pick(dd, "Lab")
            Tools.showPanel("Swatches")  // the mode is Tools': the panel made again shows it
            Tools.showPanel("Color")
            compare(root.find(shown(w, ColorPanel), o => o instanceof Dropdown).value, "Lab")
        }
        function test_color_typing() {  // the numbers, a slider, the hex, + to Swatches
            const w = open()
            Tools.setFg("#e9c7b3")
            const p = shown(w, ColorPanel), dd = root.find(p, o => o instanceof Dropdown)
            const val = i => root.all(p, o => o instanceof T.TextField && o.width === 50 && o.visible)[i]
            pick(dd, "RGB")
            type(val(0), "18")
            type(val(1), "52")
            type(val(2), "300")  // past the top: the top
            compare(Tools.fg, "#1234ff")
            type(val(2), "86")
            compare(Tools.fg, "#123456")
            compare(val(2).text, "86")
            type(val(1), "abc")  // no number: as it was
            compare([Tools.fg, val(1).text], ["#123456", "52"])
            pick(dd, "Lab")
            compare([val(0).text, val(1).text, val(2).text], ["21", "1", "−24"])
            const lab = Color.rgb2lab([18, 52, 86])
            type(val(0), "50")
            compare(Tools.fg, Color.css(Color.lab2rgb([50, lab[1], lab[2]])))
            pick(dd, "HSB")
            Tools.setFg("#e9c7b3")
            const s = root.all(p, o => o instanceof SliderField && o.visible)
            mouseClick(s[0], s[0].width / 2, s[0].height / 2)  // H to 180°, give or take the pixel
            verify(Math.abs(p.hue - 180) <= 1.5, p.hue)
            compare(val(0).text, Math.round(p.hue) + "°")
            const hex = root.find(p, o => o instanceof TextBox)
            compare(hex.text, Color.hex(Color.parseHex(Tools.fg)))
            type(hex, "123456")
            compare(Tools.fg, "#123456")
            type(hex, "12345g")
            compare([Tools.fg, hex.text], ["#123456", "123456"])
            mouseClick(toggle(p, "add"))  // into the last group, as no swatch is picked
            const lib = Color.swatches(Prefs.values.swatches), last = lib.groups.length - 1
            compare(lib.groups[last][1].slice(-1), ["#123456"])
            compare(lib.sel, [last, lib.groups[last][1].length - 1])
            verify(lib.open[last])
        }
        function test_color_wheel() {  // hue round, saturation out to 70% of the radius; Brightness beside
            const w = open()
            Tools.colorMode = "Wheel"
            Tools.setFg("#ff0000")
            const p = shown(w, ColorPanel), f = root.find(p, o => o instanceof ColorField && o.visible), disc = f.children[2]
            mouseClick(disc, 75, 75 + 21)  // straight down, 40% of the way to full: 90°, 40%
            let h = Color.rgb2hsv(Color.parseHex(Tools.fg), 0)
            compare([Math.round(h.h), Math.round(h.s * 100), Math.round(h.v * 100)], [90, 40, 100])
            mouseClick(disc, 149, 75)  // the right edge: red, full
            h = Color.rgb2hsv(Color.parseHex(Tools.fg), 0)
            compare([Math.round(h.h), Math.round(h.s * 100)], [0, 100])
            keyClick(Qt.Key_Right, Qt.ShiftModifier)
            compare(Math.round(Color.rgb2hsv(Color.parseHex(Tools.fg), 0).h), 10)
            const b = root.find(p, o => o instanceof SliderField && o.visible)
            compare(b.label, "Brightness")
            mouseClick(b, b.width / 2, b.height - 6)
            verify(Math.abs(Color.rgb2hsv(Color.parseHex(Tools.fg), 0).v * 100 - 50) <= 1.5)
        }
        function test_color_picks() {  // the foreground swatch opens the Color Picker, which sets Tools.fg; the background one Tools.bg
            const w = open()
            Tools.setFg("#e9c7b3")
            const p = shown(w, ColorPanel), pair = root.find(p, o => o instanceof ColorPair && o.visible)
            mouseClick(pair, 5, 5)
            let d = null
            tryVerify(() => (d = topDialog(w)) instanceof ColorPickerDialog && d.opened)
            compare(d.what, "màu trước")
            compare(d.hex, "E9C7B3")
            type(root.find(d.contentItem, o => o instanceof TextBox), "336699")
            mouseClick(button(d.footer, "OK"))
            tryVerify(() => !topDialog(w))
            compare([Tools.fg, Tools.recentSwatches[0]], ["#336699", "#336699"])
            mouseClick(pair, 30, 30)
            tryVerify(() => (d = topDialog(w)) instanceof ColorPickerDialog && d.opened)
            compare(d.what, "màu sau")
            compare(d.hex, "000000")
            type(root.find(d.contentItem, o => o instanceof TextBox), "ffffff")
            keyClick(Qt.Key_Escape)  // drops the dialog: nothing changes
            tryVerify(() => !topDialog(w))
            compare([Tools.fg, Tools.bg], ["#336699", "#000000"])
            mouseClick(pair, 30, 30)
            tryVerify(() => (d = topDialog(w)) instanceof ColorPickerDialog && d.opened)
            type(root.find(d.contentItem, o => o instanceof TextBox), "ffffff")
            mouseClick(button(d.footer, "OK"))
            tryVerify(() => !topDialog(w))
            compare([Tools.fg, Tools.bg], ["#336699", "#ffffff"])
        }

        // ---- Color Picker --------------------------------------------------------------------------------------
        function test_picker_field() {  // drag the square and the strip, keys; OK hands the colour to picked
            let got = null
            const d = show({ color: "#e9c7b3", title: "Màu tô", picked: c => { got = c } })
            compare([d.title, d.what], ["Color Picker", "Màu tô"])
            compare(hsb(d), [22, 23, 91])
            const cur = root.find(d.contentItem, o => o instanceof Rectangle && o.width === 72 && o.height === 72 && o.radius === 5)
            verify(Qt.colorEqual(cur.color, "#e9c7b3"), String(cur.color))  // hiện tại, under mới
            const f = root.find(d.contentItem, o => o instanceof ColorField), sq = f.children[0], strip = f.children[1]
            sq.forceActiveFocus(Qt.TabFocusReason)  // Tab: FocusRing rings Controls
            verify(sq.visualFocus && sq.focusPolicy === Qt.TabFocus)
            mousePress(sq, 128, 64)  // Saturation 50%, Brightness 75%
            compare([d.s, d.v], [0.5, 0.75])
            mouseMove(sq, 255, 0)
            mouseRelease(sq, 255, 0)
            compare([Math.round(d.s * 256), d.v], [255, 1])
            mouseClick(sq, 128, 64)
            keyClick(Qt.Key_Left)
            keyClick(Qt.Key_Down, Qt.ShiftModifier)
            compare([Math.round(d.s * 100), Math.round(d.v * 100)], [49, 65])
            mouseClick(strip, 10, 64)  // the hue, from the top: 270°
            compare(Math.round(d.h), 270)
            keyClick(Qt.Key_Up, Qt.ShiftModifier)
            compare(Math.round(d.h), 280)
            compare(got, null)
            const hex = "#" + d.hex.toLowerCase()
            mouseClick(button(d.footer, "OK"))
            tryVerify(() => !d.visible)
            compare(got, hex)
            compare(Tools.fg, "#ffffff")  // picked took it, not the foreground
        }
        function test_picker_channels() {  // a radio puts its channel on the strip, the other two on the square
            const d = show({ color: "#123456", picked: () => {} })
            const f = root.find(d.contentItem, o => o instanceof ColorField), sq = f.children[0]
            const radios = root.all(d.contentItem, o => o.objectName === "radio")
            compare(radios.map(r => r.text), ["H", "R", "L", "S", "G", "a", "B", "B", "b"])
            mouseClick(radios[1])  // R: Blue across, Green up
            compare([d.ch, f.channel, d.hex], ["R", "R", "123456"])
            mouseClick(sq, 0, 0)
            compare(d.rgb.map(Math.round), [18, 255, 0])
            for (let i = 0; i < 9; i++) {  // switching changes no colour
                const before = d.hex
                mouseClick(radios[i])
                compare(d.hex, before)
            }
            const e = show({ color: "#808080", picked: () => {} }), g = root.find(e.contentItem, o => o instanceof ColorField)
            mouseClick(root.all(e.contentItem, o => o.objectName === "radio")[2])  // L: a across, b up; L stays as the square is dragged
            const L = e.lab[0]
            mousePress(g.children[0], 0, 0)  // a −128, b 127: past sRGB, so the colour clips to another L
            verify(Math.abs(e.lab[0] - L) > 1, e.lab[0])
            mouseMove(g.children[0], 110, 150)
            mouseMove(g.children[0], 120, 140)
            mouseRelease(g.children[0], 120, 140)
            verify(Math.abs(e.lab[0] - L) < 1e-4, e.lab[0] + " " + L)
            verify(Math.abs(e.lab[1] - (-128 + 255 * 120 / 256)) < 1e-4 && Math.abs(e.lab[2] - (-128 + 255 * (1 - 140 / 256))) < 1e-4, e.lab)
        }
        function test_picker_fields() {  // the nine numbers and the hex
            const d = show({ color: "#e9c7b3", picked: () => {} })
            const n = root.all(d.contentItem, o => o instanceof NumberField)
            compare(n.map(x => x.value), [22, 233, 83, 23, 199, 9, 91, 179, 14])
            typeNumber(n[1], "18")  // R
            typeNumber(n[4], "52")  // G
            typeNumber(n[7], "86")  // B
            compare(d.hex, "123456")
            compare(n.map(x => x.value), [210, 18, 21, 79, 52, 1, 34, 86, -24])
            typeNumber(n[0], "30")  // H
            compare(Math.round(d.h), 30)
            typeNumber(n[3], "100")  // S
            typeNumber(n[6], "100")  // B
            compare(d.hex, "FF8000")
            const lab = d.lab.slice()
            typeNumber(n[2], "40")  // L: a and b stay
            compare(d.hex, Color.hex(Color.lab2rgb([40, lab[1], lab[2]])))
            const hex = root.find(d.contentItem, o => o instanceof TextBox)
            type(hex, "abc")
            compare(d.hex, "AABBCC")
            type(hex, "xyz")
            compare([d.hex, hex.text], ["AABBCC", "AABBCC"])
            mouseClick(button(d.contentItem, "Thêm vào Swatches"))  // into the last group
            const lib = Color.swatches(Prefs.values.swatches)
            compare(lib.groups[lib.groups.length - 1][1].slice(-1), ["#aabbcc"])
            verify(button(d.contentItem, "Đã thêm vào Swatches"))
            compare(d.rgb.map(Math.round), [170, 187, 204])
        }
        function test_picker_foreground() {  // no params: Tools.fg, "màu trước"; OK sets it, Hủy and Esc nothing
            Tools.setFg("#808080", 200)  // a grey: its hue is kept
            let d = show({})
            compare(d.what, "màu trước")
            compare(hsb(d), [200, 0, 50])
            type(root.find(d.contentItem, o => o instanceof TextBox), "123456")
            mouseClick(button(d.footer, "Hủy"))
            tryVerify(() => !d.visible)
            compare(Tools.fg, "#808080")
            d = show({})
            type(root.find(d.contentItem, o => o instanceof TextBox), "123456")
            keyClick(Qt.Key_Escape)
            tryVerify(() => !d.visible)
            compare(Tools.fg, "#808080")
            d = show({})
            type(root.find(d.contentItem, o => o instanceof TextBox), "123456")
            button(d.footer, "OK").forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Return)
            tryVerify(() => !d.visible)
            compare([Tools.fg, Tools.recentSwatches[0]], ["#123456", "#123456"])
        }
        function test_picker_from_dialogs() {  // Fill (U17) and Thiết lập (U21) open it, and get the colour back
            const w = open()
            verify(w.hasDialog("ColorPicker"))
            const fill = w.openDialog("Fill")
            tryVerify(() => fill.opened)
            pick(root.find(fill.contentItem, o => o instanceof Dropdown), "color")
            waitForRendering(fill.contentItem)
            mouseClick(root.find(fill.contentItem, o => o instanceof Swatch && o.visible))
            let d = null
            tryVerify(() => (d = topDialog(w)) instanceof ColorPickerDialog && d.opened)
            compare(d.params.color, "#e9c7b3")
            type(root.find(d.contentItem, o => o instanceof TextBox), "123456")
            mouseClick(button(d.footer, "OK"))
            tryVerify(() => topDialog(w) === fill)
            verify(Qt.colorEqual(fill.custom, "#123456"))
            fill.close()
            tryVerify(() => !topDialog(w))
            const set = w.openDialog("Settings", { page: "transparency" })
            tryVerify(() => set.opened)
            const wells = root.all(set.contentItem, o => o instanceof Swatch && o.visible && Qt.colorEqual(o.color, "#c9c9c9"))
            compare(wells.length, 1)
            verify(wells[0].enabled)
            mouseClick(wells[0])
            tryVerify(() => (d = topDialog(w)) instanceof ColorPickerDialog && d.opened)
            compare(d.what, "Màu ô 2")
            type(root.find(d.contentItem, o => o instanceof TextBox), "abcdef")
            mouseClick(button(d.footer, "OK"))
            tryVerify(() => topDialog(w) === set)
            compare(Prefs.value("checkerColor2"), "#abcdef")
            set.close()
            tryVerify(() => !topDialog(w))
            const fx = w.openDialog("LayerStyle", { page: "colorOverlay" })  // Layer Style (U18)
            tryVerify(() => fx.opened)
            waitForRendering(fx.contentItem)
            mouseClick(root.find(fx.contentItem, o => o instanceof Swatch && o.visible && Qt.colorEqual(o.color, "#ff0000")))
            tryVerify(() => (d = topDialog(w)) instanceof ColorPickerDialog && d.opened)
            compare(d.hex, "FF0000")
            type(root.find(d.contentItem, o => o instanceof TextBox), "00ff00")
            mouseClick(button(d.footer, "OK"))
            tryVerify(() => topDialog(w) === fx)
            compare(fx.v.colorOverlay.color, "#00ff00")
        }
        function test_picker_from_bars() {  // the toolbar's colours and an options bar's colour cell (U5)
            const w = open()
            Tools.setFg("#e9c7b3")
            const pair = root.find(w.contentItem, o => o instanceof ColorPair && o.compact && o.visible)
            mouseClick(pair, 5, 5)
            let d = null
            tryVerify(() => (d = topDialog(w)) instanceof ColorPickerDialog && d.opened)
            compare([d.what, d.hex], ["màu trước", "E9C7B3"])
            type(root.find(d.contentItem, o => o instanceof TextBox), "123456")
            mouseClick(button(d.footer, "OK"))
            tryVerify(() => !topDialog(w))
            compare(Tools.fg, "#123456")
            mouseClick(pair, pair.width - 5, pair.height - 12)
            tryVerify(() => (d = topDialog(w)) instanceof ColorPickerDialog && d.opened)
            compare(d.what, "màu sau")
            type(root.find(d.contentItem, o => o instanceof TextBox), "654321")
            mouseClick(button(d.footer, "OK"))
            tryVerify(() => !topDialog(w))
            compare([Tools.fg, Tools.bg], ["#123456", "#654321"])
            Tools.setTool("rectS", "shape")
            const bar = shown(w, OptionsBar), cell = root.find(bar, o => o instanceof Swatch && o.visible && Qt.colorEqual(o.color, "#c0587e"))
            mouseClick(cell)
            tryVerify(() => (d = topDialog(w)) instanceof ColorPickerDialog && d.opened)
            compare([d.what, d.hex], ["màu Fill", "C0587E"])
            type(root.find(d.contentItem, o => o instanceof TextBox), "abcdef")
            mouseClick(button(d.footer, "OK"))
            tryVerify(() => !topDialog(w))
            verify(Qt.colorEqual(cell.color, "#abcdef"), String(cell.color))
        }

        // ---- Swatches ------------------------------------------------------------------------------------------
        function test_swatches() {  // recent, groups; a click sets the foreground; new group, add, delete
            const w = open()
            Tools.showPanel("Swatches")
            let p = shown(w, SwatchesPanel)
            const recent = root.all(p, o => o instanceof Swatch && o.width === 20 && o.visible)
            compare(recent.map(s => String(s.color)), Tools.recentSwatches)
            compare(recent.length, 10)
            mouseClick(recent[3])
            compare([Tools.fg, Tools.recentSwatches[0], Tools.recentSwatches[1]], ["#c0587e", "#c0587e", "#e9c7b3"])
            const tiles = () => root.all(p, o => o instanceof T.AbstractButton && o.width === 21 && o.visible)
            compare(tiles().length, 12 + 16)  // Tết 2026 folded
            mouseClick(tiles()[13])  // Cơ bản's second
            compare([Tools.fg, Tools.recentSwatches[0]], ["#404040", "#404040"])
            compare(p.lib.sel, [1, 1])
            verify(toggle(p, "delete").enabled)
            Tools.setFg("#123456")
            mouseClick(toggle(p, "add_box"))  // into Cơ bản, the group of the one picked
            compare(p.lib.groups[1][1].length, 17)
            compare([p.lib.groups[1][1][16], p.lib.sel], ["#123456", [1, 16]])
            compare(tiles().length, 12 + 17)
            mouseClick(toggle(p, "delete"))  // the one picked; the one before is picked then
            compare([p.lib.groups[1][1].length, p.lib.sel], [16, [1, 15]])
            mouseClick(toggle(p, "create_new_folder"))
            compare([p.lib.groups[3][0], p.lib.groups[3][1], p.lib.sel, p.lib.open[3]], ["Nhóm 1", [], [3, -1], true])
            verify(!toggle(p, "delete").enabled)
            mouseClick(toggle(p, "add_box"))
            compare(p.lib.groups[3][1], ["#123456"])
            mouseClick(toggle(p, "create_new_folder"))
            compare(p.lib.groups[4][0], "Nhóm 2")
            waitForRendering(w.contentItem)
            click(w, root.find(p, o => o instanceof T.AbstractButton && o.text === "Tông da"))
            compare(p.lib.open[0], false)
            compare(tiles().length, 16 + 1)
            Tools.showPanel("Color")  // the library is Prefs': the panel made again shows it
            Tools.showPanel("Swatches")
            p = shown(w, SwatchesPanel)
            compare([p.lib.groups.length, p.lib.open[0], p.lib.groups[3][1]], [5, false, ["#123456"]])
            act(p, "Khôi phục swatch mặc định")  // the presets, as they were
            compare([p.lib.groups.length, p.lib.groups[1][1].length, p.lib.open, p.lib.sel], [3, 16, [true, true, false], null])
        }

        // ---- Gradients -----------------------------------------------------------------------------------------
        function test_gradients() {  // recent, groups; a click picks the gradient; new group, add, delete
            const w = open()
            Tools.showPanel("Gradients")
            const p = shown(w, GradientsPanel)
            const recent = () => root.all(p, o => o instanceof T.AbstractButton && o.height === 30 && o.width === p.recentWidth && o.visible)
            compare(recent().length, 4)
            const tiles = () => root.all(p, o => o instanceof T.AbstractButton && o.height === 30 && o.width !== p.recentWidth && o.visible)
            compare(tiles().length, 4 + 6)  // Hoàng hôn, Ánh sáng folded
            compare(p.current, [0, 0])
            mouseClick(tiles()[6])  // Da và son's third
            compare([Tools.gradient, Tools.recentGradients[0], Tools.recentGradients.length], [[1, 2], [1, 2], 5])
            compare(p.current, [1, 2])
            Tools.setFg("#ff0000")
            mouseClick(toggle(p, "add_box"))  // the foreground to the background, into Da và son
            compare(p.groups[1][1][6], ["Gradient 1", "linear-gradient(90deg, #ff0000, #000000)", 6])
            compare([Tools.gradient, Tools.gradientCss()], [[1, 6], "linear-gradient(90deg, #ff0000, #000000)"])
            compare(tiles().length, 4 + 7)
            mouseClick(toggle(p, "delete"))  // hidden: Tools keeps it, the panel has none picked
            compare([tiles().length, p.current[1], toggle(p, "delete").enabled], [4 + 6, -1, false])
            compare(Tools.gradientCss(), "linear-gradient(90deg, #ff0000, #000000)")
            compare(recent().length, 4)  // the one deleted left the row
            click(w, tiles()[0])
            mouseClick(toggle(p, "delete"))
            compare(Prefs.values.gradients.hidden, ["1,6", "0,0"])
            compare(tiles().length, 3 + 6)
            mouseClick(toggle(p, "create_new_folder"))
            compare([p.groups.length, p.groups[4][0]], [5, "Nhóm 1"])
            mouseClick(toggle(p, "add_box"))  // into the new group
            compare([Tools.gradient, p.groups[4][1].length], [[4, 0], 1])
            compare(p.current, [4, 0])
            click(w, root.find(p, o => o instanceof T.AbstractButton && o.text === "Hoàng hôn"))
            waitForRendering(w.contentItem)
            compare(tiles().length, 3 + 6 + 3 + 1)
            act(p, "Khôi phục gradient mặc định")  // what was added goes, what was deleted comes back
            compare([p.groups.length, p.groups[0][1].length, p.groups[1][1].length, Prefs.values.gradients], [4, 4, 6, undefined])
        }
        function test_gradient_tile() {  // a tile is the pieces of its CSS, over the checker when it lets through
            const w = open()
            Tools.showPanel("Gradients")
            const p = shown(w, GradientsPanel)
            const tile = root.all(p, o => o instanceof T.AbstractButton && o.height === 30 && o.width !== p.recentWidth && o.visible)[1]  // Màu trước sang trong suốt
            const pieces = root.all(tile, o => o instanceof Rectangle && o.gradient && o.visible)
            compare(pieces.length, 1)
            verify(root.find(tile, o => o instanceof Canvas && o.visible))
            const img = grabImage(w.contentItem), at = tile.mapToItem(w.contentItem, 2, tile.height / 2)
            verify(Qt.colorEqual(img.pixel(at.x, at.y), "#e9c7b3") || img.red(at.x, at.y) > 200, String(img.pixel(at.x, at.y)))
        }

        // ---- GradientBar ---------------------------------------------------------------------------------------
        function test_gradient_bar() {  // add, drag along, drag off, midpoints, the fields
            const host = createTemporaryObject(hostC, root)
            tryVerify(() => host.visible)
            const b = root.find(host.contentItem, o => o instanceof GradientBar), area = b.children[0]
            b.value = Color.gradient("linear-gradient(90deg, #2b1d4e 0%, #c0587e 48%, rgba(247,200,115,.55) 100%)")
            waitForRendering(b)
            compare([b.kind, b.at], ["c", 0])
            const before = Color.colorAt(b.value, 0.25)
            mouseClick(area, b.px(0.25), 61)  // under the bar: a colour stop of the colour there
            compare(b.value.colors.length, 4)
            compare([b.kind, b.at, b.value.colors[1].p], ["c", 1, 0.25])
            compare(b.value.colors[1].c.map(Math.round), before.map(Math.round))
            mousePress(area, b.px(0.25), 61)  // along, past the stop at 48%
            mouseMove(area, b.px(0.75), 61)
            mouseRelease(area, b.px(0.75), 61)
            compare([b.at, b.value.colors.map(s => s.p)], [2, [0, 0.48, 0.75, 1]])
            mousePress(area, b.px(0.75), 61)  // off the bar: gone
            mouseMove(area, b.px(0.75), 61 + 40)
            mouseRelease(area, b.px(0.75), 61 + 40)
            compare(b.value.colors.map(s => s.p), [0, 0.48, 1])
            mousePress(area, b.px(1), 61)
            mouseMove(area, b.px(1), 61 + 40)
            mouseMove(area, b.px(1), 61 + 40)
            mouseRelease(area, b.px(1), 61 + 40)
            mousePress(area, b.px(0), 61)  // two left: they stay
            mouseMove(area, b.px(0), 61 + 40)
            mouseRelease(area, b.px(0), 61 + 40)
            compare(b.value.colors.length, 2)
            const a0 = Color.alphaAt(b.value, 0.75)
            mouseClick(area, b.px(0.75), 6)  // over the bar: an opacity stop of the opacity there
            compare([b.kind, b.at, b.value.alphas.length, b.value.alphas[2].a], ["a", 2, 4, a0])
            mouseClick(area, b.px(0), 61)  // the first colour stop; the diamond half way to the next
            compare([b.kind, b.at], ["c", 0])
            mousePress(area, b.px(0.24), 52)
            mouseMove(area, b.px(0.36), 52)
            mouseRelease(area, b.px(0.36), 52)
            verify(Math.abs(b.value.colors[0].m - 0.75) < 0.01, b.value.colors[0].m)
            const n = root.all(b, o => o instanceof NumberField && o.visible)
            compare(n[0].label, "Vị trí")
            typeNumber(n[0], "30")
            compare([b.at, b.value.colors[0].p], [0, 0.3])
            area.forceActiveFocus()
            keyClick(Qt.Key_Right, Qt.ShiftModifier)
            fuzzyCompare(b.value.colors[0].p, 0.4, 1e-9)
            b.value = Color.gradient("linear-gradient(90deg, #000000, #ffffff)")
            mouseClick(area, b.px(0.5), 6)
            compare([b.kind, b.at, b.value.alphas.length], ["a", 1, 3])
            waitForRendering(host.contentItem)  // Độ mờ in the place of Màu
            const a = root.all(b, o => o instanceof NumberField && o.visible)
            compare(a[1].label, "Độ mờ")
            typeNumber(a[1], "20")
            compare(b.value.alphas[b.at].a, 0.2)
            const erase = button(b, "Xóa điểm")
            verify(erase.enabled)
            mouseClick(erase)
            compare(b.value.alphas.length, 2)
            verify(!erase.enabled)
        }
        function test_gradient_bar_color() {  // double-click a colour stop, or its Màu: the window's Color Picker
            const host = createTemporaryObject(hostC, root)
            tryVerify(() => host.visible)
            const b = root.find(host.contentItem, o => o instanceof GradientBar), area = b.children[0]
            b.value = Color.gradient("linear-gradient(90deg, #2b1d4e 0%, #c0587e 48%, #f7c873 100%)")
            waitForRendering(b)
            mouseDoubleClickSequence(area, b.px(0.48), 61)
            compare(host.calls.length, 1)
            compare([host.calls[0][0], host.calls[0][1].color], ["ColorPicker", "#c0587e"])
            host.calls[0][1].picked("#00ff00")
            compare([b.at, b.value.colors[1].c], [1, [0, 255, 0]])
            mouseClick(root.find(b, o => o instanceof Swatch && o.visible))
            compare([host.calls.length, host.calls[1][1].color], [2, "#00ff00"])
        }
    }
}
