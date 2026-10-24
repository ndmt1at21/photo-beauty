import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Templates as T
import QtCore
import QtTest
import PhotoBeauty

// N2: real documents saved as .pbd (Session.save(): save_pbd(), M2, M3) in a Background job, Docs.job showing its
// progress and Hủy, the document going on meanwhile; Lưu appending into the .pbd, or asking where first (Main's
// FileDialog) for one opened from a PNG, JPEG, TIFF; the unsaved dot by the history's step saved (T2); closing unsaved
// through SaveChanges (U20); the status bar's RAM, compressed and scratch from the tile store's stats() (K1); Thiết lập ›
// Hiệu năng on this machine (Prefs.machine): its RAM and disks, the budget and scratch folder set. PB_CANVAS=<a .pbd>
// (D2-mini: pb-bench make d2mini) adds a save of it, its progress shown while layers change, and Hủy in its middle.
Item {
    id: root
    width: 400
    height: 300
    Component { id: mainC; Main {} }
    Component { id: statusC; StatusBar { width: 1440 } }

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
    function all(item, pred, out) {
        out = out || []
        for (const c of item.children) {
            if (pred(c))
                out.push(c)
            all(c, pred, out)
        }
        return out
    }

    SignalSpy { id: asked; target: Docs; signalName: "pathWanted" }
    SignalSpy { id: ended; target: Session; signalName: "saveEnded" }
    SignalSpy { id: gpus; target: Session; signalName: "gpuChanged" }
    Connections {  // the progress a real save shows; Hủy as it first shows some (test_d2mini)
        id: hurry
        property bool armed: false
        property var seen: []
        target: Docs
        function onJobChanged() {
            if (!Docs.job.real || !Docs.job.run)
                return
            if (hurry.seen.indexOf(Docs.job.p) < 0)
                hurry.seen.push(Docs.job.p)
            if (hurry.armed && Docs.job.p > 0) {
                hurry.armed = false
                Docs.cancelJob()
            }
        }
    }

    TestCase {
        name: "N2"
        when: windowShown

        property real budget: 0  // the tile store's before the test

        function initTestCase() { budget = Session.stats().budget }
        function init() {
            Docs.reset("Main")
            Tools.reset("Main")
            Docs.error = ""
            Session.cancelAt = 2
            asked.clear()
            ended.clear()
        }
        function cleanup() {
            tryVerify(() => !Session.saving, 60000, "no save left running")
            Prefs.reset()
            Prefs.machine = false
            Prefs.found = null
            Prefs.applied = ""
            Session.setBudget(budget)
            Session.useScratch([])
        }

        // ---- helpers ------------------------------------------------------------------------------------------------
        function file(name, kind, w, h, o) {
            o = o || {}
            const p = fixtures.image(name, kind, w, h, o.bits || 8, !!o.gray, !!o.alpha)
            verify(p, "made " + name)
            return p
        }
        function at(name) { return fixtures.folder() + "/" + name }
        function opened(path, ms) {
            const d = Docs.openFile(path)
            verify(d && d.real, "opened for real: " + path)
            tryVerify(() => !d.model.loading, ms || 20000, "read")
            compare(d.model.error, "")
            return d
        }
        // The layers as the core keeps them, bottom first, without tile ids (a file read again has others).
        function shape(d) {
            const strip = (l) => ({ kind: l.kind, props: l.props, layers: (l.layers || []).map(strip) })
            return JSON.parse(d.model.dump()).layers.map(strip)
        }
        function saved() {  // the save running ended
            tryVerify(() => !Session.saving, 60000, "saved")
            wait(0)
        }
        function bytes(path) { return Session.fileInfo(path).bytes || 0 }
        function last() {  // saveEnded's: path, error, cancelled
            return Array.prototype.slice.call(ended.signalArguments[ended.count - 1], 1, 4)
        }
        function shot(item, name) {  // for the report: pb-n2-<name>.png in the temporary folder
            waitForRendering(item)
            grabImage(item).save(Session.localPath(String(StandardPaths.writableLocation(StandardPaths.TempLocation))) + "/pb-n2-" + name + ".png")
        }
        function openMain() {
            const w = createTemporaryObject(mainC, root, { startState: "Main" })
            w.requestActivate()
            tryVerify(() => w.active)
            waitForRendering(w.contentItem)
            return w
        }
        function dialogOn(w, type) {
            let d = null
            tryVerify(() => (d = w.dialogs.find(x => x instanceof type) || null) && d.opened)
            return d
        }
        function button(item, text) { return find(item, o => o instanceof T.AbstractButton && o.text === text && o.visible) }
        function texts(item) { return all(item, o => o instanceof Text && o.visible && o.text !== "").map(t => t.text) }

        // ---- Lưu, Lưu thành -----------------------------------------------------------------------------------------
        // A PNG, then a 16-bit grey TIFF: Lưu asks where (no .pbd yet) and saves nothing; saved there in the background,
        // the job saying so; then the document is that .pbd, saved, first in Gần đây; opened again, the same pixels and
        // layers.
        function test_png_to_pbd() {
            for (const [name, kind, o] of [["n2-a", "png", { alpha: true }], ["n2-b", "tif", { bits: 16, gray: true }]]) {
                const d = opened(file(name + "." + kind, kind, 300, 200, o))
                verify(!d.dirty)
                const pixels = d.model.digest(), layers = shape(d), mode = [d.w, d.h, d.mode, d.bits]
                verify(pixels.length > 0)
                Docs.save(d)
                compare(asked.count, 1, "Lưu asks where: no .pbd yet")
                compare([asked.signalArguments[0][0], asked.signalArguments[0][1]], [d, false])
                verify(!Session.saving)
                asked.clear()

                verify(Docs.saveAs(d, at(name), false))  // ".pbd" added
                const path = at(name + ".pbd")
                verify(Session.saving)
                compare([Docs.job.on, Docs.job.run, Docs.job.real, Docs.job.label], [true, true, true, "Đang lưu " + name + ".pbd"])
                saved()
                compare([Docs.job.on, Docs.job.done, Docs.job.label], [true, true, "Đã lưu " + name + ".pbd"])
                compare([d.model.path, d.model.pbd, d.file, d.dirty, d.unsaved], [path, true, name + ".pbd", false, 0])
                compare(Docs.recent[0].path, path, "Gần đây")
                compare(d.model.bytes, bytes(path))
                verify(bytes(path) > 0)
                compare(last(), [path, "", false])

                Docs.close(d)
                const e = opened(path)
                compare([e.w, e.h, e.mode, e.bits], mode)
                compare(e.model.digest(), pixels, "the same pixels")
                compare(shape(e), layers, "the same layers")
                verify(!e.dirty && e.model.pbd)
                Docs.close(e)
            }
        }

        // Lưu into the .pbd it is: appended (M3: the file grows by a preview, JSON, index; a full save would come out
        // about as long), no path asked; the change there once opened again.
        function test_append() {
            const d = opened(file("n2-c.png", "png", 600, 400))
            const path = at("n2-c.pbd")
            verify(Docs.saveAs(d, path, false))
            saved()
            const first = bytes(path), pixels = d.model.digest()
            verify(d.run({ op: "setProps", props: { opacity: 0.5 } }))
            verify(d.dirty)
            verify(Docs.exec("@save"))  // Lưu, Ctrl S
            compare(asked.count, 0)
            verify(Session.saving)
            saved()
            verify(!d.dirty)
            verify(bytes(path) > first + 200, "appended: " + first + " → " + bytes(path))
            const grown = bytes(path)
            verify(Docs.exec("@save"))  // saved and unchanged: nothing to write
            verify(!Session.saving)
            compare(bytes(path), grown)
            Docs.close(d)
            const e = opened(path)
            compare(e.layers[0].props.opacity, 0.5)
            compare(e.model.digest(), pixels)
            compare(e.history.length, 1, "opened at its step")
        }

        // The dot follows the step saved: undo back to it is saved, redo not; a save is of the step it was asked on, the
        // document going on meanwhile; a command after undoing past the step saved leaves no way back to it.
        function test_dirty() {
            const d = opened(file("n2-d.png", "png", 300, 200))
            verify(Docs.saveAs(d, at("n2-d.pbd"), false))
            saved()
            compare([d.dirty, d.unsaved], [false, 0])
            verify(d.run({ op: "setProps", props: { name: "Một" } }))
            compare([d.dirty, d.unsaved], [true, 1])
            verify(d.run({ op: "setProps", props: { visible: false } }))
            compare([d.dirty, d.unsaved, d.index], [true, 2, 2])
            d.undo()
            compare([d.dirty, d.unsaved], [true, 1])
            d.undo()
            compare([d.dirty, d.unsaved], [false, 0], "undone to the step saved")
            d.redo()
            compare([d.dirty, d.unsaved], [true, 1], "redone past it")
            d.redo()
            compare(d.index, 2)

            Docs.save(d)  // of step 2; the document goes on before it ends
            verify(Session.saving)
            verify(d.run({ op: "setProps", props: { opacity: 0.25 } }))
            saved()
            compare([d.index, d.dirty, d.unsaved], [3, true, 1], "the step after the one saved")
            d.undo()
            compare([d.dirty, d.unsaved], [false, 0], "step 2 is the one saved")
            d.jumpTo(0)
            compare([d.dirty, d.unsaved], [true, 2])
            verify(d.run({ op: "setProps", props: { name: "Hai" } }))  // steps 1.. go, the one saved with them
            compare([d.dirty, d.unsaved, d.index], [true, 1, 1])
            d.undo()
            compare([d.dirty, d.index], [true, 0], "the step saved is gone")

            Docs.close(d)
            const e = opened(at("n2-d.pbd"))
            compare([e.layers[0].props.name, e.layers[0].props.visible, e.layers[0].props.opacity], ["Một", false, 1], "the step saved")
        }

        // Hủy in the middle of a full save over another file: that file opens as it was; the document as it was (not
        // saved, its path its PNG's). Cancelled as the first batch of tiles is written (Session.cancelAt), as Hủy would.
        function test_cancel() {
            const path = at("n2-e.pbd")
            const b = opened(file("n2-e.png", "png", 256, 256))
            verify(Docs.saveAs(b, path, false))
            saved()
            const was = b.model.digest(), size = bytes(path)
            Docs.close(b)

            const png = file("n2-f.png", "png", 1200, 900, { alpha: true })
            const a = opened(png)
            verify(a.run({ op: "setProps", props: { name: "Đổi" } }))
            Session.cancelAt = 0
            verify(Docs.saveAs(a, path, false))
            compare(Session.cancelAt, 2, "once")
            saved()
            compare(last(), [path, "", true], "cancelled")
            compare([Docs.job.on, Docs.job.done], [false, false], "the job gone, not done")
            compare([a.dirty, a.model.path, a.model.pbd, a.file], [true, png, false, "n2-f.png"])
            compare(bytes(path), size)
            const e = opened(path)
            compare(e.model.digest(), was, "the file as it was")
            Docs.close(e)

            // Hủy on the job: it goes at once, the save running stops, the one waiting with it; a save after that runs
            verify(Docs.saveAs(a, at("n2-f.pbd"), false))
            verify(Docs.saveAs(a, at("n2-f2.pbd"), true))
            compare(Session.waiting, 1)
            Docs.cancelJob()
            compare([Docs.job.on, Session.waiting], [false, 0])
            saved()
            compare(bytes(at("n2-f2.pbd")), 0, "the one waiting never made")
            verify(Docs.saveAs(a, at("n2-f.pbd"), false))
            saved()
            compare([Docs.job.done, a.dirty, a.model.path], [true, false, at("n2-f.pbd")])
        }

        // A file that cannot be written: why, in Main's toast (Docs.error); the document as it was.
        function test_error() {
            const d = opened(file("n2-k.png", "png", 300, 200))
            verify(d.run({ op: "setProps", props: { name: "X" } }))
            const path = at("no/such/folder/n2-k.pbd")
            verify(Docs.saveAs(d, path, false))
            saved()
            const l = last()
            compare([l[0], l[2]], [path, false])
            verify(l[1] !== "", "why")
            compare(Docs.error, "Không lưu được n2-k.pbd: " + l[1])
            compare([Docs.job.on, d.dirty, d.model.pbd, d.model.path], [false, true, false, at("n2-k.png")])
        }

        // Lưu while a save runs: it waits for it, then saves the step it is on then; a copy (Lưu bản sao) leaves the
        // document as it was.
        function test_queue() {
            const d = opened(file("n2-g.png", "png", 300, 200))
            verify(Docs.saveAs(d, at("n2-g.pbd"), false))
            verify(d.run({ op: "setProps", props: { name: "Sau" } }))
            Docs.save(d)  // its .pbd once the first save ends: waits
            compare(Session.waiting, 1)
            Docs.save(d)  // the same again: once
            compare(Session.waiting, 1)
            tryVerify(() => ended.count === 2, 30000, "both")
            saved()
            compare(Session.waiting, 0)
            compare([d.dirty, d.file], [false, "n2-g.pbd"])

            verify(d.run({ op: "setProps", props: { name: "Bản sao" } }))
            verify(Docs.saveAs(d, at("n2-g copy"), true))
            saved()
            compare([d.dirty, d.model.path, d.file], [true, at("n2-g.pbd"), "n2-g.pbd"], "a copy: the document as it was")
            verify(bytes(at("n2-g copy.pbd")) > 0)
            Docs.close(d)
            compare(opened(at("n2-g copy.pbd")).layers[0].props.name, "Bản sao")

            // one waiting, its document closed meanwhile (SaveChanges' Lưu, then the tab goes): still made, as it was
            const a = opened(file("n2-l.png", "png", 300, 200)), b = opened(file("n2-m.png", "png", 300, 200))
            verify(b.run({ op: "setProps", props: { name: "Đóng rồi" } }))
            ended.clear()
            verify(Docs.saveAs(a, at("n2-l.pbd"), false))
            verify(Docs.saveAs(b, at("n2-m.pbd"), false))
            compare(Session.waiting, 1)
            compare(Session.savingTo(b.model), at("n2-m.pbd"))
            verify(b.run({ op: "setProps", props: { name: "Sau khi lưu" } }))
            Docs.close(b)
            tryVerify(() => ended.count === 2, 30000)
            saved()
            compare(last(), [at("n2-m.pbd"), "", false])
            compare(opened(at("n2-m.pbd")).layers[0].props.name, "Đóng rồi", "the state asked for")
        }

        // ---- Main: Ctrl S, Lưu thành…, Lưu bản sao…, closing unsaved ----------------------------------------------------
        function test_menu() {
            const w = openMain()
            const fd = findChild(w, "savePath")
            verify(fd)
            const d = opened(file("n2-h.png", "png", 300, 200))
            compare(Docs.current, d)
            keySequence("Ctrl+S")  // no .pbd yet: Lưu thành
            compare(asked.count, 1)
            tryVerify(() => fd.visible)
            compare([fd.doc, fd.copy, fd.title], [d, false, "Lưu thành"])
            compare(Session.localPath(String(fd.selectedFile)), at("n2-h.pbd"), "beside it, named after it")
            fd.accept()  // as proposed
            verify(Session.saving)
            saved()
            compare([d.model.path, d.dirty, d.file], [at("n2-h.pbd"), false, "n2-h.pbd"])
            shot(w.contentItem, "saved")  // the job's Đã lưu, the tab's name, the status bar's numbers

            verify(d.run({ op: "setProps", props: { name: "Sao" } }))
            keySequence("Ctrl+Alt+S")  // Lưu bản sao…: the FileDialog, not the mock's SaveOptions
            compare([fd.doc, fd.copy, fd.title], [d, true, "Lưu bản sao"])
            compare(Session.localPath(String(fd.selectedFile)), at("n2-h bản sao.pbd"))
            verify(!w.dialogs.some(x => x instanceof SaveOptionsDialog))
            fd.accept()
            saved()
            compare([d.dirty, d.model.path], [true, at("n2-h.pbd")])
            verify(bytes(at("n2-h bản sao.pbd")) > 0)

            keySequence("Ctrl+Shift+S")  // Lưu thành…, then no file picked: nothing
            compare([fd.doc, fd.copy], [d, false])
            fd.reject()
            verify(!Session.saving)

            Docs.select(0)  // a mock document: SaveOptions as before
            verify(!Docs.current.real)
            keySequence("Ctrl+Shift+S")
            dialogOn(w, SaveOptionsDialog).close()
        }

        // Closing unsaved asks (U20) with the steps since the save; Lưu of one without a .pbd asks where, then saves and
        // closes; no file picked: nothing closes. One with a .pbd saves into it and closes at once.
        function test_close_unsaved() {
            const w = openMain()
            const fd = findChild(w, "savePath")
            const d = opened(file("n2-i.png", "png", 300, 200))
            verify(d.run({ op: "setProps", props: { name: "Một" } }))
            verify(d.run({ op: "setProps", props: { opacity: 0.5 } }))
            w.closeDoc(d)
            let s = dialogOn(w, SaveChangesDialog)
            verify(texts(s.contentItem).indexOf("Không lưu thì mất 2 bước chỉnh từ lần lưu trước.") >= 0, texts(s.contentItem))
            mouseClick(button(s.footer, "Lưu"))
            compare(asked.count, 1)
            fd.reject()  // no file: nothing closes
            verify(s.visible && Docs.docs.indexOf(d) >= 0)
            mouseClick(button(s.footer, "Lưu"))
            fd.accept()  // beside its PNG, as proposed
            tryVerify(() => Docs.docs.indexOf(d) < 0, 5000, "closed once it saves")
            saved()
            const e = opened(at("n2-i.pbd"))
            compare(e.layers[0].props.opacity, 0.5)

            verify(e.run({ op: "setProps", props: { visible: false } }))
            verify(e.run({ op: "setProps", props: { name: "Ba" } }))
            Docs.save(e)  // at step 2
            saved()
            verify(e.run({ op: "setProps", props: { opacity: 0.25 } }))
            compare([e.index, e.unsaved], [3, 1])
            w.closeDoc(e)
            s = dialogOn(w, SaveChangesDialog)
            verify(texts(s.contentItem).indexOf("Không lưu thì mất 1 bước chỉnh từ lần lưu trước.") >= 0)
            asked.clear()
            mouseClick(button(s.footer, "Lưu"))
            compare(asked.count, 0, "its .pbd")
            tryVerify(() => Docs.docs.indexOf(e) < 0)
            saved()
            const f = opened(at("n2-i.pbd"))
            compare([f.layers[0].props.visible, f.layers[0].props.opacity], [false, 0.25])
        }

        // ---- the status bar ---------------------------------------------------------------------------------------------
        // A real document: RAM (with its compressed part) against the budget, scratch, as stats() has them, the
        // document's size; pushed out of RAM under a small budget, the numbers follow. A mock one: the prototype's.
        function test_status_bar() {
            const sb = createTemporaryObject(statusC, root)
            const d = opened(file("n2-j.png", "png", 1000, 800, { alpha: true }))
            compare(Docs.current, d)
            verify(sb.live)
            const mono = () => all(sb, o => o instanceof Text && o.visible && o.font.family === Theme.mono).map(t => t.text)
            let seen = ""
            const matches = () => {  // stats() as the bar read it, the store still (its Background job may be moving tiles)
                const before = JSON.stringify(Session.stats())
                sb.refresh()
                const s = Session.stats()
                seen = before + " " + JSON.stringify(sb.stats) + " " + JSON.stringify(mono())
                return before === JSON.stringify(s) && JSON.stringify(sb.stats) === before
                    && mono().indexOf(sb.bytes(s.ram) + " / " + sb.bytes(s.budget) + " · nén " + sb.bytes(s.compressed)) >= 0
                    && mono().indexOf(sb.bytes(s.scratch)) >= 0
            }
            const follows = (what) => {
                for (let t = Date.now(); !matches(); wait(50))
                    if (Date.now() - t > 10000)
                        fail(what + ": " + seen)
            }
            follows("the bar shows stats()")
            const s = Session.stats()
            verify(s.raw >= 16 * 256 * 256 * 4, "the document's 16 tiles in RAM")
            compare(s.ram, s.raw + s.compressed + s.charged)
            compare([sb.bytes(0), sb.bytes(516096), sb.bytes(3 * 1048576), sb.bytes(1.25 * 1073741824)], ["0 MB", "504 KB", "3 MB", "1,3 GB"])
            const mb = 1048576, made = { raw: mb, compressed: 2 * mb, charged: 5 * mb, scratch: 7 * mb, pinned: 0, ram: 8 * mb, budget: 1024 * mb }
            sb.stats = made  // which number goes where
            compare([sb.ramText, sb.scratchText], ["8 MB / 1,0 GB · nén 2 MB", "7 MB"])
            verify(mono().indexOf("1.000 × 800 px · 72 ppi") >= 0, "its size")
            Tools.statInfo = 0
            compare(sb.infoText, d.mem)
            compare(d.mem, "2,4 MB / 4,19 MB", "flattened, all layers")
            Tools.statInfo = 3
            tryVerify(() => { sb.refresh(); return sb.infoText === sb.bytes(Session.stats().ram) + " · " + sb.bytes(Session.stats().scratch) })

            Session.setBudget(1048576)  // 1 MB: pushed out, compressed or to scratch
            tryVerify(() => Session.stats().compressed + Session.stats().scratch > 0, 10000, "pushed out")
            follows("the bar follows")
            Session.setBudget(budget)

            Docs.select(0)  // a mock document: the prototype's numbers
            verify(!sb.live)
            verify(mono().indexOf("3,1 / 9,6 GB") >= 0 && mono().indexOf("1,2 GB") >= 0)
        }

        // ---- Thiết lập › Hiệu năng on this machine ------------------------------------------------------------------
        function test_machine() {
            const proto = Prefs.system
            compare([proto.ramGB, proto.gpuName], [16, "Intel Iris Xe Graphics"], "the prototype's until machine")
            const m = Session.machine()
            verify(m.ram > 1e9 && m.disks.length > 0, JSON.stringify(m))
            compare(m.disks[0].dir, m.temp, "the temporary folder's disk first, scratch in that folder")
            Prefs.machine = true
            tryVerify(() => Prefs.found !== null, 10000, "the disks looked for, in the background")
            const gb = 1073741824
            compare(Prefs.system.ramGB, m.ram / gb)
            compare(Object.keys(Prefs.system.disks), m.disks.map(x => x.name))
            compare(Prefs.value("scratchDisks"), m.disks.map((x, i) => ({ name: x.name, on: i === 0 })), "this machine's disks, the first on")
            compare(Session.stats().budget, Math.round(m.ram * 0.6), "60 % of its RAM")
            compare(Session.scratchDir, m.temp)
            Prefs.set("ramBudget", 45)
            compare(Session.stats().budget, Math.round(m.ram * 0.45))
            tryVerify(() => Prefs.used !== null)
            tryVerify(() => { const u = Session.stats(); return Math.abs(Prefs.system.ramUsed - u.ram / gb) < 1e-9 }, 5000)
            tryVerify(() => Session.gpu.found !== undefined, 20000, "the GPU looked for")
            verify(Prefs.system.gpuName !== "" && Prefs.system.gpuName !== proto.gpuName)
            compare(Prefs.system.vramUsed, 0)

            // a scratch folder that can be made; then none on: the temporary folder
            compare(Session.useScratch(["", at("scratch/deeper")]), at("scratch/deeper"))
            compare(Session.scratchDir, at("scratch/deeper"))
            Prefs.set("scratchDisks", Prefs.value("scratchDisks").map(x => ({ name: x.name, on: false })))
            compare(Session.scratchDir, m.temp)

            // Kiểm tra lại looks again
            const w = openMain()
            Prefs.machine = true  // Main sets it as main.cpp says: not in tests
            const dlg = w.openDialog("Settings", { page: "performance" })
            tryVerify(() => dlg.opened)
            shot(w.contentItem, "settings")
            gpus.clear()
            const again = find(dlg.contentItem, o => o instanceof Btn && o.text === "Kiểm tra lại")
            verify(again)
            mouseClick(again)
            tryVerify(() => gpus.count === 1, 20000)
            verify(texts(dlg.contentItem).indexOf(Prefs.system.gpuName) >= 0)
            dlg.close()
        }

        // ---- D2-mini (PB_CANVAS) ---------------------------------------------------------------------------------------
        // Saved as a .pbd in the background: the UI only hands its state over, the job shows the save going, the layers
        // change meanwhile, each a step; the file has the step saved. Then Hủy in the middle of a save over another file:
        // that file opens as it was.
        function test_d2mini() {
            if (CanvasEnv.document === "")
                skip("PB_CANVAS=<D2-mini .pbd> runs this")
            const w = openMain()  // opens PB_CANVAS's file, as the command line's (N1)
            const d = Docs.docs.find(x => x.real && x.model.path === CanvasEnv.document)
            verify(d)
            tryVerify(() => !d.model.loading, 120000, "read")
            compare(Docs.current, d)
            const ids = d.layers.filter(l => l.kind === "pixel").map(l => l.id)
            verify(ids.length >= 10, "D2-mini's layers")
            verify(d.run({ op: "setProps", layer: ids[0], props: { visible: false } }))
            const snap = shape(d), index = d.index
            const path = at("n2-d2.pbd")
            const t0 = Date.now()
            verify(Docs.saveAs(d, path, false))
            const handed = Date.now() - t0
            verify(handed < 100, "handed over in " + handed + " ms")
            hurry.seen = []
            let edits = 0, longest = 0, shown = false
            while (Session.saving) {
                wait(100)
                if (!shown && Docs.job.p >= 30) {
                    shown = true
                    shot(w.contentItem, "saving")  // the job going: Đang lưu, its bar, Hủy
                }
                const t = Date.now()
                if (Session.saving && d.run({ op: "setProps", layer: ids[1 + edits % (ids.length - 1)], props: { name: "Sửa " + edits } }))
                    edits++
                longest = Math.max(longest, Date.now() - t)
            }
            saved()
            const seen = hurry.seen.map(Math.round)
            console.log("n2 d2mini: handed over in", handed, "ms; progress", JSON.stringify(seen), "; edits", edits, "the longest", longest, "ms")
            verify(seen.length >= 10 && seen.every((x, i) => !i || x >= seen[i - 1]), "progress shown, going up: " + seen)
            verify(edits >= 5, "layers changed while saving: " + edits)
            compare(Docs.job.done, true)
            compare([d.dirty, d.index, d.unsaved], [true, index + edits, edits], "the edits after the step saved")
            d.jumpTo(index)
            verify(!d.dirty, "the step saved")

            // Hủy in the middle of a full save over another file
            const other = at("n2-small.pbd")
            const b = opened(file("n2-small.png", "png", 256, 256))
            verify(Docs.saveAs(b, other, false))
            saved()
            const was = b.model.digest()
            Docs.close(b)
            Docs.select(Docs.docs.indexOf(d))
            ended.clear()
            hurry.armed = true
            verify(Docs.saveAs(d, other, true))
            saved()
            verify(!hurry.armed, "Hủy as it went")
            compare(Docs.job.on, false)
            compare(last(), [other, "", true], "cancelled in the middle")
            compare(opened(other).model.digest(), was, "the file as it was")

            Docs.close(d)
            compare(shape(opened(path, 120000)), snap, "the file has the step saved")
        }
    }
}
