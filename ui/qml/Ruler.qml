pragma ComponentBehavior: Bound
import QtQuick
import "View.js" as View
import "Fmt.js" as Fmt

// A ruler 18 px across (docs/ui-tasks.md, U6; artboard Main), along the frame's top, or its left when vertical: a long
// tick and its number every major step, the first 1, 2, 5 × 10ⁿ units 90 px or more apart (the prototype's), a middle
// tick at the halves, short ticks at the tenths; 0 at `origin`; a bright tick where the pointer is. With the view turned
// it reads the document as if it were not, as Photoshop's does. A right click picks the unit.
Rectangle {
    id: ruler
    property bool vertical: false
    property var view: null  // Viewport.view
    property var size: null  // Viewport.size: the frame, whose width (height when vertical) the ruler runs along
    property real origin: 0  // the document coordinate of 0
    property real perUnit: 1  // document pixels in a unit
    property real mark: NaN  // where the pointer is along the frame
    property var units: []  // the right-click menu's names
    property int unit: 0  // the one ticked
    signal picked(int unit)

    readonly property real s: view && size ? View.scale(view, size) : 1
    readonly property real at: view ? (vertical ? view.y : view.x) : 0  // the document coordinate at the frame's centre
    readonly property real centre: size ? (vertical ? size.h : size.w) / 2 : 0
    readonly property real major: {  // units between numbers
        const u = perUnit * s
        for (let e = -3; e <= 7; e++)
            for (const m of [1, 2, 5])
                if (m * Math.pow(10, e) * u >= 90)
                    return m * Math.pow(10, e)
        return 1e8
    }
    readonly property int decimals: Math.max(0, -Math.floor(Math.log10(major) + 1e-9))
    readonly property real step: major / 10 * perUnit  // document pixels between ticks
    readonly property int first: Math.floor((at - centre / s - origin) / step)  // the tick at the near end
    function pos(k) { return centre + (origin + k * step - at) * s }  // tick k along the frame

    implicitWidth: vertical ? 18 : 1034
    implicitHeight: vertical ? 750 : 18
    color: Theme.p1
    clip: true

    Repeater {  // as many ticks as the length holds, whatever the pan: panning moves them, never makes new ones
        model: ruler.view && ruler.size ? Math.ceil((ruler.vertical ? ruler.height : ruler.width) / (ruler.step * ruler.s)) + 2 : 0
        delegate: Item {
            id: t
            required property int index
            readonly property int k: ruler.first + index
            readonly property int kind: (k % 10 + 10) % 10 === 0 ? 0 : (k % 5 + 5) % 5 === 0 ? 1 : 2  // number, half, tenth
            readonly property int len: [17, 7, 4][kind]
            x: ruler.vertical ? 0 : Math.round(ruler.pos(k))
            y: ruler.vertical ? Math.round(ruler.pos(k)) : 0
            Rectangle {
                x: ruler.vertical ? 17 - t.len : 0
                y: ruler.vertical ? 0 : 17 - t.len
                width: ruler.vertical ? t.len : 1
                height: ruler.vertical ? 1 : t.len
                color: Theme.t4
            }
            Text {  // vertical: turned to read downwards, its top to the right (writing-mode: vertical-lr)
                visible: t.kind === 0
                x: ruler.vertical ? 11 : 3
                y: ruler.vertical ? 3 : 1
                height: 10
                rotation: ruler.vertical ? 90 : 0
                transformOrigin: Item.TopLeft
                verticalAlignment: Text.AlignVCenter
                text: visible ? Fmt.vn(t.k / 10 * ruler.major, ruler.decimals) : ""
                color: Theme.t3
                font.family: Theme.mono
                font.pointSize: Theme.pt(9)
            }
        }
    }
    Rectangle {  // the pointer
        visible: !isNaN(ruler.mark)
        x: ruler.vertical ? 0 : Math.round(ruler.mark)
        y: ruler.vertical ? Math.round(ruler.mark) : 0
        width: ruler.vertical ? 17 : 1
        height: ruler.vertical ? 1 : 17
        color: Theme.t1
    }
    Rectangle {  // its edge on the canvas
        x: ruler.vertical ? 17 : 0
        y: ruler.vertical ? 0 : 17
        width: ruler.vertical ? 1 : ruler.width
        height: ruler.vertical ? ruler.height : 1
        color: Theme.ln
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.RightButton
        onPressed: m => menu.show(ruler.units.map((l, i) => ({ l: l, check: i === ruler.unit, unit: i })), ruler, m.x, m.y)
    }
    PopupMenu {
        id: menu
        width: 160
        modal: true  // the press that closes it ends there
        dim: false
        onActivated: item => ruler.picked(item.unit)
    }
}
