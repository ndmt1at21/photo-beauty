pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic

// Blend If slider (plan.md's thanh Blend If; build/proto/LayerStyleBlend.dc.html): a black to white bar over a black
// and a white triangle, `label` and a field per triangle above it. Alt dragging a whole triangle splits it: the half the
// drag goes towards moves, and between the halves the layer blends in softly; a split triangle has a field a half, and
// its halves drag one at a time. value: [black left, black right, white left, white right], 0..255, each held between
// its neighbours; a whole triangle has equal halves. Value protocol of the shared controls: started(), moved(v)… while
// a triangle follows the mouse, committed(v) when it lets go, the three at once for a typed value; v is a new array.
Control {
    id: s
    property string label
    property var value: [0, 0, 255, 255]
    readonly property bool blackSplit: value[0] !== value[1]
    readonly property bool whiteSplit: value[2] !== value[3]
    signal started()
    signal moved(var v)
    signal committed(var v)

    implicitWidth: 320
    implicitHeight: 56
    padding: 0

    // value with half i at x, or both halves of i's triangle when whole, held between the neighbours.
    function place(i, x, whole) {
        const v = value.slice(), a = whole ? i & 2 : i, b = whole ? a + 1 : i
        v[a] = v[b] = Math.max(a > 0 ? v[a - 1] : 0, Math.min(b < 3 ? v[b + 1] : 255, Math.round(x)))
        return v
    }
    function wholeAt(i) { return i < 2 ? !blackSplit : !whiteSplit }

    // The field of half i, or of its triangle when whole (i 0 or 2 then).
    component Half: NumberField {
        id: h
        required property int i
        readonly property bool whole: s.wholeAt(i)
        width: 46
        value: s.value[i]
        min: i === 0 ? 0 : s.value[i - 1]
        max: i === 3 || whole && i === 2 ? 255 : s.value[i + (whole ? 2 : 1)]
        Accessible.name: s.label + ": " + [qsTr("tam giác đen"), qsTr("nửa trái tam giác đen"), qsTr("nửa phải tam giác đen"), qsTr("tam giác trắng"),
            qsTr("nửa trái tam giác trắng"), qsTr("nửa phải tam giác trắng")][(i < 2 ? 0 : 3) + (whole ? 0 : 1 + i % 2)]
        onStarted: s.started()
        onMoved: x => s.moved(s.place(h.i, x, h.whole))
        onCommitted: x => s.committed(s.place(h.i, x, h.whole))
    }
    component Sep: Text {
        anchors.verticalCenter: parent.verticalCenter
        color: Theme.t3
        font.family: Theme.mono
        font.pointSize: Theme.pt(11.5)
    }

    contentItem: Item {
        Text {
            anchors.verticalCenter: fields.verticalCenter
            text: s.label
            color: s.enabled ? Theme.t2 : Theme.t4
            font.pointSize: Theme.pt(12)
        }
        Row {
            id: fields
            anchors.right: parent.right
            spacing: 4
            Half { i: 0 }
            Sep { visible: s.blackSplit; text: "/" }
            Half { i: 1; visible: s.blackSplit }
            Sep { text: "·" }
            Half { i: 2 }
            Sep { visible: s.whiteSplit; text: "/" }
            Half { i: 3; visible: s.whiteSplit }
        }
        Item {
            id: bar
            y: 28
            width: parent.width
            height: 28
            Rectangle {
                x: 7
                width: parent.width - 14
                height: 12
                radius: 2
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: "#000000" }
                    GradientStop { position: 1; color: "#ffffff" }
                }
            }
            Canvas {  // the triangles, as the prototype's .tri and .half
                anchors.fill: parent
                readonly property var deps: [s.value, Theme.t2, width]
                onDepsChanged: requestPaint()
                onPaint: {
                    const ctx = getContext("2d")
                    // apex at top, base at bottom, `half` wide each side; side -1 the left half only, 1 the right one
                    const tri = (x, side, top, bottom, half, color) => {
                        ctx.fillStyle = color
                        ctx.beginPath()
                        ctx.moveTo(x, top)
                        ctx.lineTo(side >= 0 ? x + half : x, bottom)
                        ctx.lineTo(side <= 0 ? x - half : x, bottom)
                        ctx.closePath()
                        ctx.fill()
                    }
                    ctx.reset()
                    for (let i = 0; i < 4; i++) {
                        const w = s.wholeAt(i)
                        if (w && i % 2)
                            continue
                        const x = drag.xOf(s.value[i]), side = w ? 0 : i % 2 ? 1 : -1
                        tri(x, side, 13, 25, 7, Theme.t2)
                        tri(x, side, 15, 24, w ? 5.5 : 5, i < 2 ? "#000000" : "#ffffff")
                    }
                }
            }
            MouseArea {
                id: drag
                property int grab: -1  // the half held, -1 for none
                property bool whole  // its triangle moves as one
                property bool split  // Alt at the press on a whole triangle: the way it first moves picks the half
                property real off  // from the mouse to the triangle's point
                property bool active  // started() sent
                property var v  // the value moved to
                anchors.fill: parent
                preventStealing: true
                function xOf(n) { return 7 + n / 255 * (width - 14) }
                function nOf(x) { return (x - 7) / (width - 14) * 255 }
                onPressed: e => {
                    let best = 9  // px from a triangle's middle
                    grab = -1
                    for (let i = 0; i < 4; i++) {
                        const w = s.wholeAt(i)
                        if (w && i % 2)
                            continue  // a whole triangle is its left half's
                        const c = xOf(s.value[i]) + (w ? 0 : i % 2 ? 3.5 : -3.5), d = Math.abs(e.x - c)
                        if (d < best || d === best && e.x > c) {  // on a tie, the one the mouse is right of
                            best = d
                            grab = i
                            whole = w
                            off = xOf(s.value[i]) - e.x
                        }
                    }
                    split = whole && !!(e.modifiers & Qt.AltModifier)
                    active = false
                }
                onPositionChanged: e => {
                    if (grab < 0)
                        return
                    const n = nOf(e.x + off)
                    if (split) {
                        if (Math.round(n) === s.value[grab])
                            return
                        grab = (grab & 2) + (n > s.value[grab] ? 1 : 0)
                        whole = false
                        split = false
                    }
                    const nv = s.place(grab, n, whole)
                    if (JSON.stringify(nv) === JSON.stringify(active ? v : s.value))
                        return
                    if (!active) {
                        active = true
                        s.started()
                    }
                    v = nv
                    s.moved(nv)
                }
                function end() {
                    if (active)
                        s.committed(v)
                    active = false
                    grab = -1
                }
                onReleased: end()
                onCanceled: end()
            }
        }
    }
}
