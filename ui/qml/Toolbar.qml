pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Templates as T
import "ToolSpec.js" as ToolSpec
import "MenuSpec.js" as MenuSpec

// The toolbar (docs/ui-tasks.md, U5; artboard Toolbar, the aside of Main.dc.html): the 16 groups of GROUPS, 31 tools,
// one column 44 wide or, with Thiết lập's twoColumnTools, two in 80; a group shows the tool on its top (Tools.top), its
// flyout opens at a long press or the right button; double-click Hand fits the view, Zoom goes to 100%. Then the
// foreground and background colours (a click opens the Color Picker), Quick Mask, the screen mode. It runs the keys of
// ToolSpec.KEYS but Main's (F, Q), Shift and a tool's key, 0 … 9 and Shift 0 … 9 (Opacity, Flow), and Space, Alt, Ctrl
// held: Hand, Eyedropper while painting, Move, the tool back on release.
Rectangle {
    id: tb
    readonly property bool two: Prefs.value("twoColumnTools") === true
    readonly property var win: Window.window  // Main: openDialog(), space
    // Keys with a document, no space open, and not while the menu bar has the keys (after Alt, its letters open menus).
    readonly property bool keysOn: !!Docs.current && !(win && win.space) && !(Window.activeFocusItem instanceof T.MenuBar)
    readonly property bool tipKeys: Prefs.value("tooltipKeys") !== false
    readonly property alias flyout: fly  // the group's menu
    readonly property bool tight: !two && height < 717  // the smallest window (796 is the toolbar at 900): separators 5 high

    implicitWidth: two ? 80 : 44
    implicitHeight: 796
    color: Theme.p1
    Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.ln }

    // A group's flyout, at the toolbar's right edge: its tools, the one on top marked.
    function openFlyout(btn, gid) {
        const top = Tools.top[gid] || ToolSpec.TOP0[gid], ts = ToolSpec.members(gid)
        fly.show(ts.map(t => ({ l: t.n, k: t.k, icon: t.icn, mark: t.id === top, act: () => Tools.setTool(t.id, gid) })), btn, tb.width - btn.mapToItem(tb, 0, 0).x, 0)
        fly.currentIndex = ts.findIndex(t => t.id === top)  // lit as the artboard's, and where ↑ ↓ start
    }
    function pickColor(front) {
        const w = win
        if (w && w.hasDialog && w.hasDialog("ColorPicker"))
            w.openDialog("ColorPicker", front ? { color: Tools.fg, title: qsTr("màu trước"), picked: c => Tools.setFg(String(c)) }
                                              : { color: Tools.bg, title: qsTr("màu sau"), picked: c => { Tools.bg = String(c) } })
    }

    Flickable {  // shorter still, it scrolls
        anchors.fill: parent
        anchors.rightMargin: 1
        contentHeight: col.height
        interactive: contentHeight > height
        boundsBehavior: Flickable.StopAtBounds
        clip: interactive
        Column {
            id: col
            x: 6
            width: tb.two ? 68 : 32
            topPadding: 6
            bottomPadding: 6
            spacing: 2
            Grid {
                columns: tb.two ? 2 : 1
                columnSpacing: 4
                rowSpacing: 2
                Repeater {
                    model: ToolSpec.GROUPS.filter(g => g || !tb.two)  // two columns: no separators
                    Item {
                        id: slot
                        required property var modelData  // [group, tools], or 0 a separator
                        width: 32
                        height: modelData ? 32 : tb.tight ? 5 : 9
                        Loader {
                            anchors.fill: parent
                            sourceComponent: slot.modelData ? groupC : sepC
                        }
                        Component {
                            id: sepC
                            Item { Rectangle { x: 5; y: tb.tight ? 2 : 4; width: 22; height: 1; color: Theme.ln } }
                        }
                        Component {
                            id: groupC
                            ToolBtn {
                                id: gb
                                readonly property string gid: slot.modelData[0]
                                readonly property string topId: Tools.top[gid] || ToolSpec.TOP0[gid]
                                checked: ToolSpec.tool(Tools.tool).g === gid
                                many: slot.modelData[1].length > 1
                                corner: many && !tb.two
                                icon.name: ToolSpec.tool(topId).icn
                                ToolTip.text: ToolSpec.tip(topId, tb.tipKeys)
                                Accessible.name: ToolTip.text
                                onPicked: Tools.setTool(topId)
                                onMenu: tb.openFlyout(gb, gid)
                                onDoubleClicked: {
                                    if (topId === "hand")
                                        Docs.exec("@zoomFit")
                                    else if (topId === "zoom")
                                        Docs.exec("@zoom100")
                                }
                            }
                        }
                    }
                }
            }
            Item {
                width: col.width
                height: tb.tight ? 5 : 9
                Rectangle { x: (parent.width - 22) / 2; y: tb.tight ? 2 : 4; width: 22; height: 1; color: Theme.ln }
            }
            ColorPair {
                x: (col.width - width) / 2
                compact: true
                fg: Tools.fg
                bg: Tools.bg
                onFgClicked: tb.pickColor(true)
                onBgClicked: tb.pickColor(false)
                onSwapClicked: Tools.swapColors()
                onResetClicked: Tools.resetColors()
            }
            Grid {
                topPadding: tb.tight ? 0 : 4
                columns: tb.two ? 2 : 1
                columnSpacing: 4
                rowSpacing: 2
                ToolBtn {
                    lit: Tools.quickMask
                    icon.name: "quick_mask"
                    ToolTip.text: tb.tipKeys ? qsTr("Quick Mask (Q)") : qsTr("Quick Mask")
                    Accessible.name: qsTr("Quick Mask (Q)")
                    onPicked: Docs.exec("@qm")
                }
                ToolBtn {
                    readonly property var names: [qsTr("chế độ chuẩn"), qsTr("toàn màn hình có menu"), qsTr("toàn màn hình")]
                    hover: true
                    icon.name: "desktop_windows"
                    ToolTip.text: tb.tipKeys ? qsTr("Chế độ màn hình · Screen Mode (F)") : qsTr("Chế độ màn hình")
                    Accessible.name: qsTr("Chế độ màn hình: %1 (F)").arg(names[Tools.screen] || "")
                    onPicked: Tools.screen = (Tools.screen + 1) % 3
                }
            }
        }
    }
    PopupMenu {
        id: fly
        modal: true  // a press outside closes it there, not on the canvas
        dim: false
    }

    // ---- keys ---------------------------------------------------------------------------------------------------------
    // [{ seq, act }]: a tool's key "tool:B", with Shift "next:B", the rows' acts, "op:5", "flow:5".
    readonly property var keys: {
        const out = [], seen = {}
        for (const g of ToolSpec.KEYS)
            for (const r of g[1]) {
                if (r.tool && !seen[r.k]) {
                    seen[r.k] = true
                    out.push({ seq: r.k, act: "tool:" + r.k }, { seq: "Shift+" + r.k, act: "next:" + r.k })
                } else if (r.act) {
                    out.push({ seq: MenuSpec.sequences(r.k)[0], act: r.act })
                }
            }
        for (let d = 0; d < 10; d++)
            out.push({ seq: String(d), act: "op:" + d }, { seq: "Shift+" + d, act: "flow:" + d })
        return out
    }
    function key(act) {
        const a = act.split(":"), bar = ToolSpec.barOf(Tools.tool, Tools.xf, Tools.warp), b = ToolSpec.brushBar(Tools.tool, Tools.xf)
        switch (a[0]) {
        case "tool":
        case "next": {
            const id = ToolSpec.keyTool(a[1], Tools.tool, Tools.top, a[0] === "next" || Prefs.value("shiftCycle") === false)
            if (id)
                Tools.setTool(id, ToolSpec.tool(id).g)
            return
        }
        case "swapColors": Tools.swapColors(); return
        case "resetColors": Tools.resetColors(); return
        case "size-":
        case "size+":
        case "hard-":
        case "hard+": {
            if (!ToolSpec.PAINT_T[Tools.tool] || Tools.xf)
                return
            const size = a[0][0] === "s", k = b + (size ? ":size" : ":hard"), v = Tools.value(k, ToolSpec.brush(b)[size ? "size" : "hard"])
            Tools.setValue(k, (size ? ToolSpec.sizeStep : ToolSpec.hardStep)(v, a[0].slice(-1) === "+" ? 1 : -1))
            return
        }
        case "op":
        case "flow": {
            const i = (a[0] === "op" ? ToolSpec.OPACITY : ToolSpec.FLOW)[bar]
            if (i !== undefined)
                Tools.setValue(bar + ":" + i, +a[1] ? +a[1] * 10 : 100)
            return
        }
        }
    }
    Instantiator {
        model: tb.keys
        delegate: Shortcut {
            required property var modelData
            sequence: modelData.seq
            enabled: tb.keysOn
            autoRepeat: /^size|^hard/.test(modelData.act)
            onActivated: tb.key(modelData.act)
        }
    }

    // ---- held keys ----------------------------------------------------------------------------------------------------
    // Space, Alt (with the tools of ToolSpec.ALT_PICK), Ctrl (but with the view's tools) held put Hand, Eyedropper, Move
    // in Tools.tool, and their release gives the tool back unless another was picked meanwhile. Alt and Ctrl count as held
    // after 250 ms, or at a press of the mouse before that, and not when another key comes first (a shortcut such as
    // Ctrl Z), so no bar flickers. Alt held does not end in the menu bar's keyboard mode: MenuBar arms it at an Alt press
    // and a change of focus disarms it. Not while a text field has the focus, nor in a popup or dialog. Their presses and
    // releases come through the window's root item, where every key no item took ends up.
    property string held  // "space", "alt", "ctrl"
    property string heldTool
    property string heldBase
    property string pending  // Alt or Ctrl down, not yet held
    readonly property var rootItem: Window.contentItem
    readonly property bool active: Window.active
    onActiveChanged: {  // keys let go elsewhere never come back
        if (!active) {
            release(pending)
            release(held)
        }
    }

    function canHold() {
        const f = Window.activeFocusItem, w = win
        if (!keysOn || f instanceof TextInput || f instanceof TextEdit || (w && w.dialogs && w.dialogs.length))
            return false
        for (let p = f; p; p = p.parent)
            if (p === Overlay.overlay)
                return false
        return true
    }
    function hold(k) {
        pending = ""
        armed.stop()
        const temp = k === "space" ? "hand" : k === "alt" ? "eyedrop" : "move"
        if (held || Tools.tool === temp || !canHold() || (k === "alt" ? !ToolSpec.ALT_PICK[Tools.tool] : k === "ctrl" && ToolSpec.VIEW_T[Tools.tool]))
            return
        held = k
        heldBase = Tools.tool
        heldTool = temp
        Tools.tool = temp
        if (k === "alt") {  // the focus away and back: MenuBar drops its Alt
            const f = Window.activeFocusItem
            hub.forceActiveFocus()
            if (f && f !== hub)
                f.forceActiveFocus()
        }
    }
    function release(k) {
        if (pending === k) {
            pending = ""
            armed.stop()
        }
        if (!k || held !== k)
            return
        if (Tools.tool === heldTool)
            Tools.tool = heldBase
        held = ""
    }
    Shortcut {
        sequence: "Space"
        enabled: tb.keysOn
        autoRepeat: false
        onActivated: tb.hold("space")
    }
    Timer {
        id: armed
        interval: 250
        onTriggered: tb.hold(tb.pending)
    }
    Connections {
        target: tb.rootItem ? tb.rootItem.Keys : null
        function onPressed(e) {
            if (e.isAutoRepeat)
                return
            if (e.key === Qt.Key_Alt || e.key === Qt.Key_Control) {
                if (!tb.held && tb.canHold()) {
                    tb.pending = e.key === Qt.Key_Alt ? "alt" : "ctrl"
                    armed.restart()
                }
            } else if (e.key !== Qt.Key_Shift) {
                tb.release(tb.pending)
            }
        }
        function onShortcutOverride(e) {  // a shortcut's key: Ctrl or Alt was for it
            if (tb.pending && e.key !== Qt.Key_Alt && e.key !== Qt.Key_Control && e.key !== Qt.Key_Shift)
                tb.release(tb.pending)
        }
        function onReleased(e) {
            if (!e.isAutoRepeat)
                tb.release(e.key === Qt.Key_Space ? "space" : e.key === Qt.Key_Alt ? "alt" : e.key === Qt.Key_Control ? "ctrl" : "")
        }
    }
    Connections {  // a press with Alt or Ctrl down makes it held at once, before the canvas has the press
        target: tb.Overlay.overlay
        function onPressed() {
            if (tb.pending)
                tb.hold(tb.pending)
        }
    }
    Item {  // where the focus goes for a moment as Alt becomes held
        id: hub
        parent: tb.Overlay.overlay
    }
}
