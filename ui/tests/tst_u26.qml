import QtQuick
import QtTest
import PhotoBeauty

// U26: Thiết lập › Hiệu năng › Độ trễ thấp (G1): off by default, into Prefs' lowLatency, kept with Prefs' JSON, which
// main.cpp reads at the next start to present without vsync (QSG_NO_VSYNC).
Item {
    id: root
    Component { id: settingsC; SettingsDialog {} }
    Component {
        id: hostC
        Window { width: 1440; height: 900; visible: true; color: Theme.bg }
    }

    TestCase {
        name: "U26"
        when: windowShown

        function init() { Prefs.reset() }
        function cleanup() { Prefs.reset() }

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
        function open(params) {
            const host = createTemporaryObject(hostC, root)
            const d = createTemporaryObject(settingsC, host.contentItem, { params: params || {} })
            d.open()
            tryCompare(d, "opened", true)
            return d
        }
        function toggle(d) { return find(d.contentItem, o => o instanceof Toggle && o.Accessible.name === "Độ trễ thấp (có thể xé hình)") }
        function text(d, s) { return find(d.contentItem, o => o instanceof Text && o.visible && o.text === s) }

        function test_default() {
            compare(Prefs.defaults.lowLatency, false)
            compare(Prefs.values.lowLatency, false)
            Prefs.restore("{}")
            compare(Prefs.values.lowLatency, false, "nothing kept: off")
            Prefs.restore(JSON.stringify({ lowLatency: "1" }))
            compare(Prefs.values.lowLatency, false, "not a bool: off")
        }

        function test_row() {  // under Dùng GPU, off; a click turns it on, into Prefs, kept in its JSON
            const d = open()
            const t = toggle(d)
            verify(t && t.visible, "on Hiệu năng")
            verify(text(d, "Độ trễ thấp"), "its label")
            const note = text(d, "có thể xé hình · có hiệu lực từ lần mở app sau")
            verify(note, "the note, as Ngôn ngữ's")
            const gpu = find(d.contentItem, o => o instanceof Toggle && o.Accessible.name === "Dùng GPU")
            const y = o => o.mapToItem(d.contentItem, 0, 0).y
            verify(y(t) > y(gpu) && y(t) < y(text(d, "Ổ scratch · dùng theo thứ tự")), "between Dùng GPU and the disks")
            verify(y(t) + t.height < d.contentItem.height, "in view")
            verify(!t.checked)
            mouseClick(t)
            compare(Prefs.values.lowLatency, true)
            verify(t.checked)
            Prefs.restore(JSON.stringify(Prefs.values))
            compare(Prefs.values.lowLatency, true, "kept")
            verify(t.checked)
            mouseClick(t)
            compare(Prefs.values.lowLatency, false)
            verify(!t.checked)
            mouseClick(find(d.contentItem, o => o.text === "Chung" && o.Accessible.role === Accessible.PageTab))
            verify(!t.visible, "on Hiệu năng only")
        }

        function test_keyboard() {  // Space and Enter flip it once it has the focus
            const d = open()
            const t = toggle(d)
            t.forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Space)
            compare(Prefs.values.lowLatency, true)
            keyClick(Qt.Key_Return)
            compare(Prefs.values.lowLatency, false)
        }
    }
}
