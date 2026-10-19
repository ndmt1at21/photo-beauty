pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import "MenuSpec.js" as MenuSpec

// The menu bar of the top bar (docs/ui-tasks.md, U4; artboards Main, Menus): the ten menus of MenuSpec.js, each built
// as it opens from the state then: greyed when can(item) says no (Main's), ticked (View, Window, Mode…), the step
// Hoàn tác and Làm lại would take ("Hoàn tác Layer mới", "Fade Brush…"), the recent files. Controls' MenuBar does the
// rest: with a menu open, the mouse on another title opens that one, ← → go along; Alt + the title's letter opens it
// (as Photoshop: Alt T Filter, Alt Y Type). Picking an item emits activated() with its MenuSpec item.
MenuBar {
    id: bar
    property var can: function (item) { return true }
    // A menu is open (Main's scrim, the title bar's hit test).
    readonly property bool open: {
        for (let i = 0; i < count; i++)
            if (menuAt(i)?.visible)
                return true
        return false
    }
    signal activated(var item)

    spacing: 0  // the prototype's nav has gap 1, but FreeType draws its 12.5 px titles 1 px wider each (U23)
    background: null
    Component.onCompleted: {
        for (const m of MenuSpec.MENUS)
            addMenu(menuC.createObject(bar, { title: m.title, width: m.w, full: m.items, start: m.start || m.items }))
    }
    Component {
        id: menuC
        PopupMenu {
            property var full: []
            property var start: []  // with no document open (the Start screen)
            readonly property var spec: Docs.docs.length ? full : start  // the items shown
            plain: Docs.docs.length === 0  // Start's menus
            onAboutToShow: build(bar.resolve(spec))
            onActivated: item => bar.activated(item)
        }
    }

    delegate: MenuBarItem {
        id: title
        readonly property bool on: !!menu && menu.visible
        implicitWidth: label.implicitWidth
        implicitHeight: 34  // 28 drawn; a menu opens under the item, so 6 px below the title as the prototype's
        padding: 0
        leftPadding: 0  // over the style's 12 and 16: the title's own padding is the label's
        rightPadding: 0
        bottomPadding: 6
        hoverEnabled: true  // a menu open, the mouse on a title opens it; also offscreen, where hover is off
        focusPolicy: Qt.NoFocus
        background: Rectangle {
            height: 28
            radius: 5
            color: title.on || title.hovered ? Theme.sel : "transparent"
        }
        contentItem: Text {
            id: label
            leftPadding: 9
            rightPadding: 9
            text: title.text.replace("&", "")  // Alt + the letter after & opens it; Windows shows no underline either
            color: title.on ? Theme.t1 : Theme.t2
            font.pointSize: Theme.pt(12.5)
            verticalAlignment: Text.AlignVCenter
        }
    }

    // MenuSpec items as PopupMenu shows them now.
    function resolve(items) {
        return items.map(it => {
            if (it.t === "sep")
                return it
            const o = Object.assign({}, it, { l: labelOf(it), disabled: !bar.can(it), check: checked(it.act) })
            if (it.sub === "recent") {
                o.sub = Docs.recent.map((r, i) => ({ l: r.file, act: "@open:" + i, free: true })).concat([MenuSpec.SEP, { l: qsTr("Xóa danh sách"), act: "@clearRecent", free: true }])
                o.disabled = o.disabled || !Docs.recent.length
            } else if (it.sub) {
                o.sub = resolve(it.sub)
            }
            return o
        })
    }
    function labelOf(it) {
        const d = Docs.current, n = MenuSpec.name(it.act)
        const h = d ? d.history : [], i = d ? d.index : 0
        if (n === "undo" && i > 0)
            return qsTr("Hoàn tác %1").arg(h[i].name)
        if (n === "redo" && d && i < h.length - 1)
            return qsTr("Làm lại %1").arg(h[i + 1].name)
        if (it.fade && i > 0)
            return qsTr("Fade %1…").arg(h[i].name)
        return it.l
    }
    // The tick of an action that sets something (the prototype's chkOf).
    function checked(act) {
        const d = Docs.current, a = MenuSpec.arg(act)
        switch (MenuSpec.name(act)) {
        case "mode": return !!d && d.mode === a
        case "bits": return !!d && String(d.bits) === a
        case "tog": return !!Tools[a]
        case "scr": return String(Tools.screen) === a
        case "snapTo": return !!Tools.snapTo[a]
        case "win": return Tools.panelVisible(a)
        case "qm": return Tools.quickMask
        case "aa": return Tools.antiAlias === a
        case "fp": return String(Tools.fontPreview) === a
        default: return false
        }
    }
}
