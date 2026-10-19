pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtTest
import PhotoBeauty
import "../qml/Icons.js" as Icons
import "../qml/StrokeIcons.js" as StrokeIcons

// U1: Theme, fonts, icons and the controls copied from snapone (docs/ui-tasks.md). Each control
// builds and draws in both themes; accent and font size apply at once; every icon name draws
// its own glyph; drawn icons sit and weigh like glyphs; the controls' behaviour and keyboard.
Item {
    id: root
    width: 900
    height: 700

    function find(item, pred) {
        for (const c of item.children) {
            if (pred(c)) return c
            const f = find(c, pred)
            if (f) return f
        }
        return null
    }
    function near(a, b, tol) {  // colours equal within tol of 255 per channel
        const t = (tol ?? 2) / 255
        return Math.abs(a.r - b.r) <= t && Math.abs(a.g - b.g) <= t && Math.abs(a.b - b.b) <= t && Math.abs(a.a - b.a) <= t
    }
    function over(fg, bg) {  // fg composited over an opaque bg
        return Qt.rgba(fg.r * fg.a + bg.r * (1 - fg.a), fg.g * fg.a + bg.g * (1 - fg.a), fg.b * fg.a + bg.b * (1 - fg.a), 1)
    }
    // The rows a grab inks (any channel past 128 on black), and their count, in [x0, x1) × [y0, y1).
    function ink(img, x0, y0, x1, y1) {
        let n = 0, top = -1, bottom = -1, sx = 0, sy = 0
        for (let y = y0; y < y1; y++) for (let x = x0; x < x1; x++) {
            if (Math.max(img.red(x, y), img.green(x, y), img.blue(x, y)) <= 128) continue
            n++; sx += x; sy += y
            if (top < 0) top = y
            bottom = y
        }
        return { n: n, top: top, bottom: bottom, cx: n ? sx / n : -1, cy: n ? sy / n : -1 }
    }

    Rectangle { id: bgRect; anchors.fill: parent; color: Theme.bg }
    Column {
        id: page
        x: 20
        y: 20
        spacing: 12
        Row {
            spacing: 12
            Btn { id: plain; text: "Hủy" }
            Btn { id: primary; text: "Tạo"; primary: true }
            Btn { id: onBtn; text: "Bật"; on: true; icon.name: "brush" }
            Btn { id: offBtn; text: "Mờ"; enabled: false }
            IconButton { id: iconBtn; icon.name: "more_horiz" }
            IconButton { id: onIcon; icon.name: "mask"; on: true }
            Link { id: link; text: "Mở thư mục" }
        }
        Row {
            spacing: 12
            Toggle { id: tog; text: "Tự lưu"; checked: true }
            Toggle { id: togOff; text: "Hiện lưới"; sub: "Mỗi 100 px" }
            Select { id: sel; width: 150; options: [["srgb", "sRGB IEC61966-2.1"], ["adobe", "Adobe RGB (1998)"], ["p3", "Display P3"]]; value: "srgb" }
            TextBox { id: box; width: 170; placeholderText: "Tìm preset" }
            Spinner { id: spin }
        }
        Row {
            spacing: 12
            Icon { id: glyph; name: "add" }
            Icon { id: drawn; name: "plus" }
            StrokeIcon { id: stroke; name: "mode_new" }
        }
        PanelHead { id: head; width: 284; text: "Biến đổi"; IconButton { icon.name: "add" } }
        PanelSection { id: section; width: 284; title: "Tách tần số"; Text { text: "Radius"; color: Theme.t2 } }
    }
    // In the overlay of the window, as Main has them: made once the window is there.
    property Item ring
    property Item toast
    property color accent0
    Component { id: ringC; FocusRing {} }
    Component { id: toastC; Toast {} }

    Component {
        id: dialogC
        DialogBox {
            title: "Hồ sơ màu khác không gian làm việc"
            Text { text: "cuoi-trang-an.psd"; color: Theme.t1 }
        }
    }
    Component { id: iconC; Icon { size: 20; color: "white" } }
    Component { id: textC; Text { text: "photo-beauty 4.400 px"; font.pixelSize: 13 } }
    Component { id: blackC; Rectangle { color: "black" } }

    TestCase {
        name: "U1"
        when: windowShown
        function initTestCase() {
            root.accent0 = Theme.accent  // before init() sets it
            root.ring = ringC.createObject(root)
            root.toast = toastC.createObject(root)
        }
        function init() {
            mouseMove(root, 1, 1)  // hovers nothing
            Theme.dark = true
            Theme.accent = Theme.accents[0]
            Theme.fontSize = "m"
            root.toast.text = ""
            root.toast.ok = false
            root.toast.err = false
            root.toast.act = null
        }
        function cleanupTestCase() { init() }

        // ---- tokens ----
        // The Tokens artboard, its tables copied: 13 colours a theme, data colours, the rest.
        function test_tokens_data() {
            const names = ["bg", "p1", "p2", "p3", "ln", "t1", "t2", "t3", "t4", "view", "trk", "thumb", "sel"]
            const dark = ["#161617", "#1c1c1e", "#252527", "#323235", "#2b2b2e", "#ececed", "#b4b4b7", "#86868b", "#5d5d62", "#282828", "#3a3a3e", "#dededf", "rgba(255,255,255,.07)"]
            const light = ["#e7e7e8", "#f3f3f3", "#e3e3e4", "#d3d3d5", "#d2d2d4", "#19191a", "#3c3c3f", "#636367", "#98989c", "#8e8e8e", "#c8c8cb", "#242426", "rgba(0,0,0,.06)"]
            const data = ["h1", "h2", "h3", "grid", "faint"]
            const darkData = ["rgba(214,71,61,.8)", "rgba(71,179,92,.8)", "rgba(66,110,224,.85)", "rgba(255,255,255,.08)", "rgba(255,255,255,.06)"]
            const lightData = ["rgba(240,120,110,.9)", "rgba(120,199,130,.9)", "rgba(120,150,240,.9)", "rgba(0,0,0,.11)", "rgba(0,0,0,.07)"]
            const rows = []
            names.concat(data).forEach((n, i) => {
                rows.push({ tag: n + " dark", dark: true, name: n, css: i < 13 ? dark[i] : darkData[i - 13] })
                rows.push({ tag: n + " light", dark: false, name: n, css: i < 13 ? light[i] : lightData[i - 13] })
            })
            for (const [n, css] of [["danger", "#ef5350"], ["warn", "#e5a33a"], ["closeFill", "#c42b1c"], ["acct", "#1d0f0b"]])
                rows.push({ tag: n, dark: true, name: n, css: css })
            return rows
        }
        function test_tokens(data) {
            Theme.dark = data.dark
            const m = /rgba\((\d+),(\d+),(\d+),([.\d]+)\)/.exec(data.css)
            const want = m ? Qt.rgba(m[1] / 255, m[2] / 255, m[3] / 255, Number(m[4])) : Qt.color(data.css)
            verify(root.near(Theme[data.name], want, 1), data.name + " " + Theme[data.name] + ", artboard " + data.css)
        }
        function test_accents() {
            compare(Theme.accents, ["#2FB5B0", "#4C8DF6", "#E0A53A", "#E8664A"])
            verify(Qt.colorEqual(root.accent0, "#2FB5B0"), "teal by default (decision 5)")
            compare(Theme.views, ["#000000", "#282828", "#535353", "#8e8e8e"])
            verify(Qt.colorEqual(Theme.view, Theme.views[1]), "dark: Xám đậm")
            fuzzyCompare(Theme.acc2.a, 0.16, 0.004)
            verify(Qt.colorEqual(Theme.hist[2], Theme.h3))
            Theme.dark = false
            verify(Qt.colorEqual(Theme.view, Theme.views[3]), "light: Xám nhạt")
        }
        // Overlapping histogram channels: screen on dark, multiply on light (--hmix).
        function test_hmix() {
            const c = createTemporaryObject(canvasC, root)
            for (const dark of [true, false]) {
                Theme.dark = dark
                c.px = undefined
                c.requestPaint()
                tryVerify(() => c.px !== undefined)
                const a = 0x40 / 255, b = 0x80 / 255, want = dark ? 1 - (1 - a) * (1 - b) : a * b
                fuzzyCompare(c.px[0], Math.round(want * 255), 1)
            }
        }
        Component {
            id: canvasC
            Canvas {
                width: 4
                height: 4
                property var px
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    ctx.fillStyle = "#404040"
                    ctx.fillRect(0, 0, 4, 4)
                    ctx.globalCompositeOperation = Theme.hmix
                    ctx.fillStyle = "#808080"
                    ctx.fillRect(0, 0, 4, 4)
                    px = ctx.getImageData(0, 0, 1, 1).data
                }
            }
        }

        // ---- every control in both themes ----
        function test_themes_data() { return [{ tag: "dark", dark: true }, { tag: "light", dark: false }] }
        function test_themes(data) {
            Theme.dark = data.dark
            waitForRendering(page)
            for (const c of [plain, primary, onBtn, offBtn, iconBtn, onIcon, link, tog, togOff, sel, box, spin, glyph, drawn, stroke, head, section])
                verify(c.visible && c.width > 0 && c.height > 0, "built: " + c)
            const g = grabImage(root)
            const at = (item, x, y) => { const p = item.mapToItem(root, x, y); return g.pixel(Math.round(p.x), Math.round(p.y)) }
            verify(near(at(root, 2, 2), Theme.bg), "window: bg")
            verify(near(at(plain, 3, plain.height / 2), Theme.p2), "plain button: p2")
            verify(near(at(primary, 3, primary.height / 2), Theme.accent), "primary button: accent")
            verify(near(at(onBtn, 3, onBtn.height / 2), over(Theme.acc2, Theme.bg)), "button on: acc2")
            verify(near(at(offBtn, 3, offBtn.height / 2), Theme.p2), "disabled button: p2")
            verify(Qt.colorEqual(offBtn.ink, Theme.t4), "disabled button: t4 text")
            verify(near(at(onIcon, 3, onIcon.height / 2), over(Theme.acc2, Theme.bg)), "icon button on: acc2")
            verify(near(at(sel, 3, sel.height / 2), Theme.p2), "select: p2")
            verify(near(at(box, 3, box.height / 2), Theme.p2), "text box: p2")
            const track = tog.contentItem.children[1], trackOff = togOff.contentItem.children[1]
            verify(near(at(track, 4, track.height / 2), Theme.accent), "switch on: accent")
            verify(near(at(trackOff, trackOff.width - 4, trackOff.height / 2), Theme.trk), "switch off: trk")
            verify(near(at(trackOff, 9, trackOff.height / 2), Theme.knob), "knob")
            verify(near(at(section, 1, section.height - 1), Theme.ln), "section rule: ln")
            verify(Qt.colorEqual(sel.contentItem.color, Theme.t1) && Qt.colorEqual(head.children[0].color, Theme.t3))

            const d = createTemporaryObject(dialogC, root, { buttons: [{ text: "Hủy", act: () => {} }, { text: "OK", primary: true, act: () => {} }] })
            d.open()
            tryCompare(d, "opened", true)
            waitForRendering(d.contentItem)
            const dg = grabImage(root), dp = d.background.mapToItem(root, d.width / 2, 60)
            verify(near(dg.pixel(dp.x, dp.y), Theme.p1), "dialog: p1 " + dg.pixel(dp.x, dp.y) + " " + Theme.p1)
            verify(Qt.colorEqual(d.header.children[0].children[1].color, Theme.t1), "dialog title: t1")
            d.close()
            tryCompare(d, "visible", false)
        }

        // ---- accent and font size apply at once ----
        function test_accent_data() { return [0, 1, 2, 3].map(i => ({ tag: "a" + i, i: i })) }
        function test_accent(data) {
            const a = Theme.accents[data.i]
            Theme.accent = a
            verify(Qt.colorEqual(primary.fill, a) && Qt.colorEqual(onBtn.ink, a) && Qt.colorEqual(onIcon.contentItem.color, a))
            verify(Qt.colorEqual(link.contentItem.color, a) && Qt.colorEqual(spin.color, a) && Qt.colorEqual(root.ring.border.color, a))
            verify(Qt.colorEqual(tog.contentItem.children[1].color, a), "switch on")
            verify(near(Theme.acc2, Qt.rgba(Qt.color(a).r, Qt.color(a).g, Qt.color(a).b, 0.16), 1))
            waitForRendering(page)
            const g = grabImage(root), p = primary.mapToItem(root, 3, primary.height / 2)
            verify(near(g.pixel(p.x, p.y), Qt.color(a)), "drawn in the new accent")
        }
        function test_font_size() {
            const w = plain.implicitWidth, pt = plain.font.pointSize, icon = glyph.font.pixelSize
            Theme.fontSize = "l"
            fuzzyCompare(plain.font.pointSize, pt * 13.5 / 12, 0.001)
            tryVerify(() => plain.implicitWidth > w, 1000, "the button grows with its text")
            fuzzyCompare(sel.font.pointSize, Theme.pt(12), 0.001)
            fuzzyCompare(tog.font.pointSize, Theme.pt(12), 0.001)
            fuzzyCompare(head.children[0].font.pointSize, Theme.pt(10.5), 0.001)
            compare(glyph.font.pixelSize, icon, "icons keep their size")
            Theme.fontSize = "s"
            fuzzyCompare(plain.font.pointSize, pt * 11 / 12, 0.001)
            tryVerify(() => plain.implicitWidth < w)
        }

        // ---- icons ----
        // Each name of make-icons.sh and StrokeIcons.js inks the 20 px box, and differently from
        // every other: a missing glyph would fall back to the same box or nothing.
        function test_every_icon() {
            const names = Object.keys(Icons.cp).concat(Object.keys(StrokeIcons.K))
            const sheet = createTemporaryObject(blackC, root, { width: 20 * 32, height: Math.ceil(names.length / 20) * 32 })
            names.forEach((n, i) => createTemporaryObject(iconC, sheet, { name: n, x: (i % 20) * 32 + 6, y: Math.floor(i / 20) * 32 + 6, width: 20, height: 20 }))
            waitForRendering(sheet)
            const g = grabImage(sheet), seen = {}, same = []
            names.forEach((n, i) => {
                const x0 = (i % 20) * 32, y0 = Math.floor(i / 20) * 32
                let key = ""
                for (let y = y0 + 4; y < y0 + 28; y++) for (let x = x0 + 4; x < x0 + 28; x++) key += g.red(x, y) > 128 ? "1" : "0"
                verify(key.indexOf("1") >= 0, "no ink: " + n)
                if (key in seen) same.push(n + " = " + seen[key])
                seen[key] = n
            })
            compare(same.join(", "), "photo = image", "only Material's aliases (one glyph, two names) draw alike")
        }
        // A drawn icon takes the room of a glyph, sits where one would and has its 52/960 line:
        // plus (drawn) against add (glyph), both a cross of 52-unit bars on the same centre.
        function test_drawn_like_glyph() {
            for (const s of [16, 20, 24]) {
                glyph.size = s
                drawn.size = s
                compare(drawn.implicitWidth, glyph.implicitWidth)
                compare(drawn.implicitHeight, glyph.implicitHeight)
            }
            glyph.size = 20
            drawn.size = 20
            compare(stroke.implicitWidth, 20)
            const sheet = createTemporaryObject(blackC, root, { width: 240, height: 120 })
            const a = createTemporaryObject(iconC, sheet, { name: "add", size: 96, x: 10, y: 10 })
            const b = createTemporaryObject(iconC, sheet, { name: "plus", size: 96, x: 130, y: 10 })
            waitForRendering(sheet)
            const g = grabImage(sheet), ia = ink(g, 0, 0, 120, 120), ib = ink(g, 120, 0, 240, 120)
            verify(Math.abs(ia.cx - (ib.cx - 120)) < 1 && Math.abs(ia.cy - ib.cy) < 1, "same centre")
            // the vertical bar at x = centre ± 20 is the horizontal stroke: 52/960 of 96 = 5.2 px
            const bar = (x0) => { const r = ink(g, x0, 0, x0 + 1, 120); return r.bottom - r.top + 1 }
            verify(Math.abs(bar(Math.round(ia.cx) - 20) - 5.2) <= 1.2, "glyph line " + bar(Math.round(ia.cx) - 20))
            verify(Math.abs(bar(Math.round(ib.cx) - 20) - 5.2) <= 1.2, "drawn line " + bar(Math.round(ib.cx) - 20))
            b.color = "black"
            waitForRendering(sheet)
            compare(ink(grabImage(sheet), 120, 0, 240, 120).n, 0, "colour follows")
        }
        // Dashed parts (x) are dashed: the dashed circle of load_sel, 300 of 960 wide, breaks
        // about 16 times (64 on, 52 off) around.
        function test_dashes() {
            const sheet = createTemporaryObject(blackC, root, { width: 120, height: 120 })
            createTemporaryObject(iconC, sheet, { name: "load_sel", size: 96, x: 12, y: 12, width: 96, height: 96 })
            waitForRendering(sheet)
            const g = grabImage(sheet), r = 300 / 960 * 96
            let gaps = 0, was = true
            for (let k = 0; k < 360; k++) {
                const a = k * Math.PI / 180, on = g.red(Math.round(60 + r * Math.cos(a)), Math.round(60 + r * Math.sin(a))) > 128
                if (was && !on) gaps++
                was = on
            }
            verify(gaps >= 12 && gaps <= 20, gaps + " gaps")
        }
        // The bundled fonts answer to the names Theme gives: sans, mono, the wordmark's Manrope 800.
        function test_fonts() {
            for (const [family, weight] of [[Theme.sans, Font.Normal], [Theme.sans, Font.DemiBold], [Theme.mono, Font.Normal], [Theme.logo, Font.ExtraBold], [Theme.icons, Font.Normal]]) {
                const t = createTemporaryObject(textC, root, { "font.family": family, "font.weight": weight })
                compare(t.fontInfo.family, family)
            }
            const logo = createTemporaryObject(textC, root, { "font.family": Theme.logo, "font.weight": Font.ExtraBold })
            compare(logo.fontInfo.styleName, "ExtraBold")
        }
        // Each CSS weight of the prototype's Space Grotesk is its own face (U23): 600 is SpaceGrotesk-SemiBold.ttf, not
        // the variable font's Medium; more ink at each step.
        function test_weights() {
            const sheet = createTemporaryObject(blackC, root, { width: 420, height: 4 * 34 })
            const ws = [Font.Normal, Font.Medium, Font.DemiBold, Font.Bold], ts = ws.map((w, i) =>
                createTemporaryObject(textC, sheet, { y: i * 34, color: "white", text: "Hamburgefonstiv Dodge & Burn", "font.family": Theme.sans,
                                                      "font.pixelSize": 24, "font.weight": w }))
            waitForRendering(sheet)
            const g = grabImage(sheet), n = ts.map((t, i) => ink(g, 0, i * 34, 420, i * 34 + 34).n)
            compare(ts.map(t => t.fontInfo.weight), [400, 500, 600, 700])
            compare(ts[2].fontInfo.styleName, "SemiBold")
            for (let i = 1; i < 4; i++)
                verify(n[i] > n[i - 1] * 1.05, "ink " + n.join(", "))
        }
        function test_unknown_icon() {
            const i = createTemporaryObject(iconC, root, { name: "no_such_icon" })
            compare(i.text, "")
        }
        // The spinner turns on the GUI thread: an Animator's rotation only lands when it ends.
        function test_spinner_turns() {
            const r = spin.rotation
            wait(150)
            verify(spin.rotation !== r, "rotation " + r + " → " + spin.rotation)
        }

        // ---- behaviour ----
        function test_select() {
            const picked = createTemporaryObject(signalSpy, root, { target: sel, signalName: "picked" })
            compare(sel.displayText, "sRGB IEC61966-2.1")
            mouseClick(sel)
            tryCompare(sel.popup, "opened", true)
            waitForRendering(sel.popup.contentItem)
            const lv = sel.popup.contentItem
            mouseClick(lv.itemAtIndex(2))
            tryCompare(sel.popup, "visible", false)
            compare(picked.count, 1)
            compare(picked.signalArguments[0][0], "p3")
            compare(sel.displayText, "sRGB IEC61966-2.1", "the model answers, not the select")
            sel.value = "p3"
            compare(sel.displayText, "Display P3")
            compare(sel.currentIndex, 2)
            sel.value = "srgb"
            // The list as wide as its longest option when the box is not (.pop's min-width: 100%, U23; tst_u21's
            // test_select: in a dialog, where the font comes late)
            const narrow = createTemporaryQmlObject("import PhotoBeauty; Select { x: 20; y: 20; width: 90; value: 'p3'; options: [['srgb', 'sRGB IEC61966-2.1'], ['p3', 'Display P3']] }", root)
            mouseClick(narrow)
            tryCompare(narrow.popup, "opened", true)
            waitForRendering(narrow.popup.contentItem)
            const row = narrow.popup.contentItem.itemAtIndex(0)
            verify(narrow.popup.width > 90 && row.contentItem.implicitWidth <= row.availableWidth, narrow.popup.width)
            compare(narrow.popup.x, 0, "from the box's left")
            keyClick(Qt.Key_Escape)
            tryCompare(narrow.popup, "visible", false)
        }
        function test_toggle() {
            const clicked = createTemporaryObject(signalSpy, root, { target: togOff, signalName: "clicked" })
            mouseClick(togOff)
            compare(clicked.count, 1)
            verify(!togOff.checked, "checked comes from the model")
        }
        function test_dialog() {
            let did = ""
            const d = createTemporaryObject(dialogC, root, {
                note: "Hồ sơ nhúng khác với không gian làm việc.", icon: "warning", iconColor: Theme.warn, closable: true,
                buttons: [{ text: "Hủy", act: () => { did = "cancel" } }, { text: "OK", primary: true, act: () => { did = "ok" } }]
            })
            d.open()
            tryCompare(d, "opened", true)
            waitForRendering(d.contentItem)
            const ok = find(d.footer, o => o instanceof Btn && o.text === "OK")
            verify(ok.primary && ok.width >= 84, "primary at least 84 wide")
            compare(d.header.height, 46)
            compare([d.footer.height, ok.mapToItem(d.footer, 0, 0).y, ok.height], [53, 13, 28], ".df: its rule, 12, the 28 px buttons, 12 (U23)")
            const bg = d.background
            compare([bg.x, bg.y, bg.width, bg.height], [-1, -1, d.width + 2, d.height + 2], "the edge outside, as .dlg's box-shadow")
            d.boxWidth = 441
            verify(Number.isInteger(d.x) && Number.isInteger(d.y), "centred on whole pixels: " + d.x + ", " + d.y)
            mouseClick(ok)
            compare(did, "ok")
            keyClick(Qt.Key_Escape)
            tryCompare(d, "visible", false)
            d.open()
            tryCompare(d, "opened", true)
            mouseClick(find(d.header, o => o instanceof IconButton && o.icon.name === "close"))
            tryCompare(d, "visible", false)
            const bare = createTemporaryObject(dialogC, root)
            bare.open()
            tryCompare(bare, "opened", true)
            compare(bare.footer.height, 0, "no buttons, no footer")
        }
        function test_toast() {
            const done = createTemporaryObject(signalSpy, root, { target: root.toast, signalName: "done" })
            verify(!root.toast.visible)
            root.toast.duration = 80
            root.toast.ok = true
            root.toast.text = "Đã lưu chân dung.psd"
            verify(root.toast.visible)
            compare(root.toast.parent, root.Overlay.overlay)
            fuzzyCompare(root.toast.x + root.toast.width / 2, root.width / 2, 1)
            compare(root.toast.y + root.toast.height + 22, root.height)
            const mark = find(root.toast, o => o instanceof Icon)
            verify(mark.visible && mark.name === "check" && Qt.colorEqual(mark.color, Theme.accent))
            tryCompare(done, "count", 1, 1000, "gone after its duration")
            root.toast.text = ""
            verify(!root.toast.visible)

            root.toast.err = true
            root.toast.text = "Không lưu được"
            verify(mark.name === "error" && Qt.colorEqual(mark.color, Theme.warn))
            wait(50)
            root.toast.restart()  // the same text again: another full duration
            wait(50)
            compare(done.count, 1)
            tryCompare(done, "count", 2, 1000)

            let ran = false
            root.toast.duration = 5000
            root.toast.act = ["Mở", () => { ran = true }]
            root.toast.restart()
            waitForRendering(root)
            const btn = find(root.Overlay.overlay, o => o instanceof Btn && o.text === "Mở")
            mouseClick(btn)
            verify(ran)
            compare(done.count, 3)
            root.toast.duration = Qt.binding(() => root.toast.err || root.toast.act ? 4500 : 2200)
        }

        // ---- keyboard: Tab reaches each control and shows the ring, Enter and Space press, Esc closes ----
        function test_keyboard() {
            plain.forceActiveFocus(Qt.TabFocusReason)
            const order = []
            for (let i = 0; i < 12; i++) {
                order.push(root.Window.activeFocusItem)
                keyClick(Qt.Key_Tab)
            }
            for (const c of [plain, primary, onBtn, iconBtn, onIcon, link, tog, togOff, sel, box])
                verify(order.indexOf(c) >= 0, "Tab reaches " + c)
            verify(order.indexOf(offBtn) < 0, "not the disabled button")

            primary.forceActiveFocus(Qt.TabFocusReason)
            const p = primary.mapToItem(root.ring.parent, 0, 0)
            tryVerify(() => root.ring.visible && Math.abs(root.ring.x + 4 - p.x) < 1 && Math.abs(root.ring.y + 4 - p.y) < 1, 2000, "a ring 2 px off what Tab reached")
            const clicked = createTemporaryObject(signalSpy, root, { target: primary, signalName: "clicked" })
            keyClick(Qt.Key_Space)
            compare(clicked.count, 1, "Space presses")
            keyClick(Qt.Key_Return)
            compare(clicked.count, 2, "Enter presses (EnterPresses of look.h)")
            mouseClick(plain)
            verify(!root.ring.visible, "no ring on what a click focused")

            sel.forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Space)
            tryCompare(sel.popup, "opened", true)
            keyClick(Qt.Key_Escape)
            tryCompare(sel.popup, "visible", false)
        }
        Component { id: signalSpy; SignalSpy {} }
    }
}
