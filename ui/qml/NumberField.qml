pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
import "Fmt.js" as Fmt

// Number field of the prototype (.fld, "Ô số" of plan.md): a 24 px box with the value in JetBrains
// Mono and its unit, the label inside the box (Properties, Color Picker) or, labelInside false, before
// it (options bar, Layers). Drag the label (a step a pixel, Shift 10), ↑ ↓ (Shift ×10), type a sum or
// a unit ("1240*2", "5cm" at ppi, "50%" of percentOf) then Enter, Esc to drop the typing, double-click
// the label for defaultValue. Past min or max the box turns red with "Tối đa 1.000 px" under it, and
// Enter takes the limit. `slider` adds the chevron of the options bar's FC cells, a slider under it.
// Value protocol of the shared controls: the owner binds `value`; every change comes as started(),
// moved(v)… (live, set the model here), committed(v) (one history step ends); a drag sends many
// moved(), a typed value or a key one.
Item {
    id: f
    property string label
    property bool labelInside: true
    property color labelColor: Theme.t2
    property real value
    property string unit
    property int decimals: 0
    property real min: -Infinity
    property real max: Infinity
    property real step: 1
    property real defaultValue: NaN
    property real ppi: 72
    property real percentOf: NaN  // what "100%" is in a field of another unit; NaN: the value
    property bool slider: false
    property real sliderMin: min
    property real sliderMax: max
    readonly property bool editing: input.activeFocus
    readonly property bool scrubbing: st.scrubbing
    readonly property string shown: Fmt.unit(value, unit, decimals)
    readonly property real typed: editing ? read(input.text) : NaN
    readonly property bool over: !isNaN(typed) && (round(typed) > max || round(typed) < min)
    readonly property string note: !over ? "" : round(typed) > max ? qsTr("Tối đa %1").arg(Fmt.unit(max, unit, decimals))
                                                                   : qsTr("Tối thiểu %1").arg(Fmt.unit(min, unit, decimals))
    // The typed value in the field's unit when it was typed in another one ("5cm" → "591 px").
    readonly property string preview: !isNaN(typed) && /[a-z%°]/.test(input.text.toLowerCase().replace(unit.toLowerCase(), ""))
                                      ? Fmt.unit(round(typed), unit, decimals) : ""
    signal started()
    signal moved(real v)
    signal committed(real v)

    implicitWidth: (outLabel.visible ? outLabel.implicitWidth + 6 : 0) + 64 + (inLabel.visible ? inLabel.implicitWidth + 4 : 0)
    implicitHeight: 24

    onShownChanged: if (!input.activeFocus) input.text = shown

    function round(v) { const k = Math.pow(10, decimals); return Math.round(v * k) / k }
    function clamp(v) { return Math.max(min, Math.min(max, v)) }
    function read(t) { return Fmt.parse(t, unit, ppi, isNaN(percentOf) ? value : percentOf) }
    function apply(v) {  // a change by key or typing: one step
        v = clamp(round(v))
        if (isNaN(v) || v === value) return
        started()
        moved(v)
        committed(v)
    }
    function nudge(n) {
        const t = read(input.text)
        apply((isNaN(t) ? value : clamp(round(t))) + n * step)
        input.text = shown
        input.selectAll()
    }
    function reset() { if (!isNaN(defaultValue)) apply(defaultValue) }

    QtObject {
        id: st
        property bool scrubbing
        property bool cancelled  // Esc: leave without applying
        property real x0  // where the next whole step starts
        property real v  // the value scrubbed to
    }
    function scrubStart(x) {
        input.focus = false  // applies what was being typed
        st.x0 = x
        st.scrubbing = false
    }
    function scrubTo(x, shift) {
        if (!st.scrubbing) {
            if (Math.abs(x - st.x0) < 3) return
            st.scrubbing = true
            st.v = value
            started()
        }
        const n = Math.trunc(x - st.x0)
        if (!n) return
        st.x0 += n
        const v = clamp(round(st.v + n * step * (shift ? 10 : 1)))
        if (v === st.v) return
        st.v = v
        moved(v)
    }
    function scrubEnd() {
        if (!st.scrubbing) return
        st.scrubbing = false
        committed(st.v)
    }

    component Scrub: MouseArea {
        anchors.fill: parent
        cursorShape: Qt.SizeHorCursor
        preventStealing: true
        onPressed: e => f.scrubStart(e.x)
        onPositionChanged: e => f.scrubTo(e.x, e.modifiers & Qt.ShiftModifier)
        onReleased: f.scrubEnd()
        onCanceled: f.scrubEnd()
        onDoubleClicked: f.reset()
    }
    component Lbl: Text {
        text: f.label
        color: !f.enabled ? Theme.t4 : f.scrubbing ? Theme.t1 : f.labelColor
        font.pointSize: Theme.pt(12)
    }

    Lbl {
        id: outLabel
        visible: !f.labelInside && f.label !== ""
        anchors.verticalCenter: parent.verticalCenter
        Scrub {}
    }
    Rectangle {
        id: box
        x: outLabel.visible ? outLabel.width + 6 : 0
        width: f.width - x
        height: 24
        anchors.verticalCenter: parent.verticalCenter
        radius: 4
        color: Theme.p2
        border.width: f.scrubbing ? 1 : 0
        border.color: Theme.t4
        Rectangle {  // focus ring, red past a limit
            readonly property int w: f.over ? 1 : 2
            visible: f.editing
            anchors.fill: parent
            anchors.margins: -w
            radius: box.radius + w
            color: "transparent"
            border.width: w
            border.color: f.over ? Theme.danger : Theme.accent
        }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 6
            anchors.rightMargin: f.slider ? 2 : 6
            spacing: 4
            Lbl {
                id: inLabel
                visible: f.labelInside && f.label !== ""
                Scrub {}
            }
            TextField {
                id: input
                Layout.fillWidth: true
                Layout.fillHeight: true
                implicitWidth: 20
                padding: 0
                background: null
                verticalAlignment: TextInput.AlignVCenter
                font.family: Theme.mono
                font.pointSize: Theme.pt(11.5)
                color: f.enabled ? Theme.t1 : Theme.t4
                selectByMouse: true
                Component.onCompleted: text = f.shown
                onActiveFocusChanged: {
                    if (activeFocus) {
                        st.cancelled = false
                        Qt.callLater(selectAll)  // after the click has placed the cursor
                    } else {
                        if (!st.cancelled) f.apply(f.read(text))
                        text = f.shown
                    }
                }
                onAccepted: focus = false
                Keys.onEscapePressed: { st.cancelled = true; focus = false }
                Keys.onUpPressed: e => f.nudge(e.modifiers & Qt.ShiftModifier ? 10 : 1)
                Keys.onDownPressed: e => f.nudge(e.modifiers & Qt.ShiftModifier ? -10 : -1)
            }
            Text {
                visible: f.preview !== ""
                text: f.preview
                color: Theme.t3
                font.family: Theme.mono
                font.pointSize: Theme.pt(10.5)
            }
            Icon { visible: f.scrubbing; name: "scrub"; size: 16; color: Theme.t1 }  // a label being dragged
            AbstractButton {
                id: chevron
                visible: f.slider
                Layout.preferredWidth: 18
                Layout.fillHeight: true
                focusPolicy: Qt.NoFocus
                ToolTip.visible: hovered
                ToolTip.delay: 600
                ToolTip.text: qsTr("Thanh trượt %1").arg(f.label)
                hoverEnabled: true
                onClicked: pop.opened ? pop.close() : pop.open()
                contentItem: Icon { name: "expand_more"; size: 14; color: chevron.hovered ? Theme.t1 : Theme.t3 }
            }
        }
    }
    Popup {  // the slider of an FC cell
        id: pop
        popupType: Popup.Item
        x: box.x
        y: f.height + 4
        width: Math.max(box.width, 160)
        padding: 10
        focus: true  // ← → move the slider at once
        background: Rectangle { radius: 8; color: Theme.p2; border.color: Theme.ln }
        contentItem: SliderField {
            focus: true
            value: f.value
            min: f.sliderMin
            max: f.sliderMax
            step: f.step
            decimals: f.decimals
            onStarted: f.started()
            onMoved: v => f.moved(v)
            onCommitted: v => f.committed(v)
        }
    }
    Popup {  // "Tối đa 1.000 px" under a value past a limit
        popupType: Popup.Item
        visible: f.note !== ""
        x: box.x
        y: f.height + 4
        padding: 0
        closePolicy: Popup.NoAutoClose
        background: Rectangle { radius: 3; color: Theme.p1 }
        contentItem: Text { text: f.note; color: Theme.danger; font.pointSize: Theme.pt(11) }
    }
}
