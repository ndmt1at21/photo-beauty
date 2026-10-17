// copied from snapone 6bdb5c8 (ui/qml/FocusRing.qml)
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Templates as T

// Ring around whatever Tab reached: the browser outline (:focus-visible) the prototype gets for
// free, 2 px of accent 2 px off the item (Focus of the Controls artboard); a click never shows
// it. It follows its item each frame, clipped as the item is, and a newly focused item is
// scrolled into view in its panel. Text fields show focus themselves. One for the whole window.
Rectangle {
    id: ring
    readonly property Item item: {
        const c = Window.activeFocusItem as T.Control  // text fields are no Control
        return c && c.visualFocus && !(c instanceof T.MenuItem) ? c : null
    }
    readonly property Item tabbed: {  // anything Tab reached, text fields too
        const f = Window.activeFocusItem, why = (f as T.Control)?.focusReason ?? (f as T.TextField)?.focusReason
        return why === Qt.TabFocusReason || why === Qt.BacktabFocusReason ? f : null
    }
    parent: Overlay.overlay
    z: 1000000
    visible: false
    color: "transparent"
    radius: 10
    border.width: 2
    border.color: Theme.accent

    onTabbedChanged: if (tabbed) reveal(tabbed)
    onItemChanged: {
        visible = false
        if (item) place()
    }
    Connections {
        target: ring.Window.window
        function onAfterAnimating() { if (ring.item) ring.place() }
    }
    function place() {
        const f = item, r = f.mapToItem(parent, 0, 0, f.width, f.height)
        let x0 = r.x, y0 = r.y, x1 = r.x + r.width, y1 = r.y + r.height
        for (let p = f.parent; p && p !== parent; p = p.parent) {
            if (!p.clip) continue
            const c = p.mapToItem(parent, 0, 0, p.width, p.height)
            x0 = Math.max(x0, c.x); y0 = Math.max(y0, c.y); x1 = Math.min(x1, c.x + c.width); y1 = Math.min(y1, c.y + c.height)
        }
        visible = f.visible && x1 > x0 && y1 > y0
        if (!visible) return
        x = x0 - 4; y = y0 - 4; width = x1 - x0 + 8; height = y1 - y0 + 8
    }
    function reveal(f) {  // the nearest scrolling ancestor shows all of f, 8 px to spare
        for (let p = f.parent; p; p = p.parent) {
            if (!(p instanceof Flickable)) continue
            const r = f.mapToItem(p.contentItem, 0, 0, f.width, f.height), m = 8
            if (p.contentHeight > p.height) {
                if (r.y - m < p.contentY) p.contentY = Math.max(p.originY, r.y - m)
                else if (r.y + r.height + m > p.contentY + p.height)
                    p.contentY = Math.min(p.originY + p.contentHeight - p.height, r.y + r.height + m - p.height)
            }
            if (p.contentWidth > p.width) {
                if (r.x - m < p.contentX) p.contentX = Math.max(p.originX, r.x - m)
                else if (r.x + r.width + m > p.contentX + p.width)
                    p.contentX = Math.min(p.originX + p.contentWidth - p.width, r.x + r.width + m - p.width)
            }
            return
        }
    }
}
