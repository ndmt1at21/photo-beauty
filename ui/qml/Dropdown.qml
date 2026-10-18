// copied from snapone 6bdb5c8 (ui/qml/Select.qml), restyled as the prototype's .dd
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Effects
import QtQuick.Controls.Basic

// Drop-down of the prototype (.dd, dd()): a 24 px box, `prefix` in t3 before the value ("Preset: "),
// the list in a menu (.pop, .mi rows with a tick at the value) at most 320 px tall, scrolling, and
// above the box when there is no room under it. options: strings, or [value, label] pairs; `value`
// comes from the owner, picked(value) asks it to change (a click, or ↑ ↓ on the closed box). A row lights only once the
// mouse or a key has moved in the open list; the list's 1 px edge lies outside it, as .pop's box-shadow. Boxes over 24
// (dialogs) have the 16 px chevron. `plain`: Start's .mi, the text 12 px in, the tick on the right.
ComboBox {
    id: c
    property var options: []
    property var value
    property string prefix
    property bool plain
    property bool moved  // the mouse or a key has moved in the open list
    readonly property var pairs: options.map(o => typeof o === "object" ? o : [o, o])  // a list from C++ is no Array
    readonly property int valueIndex: pairs.findIndex(o => o[0] === value)
    readonly property real sceneHeight: Window.height  // whether the list fits under the box
    signal picked(var value)

    model: pairs
    displayText: valueIndex >= 0 ? pairs[valueIndex][1] : ""
    // currentIndex is not bound: ComboBox overwrites it on every pick.
    Component.onCompleted: currentIndex = valueIndex
    onValueIndexChanged: currentIndex = valueIndex
    onActivated: i => c.picked(pairs[i][0])
    onHighlightedIndexChanged: if (popup.opened) moved = true

    implicitWidth: 104
    implicitHeight: 24
    leftPadding: 8
    rightPadding: 22
    hoverEnabled: true
    font.pointSize: Theme.pt(12)

    background: Rectangle {
        radius: 4
        color: Theme.p2
    }
    contentItem: Item {
        Text {
            id: pre
            height: parent.height
            text: c.prefix
            font: c.font
            color: Theme.t3
            verticalAlignment: Text.AlignVCenter
        }
        Text {
            x: pre.width
            width: parent.width - x
            height: parent.height
            text: c.displayText
            font: c.font
            color: c.enabled ? Theme.t1 : Theme.t4
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }
    indicator: Icon {
        x: c.width - width - 4
        y: (c.height - height) / 2
        name: "expand_more"
        size: c.height > 24 ? 16 : 14
        color: Theme.t3
    }
    delegate: ItemDelegate {
        id: opt
        required property var modelData
        required property int index
        width: ListView.view.width
        height: 28
        leftPadding: c.plain ? 12 : 6
        rightPadding: c.plain ? 12 : 10
        hoverEnabled: true
        highlighted: c.moved && c.highlightedIndex === index
        text: modelData[1]
        font.pointSize: Theme.pt(12.5)
        background: Rectangle { color: opt.highlighted ? Theme.sel : "transparent" }
        contentItem: Row {
            spacing: 6
            layoutDirection: c.plain ? Qt.RightToLeft : Qt.LeftToRight
            Item {
                width: 16
                height: parent.height
                Icon { anchors.centerIn: parent; visible: opt.index === c.valueIndex; name: "check"; size: 16; color: Theme.t2 }
            }
            Text {
                width: parent.width - 22
                height: parent.height
                text: opt.text
                font: opt.font
                color: Theme.t1
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
        }
    }
    popup: Popup {
        id: pop
        property bool up  // no room under the box
        property real wide  // the longest option's row
        popupType: Popup.Item
        y: up ? -height - 4 : c.height + 4
        width: Math.max(c.width, wide)
        height: Math.min(list.contentHeight + topPadding + bottomPadding, 320)
        topPadding: 6
        bottomPadding: 6
        leftPadding: 0
        rightPadding: 0
        topInset: -1  // the edge outside, as .pop's box-shadow
        bottomInset: -1
        leftInset: -1
        rightInset: -1
        onAboutToShow: {
            c.moved = false
            up = c.sceneHeight > 0 && c.mapToItem(null, 0, c.height).y + 4 + height > c.sceneHeight
            // Main's .mi: 6 + tick 16 + 6 + the text + 10; Start's: 12 + the text + 24 + tick 16 + 12
            wide = Math.ceil(c.pairs.reduce((m, o) => Math.max(m, fm.advanceWidth(o[1])), 0)) + (c.plain ? 64 : 38)
            list.positionViewAtIndex(Math.max(0, c.valueIndex), ListView.Contain)
        }
        FontMetrics { id: fm; font.pointSize: Theme.pt(12.5) }
        contentItem: ListView {
            id: list
            implicitHeight: contentHeight
            model: c.delegateModel
            currentIndex: c.highlightedIndex
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollIndicator.vertical: ScrollIndicator {}
        }
        background: Rectangle {
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
    }
}
