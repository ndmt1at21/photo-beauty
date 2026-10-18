pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import "Fmt.js" as Fmt

// Slider row of the prototype (.sr): label and value over a 3 px track with an 11 px thumb (7 px inside its ring); `track`
// lists the colours of a gradient track (hue, saturation…, evenly spaced), plain trk when empty; no
// label: the track alone (the slider of a number field). Press or drag the track, ← → ↑ ↓ (Shift ×10)
// when focused. Value protocol of NumberField: started(), moved(v)… while the value follows the
// mouse, committed(v) when the mouse lets go; a key sends the three at once.
Control {
    id: s
    property string label
    property real value
    property real min: 0
    property real max: 100
    property real step: 1
    property int decimals: 0
    property string unit
    property var track: []
    property string valueText: (min < 0 ? Fmt.sg(value, decimals) : Fmt.vn(value, decimals)) + Fmt.suffix(unit)
    readonly property real pos: max > min ? Math.max(0, Math.min(1, (value - min) / (max - min))) : 0
    readonly property bool dragging: area.active
    signal started()
    signal moved(real v)
    signal committed(real v)

    implicitWidth: 200
    implicitHeight: label !== "" ? 25 : 15
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    padding: 0

    function snap(v) {
        const k = Math.pow(10, decimals)
        v = min + Math.round((v - min) / step) * step
        return Math.round(Math.max(min, Math.min(max, v)) * k) / k
    }
    function nudge(n) {
        const v = snap(value + n * step)
        if (v === value) return
        started()
        moved(v)
        committed(v)
    }
    Keys.onLeftPressed: e => nudge(e.modifiers & Qt.ShiftModifier ? -10 : -1)
    Keys.onDownPressed: e => nudge(e.modifiers & Qt.ShiftModifier ? -10 : -1)
    Keys.onRightPressed: e => nudge(e.modifiers & Qt.ShiftModifier ? 10 : 1)
    Keys.onUpPressed: e => nudge(e.modifiers & Qt.ShiftModifier ? 10 : 1)

    contentItem: Item {
        Text {
            visible: s.label !== ""
            text: s.label
            color: s.enabled ? Theme.t2 : Theme.t4
            font.pointSize: Theme.pt(12)
        }
        Text {
            visible: s.label !== ""
            anchors.right: parent.right
            text: s.valueText
            color: s.enabled ? Theme.t1 : Theme.t4
            font.family: Theme.mono
            font.pointSize: Theme.pt(11.5)
        }
        Item {
            id: bar
            y: s.label !== "" ? 22 : 6
            width: parent.width
            height: 3
            Rectangle {
                visible: s.track.length < 2
                anchors.fill: parent
                radius: 1.5
                color: Theme.trk
            }
            Row {  // a gradient track: one two-colour piece between each pair of colours
                visible: s.track.length >= 2
                Repeater {
                    model: Math.max(0, s.track.length - 1)
                    Rectangle {
                        id: piece
                        required property int index
                        width: bar.width / (s.track.length - 1)
                        height: 3
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0; color: s.track[piece.index] }
                            GradientStop { position: 1; color: s.track[piece.index + 1] }
                        }
                    }
                }
            }
            Rectangle {  // thumb: the prototype's 11 px, its 2 px border a ring in the panel colour
                x: s.pos * bar.width - 5.5
                y: -4
                width: 11
                height: 11
                radius: 5.5
                color: Theme.p1
                Rectangle { x: 2; y: 2; width: 7; height: 7; radius: 3.5; color: s.enabled ? Theme.thumb : Theme.t4 }
            }
            MouseArea {
                id: area
                property bool active  // a drag that changed the value
                property real v
                y: -7
                width: parent.width
                height: 17
                preventStealing: true
                function go(x) {
                    const nv = s.snap(s.min + x / width * (s.max - s.min))
                    if (nv === (active ? v : s.value)) return
                    if (!active) { active = true; s.started() }
                    v = nv
                    s.moved(v)
                }
                function end() {
                    if (!active) return
                    active = false
                    s.committed(v)
                }
                onPressed: e => { s.forceActiveFocus(Qt.MouseFocusReason); go(e.x) }
                onPositionChanged: e => go(e.x)
                onReleased: end()
                onCanceled: end()
            }
        }
    }
}
