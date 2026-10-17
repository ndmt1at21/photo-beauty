// copied from snapone 6bdb5c8 (ui/qml/PopupMenu.qml), without App, Stats and the rating rows
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Effects
import QtQuick.Controls.Basic

// Menu of the prototype (.pop, .mi): the menu bar's menus, a tool group, right-click menus, built by
// show() from an array of items, `width` wide (236; a submenu takes its item's w):
//   { l, k, icon, check, mark, disabled, sub, w, act }
//       l the label; k its keys as shown ("Ctrl ⇧ Z"); icon the name of an Icon (a tool group:
//       the menu then has a 10 px column for mark, the square of the tool on, then the icons); check
//       a tick in the 16 px column of other menus; disabled greyed, does nothing; sub the items of a
//       submenu that opens on hover or →, w its width; act a function to call, or anything else (a
//       command name): either way the menu, closing, emits activated(item), a submenu's items too.
//   { t: "sep" }    a separator
// Rows are 28 px; ↑ ↓ (past separators and greyed rows) → ← Enter Esc come with Controls' Menu.
Menu {
    id: menu
    property var items: []  // a submenu builds itself from these
    signal activated(var item)
    Component.onCompleted: if (items.length) build(items)

    popupType: Popup.Item
    width: 236
    topPadding: 6
    bottomPadding: 6
    leftPadding: 0
    rightPadding: 0
    margins: 8
    overlap: 4
    delegate: entry  // the rows addMenu() makes for submenus

    // Opens at the cursor, or at x, y in `item` when given.
    function show(items, item, x, y) {
        build(items)
        if (item) popup(item, x, y)
        else popup()
    }
    // Rows have no parent until addItem(), so `made` holds them: else the garbage collector may
    // delete one the menu still lists, and the loop below would spin on a null item.
    property var made: []
    function build(items) {
        while (count) {  // remove*() also destroy; takeItem() would leave Menu's own entries alive
            const sub = (itemAt(0) as MenuItem)?.subMenu
            if (sub) removeMenu(sub)
            else removeItem(itemAt(0))
        }
        made = []
        const icons = items.some(s => !!s.icon)
        for (const s of items) {
            if (s.t === "sep") {
                const row = sep.createObject(null) as Item
                made.push(row)
                addItem(row)
            } else if (s.sub && !s.disabled) {  // a disabled one: a row like any other, greyed
                const sub = Qt.createComponent("PhotoBeauty", "PopupMenu").createObject(menu, { title: s.l, items: s.sub, width: s.w || 236 }) as PopupMenu
                sub.activated.connect(menu.activated)
                addMenu(sub)
            } else {
                const it = entry.createObject(null, { text: s.l, keys: s.k || "", tick: !!s.check, mark: !!s.mark, icons: icons, enabled: !s.disabled }) as MenuItem
                made.push(it)
                it.icon.name = s.icon || ""
                it.triggered.connect(() => {
                    if (typeof s.act === "function") s.act()
                    menu.activated(s)
                })
                addItem(it)
            }
        }
    }

    background: Rectangle {
        implicitWidth: 236
        radius: 8
        color: Theme.p2
        border.color: Theme.ln
        RectangularShadow {
            z: -1
            anchors.fill: parent
            offset.y: 12
            blur: 32
            radius: 8
            color: Theme.shadow
        }
    }

    Component {
        id: entry
        MenuItem {
            id: e
            property string keys
            property bool tick
            property bool mark
            property bool icons
            implicitHeight: 28
            leftPadding: 6
            rightPadding: 10
            topPadding: 0
            bottomPadding: 0
            hoverEnabled: true  // hover opens submenus; the default follows styleHints
            arrow: null
            indicator: null
            font.pointSize: Theme.pt(12.5)
            background: Rectangle { color: e.highlighted && e.enabled ? Theme.sel : "transparent" }
            contentItem: RowLayout {
                spacing: 6
                Item {
                    Layout.preferredWidth: e.icons ? 10 : 16
                    Layout.preferredHeight: 16
                    Icon { anchors.centerIn: parent; visible: e.tick; name: "check"; size: 16; color: Theme.t2 }
                    Rectangle { anchors.centerIn: parent; visible: e.mark; width: 5; height: 5; color: Theme.t1 }
                }
                Icon {
                    visible: e.icons
                    name: e.icon.name
                    size: 18
                    color: e.enabled ? Theme.t2 : Theme.t4
                }
                Text {
                    Layout.fillWidth: true
                    text: e.text
                    font: e.font
                    color: e.enabled ? Theme.t1 : Theme.t4
                    elide: Text.ElideRight
                }
                Text {
                    visible: e.keys !== ""
                    text: e.keys
                    color: e.enabled ? Theme.t3 : Theme.t4
                    font.family: Theme.mono
                    font.pointSize: Theme.pt(11)
                }
                Icon {
                    visible: !!e.subMenu
                    name: "chevron_right"
                    size: 16
                    color: Theme.t3
                }
            }
        }
    }
    Component {
        id: sep
        Item {
            implicitHeight: 11
            Rectangle { y: 5; width: parent.width; height: 1; color: Theme.ln }
        }
    }
}
